#include "zeno_internal.h"

#include <ctype.h>
#include <stdint.h>

#if defined(ZENO_HAVE_CURL) || defined(_WIN32)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#ifdef ZENO_HAVE_CURL
#include <curl/curl.h>
#endif
#endif

/* Windows fallback: when libcurl is not available the Windows HTTP stack
 * (WinHTTP) provides the same transport surface, so providers, MCP over HTTP
 * and the HTTP tools keep working on a plain MinGW/MSVC build. */
#if defined(_WIN32) && !defined(ZENO_HAVE_CURL)
#define ZENO_HAVE_WINHTTP 1
#include <windows.h>
#include <winhttp.h>
#endif

#if defined(ZENO_HAVE_CURL) || defined(ZENO_HAVE_WINHTTP)
#define ZENO_HAS_HTTP_TRANSPORT 1
#endif

static void sort_providers(ZenoRouter *router);
static char *provider_model_from(const char *models_csv, const char *preferred,
                            const char *fallbacks);
static int transient_error(const char *error);

#define ZENO_MAX_HTTP_RESPONSE (16U * 1024U * 1024U)
#define ZENO_MAX_SCRAPE_CHARS (8U * 1024U * 1024U)

typedef struct HttpBuffer {
    char *data;
    size_t length;
    size_t capacity;
    size_t max_chars;
    int limited;
} HttpBuffer;

static int http_append(HttpBuffer *buffer, const char *data, size_t amount) {
    if (buffer->max_chars > 0 && buffer->length >= buffer->max_chars) { buffer->limited = 1; return 0; }
    if (buffer->max_chars > 0 && amount > buffer->max_chars - buffer->length) { amount = buffer->max_chars - buffer->length; buffer->limited = 1; }
    if (buffer->length > (size_t)-1 - amount - 1) return 0;
    if (buffer->length + amount + 1 > buffer->capacity) {
        size_t next = buffer->capacity == 0 ? 4096 : buffer->capacity;
        while (next < buffer->length + amount + 1) {
            if (next > (size_t)-1 / 2) return 0;
            next *= 2;
        }
        char *grown = (char *)realloc(buffer->data, next); if (grown == NULL) return 0;
        buffer->data = grown; buffer->capacity = next;
    }
    memcpy(buffer->data + buffer->length, data, amount); buffer->length += amount; buffer->data[buffer->length] = '\0'; return amount > 0;
}

static int starts_with_ci(const char *text, const char *prefix) {
    if (text == NULL || prefix == NULL) return 0;
    while (*prefix != '\0') {
        if (*text == '\0' || tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return 0;
        text++; prefix++;
    }
    return 1;
}

static int host_is_private_literal(const char *host) {
    if (host == NULL || *host == '\0') return 1;
    char *lower = zeno_lower_copy(host);
    if (lower == NULL) return 1;
    int blocked = strcmp(lower, "localhost") == 0 ||
                  strstr(lower, ".localhost") != NULL ||
                  strstr(lower, ".local") != NULL ||
                  strstr(lower, ".internal") != NULL ||
                  strcmp(lower, "metadata") == 0 ||
                  strcmp(lower, "metadata.google.internal") == 0;
    if (!blocked && lower[0] != '[' && strchr(lower, ':') == NULL) {
        int numeric = 1;
        for (const char *cursor = lower; *cursor != '\0'; cursor++)
            if (!isdigit((unsigned char)*cursor) && *cursor != '.') numeric = 0;
        if (numeric && strchr(lower, '.') != NULL) {
            unsigned int first = 0, second = 0, third = 0, fourth = 0;
            char tail = '\0';
            if (sscanf(lower, "%u.%u.%u.%u%c", &first, &second, &third, &fourth, &tail) == 4 &&
                first <= 255U && second <= 255U && third <= 255U && fourth <= 255U) {
                blocked = first == 0U || first == 10U || first == 127U ||
                          (first == 100U && second >= 64U && second <= 127U) ||
                          (first == 169U && second == 254U) ||
                          (first == 172U && second >= 16U && second <= 31U) ||
                          (first == 192U && second == 168U) ||
                          (first == 198U && (second == 18U || second == 19U)) ||
                          first >= 224U;
            }
        }
    }
    if (!blocked && lower[0] == '[') {
        blocked = starts_with_ci(lower, "[::1") || starts_with_ci(lower, "[fe80:") ||
                  starts_with_ci(lower, "[fc") || starts_with_ci(lower, "[fd") ||
                  starts_with_ci(lower, "[::ffff:0.") || starts_with_ci(lower, "[::ffff:10.") ||
                  starts_with_ci(lower, "[::ffff:100.64.") || starts_with_ci(lower, "[::ffff:127.") ||
                  starts_with_ci(lower, "[::ffff:169.254.") || starts_with_ci(lower, "[::ffff:172.") ||
                  starts_with_ci(lower, "[::ffff:192.168.") || starts_with_ci(lower, "[::ffff:198.18.") ||
                  starts_with_ci(lower, "[::ffff:198.19.") || starts_with_ci(lower, "[::ffff:224.");
    }
    free(lower);
    return blocked;
}

#if defined(ZENO_HAVE_CURL) || defined(_WIN32)
static int resolved_address_is_private(const struct sockaddr *address) {
    if (address == NULL) return 1;
    if (address->sa_family == AF_INET) {
        uint32_t ip = ntohl(((const struct sockaddr_in *)address)->sin_addr.s_addr);
        unsigned int first = (unsigned int)(ip >> 24);
        unsigned int second = (unsigned int)((ip >> 16) & 255U);
        return first == 0U || first == 10U || first == 127U ||
               (first == 100U && second >= 64U && second <= 127U) ||
               (first == 169U && second == 254U) ||
               (first == 172U && second >= 16U && second <= 31U) ||
               (first == 192U && second == 168U) ||
               (first == 198U && (second == 18U || second == 19U)) || first >= 224U;
    }
    if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)address;
        const unsigned char *bytes = ipv6->sin6_addr.s6_addr;
        if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr) || IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr) || IN6_IS_ADDR_MULTICAST(&ipv6->sin6_addr) ||
            (bytes[0] & 0xfeU) == 0xfcU || IN6_IS_ADDR_SITELOCAL(&ipv6->sin6_addr)) return 1;
        if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
            uint32_t ip = ((uint32_t)bytes[12] << 24) | ((uint32_t)bytes[13] << 16) |
                          ((uint32_t)bytes[14] << 8) | (uint32_t)bytes[15];
            unsigned int first = (unsigned int)(ip >> 24);
            unsigned int second = (unsigned int)((ip >> 16) & 255U);
            return first == 0U || first == 10U || first == 127U ||
                   (first == 100U && second >= 64U && second <= 127U) ||
                   (first == 169U && second == 254U) ||
                   (first == 172U && second >= 16U && second <= 31U) ||
                   (first == 192U && second == 168U) ||
                   (first == 198U && (second == 18U || second == 19U)) || first >= 224U;
        }
    }
    return 0;
}

static int external_dns_host_allowed(const char *host) {
    if (host == NULL || *host == '\0') return 0;
    char lookup[256];
    size_t length = strlen(host);
    if (host[0] == '[' && length > 2 && host[length - 1] == ']') {
        if (length - 2 >= sizeof(lookup)) return 0;
        memcpy(lookup, host + 1, length - 2); lookup[length - 2] = '\0';
    } else {
        if (length >= sizeof(lookup)) return 0;
        memcpy(lookup, host, length + 1);
    }
#ifdef _WIN32
    WSADATA winsock;
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 0;
#endif
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    int status = getaddrinfo(lookup, NULL, &hints, &addresses);
    int allowed = status == 0 && addresses != NULL;
    for (struct addrinfo *address = addresses; allowed && address != NULL; address = address->ai_next)
        if (resolved_address_is_private(address->ai_addr)) allowed = 0;
    if (addresses != NULL) freeaddrinfo(addresses);
#ifdef _WIN32
    WSACleanup();
#endif
    return allowed;
}
#endif

static int external_http_url_allowed(const char *url) {
    if (url == NULL || *url == '\0' || strpbrk(url, "\r\n") != NULL) return 0;
    const char *authority = NULL;
    if (starts_with_ci(url, "http://")) authority = url + 7;
    else if (starts_with_ci(url, "https://")) authority = url + 8;
    else return 0;
    const char *end = authority;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') end++;
    if (end == authority || memchr(authority, '@', (size_t)(end - authority)) != NULL) return 0;
    const char *host_start = authority;
    const char *host_end = end;
    if (*host_start == '[') {
        const char *closing = memchr(host_start, ']', (size_t)(end - host_start));
        if (closing == NULL || closing == host_start + 1) return 0;
        host_end = closing + 1;
        if (host_end < end && *host_end != ':') return 0;
    } else {
        const char *first_colon = memchr(host_start, ':', (size_t)(end - host_start));
        if (first_colon != NULL) {
            if (memchr(first_colon + 1, ':', (size_t)(end - first_colon - 1)) != NULL) return 0;
            for (const char *cursor = first_colon + 1; cursor < end; cursor++)
                if (!isdigit((unsigned char)*cursor)) return 0;
            host_end = first_colon;
        }
    }
    size_t host_length = (size_t)(host_end - host_start);
    if (host_length == 0 || host_length >= 256) return 0;
    char host[256];
    memcpy(host, host_start, host_length);
    host[host_length] = '\0';
    int blocked = host_is_private_literal(host);
#if defined(ZENO_HAVE_CURL) || defined(_WIN32)
    if (!blocked && !external_dns_host_allowed(host)) blocked = 1;
#endif
    return !blocked;
}

#ifdef ZENO_HAVE_CURL
static int external_http_resolve(const char *url, struct curl_slist **resolve_out) {
    if (resolve_out != NULL) *resolve_out = NULL;
    if (url == NULL || resolve_out == NULL) return 0;
    const char *authority = starts_with_ci(url, "http://") ? url + 7 : (starts_with_ci(url, "https://") ? url + 8 : NULL);
    if (authority == NULL) return 0;
    const char *end = authority;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') end++;
    const char *host_start = authority;
    const char *host_end = end;
    const char *port_start = NULL;
    int bracketed = *host_start == '[';
    if (bracketed) {
        const char *closing = memchr(host_start, ']', (size_t)(end - host_start));
        if (closing == NULL || closing == host_start + 1) return 0;
        host_start++;
        host_end = closing;
        if (closing + 1 < end) {
            if (closing[1] != ':') return 0;
            port_start = closing + 2;
        }
    } else {
        const char *colon = memchr(host_start, ':', (size_t)(end - host_start));
        if (colon != NULL) { host_end = colon; port_start = colon + 1; }
    }
    size_t host_length = (size_t)(host_end - host_start);
    if (host_length == 0 || host_length >= 256U) return 0;
    char host[256]; memcpy(host, host_start, host_length); host[host_length] = '\0';
    char port[6];
    if (port_start == end) return 0;
    if (port_start == NULL) {
        zeno_copy_string(port, sizeof(port), starts_with_ci(url, "https://") ? "443" : "80");
    } else {
        size_t port_length = (size_t)(end - port_start);
        if (port_length == 0 || port_length >= sizeof(port)) return 0;
        for (size_t index = 0; index < port_length; index++) if (!isdigit((unsigned char)port_start[index])) return 0;
        memcpy(port, port_start, port_length); port[port_length] = '\0';
        unsigned long numeric_port = strtoul(port, NULL, 10);
        if (numeric_port == 0 || numeric_port > 65535UL) return 0;
    }
    if (bracketed) return 1; /* A literal IPv6 host has no DNS rebinding step. */
    struct addrinfo hints; memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
#ifdef _WIN32
    WSADATA winsock;
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 0;
#endif
    struct addrinfo *addresses = NULL;
    int status = getaddrinfo(host, NULL, &hints, &addresses);
    int ok = status == 0 && addresses != NULL;
    struct curl_slist *entries = NULL;
    size_t entry_count = 0;
    char address_text[INET6_ADDRSTRLEN];
    for (struct addrinfo *address = addresses; ok && address != NULL; address = address->ai_next) {
        if (resolved_address_is_private(address->ai_addr)) { ok = 0; break; }
        const void *source = NULL;
        if (address->ai_family == AF_INET) source = &((const struct sockaddr_in *)address->ai_addr)->sin_addr;
        else if (address->ai_family == AF_INET6) source = &((const struct sockaddr_in6 *)address->ai_addr)->sin6_addr;
        else continue;
        if (inet_ntop(address->ai_family, source, address_text, sizeof(address_text)) == NULL) { ok = 0; break; }
        char *entry = zeno_format("%s:%s:%s", host, port, address_text);
        if (entry == NULL) { ok = 0; break; }
        struct curl_slist *grown = curl_slist_append(entries, entry);
        free(entry);
        if (grown == NULL) { ok = 0; break; }
        entries = grown; entry_count++;
    }
    if (addresses != NULL) freeaddrinfo(addresses);
#ifdef _WIN32
    WSACleanup();
#endif
    if (!ok || entry_count == 0) { curl_slist_free_all(entries); return 0; }
    *resolve_out = entries;
    return 1;
}
#endif

/* --- SSE streaming accumulator (OpenAI-compatible) --- */
typedef struct SseToolCall {
    char *id;
    char *name;
    char *arguments;
} SseToolCall;
static int csv_contains(const char *csv, const char *value) {
    if (csv == NULL || value == NULL) return 0;
    size_t length = strlen(value);
    const char *cursor = csv;
    while (*cursor != '\0') {
        while (*cursor == ',' || *cursor == ' ' || *cursor == '\t') cursor++;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != ',') cursor++;
        const char *end = cursor;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
        if ((size_t)(end - start) == length && strncmp(start, value, length) == 0) return 1;
        if (*cursor == ',') cursor++;
    }
    return 0;
}

static char **csv_split(const char *csv, size_t *count) {
    *count = 0;
    if (csv == NULL) return NULL;
    char **items = NULL; size_t capacity = 0;
    const char *cursor = csv;
    while (*cursor != '\0') {
        while (*cursor == ',' || *cursor == ' ' || *cursor == '\t') cursor++;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != ',') cursor++;
        const char *end = cursor;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
        if (end == start) { if (*cursor == ',') cursor++; continue; }
        if (*count == capacity) { capacity = capacity == 0 ? 4 : capacity * 2; char **grown = (char **)realloc(items, capacity * sizeof(*grown)); if (grown == NULL) { for (size_t i = 0; i < *count; i++) free(items[i]); free(items); return NULL; } items = grown; }
        items[(*count)++] = zeno_strndup(start, (size_t)(end - start));
        if (*cursor == ',') cursor++;
    }
    return items;
}

static void csv_split_free(char **items, size_t count) { for (size_t index = 0; index < count; index++) free(items[index]); free(items); }

static char *provider_model_from(const char *models_csv, const char *preferred, const char *fallbacks) {
    const char *csv = models_csv != NULL ? models_csv : "";
    if (preferred != NULL && *preferred != '\0' && (csv[0] == '\0' || csv_contains(csv, preferred))) return zeno_strdup(preferred);
    size_t fallback_count = 0; char **fallback_items = csv_split(fallbacks, &fallback_count);
    for (size_t index = 0; index < fallback_count; index++) if (csv[0] == '\0' || csv_contains(csv, fallback_items[index])) { char *found = zeno_strdup(fallback_items[index]); csv_split_free(fallback_items, fallback_count); return found; }
    csv_split_free(fallback_items, fallback_count);
    size_t own_count = 0; char **own_items = csv_split(csv, &own_count);
    char *result = own_count > 0 ? zeno_strdup(own_items[0]) : zeno_strdup(preferred != NULL ? preferred : "");
    csv_split_free(own_items, own_count);
    return result;
}

typedef struct RouterView {
    char *id;
    char *base_url;
    char *api_key;
    char *models_csv;
    int timeout_ms;
    int max_errors;
    long long cooldown_until_ms;
    size_t consecutive_errors;
} RouterView;

typedef struct RouterPlan {
    RouterView *views;
    size_t count;
    char *fallback_models;
    ZenoTransport transport;
    void *transport_context;
} RouterPlan;

/* Providers are snapshotted under the router lock; transports run outside it
 * so parallel agent threads keep issuing LLM calls concurrently. Stats are
 * written back under the lock by provider id. */
static void router_plan_free(RouterPlan *plan) {
    if (plan == NULL) return;
    for (size_t index = 0; index < plan->count; index++) {
        free(plan->views[index].id); free(plan->views[index].base_url);
        free(plan->views[index].api_key); free(plan->views[index].models_csv);
    }
    free(plan->views); free(plan->fallback_models); memset(plan, 0, sizeof(*plan));
}

static void router_plan_snapshot(ZenoRouter *router, RouterPlan *plan) {
    memset(plan, 0, sizeof(*plan));
    if (router == NULL) return;
    zeno_mutex_lock(&router->lock);
    sort_providers(router);
    plan->transport = router->transport;
    plan->transport_context = router->transport_context;
    plan->fallback_models = zeno_strdup(router->fallback_models != NULL ? router->fallback_models : "");
    if (router->count > 0) {
        plan->views = (RouterView *)calloc(router->count, sizeof(*plan->views));
        if (plan->views != NULL) for (size_t index = 0; index < router->count; index++) {
            ZenoProvider *provider = &router->providers[index];
            plan->views[index].id = zeno_strdup(provider->id != NULL ? provider->id : "");
            plan->views[index].base_url = zeno_strdup(provider->base_url != NULL ? provider->base_url : "");
            plan->views[index].api_key = zeno_strdup(provider->api_key != NULL ? provider->api_key : "");
            plan->views[index].models_csv = zeno_strdup(provider->models_csv != NULL ? provider->models_csv : "");
            plan->views[index].timeout_ms = provider->timeout_ms;
            plan->views[index].max_errors = provider->max_errors;
            plan->views[index].cooldown_until_ms = provider->stats.cooldown_until_ms;
            plan->views[index].consecutive_errors = provider->stats.consecutive_errors;
            plan->count++;
        }
    }
    zeno_mutex_unlock(&router->lock);
}

static void router_stats_record(ZenoRouter *router, const char *provider_id,
                                int ok, long long started_ms, const char *error_text) {
    if (router == NULL || provider_id == NULL) return;
    zeno_mutex_lock(&router->lock);
    for (size_t index = 0; index < router->count; index++) {
        ZenoProvider *provider = &router->providers[index];
        if (provider->id == NULL || strcmp(provider->id, provider_id) != 0) continue;
        provider->stats.total_calls++;
        if (ok) {
            provider->stats.success_calls++;
            provider->stats.consecutive_errors = 0;
            double latency = (double)(zeno_now_ms() - started_ms);
            provider->stats.average_latency = ((provider->stats.average_latency * (double)(provider->stats.success_calls - 1)) + latency) / (double)provider->stats.success_calls;
        } else {
            provider->stats.error_calls++;
            provider->stats.consecutive_errors++;
            if (error_text != NULL) zeno_copy_string(provider->stats.last_error, sizeof(provider->stats.last_error), error_text);
            if (zeno_contains_ci(error_text != NULL ? error_text : "", "429")) provider->stats.cooldown_until_ms = zeno_now_ms() + 60000;
        }
        break;
    }
    zeno_mutex_unlock(&router->lock);
}

#define ZENO_MAX_SSE_BYTES (16U * 1024U * 1024U)

typedef struct SseAccum {
    char *raw;
    size_t raw_len;
    size_t received_bytes;
    size_t raw_cap;
    char *content;
    size_t content_len;
    size_t content_cap;
    SseToolCall *calls;
    size_t call_count;
    char *usage_json;
    ZenoChunkCallback chunk;
    void *chunk_context;
    int failed;
    char *error;
} SseAccum;

static void sse_mark_failed(SseAccum *accum, const char *message) {
    if (accum == NULL || accum->failed) return;
    accum->failed = 1;
    accum->error = zeno_strdup(message != NULL ? message : "SSE response exceeded its size limit.");
}

static int sse_append_raw(SseAccum *accum, const char *data, size_t amount) {
    if (amount > ZENO_MAX_SSE_BYTES || accum->raw_len > ZENO_MAX_SSE_BYTES - amount - 1) {
        sse_mark_failed(accum, "SSE response exceeded its size limit.");
        return 0;
    }
    if (accum->raw_len + amount + 1 > accum->raw_cap) {
        size_t next = accum->raw_cap == 0 ? 4096 : accum->raw_cap;
        while (next < accum->raw_len + amount + 1) next *= 2;
        char *grown = (char *)realloc(accum->raw, next); if (grown == NULL) { sse_mark_failed(accum, "Out of memory while buffering SSE response."); return 0; }
        accum->raw = grown; accum->raw_cap = next;
    }
    memcpy(accum->raw + accum->raw_len, data, amount); accum->raw_len += amount; accum->raw[accum->raw_len] = '\0'; return 1;
}

static int sse_append_content(SseAccum *accum, const char *text) {
    size_t amount = strlen(text != NULL ? text : "");
    if (amount > ZENO_MAX_SSE_BYTES || accum->content_len > ZENO_MAX_SSE_BYTES - amount - 1) {
        sse_mark_failed(accum, "SSE response exceeded its size limit.");
        return 0;
    }
    if (accum->content_len + amount + 1 > accum->content_cap) {
        size_t next = accum->content_cap == 0 ? 1024 : accum->content_cap;
        while (next < accum->content_len + amount + 1) next *= 2;
        char *grown = (char *)realloc(accum->content, next); if (grown == NULL) { sse_mark_failed(accum, "Out of memory while buffering SSE content."); return 0; }
        accum->content = grown; accum->content_cap = next;
    }
    memcpy(accum->content + accum->content_len, text, amount); accum->content_len += amount; accum->content[accum->content_len] = '\0'; return 1;
}

static void sse_parse_event(SseAccum *accum, const char *event) {
    const char *line = event;
    while ((line = strstr(line, "data:")) != NULL) {
        line += 5;
        while (*line == ' ') line++;
        const char *end = line;
        while (*end != '\0' && *end != '\r' && *end != '\n') end++;
        if (end - line >= 6 && strncmp(line, "[DONE]", 6) == 0) return;
        char *payload = zeno_strndup(line, (size_t)(end - line));
        if (payload == NULL) return;
        char *error = NULL;
        ZjNode *root = zj_parse(payload, &error); free(error);
        if (root != NULL) {
            ZjNode *usage = zj_object_get(root, "usage");
            if (usage != NULL && usage->type == ZJ_OBJECT) { char *usage_json = zj_stringify_compact(usage); free(accum->usage_json); accum->usage_json = usage_json; }
            ZjNode *choice = zj_array_get(zj_object_get(root, "choices"), 0);
            ZjNode *delta = choice != NULL ? zj_object_get(choice, "delta") : NULL;
            if (delta != NULL) {
                const char *content = zj_string(zj_object_get(delta, "content"));
                if (content != NULL && *content != '\0') {
                    if (!sse_append_content(accum, content)) {
                        zj_free(root);
                        free(payload);
                        return;
                    }
                    if (accum->chunk != NULL) accum->chunk(accum->chunk_context, content);
                }
                ZjNode *deltas = zj_object_get(delta, "tool_calls");
                if (deltas != NULL && deltas->type == ZJ_ARRAY) for (size_t index = 0; index < deltas->count; index++) {
                    ZjNode *item = deltas->items[index];
                    size_t slot = (size_t)zj_integer(zj_object_get(item, "index"), (long long)index);
                    if (slot >= 256) {
                        sse_mark_failed(accum, "SSE response contains too many tool calls.");
                        break;
                    }
                    if (slot >= accum->call_count) {
                        SseToolCall *grown = (SseToolCall *)realloc(accum->calls, (slot + 1) * sizeof(*grown));
                        if (grown == NULL) break;
                        accum->calls = grown;
                        while (accum->call_count <= slot) {
                            memset(&accum->calls[accum->call_count], 0, sizeof(SseToolCall));
                            accum->calls[accum->call_count].id = zeno_strdup("");
                            accum->calls[accum->call_count].name = zeno_strdup("");
                            accum->calls[accum->call_count].arguments = zeno_strdup("");
                            if (accum->calls[accum->call_count].id == NULL || accum->calls[accum->call_count].name == NULL || accum->calls[accum->call_count].arguments == NULL) {
                                free(accum->calls[accum->call_count].id);
                                free(accum->calls[accum->call_count].name);
                                free(accum->calls[accum->call_count].arguments);
                                memset(&accum->calls[accum->call_count], 0, sizeof(SseToolCall));
                                sse_mark_failed(accum, "Out of memory while buffering SSE tool calls.");
                                break;
                            }
                            accum->call_count++;
                        }
                        if (accum->failed) break;
                    }
                    const char *id = zj_string(zj_object_get(item, "id"));
                    if (id != NULL && *id != '\0') {
                        char *copy = zeno_strdup(id);
                        if (copy == NULL) { sse_mark_failed(accum, "Out of memory while buffering SSE tool id."); break; }
                        free(accum->calls[slot].id); accum->calls[slot].id = copy;
                    }
                    ZjNode *function = zj_object_get(item, "function");
                    if (function != NULL) {
                        const char *name = zj_string(zj_object_get(function, "name"));
                        if (name != NULL) {
                            size_t name_length = strlen(name);
                            if (name_length >= ZENO_MAX_SSE_BYTES || strlen(accum->calls[slot].name) > ZENO_MAX_SSE_BYTES - name_length - 1) { sse_mark_failed(accum, "SSE tool call exceeded its size limit."); break; }
                            char *merged = zeno_format("%s%s", accum->calls[slot].name, name);
                            if (merged == NULL) { sse_mark_failed(accum, "Out of memory while buffering SSE tool call."); break; }
                            free(accum->calls[slot].name); accum->calls[slot].name = merged;
                        }
                        const char *arguments = zj_string(zj_object_get(function, "arguments"));
                        if (arguments != NULL) {
                            size_t arguments_length = strlen(arguments);
                            if (arguments_length >= ZENO_MAX_SSE_BYTES || strlen(accum->calls[slot].arguments) > ZENO_MAX_SSE_BYTES - arguments_length - 1) { sse_mark_failed(accum, "SSE tool arguments exceeded their size limit."); break; }
                            char *merged = zeno_format("%s%s", accum->calls[slot].arguments, arguments);
                            if (merged == NULL) { sse_mark_failed(accum, "Out of memory while buffering SSE arguments."); break; }
                            free(accum->calls[slot].arguments); accum->calls[slot].arguments = merged;
                        }
                    }
                }
            }
        }
        zj_free(root); free(payload);
        line = end;
    }
}

#ifdef ZENO_HAVE_CURL
static int curl_provider_inputs_valid(const char *base_url, const char *api_key, const char *body_json, char **error_message) {
    if ((base_url != NULL && strlen(base_url) > 4096U) || (body_json != NULL && strlen(body_json) > ZENO_MAX_HTTP_RESPONSE) || (api_key != NULL && strpbrk(api_key, "\r\n") != NULL)) {
        if (error_message != NULL) *error_message = zeno_strdup("Provider request exceeded a size limit or contained an invalid header value.");
        return 0;
    }
    return 1;
}

static size_t sse_curl_write(void *data, size_t size, size_t count, void *context) {
    SseAccum *accum = (SseAccum *)context;
    if (count > 0 && size > (size_t)-1 / count) {
        sse_mark_failed(accum, "SSE response chunk is too large.");
        return 0;
    }
    size_t amount = size * count;
    if (amount > ZENO_MAX_SSE_BYTES || accum->received_bytes > ZENO_MAX_SSE_BYTES - amount) {
        sse_mark_failed(accum, "SSE response exceeded its size limit.");
        return 0;
    }
    accum->received_bytes += amount;
    if (accum->failed || !sse_append_raw(accum, (const char *)data, amount)) return 0;
    size_t scan_offset = 0;
    size_t event_offset = 0;
    while (scan_offset < accum->raw_len) {
        size_t delimiter_length = 0;
        if (accum->raw[scan_offset] == '\n' && scan_offset + 1U < accum->raw_len && accum->raw[scan_offset + 1U] == '\n') delimiter_length = 2;
        else if (accum->raw[scan_offset] == '\r' && scan_offset + 3U < accum->raw_len && accum->raw[scan_offset + 1U] == '\n' && accum->raw[scan_offset + 2U] == '\r' && accum->raw[scan_offset + 3U] == '\n') delimiter_length = 4;
        if (delimiter_length == 0) { scan_offset++; continue; }
        char saved = accum->raw[scan_offset];
        accum->raw[scan_offset] = '\0';
        sse_parse_event(accum, accum->raw + event_offset);
        accum->raw[scan_offset] = saved;
        scan_offset += delimiter_length;
        event_offset = scan_offset;
        if (accum->failed) return 0;
    }
    if (event_offset > 0) {
        size_t keep = accum->raw_len - event_offset;
        memmove(accum->raw, accum->raw + event_offset, keep);
        accum->raw_len = keep;
        accum->raw[keep] = '\0';
    }
    return amount;
}

static int curl_stream_transport(const char *base_url, const char *api_key,
                                 const char *body_json, int timeout_ms,
                                 SseAccum *accum, char **error_message) {
    if (!curl_provider_inputs_valid(base_url, api_key, body_json, error_message)) return 0;
    CURL *curl = curl_easy_init(); if (curl == NULL) { if (error_message != NULL) *error_message = zeno_strdup("libcurl initialization failed"); return 0; }
    char *url = zeno_format("%s%s", base_url != NULL ? base_url : "", base_url != NULL && *base_url != '\0' && base_url[strlen(base_url) - 1] == '/' ? "chat/completions" : "/chat/completions");
    struct curl_slist *headers = NULL; headers = curl_slist_append(headers, "Content-Type: application/json");
    if (api_key != NULL && *api_key != '\0') { char *authorization = zeno_format("Authorization: Bearer %s", api_key); if (authorization != NULL) { headers = curl_slist_append(headers, authorization); free(authorization); } }
    curl_easy_setopt(curl, CURLOPT_URL, url != NULL ? url : ""); curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json != NULL ? body_json : "{}"); curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)(timeout_ms > 0 ? timeout_ms : 120000)); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sse_curl_write); curl_easy_setopt(curl, CURLOPT_WRITEDATA, accum); curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZenoC/1.0");
    CURLcode code = curl_easy_perform(curl); long status = 0; (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    int ok = code == CURLE_OK && status >= 200 && status < 300;
    if (!ok && error_message != NULL) *error_message = code != CURLE_OK ? zeno_strdup(curl_easy_strerror(code)) : zeno_format("API error %ld during streaming", status);
    if (getenv("ZENO_LLM_DEBUG") != NULL)
        fprintf(stderr, "[llm] http=%ld curl=%d sse_failed=%d sse_error=<%s> raw_len=%zu content_len=%zu calls=%zu\n",
                status, (int)code, accum->failed, accum->error != NULL ? accum->error : "", accum->raw_len, accum->content_len, accum->call_count);
    curl_slist_free_all(headers); curl_easy_cleanup(curl); free(url); return ok;
}
#endif

static char *sse_build_completion(SseAccum *accum) {
    char *result = zeno_format("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":%s,", zeno_json_escape(accum->content != NULL ? accum->content : ""));
    char *with_calls = result;
    if (accum->call_count > 0) {
        char *calls = zeno_strdup("[");
        for (size_t index = 0; index < accum->call_count; index++) {
            char *item = zeno_format("{\"id\":%s,\"type\":\"function\",\"function\":{\"name\":%s,\"arguments\":%s}}", zeno_json_escape(accum->calls[index].id), zeno_json_escape(accum->calls[index].name), zeno_json_escape(accum->calls[index].arguments != NULL && *accum->calls[index].arguments != '\0' ? accum->calls[index].arguments : "{}"));
            char *next = item != NULL ? zeno_json_array_append(calls, item) : NULL; free(item); free(calls); calls = next;
        }
        char *next = calls != NULL ? zeno_format("%s\"tool_calls\":%s},", with_calls, calls) : NULL; free(with_calls); free(calls); with_calls = next;
    } else {
        char *next = zeno_format("%s},", with_calls); free(with_calls); with_calls = next;
    }
    char *final = zeno_format("%s\"finish_reason\":\"stop\"}],", with_calls); free(with_calls);
    char *result_json = NULL;
    if (accum->usage_json != NULL) result_json = zeno_format("%s\"usage\":%s}", final, accum->usage_json);
    else { char *close = zeno_format("%s\"usage\":{}}", final); result_json = close; }
    free(final);
    return result_json;
}

int zeno_router_complete_stream(ZenoRouter *router, const char *preferred_model,
                                const char *messages_json, const char *tools_json,
                                double temperature, int max_tokens,
                                ZenoChunkCallback chunk, void *chunk_context,
                                char **response_json, char **used_provider,
                                char **used_model) {
    if (response_json != NULL) *response_json = NULL; if (used_provider != NULL) *used_provider = NULL; if (used_model != NULL) *used_model = NULL;
    if (router == NULL) return 0;
#ifndef ZENO_HAVE_CURL
    /* Fallback determinístico: sem libcurl, entrega o completion não-streaming via chunk. */
    int cache_hit = 0;
    int ok = zeno_router_complete(router, preferred_model, messages_json, tools_json, temperature, max_tokens, response_json, used_provider, used_model, &cache_hit);
    if (ok && chunk != NULL) { char *content = zeno_json_get_path_string(*response_json, "choices[0].message.content"); if (content != NULL) { chunk(chunk_context, content); free(content); } }
    return ok;
#else
    RouterPlan plan; router_plan_snapshot(router, &plan);
    char *last_error = NULL;
    for (size_t index = 0; index < plan.count; index++) {
        RouterView *view = &plan.views[index]; long long now = zeno_now_ms();
        if (view->cooldown_until_ms > now || (view->max_errors > 0 && view->consecutive_errors >= (size_t)view->max_errors)) continue;
        char *model = provider_model_from(view->models_csv, preferred_model, plan.fallback_models != NULL ? plan.fallback_models : ""); if (model == NULL) continue;
        char *body = zeno_format("{\"model\":%s,\"messages\":%s,\"temperature\":%.3f,\"max_tokens\":%d,\"stream\":true%s%s}", zeno_json_escape(model), messages_json != NULL ? messages_json : "[]", temperature, max_tokens > 0 ? max_tokens : 4096, tools_json != NULL ? ",\"tools\":" : "", tools_json != NULL ? tools_json : "");
        for (int attempt = 1; attempt <= 3; attempt++) {
            long long started = zeno_now_ms(); SseAccum accum; memset(&accum, 0, sizeof(accum)); accum.chunk = chunk; accum.chunk_context = chunk_context; char *error = NULL; char *completion = NULL; int ok;
            if (plan.transport != NULL) {
                ok = plan.transport(plan.transport_context, view->base_url, view->api_key, body, view->timeout_ms, &completion, &error);
                if (ok && chunk != NULL) { char *content = zeno_json_get_path_string(completion, "choices[0].message.content"); if (content != NULL) { chunk(chunk_context, content); free(content); } }
            } else {
                ok = curl_stream_transport(view->base_url, view->api_key, body, view->timeout_ms, &accum, &error);
                if (ok) completion = sse_build_completion(&accum);
            }
            router_stats_record(router, view->id, ok, started, error);
            if (ok) {
                free(error);
                if (response_json != NULL) *response_json = completion; else free(completion);
                if (used_provider != NULL) *used_provider = zeno_strdup(view->id);
                if (used_model != NULL) *used_model = model; else free(model);
                free(accum.raw); free(accum.content); for (size_t call = 0; call < accum.call_count; call++) { free(accum.calls[call].id); free(accum.calls[call].name); free(accum.calls[call].arguments); } free(accum.calls); free(accum.usage_json); free(accum.error); free(last_error); free(body); router_plan_free(&plan); return 1;
            }
            free(completion);
            free(last_error); last_error = error != NULL ? error : zeno_strdup("streaming request failed");
            if (!transient_error(last_error) || attempt == 3) { free(accum.raw); free(accum.content); for (size_t call = 0; call < accum.call_count; call++) { free(accum.calls[call].id); free(accum.calls[call].name); free(accum.calls[call].arguments); } free(accum.calls); free(accum.usage_json); free(accum.error); break; }
            zeno_sleep_ms(500 * (1 << (attempt - 1)));
            free(accum.raw); free(accum.content); for (size_t call = 0; call < accum.call_count; call++) { free(accum.calls[call].id); free(accum.calls[call].name); free(accum.calls[call].arguments); } free(accum.calls); free(accum.usage_json); free(accum.error);
        }
        free(model); free(body);
    }
    router_plan_free(&plan); free(last_error); return 0;
#endif
}

#ifdef ZENO_HAVE_CURL
static size_t curl_write(void *data, size_t size, size_t count, void *context) {
    if (count > 0 && size > (size_t)-1 / count) return 0;
    HttpBuffer *buffer = (HttpBuffer *)context;
    size_t amount = size * count;
    return http_append(buffer, (const char *)data, amount) ? amount : 0;
}

static int curl_transport(void *context, const char *base_url, const char *api_key,
                          const char *body_json, int timeout_ms, char **response,
                          char **error_message) {
    (void)context;
    if (!curl_provider_inputs_valid(base_url, api_key, body_json, error_message)) return 0;
    CURL *curl = curl_easy_init(); if (curl == NULL) { if (error_message != NULL) *error_message = zeno_strdup("libcurl initialization failed"); return 0; }
    char *url = zeno_format("%s%s", base_url != NULL ? base_url : "", base_url != NULL && *base_url != '\0' && base_url[strlen(base_url) - 1] == '/' ? "chat/completions" : "/chat/completions");
    struct curl_slist *headers = NULL; headers = curl_slist_append(headers, "Content-Type: application/json");
    if (api_key != NULL && *api_key != '\0') { char *authorization = zeno_format("Authorization: Bearer %s", api_key); if (authorization != NULL) { headers = curl_slist_append(headers, authorization); free(authorization); } }
    HttpBuffer buffer = {0}; buffer.max_chars = 16U * 1024U * 1024U;
    curl_easy_setopt(curl, CURLOPT_URL, url != NULL ? url : ""); curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json != NULL ? body_json : "{}"); curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)(timeout_ms > 0 ? timeout_ms : 120000)); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer); curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZenoC/1.0");
    CURLcode code = curl_easy_perform(curl); long status = 0; (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    int ok = code == CURLE_OK && status >= 200 && status < 300;
    if (!ok && error_message != NULL) *error_message = code != CURLE_OK ? zeno_strdup(curl_easy_strerror(code)) : zeno_format("API error %ld: %s", status, buffer.data != NULL ? buffer.data : "");
    if (response != NULL) *response = buffer.data != NULL ? buffer.data : zeno_strdup(""); else free(buffer.data);
    curl_slist_free_all(headers); curl_easy_cleanup(curl); free(url); return ok;
}
#endif

#ifdef ZENO_HAVE_WINHTTP
static void winhttp_close(HINTERNET *handle) {
    if (*handle != NULL) { WinHttpCloseHandle(*handle); *handle = NULL; }
}

/* Minimal WinHTTP exchange used when libcurl is unavailable. Returns 1 when
 * the HTTP exchange completed (any status code), 0 on transport errors. The
 * response body is copied to *body_out and the HTTP status to *status_out. */
static int winhttp_exchange(const char *url, const char *method,
                            const char *headers_json, const char *body,
                            int timeout_ms, size_t max_chars,
                            char **body_out, long *status_out,
                            char **error_message) {
    if (body_out != NULL) *body_out = NULL;
    if (status_out != NULL) *status_out = 0;
    if (error_message != NULL) *error_message = NULL;
    if (url == NULL || *url == '\0') { if (error_message != NULL) *error_message = zeno_strdup("URL is empty"); return 0; }
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    if (wide_length <= 0 || wide_length > 8192) { if (error_message != NULL) *error_message = zeno_strdup("URL is invalid"); return 0; }
    wchar_t *wide_url = (wchar_t *)malloc((size_t)wide_length * sizeof(wchar_t));
    if (wide_url == NULL) { if (error_message != NULL) *error_message = zeno_strdup("Out of memory"); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wide_url, wide_length);
    URL_COMPONENTS components;
    memset(&components, 0, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = (DWORD)-1;
    components.dwUrlPathLength = (DWORD)-1;
    components.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wide_url, 0, 0, &components)) {
        free(wide_url);
        if (error_message != NULL) *error_message = zeno_strdup("URL could not be parsed");
        return 0;
    }
    wchar_t host[256];
    wchar_t path[4096];
    size_t host_length = (size_t)components.dwHostNameLength;
    size_t path_length = (size_t)components.dwUrlPathLength + (size_t)components.dwExtraInfoLength;
    if (host_length == 0 || host_length >= sizeof(host) / sizeof(host[0]) || path_length >= sizeof(path) / sizeof(path[0])) {
        free(wide_url);
        if (error_message != NULL) *error_message = zeno_strdup("URL host or path exceeds its size limit");
        return 0;
    }
    memcpy(host, components.lpszHostName, host_length * sizeof(wchar_t));
    host[host_length] = L'\0';
    if (path_length == 0) { path[0] = L'/'; path[1] = L'\0'; }
    else { memcpy(path, components.lpszUrlPath, path_length * sizeof(wchar_t)); path[path_length] = L'\0'; }
    int secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    INTERNET_PORT port = components.nPort;
    free(wide_url);
    const char *verb = method != NULL && *method != '\0' ? method : "GET";
    int wide_verb_length = MultiByteToWideChar(CP_UTF8, 0, verb, -1, NULL, 0);
    if (wide_verb_length <= 0 || wide_verb_length > 32) { if (error_message != NULL) *error_message = zeno_strdup("HTTP method is invalid"); return 0; }
    wchar_t wide_verb[32];
    MultiByteToWideChar(CP_UTF8, 0, verb, -1, wide_verb, wide_verb_length);
    HINTERNET session = NULL, connection = NULL, request = NULL;
    int ok = 0;
    session = WinHttpOpen(L"ZenoC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) { if (error_message != NULL) *error_message = zeno_strdup("WinHTTP session could not be created"); return 0; }
    int effective_timeout = timeout_ms > 0 ? timeout_ms : 60000;
    WinHttpSetTimeouts(session, effective_timeout, effective_timeout, effective_timeout, effective_timeout);
    connection = WinHttpConnect(session, host, port, 0);
    if (connection == NULL) { if (error_message != NULL) *error_message = zeno_strdup("WinHTTP connect failed"); goto cleanup; }
    DWORD request_flags = secure ? WINHTTP_FLAG_SECURE : 0;
    request = WinHttpOpenRequest(connection, wide_verb, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, request_flags);
    if (request == NULL) { if (error_message != NULL) *error_message = zeno_strdup("WinHTTP request could not be created"); goto cleanup; }
    if (headers_json != NULL && *headers_json != '\0') {
        char *parse_error = NULL; ZjNode *root = zj_parse(headers_json, &parse_error); free(parse_error);
        size_t header_count = 0; int valid = root != NULL && root->type == ZJ_OBJECT;
        char *wire = valid ? zeno_strdup("") : NULL;
        for (ZjPair *pair = valid ? root->object : NULL; pair != NULL; pair = pair->next) {
            const char *value = zj_string(pair->value);
            if (value == NULL || pair->key == NULL || strpbrk(pair->key, "\r\n:") != NULL || strpbrk(value, "\r\n") != NULL || strlen(pair->key) > 256U || strlen(value) > 8192U || ++header_count > 64U) { valid = 0; break; }
            char *next = zeno_format("%s%s: %s\r\n", wire != NULL ? wire : "", pair->key, value);
            free(wire); wire = next;
            if (wire == NULL) { valid = 0; break; }
        }
        zj_free(root);
        if (!valid || wire == NULL) {
            free(wire);
            if (error_message != NULL) *error_message = zeno_strdup("Invalid headers JSON or header value");
            goto cleanup;
        }
        int wide_headers_length = MultiByteToWideChar(CP_UTF8, 0, wire, -1, NULL, 0);
        if (wide_headers_length <= 1) {
            free(wire);
            if (error_message != NULL) *error_message = zeno_strdup("Headers could not be encoded");
            goto cleanup;
        }
        wchar_t *wide_headers = (wchar_t *)malloc((size_t)wide_headers_length * sizeof(wchar_t));
        if (wide_headers == NULL) {
            free(wire);
            if (error_message != NULL) *error_message = zeno_strdup("Out of memory");
            goto cleanup;
        }
        MultiByteToWideChar(CP_UTF8, 0, wire, -1, wide_headers, wide_headers_length);
        free(wire);
        BOOL added = WinHttpAddRequestHeaders(request, wide_headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        free(wide_headers);
        if (!added) {
            if (error_message != NULL) *error_message = zeno_strdup("Headers were rejected by WinHTTP");
            goto cleanup;
        }
    }
    DWORD body_length = body != NULL ? (DWORD)strlen(body) : 0;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body != NULL ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA, body_length, body_length, 0)) {
        if (error_message != NULL) *error_message = zeno_format("WinHTTP send failed (%lu)", (unsigned long)GetLastError());
        goto cleanup;
    }
    if (!WinHttpReceiveResponse(request, NULL)) {
        if (error_message != NULL) *error_message = zeno_format("WinHTTP receive failed (%lu)", (unsigned long)GetLastError());
        goto cleanup;
    }
    DWORD status = 0; DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) status = 0;
    if (status_out != NULL) *status_out = (long)status;
    HttpBuffer buffer; memset(&buffer, 0, sizeof(buffer));
    buffer.max_chars = max_chars > 0 && max_chars < ZENO_MAX_HTTP_RESPONSE ? max_chars : (max_chars >= ZENO_MAX_HTTP_RESPONSE ? ZENO_MAX_HTTP_RESPONSE : 8000);
    char chunk[8192];
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk, sizeof(chunk), &read)) {
            if (error_message != NULL) *error_message = zeno_format("WinHTTP read failed (%lu)", (unsigned long)GetLastError());
            free(buffer.data);
            goto cleanup;
        }
        if (read == 0) break;
        if (!http_append(&buffer, chunk, (size_t)read)) break;
    }
    if (body_out != NULL) *body_out = buffer.data != NULL ? buffer.data : zeno_strdup("");
    else free(buffer.data);
    ok = 1;
cleanup:
    winhttp_close(&request);
    winhttp_close(&connection);
    winhttp_close(&session);
    return ok;
}

/* Router transport: POST {base_url}/chat/completions with Bearer auth. */
static int winhttp_transport(void *context, const char *base_url, const char *api_key,
                             const char *body_json, int timeout_ms, char **response,
                             char **error_message) {
    (void)context;
    if (response != NULL) *response = NULL;
    if (base_url == NULL || *base_url == '\0' || body_json == NULL) {
        if (error_message != NULL) *error_message = zeno_strdup("Provider base URL and request body are required");
        return 0;
    }
    char *url = zeno_format("%s%s", base_url, base_url[strlen(base_url) - 1] == '/' ? "chat/completions" : "/chat/completions");
    if (url == NULL) { if (error_message != NULL) *error_message = zeno_strdup("Out of memory"); return 0; }
    char *headers = api_key != NULL && *api_key != '\0' ? zeno_format("{\"Authorization\":\"Bearer %s\",\"Content-Type\":\"application/json\"}", api_key) : zeno_strdup("{\"Content-Type\":\"application/json\"}");
    char *raw = NULL; long status = 0; char *error = NULL;
    int ok = headers != NULL && winhttp_exchange(url, "POST", headers, body_json, timeout_ms, ZENO_MAX_HTTP_RESPONSE, &raw, &status, &error);
    free(url); free(headers);
    if (!ok) {
        if (error_message != NULL) *error_message = error != NULL ? error : zeno_strdup("provider request failed");
        else free(error);
        free(raw);
        return 0;
    }
    if (status < 200 || status >= 300) {
        if (error_message != NULL) *error_message = zeno_format("API error %ld: %.512s", status, raw != NULL ? raw : "");
        free(raw);
        return 0;
    }
    free(error);
    if (response != NULL) *response = raw; else free(raw);
    if (getenv("ZENO_LLM_DEBUG") != NULL) fprintf(stderr, "[llm] winhttp status=%ld\n", status);
    return 1;
}
#endif /* ZENO_HAVE_WINHTTP */

int zeno_http_request(const char *url, const char *method, const char *headers_json,
                      const char *body, int timeout_ms, size_t max_chars,
                      char **response) {
    if (response != NULL) *response = NULL;
    if (url == NULL || *url == '\0') { if (response != NULL) *response = zeno_strdup("Request error: URL is empty."); return 0; }
    if (strlen(url) > 4096U || (body != NULL && strlen(body) > ZENO_MAX_HTTP_RESPONSE)) {
        if (response != NULL) *response = zeno_strdup("Request error: URL or request body exceeds its size limit.");
        return 0;
    }
    if (!external_http_url_allowed(url)) {
        if (response != NULL) *response = zeno_strdup("Request error: only public HTTP(S) URLs are allowed.");
        return 0;
    }
#ifndef ZENO_HAVE_CURL
#ifdef ZENO_HAVE_WINHTTP
    const char *win_verb = method != NULL && *method != '\0' ? method : "GET";
    size_t win_verb_length = strlen(win_verb);
    if (win_verb_length == 0 || win_verb_length > 32U) {
        if (response != NULL) *response = zeno_strdup("Request error: invalid HTTP method.");
        return 0;
    }
    for (size_t index = 0; index < win_verb_length; index++) {
        if (!isupper((unsigned char)win_verb[index])) {
            if (response != NULL) *response = zeno_strdup("Request error: HTTP method must contain uppercase letters only.");
            return 0;
        }
    }
    char *win_body = NULL; long win_status = 0; char *win_error = NULL;
    int win_ok = winhttp_exchange(url, win_verb, headers_json, body, timeout_ms, max_chars, &win_body, &win_status, &win_error);
    if (!win_ok) {
        if (response != NULL) *response = zeno_format("Request error: %s", win_error != NULL ? win_error : "request failed");
        free(win_error); free(win_body);
        return 0;
    }
    free(win_error);
    char *win_formatted = zeno_format("Status: %ld\nContent-Type: \n\n%s", win_status, win_body != NULL ? win_body : "");
    free(win_body);
    if (response != NULL) *response = win_formatted; else free(win_formatted);
    return 1;
#else
    (void)method; (void)headers_json; (void)body; (void)timeout_ms; (void)max_chars;
    if (response != NULL) *response = zeno_strdup("Request error: libcurl adapter is not enabled.");
    return 0;
#endif
#else
    const char *verb = method != NULL && *method != '\0' ? method : "GET";
    size_t verb_length = strlen(verb);
    if (verb_length == 0 || verb_length > 32U) {
        if (response != NULL) *response = zeno_strdup("Request error: invalid HTTP method.");
        return 0;
    }
    for (size_t index = 0; index < verb_length; index++) {
        if (!isupper((unsigned char)verb[index])) {
            if (response != NULL) *response = zeno_strdup("Request error: HTTP method must contain uppercase letters only.");
            return 0;
        }
    }
    CURL *curl = curl_easy_init(); if (curl == NULL) { if (response != NULL) *response = zeno_strdup("Request error: libcurl initialization failed."); return 0; }
    struct curl_slist *resolve = NULL;
    if (!external_http_resolve(url, &resolve)) {
        curl_easy_cleanup(curl);
        if (response != NULL) *response = zeno_strdup("Request error: DNS resolution was unavailable or resolved to a private address.");
        return 0;
    }
    struct curl_slist *headers = NULL; headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers_json != NULL && *headers_json != '\0') {
        char *parse_error = NULL; ZjNode *root = zj_parse(headers_json, &parse_error); free(parse_error);
        size_t header_count = 0; int headers_valid = root != NULL && root->type == ZJ_OBJECT;
        if (headers_valid) for (ZjPair *pair = root->object; pair != NULL; pair = pair->next) {
            const char *value = zj_string(pair->value);
            if (value == NULL || pair->key == NULL || strpbrk(pair->key, "\r\n:") != NULL || strpbrk(value, "\r\n") != NULL || strlen(pair->key) > 256U || strlen(value) > 8192U || ++header_count > 64U) { headers_valid = 0; break; }
            char *header = zeno_format("%s: %s", pair->key, value);
            if (header == NULL) { headers_valid = 0; break; }
            struct curl_slist *grown = curl_slist_append(headers, header);
            free(header);
            if (grown == NULL) { headers_valid = 0; break; }
            headers = grown;
        }
        zj_free(root);
        if (!headers_valid) {
            curl_slist_free_all(headers); curl_slist_free_all(resolve); curl_easy_cleanup(curl);
            if (response != NULL) *response = zeno_strdup("Request error: invalid headers JSON or header value.");
            return 0;
        }
    }
    HttpBuffer buffer = {0}; buffer.max_chars = max_chars > 0 && max_chars < ZENO_MAX_HTTP_RESPONSE ? max_chars : (max_chars >= ZENO_MAX_HTTP_RESPONSE ? ZENO_MAX_HTTP_RESPONSE : 8000);
    curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve); curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, verb); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); if (body != NULL) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body); curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)(timeout_ms > 0 ? timeout_ms : 60000)); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZenoC/1.0");
    CURLcode code = curl_easy_perform(curl); long status = 0; char *content_type = NULL; (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status); (void)curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
    const char *body_text = buffer.data != NULL ? buffer.data : "";
    char *formatted = code == CURLE_OK ? zeno_format("Status: %ld\nContent-Type: %s\n\n%s%s", status, content_type != NULL ? content_type : "", body_text, buffer.limited ? "\n\n... [truncated]" : "") : zeno_format("Request error: %s", curl_easy_strerror(code));
    if (response != NULL) *response = formatted; else free(formatted); free(buffer.data); curl_slist_free_all(headers); curl_slist_free_all(resolve); curl_easy_cleanup(curl); return code == CURLE_OK;
#endif
}

static char *strip_html(const char *html) {
    if (html == NULL) return zeno_strdup("");
    char *result = (char *)malloc(strlen(html) + 1); if (result == NULL) return NULL; size_t write = 0; int in_tag = 0; int in_script = 0; int in_style = 0;
    for (size_t index = 0; html[index] != '\0'; index++) {
        if (html[index] == '<') { in_tag = 1; if (starts_with_ci(html + index, "<script")) in_script = 1; if (starts_with_ci(html + index, "<style")) in_style = 1; if (starts_with_ci(html + index, "</script")) in_script = 0; if (starts_with_ci(html + index, "</style")) in_style = 0; continue; }
        if (in_tag) { if (html[index] == '>') { in_tag = 0; if (!in_script && !in_style) result[write++] = '\n'; } continue; }
        if (!in_script && !in_style) result[write++] = html[index];
    }
    result[write] = '\0';
    char *trimmed = zeno_trim_copy(result); free(result); return trimmed;
}

int zeno_scrape_url(const char *url, int timeout_ms, size_t max_chars, char **response) {
    size_t limit = max_chars > 0 && max_chars <= ZENO_MAX_SCRAPE_CHARS ? max_chars : 8000;
    size_t raw_limit = limit <= ZENO_MAX_HTTP_RESPONSE / 2 ? limit * 2 : ZENO_MAX_HTTP_RESPONSE;
    char *raw = NULL; int ok = zeno_http_request(url, "GET", NULL, NULL, timeout_ms > 0 ? timeout_ms : 30000, raw_limit, &raw);
    if (raw == NULL) { if (response != NULL) *response = zeno_strdup("Scraping error."); return 0; }
    char *text = strip_html(raw); char *bounded = text != NULL && strlen(text) > limit ? zeno_format("%.*s\n... [truncated]", (int)limit, text) : zeno_strdup(text != NULL ? text : "");
    if (response != NULL) *response = zeno_format("URL: %s\n\n%s", url != NULL ? url : "", bounded != NULL ? bounded : ""); else free(bounded); free(text); free(raw); return ok;
}

static void provider_free(ZenoProvider *provider) { free(provider->id); free(provider->base_url); free(provider->api_key); free(provider->models_csv); }

ZenoRouter *zeno_router_create(void) { return (ZenoRouter *)calloc(1, sizeof(ZenoRouter)); }

void zeno_router_destroy(ZenoRouter *router) { if (router == NULL) return; for (size_t index = 0; index < router->count; index++) provider_free(&router->providers[index]); free(router->providers); free(router->fallback_models); zeno_mutex_destroy(&router->lock); free(router); }

int zeno_router_add_provider(ZenoRouter *router, ZenoProviderConfig provider) {
    if (router == NULL || provider.id == NULL || provider.base_url == NULL) return 0;
    zeno_mutex_lock(&router->lock);
    if (router->count == router->capacity) {
        size_t next = router->capacity == 0 ? 4 : router->capacity * 2;
        if (next < router->capacity || next > (size_t)-1 / sizeof(*router->providers)) { zeno_mutex_unlock(&router->lock); return 0; }
        ZenoProvider *grown = (ZenoProvider *)realloc(router->providers, next * sizeof(*grown));
        if (grown == NULL) { zeno_mutex_unlock(&router->lock); return 0; }
        router->providers = grown; router->capacity = next;
    }
    ZenoProvider candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.id = zeno_strdup(provider.id);
    candidate.base_url = zeno_strdup(provider.base_url);
    candidate.api_key = zeno_strdup(provider.api_key != NULL ? provider.api_key : "");
    candidate.models_csv = zeno_strdup(provider.models_csv != NULL ? provider.models_csv : "");
    if (candidate.id == NULL || candidate.base_url == NULL || candidate.api_key == NULL || candidate.models_csv == NULL) {
        provider_free(&candidate); zeno_mutex_unlock(&router->lock); return 0;
    }
    candidate.priority = provider.priority;
    candidate.max_errors = provider.max_errors > 0 ? provider.max_errors : 3;
    candidate.timeout_ms = provider.timeout_ms > 0 ? provider.timeout_ms : 120000;
    zeno_copy_string(candidate.stats.id, sizeof(candidate.stats.id), provider.id);
    router->providers[router->count++] = candidate;
    zeno_mutex_unlock(&router->lock);
    return 1;
}

void zeno_router_set_fallback_models(ZenoRouter *router, const char *models_csv) { if (router == NULL) return; zeno_mutex_lock(&router->lock); free(router->fallback_models); router->fallback_models = zeno_strdup(models_csv != NULL ? models_csv : ""); zeno_mutex_unlock(&router->lock); }
void zeno_router_set_transport(ZenoRouter *router, ZenoTransport transport, void *context) { if (router != NULL) { zeno_mutex_lock(&router->lock); router->transport = transport; router->transport_context = context; zeno_mutex_unlock(&router->lock); } }
int zeno_router_has_providers(const ZenoRouter *router) {
    if (router == NULL) return 0;
    zeno_mutex_lock((ZenoMutex *)&router->lock);
    int available = router->count > 0;
    zeno_mutex_unlock((ZenoMutex *)&router->lock);
    return available;
}

static int transient_error(const char *error) { return error != NULL && (zeno_contains_ci(error, "timeout") || zeno_contains_ci(error, "connection") || zeno_contains_ci(error, "429") || zeno_contains_ci(error, "5")); }
static void sort_providers(ZenoRouter *router) { for (size_t i = 0; i < router->count; i++) for (size_t j = i + 1; j < router->count; j++) if (router->providers[j].priority < router->providers[i].priority) { ZenoProvider temp = router->providers[i]; router->providers[i] = router->providers[j]; router->providers[j] = temp; } }

int zeno_router_complete(ZenoRouter *router, const char *preferred_model,
                         const char *messages_json, const char *tools_json,
                         double temperature, int max_tokens,
                         char **response_json, char **used_provider,
                         char **used_model, int *cache_hit) {
    if (response_json != NULL) *response_json = NULL; if (used_provider != NULL) *used_provider = NULL; if (used_model != NULL) *used_model = NULL; if (cache_hit != NULL) *cache_hit = 0;
    RouterPlan plan; router_plan_snapshot(router, &plan);
    char *last_error = NULL;
    for (size_t index = 0; index < plan.count; index++) {
        RouterView *view = &plan.views[index]; long long now = zeno_now_ms();
        if (view->cooldown_until_ms > now || (view->max_errors > 0 && view->consecutive_errors >= (size_t)view->max_errors)) continue;
        char *model = provider_model_from(view->models_csv, preferred_model, plan.fallback_models != NULL ? plan.fallback_models : ""); if (model == NULL) continue;
        char *body = zeno_format("{\"model\":%s,\"messages\":%s,\"temperature\":%.3f,\"max_tokens\":%d%s%s}", zeno_json_escape(model), messages_json != NULL ? messages_json : "[]", temperature, max_tokens > 0 ? max_tokens : 4096, tools_json != NULL ? ",\"tools\":" : "", tools_json != NULL ? tools_json : "");
        for (int attempt = 1; attempt <= 3; attempt++) {
            long long started = zeno_now_ms(); char *response = NULL; char *error = NULL; ZenoTransport transport = plan.transport;
#ifndef ZENO_HAVE_CURL
#ifdef ZENO_HAVE_WINHTTP
            if (transport == NULL) transport = winhttp_transport;
#else
            if (transport == NULL) { error = zeno_strdup("No transport configured; build with libcurl or inject a transport."); }
#endif
#else
            if (transport == NULL) transport = curl_transport;
#endif
            int ok = transport != NULL && transport(plan.transport_context, view->base_url, view->api_key, body, view->timeout_ms, &response, &error);
            router_stats_record(router, view->id, ok, started, error);
            if (ok) {
                free(error);
                if (response_json != NULL) *response_json = response; else free(response);
                if (used_provider != NULL) *used_provider = zeno_strdup(view->id);
                if (used_model != NULL) *used_model = model; else free(model);
                free(last_error); free(body); router_plan_free(&plan); return 1;
            }
            free(response); free(last_error); last_error = error != NULL ? error : zeno_strdup("provider request failed");
            if (!transient_error(last_error) || attempt == 3) break;
            zeno_sleep_ms(500 * (1 << (attempt - 1)));
        }
        free(model); free(body);
    }
    router_plan_free(&plan); free(last_error); return 0;
}

char *zeno_router_stats_json(const ZenoRouter *router) { if (router != NULL) zeno_mutex_lock((ZenoMutex *)&router->lock); char *result = zeno_strdup("["); for (size_t index = 0; router != NULL && index < router->count; index++) { const ZenoProviderStats *stats = &router->providers[index].stats; char *item = zeno_format("{\"provider_id\":%s,\"total_calls\":%zu,\"success_calls\":%zu,\"error_calls\":%zu,\"success_rate\":%.3f,\"avg_latency\":%.1f,\"is_available\":%s,\"consecutive_errors\":%zu,\"last_error\":%s}", zeno_json_escape(stats->id), stats->total_calls, stats->success_calls, stats->error_calls, stats->total_calls > 0 ? (double)stats->success_calls / (double)stats->total_calls : 1.0, stats->average_latency, stats->cooldown_until_ms <= zeno_now_ms() ? "true" : "false", stats->consecutive_errors, zeno_json_escape(stats->last_error)); char *next = item != NULL ? zeno_json_array_append(result, item) : NULL; free(item); free(result); result = next; } if (router != NULL) zeno_mutex_unlock((ZenoMutex *)&router->lock); return result != NULL ? result : zeno_strdup("[]"); }

/* --- Adapters reais com libcurl: MCP e vision --- */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *data, size_t length) {
    size_t out_len = 4 * ((length + 2) / 3);
    char *output = (char *)malloc(out_len + 1);
    if (output == NULL) return NULL;
    size_t index = 0, offset = 0;
    while (offset + 2 < length) {
        uint32_t value = ((uint32_t)data[offset] << 16) | ((uint32_t)data[offset + 1] << 8) | data[offset + 2];
        output[index++] = base64_table[(value >> 18) & 63]; output[index++] = base64_table[(value >> 12) & 63]; output[index++] = base64_table[(value >> 6) & 63]; output[index++] = base64_table[value & 63]; offset += 3;
    }
    size_t remaining = length - offset;
    if (remaining == 1) { uint32_t value = (uint32_t)data[offset] << 16; output[index++] = base64_table[(value >> 18) & 63]; output[index++] = base64_table[(value >> 12) & 63]; output[index++] = '='; output[index++] = '='; }
    else if (remaining == 2) { uint32_t value = ((uint32_t)data[offset] << 16) | ((uint32_t)data[offset + 1] << 8); output[index++] = base64_table[(value >> 18) & 63]; output[index++] = base64_table[(value >> 12) & 63]; output[index++] = base64_table[(value >> 6) & 63]; output[index++] = '='; }
    output[index] = '\0';
    return output;
}

static char *mcp_rpc_request(const char *server_url, const char *method, const char *params_json) {
#ifdef ZENO_HAS_HTTP_TRANSPORT
    char *body = zeno_format("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":%s,\"params\":%s}", zeno_json_escape(method), params_json != NULL && *params_json != '\0' ? params_json : "{}");
    char *response = NULL;
    int ok = zeno_http_request(server_url, "POST", NULL, body, 30000, 200000, &response);
    free(body);
    if (!ok) { free(response); return NULL; }
    return response;
#else
    (void)server_url; (void)method; (void)params_json;
    return NULL;
#endif
}

char *zeno_mcp_call(const char *server_url, const char *tool_name, const char *args_json) {
    if (server_url == NULL || *server_url == '\0' || tool_name == NULL || *tool_name == '\0') return zeno_strdup("MCP error: server_url and tool_name are required.");
#ifndef ZENO_HAS_HTTP_TRANSPORT
    (void)args_json;
    return zeno_strdup("MCP adapter not enabled; rebuild with ZENO_ENABLE_CURL=ON and libcurl.");
#else
    char *arguments = zeno_strdup(args_json != NULL && *args_json != '\0' ? args_json : "{}");
    char *params = zeno_format("{\"name\":%s,\"arguments\":%s}", zeno_json_escape(tool_name), arguments);
    char *response = mcp_rpc_request(server_url, "tools/call", params);
    free(arguments); free(params);
    if (response == NULL) return zeno_format("MCP error: request to %s failed; check that the server is reachable.", server_url);
    char *error_message = zeno_json_get_path_string(response, "error.message");
    if (error_message != NULL) { char *result = zeno_format("MCP error: %s", error_message); free(error_message); free(response); return result; }
    char *text = zeno_json_get_path_string(response, "result.content[0].text");
    if (text != NULL) { free(response); return text; }
    char *fallback = zeno_format("{\"server\":%s,\"tool\":%s,\"ok\":true,\"result\":%s}", zeno_json_escape(server_url), zeno_json_escape(tool_name), response);
    free(response);
    return fallback;
#endif
}

char *zeno_mcp_list_tools(const char *server_url) {
    if (server_url == NULL || *server_url == '\0') return zeno_strdup("[]");
#ifndef ZENO_HAS_HTTP_TRANSPORT
    return zeno_strdup("MCP adapter not enabled; rebuild with ZENO_ENABLE_CURL=ON and libcurl.");
#else
    char *response = mcp_rpc_request(server_url, "tools/list", "{}");
    if (response == NULL) return zeno_format("MCP error: request to %s failed; check that the server is reachable.", server_url);
    char *error_message = zeno_json_get_path_string(response, "error.message");
    if (error_message != NULL) { char *result = zeno_format("MCP error: %s", error_message); free(error_message); free(response); return result; }
    char *error = NULL;
    ZjNode *root = zj_parse(response, &error);
    free(error);
    char *result = NULL;
    if (root != NULL) {
        ZjNode *tools = zj_object_get(zj_object_get(root, "result"), "tools");
        if (tools != NULL && tools->type == ZJ_ARRAY) result = zj_stringify_compact(tools);
        zj_free(root);
    }
    free(response);
    return result != NULL ? result : zeno_strdup("[]");
#endif
}

/* --- MCP over stdio: newline-delimited JSON-RPC to a spawned server process.
 * Performs the MCP initialize handshake, sends notifications/initialized, then
 * the target request (id 2), and returns the last response line with id 2. --- */
int zeno_process_command_stdin(const char *command, const char *cwd, const char *stdin_input,
                               int timeout_ms, size_t max_output, ZenoExecResult *result);

static char *mcp_stdio_roundtrip(const char *command, const char *method, const char *params_json) {
    if (command == NULL || *command == '\0') return NULL;
    char *initialize = zeno_format("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"zenoc\",\"version\":\"1.3.0\"}}}\n");
    char *request = zeno_format("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":%s,\"params\":%s}\n", zeno_json_escape(method), params_json != NULL && *params_json != '\0' ? params_json : "{}");
    char *input = zeno_format("%s{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n%s", initialize != NULL ? initialize : "", request != NULL ? request : "");
    free(initialize);
    free(request);
    if (input == NULL) return NULL;
    ZenoExecResult exec;
    memset(&exec, 0, sizeof(exec));
    int ran = zeno_process_command_stdin(command, NULL, input, 30000, 200000, &exec);
    free(input);
    if (getenv("ZENO_MCP_DEBUG") != NULL)
        fprintf(stderr, "[mcp-stdio] ran=%d exit=%d timed_out=%d output=<%.800s>\n", ran, exec.exit_code, exec.timed_out, exec.output != NULL ? exec.output : "");
    if (!ran) { free(exec.output); free(exec.reason); return NULL; }
    char *found = NULL;
    const char *cursor = exec.output != NULL ? exec.output : "";
    while (*cursor != '\0' && found == NULL) {
        const char *line_end = strchr(cursor, '\n');
        size_t line_length = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);
        char *line = zeno_strndup(cursor, line_length);
        if (line != NULL) {
            char *error = NULL;
            ZjNode *root = zj_parse(line, &error);
            free(error);
            if (root != NULL) {
                if (zj_integer(zj_object_get(root, "id"), 0) == 2) found = zj_stringify_compact(root);
                zj_free(root);
            }
            free(line);
        }
        cursor = line_end != NULL ? line_end + 1 : cursor + line_length;
    }
    free(exec.output);
    free(exec.reason);
    if (found == NULL && getenv("ZENO_MCP_DEBUG") != NULL) fprintf(stderr, "[mcp-stdio] no id==2 response parsed\n");
    return found;
}

char *zeno_mcp_call_stdio(const char *command, const char *tool_name, const char *args_json) {
    if (command == NULL || *command == '\0' || tool_name == NULL || *tool_name == '\0') return zeno_strdup("MCP error: command and tool_name are required.");
    char *arguments = zeno_strdup(args_json != NULL && *args_json != '\0' ? args_json : "{}");
    char *params = zeno_format("{\"name\":%s,\"arguments\":%s}", zeno_json_escape(tool_name), arguments);
    char *response = mcp_stdio_roundtrip(command, "tools/call", params);
    free(arguments);
    free(params);
    if (response == NULL) return zeno_format("MCP error: stdio server '%s' did not answer tools/call; check the command and its stderr.", command);
    char *error_message = zeno_json_get_path_string(response, "error.message");
    if (error_message != NULL) { char *result = zeno_format("MCP error: %s", error_message); free(error_message); free(response); return result; }
    char *text = zeno_json_get_path_string(response, "result.content[0].text");
    if (text != NULL) { free(response); return text; }
    return response;
}

char *zeno_mcp_list_tools_stdio(const char *command) {
    if (command == NULL || *command == '\0') return zeno_strdup("[]");
    char *response = mcp_stdio_roundtrip(command, "tools/list", "{}");
    if (response == NULL) return zeno_format("MCP error: stdio server '%s' did not answer tools/list.", command);
    char *error_message = zeno_json_get_path_string(response, "error.message");
    if (error_message != NULL) { char *result = zeno_format("MCP error: %s", error_message); free(error_message); free(response); return result; }
    char *error = NULL;
    ZjNode *root = zj_parse(response, &error);
    free(error);
    free(response);
    char *result = NULL;
    if (root != NULL) {
        ZjNode *tools = zj_object_get(zj_object_get(root, "result"), "tools");
        if (tools != NULL && tools->type == ZJ_ARRAY) result = zj_stringify_compact(tools);
        zj_free(root);
    }
    return result != NULL ? result : zeno_strdup("[]");
}

char *zeno_vision_analyze(const char *image_path, const char *prompt) {
    if (image_path == NULL) return zeno_strdup("Vision error: image path is required.");
#ifndef ZENO_HAS_HTTP_TRANSPORT
    (void)prompt;
    return zeno_strdup("Vision adapter not enabled. Build with libcurl and configure OPENAI_API_KEY to enable multimodal analysis.");
#else
    const char *api_key = getenv("OPENAI_API_KEY");
    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("VISION_MODEL");
    if (api_key == NULL || *api_key == '\0') return zeno_strdup("Vision error: OPENAI_API_KEY is not configured.");
    if (model == NULL || *model == '\0') model = "gpt-4o-mini";
    FILE *file = fopen(image_path, "rb");
    if (file == NULL) return zeno_format("Vision error: cannot read image: %s", image_path);
    char *raw = NULL; int limited = 0;
    if (!zeno_read_all(file, 20U * 1024U * 1024U, &raw, &limited)) { (void)fclose(file); return zeno_strdup("Vision error: image too large or unreadable."); }
    (void)fclose(file);
    if (limited) { free(raw); return zeno_strdup("Vision error: image exceeds 20 MB limit."); }
    char *encoded = base64_encode((const unsigned char *)raw, strlen(raw)); free(raw);
    if (encoded == NULL) return zeno_strdup("Vision error: out of memory.");
    char *body = zeno_format("{\"model\":%s,\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":%s},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,%s\"}}]}],\"max_tokens\":1024}", zeno_json_escape(model), zeno_json_escape(prompt != NULL ? prompt : "Descreva esta imagem."), encoded);
    free(encoded);
    char *endpoint = zeno_format("%s%s", base_url != NULL && *base_url != '\0' ? base_url : "https://openrouter.ai/api/v1", (base_url != NULL && base_url[strlen(base_url) - 1] == '/') || base_url == NULL ? "chat/completions" : "/chat/completions");
    char *response = NULL; char *headers = NULL;
    int ok = zeno_http_request(endpoint, "POST", NULL, body, 60000, 200000, &response);
    char *result = NULL;
    if (ok) { char *content = zeno_json_get_path_string(response, "choices[0].message.content"); result = content != NULL ? content : zeno_strdup(response != NULL ? response : "{}"); }
    else result = zeno_format("Vision error: %s", response != NULL ? response : "request failed");
    free(endpoint); free(body); free(response); free(headers);
    return result;
#endif
}
