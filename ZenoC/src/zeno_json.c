#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "zeno_internal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define ZENO_MKDIR(path) _mkdir(path)
#define ZENO_PATH_SEP '\\'
#else
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#define ZENO_MKDIR(path) mkdir(path, 0755)
#define ZENO_PATH_SEP '/'
#endif

static ZjNode *zj_new(ZjType type) {
    ZjNode *node = (ZjNode *)calloc(1, sizeof(*node));
    if (node != NULL) node->type = type;
    return node;
}

static void parser_skip(const char **cursor) {
    while (**cursor != '\0' && isspace((unsigned char)**cursor)) (*cursor)++;
}

static void parser_error(char **error, const char *message) {
    if (error != NULL && *error == NULL) *error = zeno_strdup(message);
}

static int append_char(char **buffer, size_t *length, size_t *capacity, char value) {
    if (*length + 1 >= *capacity) {
        size_t next = *capacity == 0 ? 32 : *capacity * 2;
        char *grown = (char *)realloc(*buffer, next);
        if (grown == NULL) return 0;
        *buffer = grown;
        *capacity = next;
    }
    (*buffer)[(*length)++] = value;
    (*buffer)[*length] = '\0';
    return 1;
}

static int append_utf8(char **buffer, size_t *length, size_t *capacity, unsigned long value) {
    if (value <= 0x7fUL) return append_char(buffer, length, capacity, (char)value);
    if (value <= 0x7ffUL) {
        return append_char(buffer, length, capacity, (char)(0xc0UL | (value >> 6))) &&
               append_char(buffer, length, capacity, (char)(0x80UL | (value & 0x3fUL)));
    }
    if (value <= 0xffffUL) {
        return append_char(buffer, length, capacity, (char)(0xe0UL | (value >> 12))) &&
               append_char(buffer, length, capacity, (char)(0x80UL | ((value >> 6) & 0x3fUL))) &&
               append_char(buffer, length, capacity, (char)(0x80UL | (value & 0x3fUL)));
    }
    if (value <= 0x10ffffUL) {
        return append_char(buffer, length, capacity, (char)(0xf0UL | (value >> 18))) &&
               append_char(buffer, length, capacity, (char)(0x80UL | ((value >> 12) & 0x3fUL))) &&
               append_char(buffer, length, capacity, (char)(0x80UL | ((value >> 6) & 0x3fUL))) &&
               append_char(buffer, length, capacity, (char)(0x80UL | (value & 0x3fUL)));
    }
    return append_char(buffer, length, capacity, '?');
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static char *parse_string_value(const char **cursor, char **error) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    if (**cursor != '"') {
        parser_error(error, "expected string");
        return NULL;
    }
    (*cursor)++;
    while (**cursor != '\0' && **cursor != '"') {
        unsigned char current = (unsigned char)*(*cursor)++;
        if (current < 0x20U) {
            parser_error(error, "control character in string");
            free(buffer);
            return NULL;
        }
        if (current != '\\') {
            if (!append_char(&buffer, &length, &capacity, (char)current)) break;
            continue;
        }
        current = (unsigned char)*(*cursor)++;
        switch (current) {
            case '"': if (!append_char(&buffer, &length, &capacity, '"')) goto oom; break;
            case '\\': if (!append_char(&buffer, &length, &capacity, '\\')) goto oom; break;
            case '/': if (!append_char(&buffer, &length, &capacity, '/')) goto oom; break;
            case 'b': if (!append_char(&buffer, &length, &capacity, '\b')) goto oom; break;
            case 'f': if (!append_char(&buffer, &length, &capacity, '\f')) goto oom; break;
            case 'n': if (!append_char(&buffer, &length, &capacity, '\n')) goto oom; break;
            case 'r': if (!append_char(&buffer, &length, &capacity, '\r')) goto oom; break;
            case 't': if (!append_char(&buffer, &length, &capacity, '\t')) goto oom; break;
            case 'u': {
                unsigned long codepoint = 0;
                int index;
                for (index = 0; index < 4; index++) {
                    int value = hex_value(*(*cursor)++);
                    if (value < 0) {
                        parser_error(error, "invalid unicode escape");
                        free(buffer);
                        return NULL;
                    }
                    codepoint = codepoint * 16UL + (unsigned long)value;
                }
                if (codepoint >= 0xd800UL && codepoint <= 0xdbffUL &&
                    (*cursor)[0] == '\\' && (*cursor)[1] == 'u') {
                    unsigned long low = 0;
                    *cursor += 2;
                    for (index = 0; index < 4; index++) {
                        int value = hex_value(*(*cursor)++);
                        if (value < 0) {
                            parser_error(error, "invalid unicode surrogate");
                            free(buffer);
                            return NULL;
                        }
                        low = low * 16UL + (unsigned long)value;
                    }
                    if (low >= 0xdc00UL && low <= 0xdfffUL) {
                        codepoint = 0x10000UL + ((codepoint - 0xd800UL) << 10) + (low - 0xdc00UL);
                    }
                }
                if (!append_utf8(&buffer, &length, &capacity, codepoint)) goto oom;
                break;
            }
            default:
                parser_error(error, "invalid string escape");
                free(buffer);
                return NULL;
        }
    }
    if (**cursor != '"') {
        parser_error(error, "unterminated string");
        free(buffer);
        return NULL;
    }
    (*cursor)++;
    if (buffer == NULL) buffer = zeno_strdup("");
    return buffer;
oom:
    parser_error(error, "out of memory");
    free(buffer);
    return NULL;
}

#define ZENO_JSON_MAX_DEPTH 128U

static ZjNode *parse_value(const char **cursor, char **error, size_t depth);

static ZjNode *parse_array(const char **cursor, char **error, size_t depth) {
    if (depth > ZENO_JSON_MAX_DEPTH) { parser_error(error, "JSON nesting exceeds the maximum depth"); return NULL; }
    ZjNode *node = zj_new(ZJ_ARRAY);
    if (node == NULL) {
        parser_error(error, "out of memory");
        return NULL;
    }
    (*cursor)++;
    parser_skip(cursor);
    if (**cursor == ']') {
        (*cursor)++;
        return node;
    }
    while (**cursor != '\0') {
        ZjNode *item = parse_value(cursor, error, depth + 1U);
        if (item == NULL) {
            zj_free(node);
            return NULL;
        }
        ZjNode **grown = (ZjNode **)realloc(node->items, (node->count + 1) * sizeof(*grown));
        if (grown == NULL) {
            zj_free(item);
            zj_free(node);
            parser_error(error, "out of memory");
            return NULL;
        }
        node->items = grown;
        node->items[node->count++] = item;
        parser_skip(cursor);
        if (**cursor == ']') {
            (*cursor)++;
            return node;
        }
        if (**cursor != ',') {
            parser_error(error, "expected comma in array");
            zj_free(node);
            return NULL;
        }
        (*cursor)++;
        parser_skip(cursor);
    }
    parser_error(error, "unterminated array");
    zj_free(node);
    return NULL;
}

static ZjNode *parse_object(const char **cursor, char **error, size_t depth) {
    if (depth > ZENO_JSON_MAX_DEPTH) { parser_error(error, "JSON nesting exceeds the maximum depth"); return NULL; }
    ZjNode *node = zj_new(ZJ_OBJECT);
    if (node == NULL) {
        parser_error(error, "out of memory");
        return NULL;
    }
    (*cursor)++;
    parser_skip(cursor);
    if (**cursor == '}') {
        (*cursor)++;
        return node;
    }
    while (**cursor != '\0') {
        char *key = parse_string_value(cursor, error);
        if (key == NULL) {
            zj_free(node);
            return NULL;
        }
        parser_skip(cursor);
        if (**cursor != ':') {
            free(key);
            parser_error(error, "expected colon in object");
            zj_free(node);
            return NULL;
        }
        (*cursor)++;
        parser_skip(cursor);
        ZjNode *value = parse_value(cursor, error, depth + 1U);
        if (value == NULL) {
            free(key);
            zj_free(node);
            return NULL;
        }
        ZjPair *pair = (ZjPair *)calloc(1, sizeof(*pair));
        if (pair == NULL) {
            free(key);
            zj_free(value);
            zj_free(node);
            parser_error(error, "out of memory");
            return NULL;
        }
        pair->key = key;
        pair->value = value;
        pair->next = node->object;
        node->object = pair;
        parser_skip(cursor);
        if (**cursor == '}') {
            (*cursor)++;
            return node;
        }
        if (**cursor != ',') {
            parser_error(error, "expected comma in object");
            zj_free(node);
            return NULL;
        }
        (*cursor)++;
        parser_skip(cursor);
    }
    parser_error(error, "unterminated object");
    zj_free(node);
    return NULL;
}

static ZjNode *parse_value(const char **cursor, char **error, size_t depth) {
    if (depth > ZENO_JSON_MAX_DEPTH) { parser_error(error, "JSON nesting exceeds the maximum depth"); return NULL; }
    parser_skip(cursor);
    if (**cursor == '"') {
        ZjNode *node = zj_new(ZJ_STRING);
        if (node == NULL) {
            parser_error(error, "out of memory");
            return NULL;
        }
        node->string = parse_string_value(cursor, error);
        if (node->string == NULL) {
            zj_free(node);
            return NULL;
        }
        return node;
    }
    if (**cursor == '{') return parse_object(cursor, error, depth);
    if (**cursor == '[') return parse_array(cursor, error, depth);
    if (strncmp(*cursor, "true", 4) == 0) {
        ZjNode *node = zj_new(ZJ_BOOL);
        if (node == NULL) return NULL;
        node->boolean = 1;
        *cursor += 4;
        return node;
    }
    if (strncmp(*cursor, "false", 5) == 0) {
        ZjNode *node = zj_new(ZJ_BOOL);
        if (node == NULL) return NULL;
        node->boolean = 0;
        *cursor += 5;
        return node;
    }
    if (strncmp(*cursor, "null", 4) == 0) {
        ZjNode *node = zj_new(ZJ_NULL);
        if (node == NULL) return NULL;
        *cursor += 4;
        return node;
    }
    if (**cursor == '-' || isdigit((unsigned char)**cursor)) {
        char *end = NULL;
        double number = strtod(*cursor, &end);
        if (end == *cursor || !isfinite(number)) {
            parser_error(error, "invalid number");
            return NULL;
        }
        ZjNode *node = zj_new(ZJ_NUMBER);
        if (node == NULL) {
            parser_error(error, "out of memory");
            return NULL;
        }
        node->number = number;
        *cursor = end;
        return node;
    }
    parser_error(error, "unexpected JSON token");
    return NULL;
}

ZjNode *zj_parse(const char *text, char **error) {
    const char *cursor = text != NULL ? text : "";
    if (error != NULL) *error = NULL;
    ZjNode *node = parse_value(&cursor, error, 0);
    if (node == NULL) return NULL;
    parser_skip(&cursor);
    if (*cursor != '\0') {
        parser_error(error, "trailing JSON data");
        zj_free(node);
        return NULL;
    }
    return node;
}

void zj_free(ZjNode *node) {
    if (node == NULL) return;
    free(node->string);
    for (size_t index = 0; index < node->count; index++) zj_free(node->items[index]);
    free(node->items);
    ZjPair *pair = node->object;
    while (pair != NULL) {
        ZjPair *next = pair->next;
        free(pair->key);
        zj_free(pair->value);
        free(pair);
        pair = next;
    }
    free(node);
}

ZjNode *zj_object_get(const ZjNode *node, const char *key) {
    if (node == NULL || node->type != ZJ_OBJECT || key == NULL) return NULL;
    for (ZjPair *pair = node->object; pair != NULL; pair = pair->next) {
        if (strcmp(pair->key, key) == 0) return pair->value;
    }
    return NULL;
}

ZjNode *zj_array_get(const ZjNode *node, size_t index) {
    if (node == NULL || node->type != ZJ_ARRAY || index >= node->count) return NULL;
    return node->items[index];
}

const char *zj_string(const ZjNode *node) {
    return node != NULL && node->type == ZJ_STRING ? node->string : NULL;
}

int zj_bool(const ZjNode *node, int fallback) {
    return node != NULL && node->type == ZJ_BOOL ? node->boolean : fallback;
}

long long zj_integer(const ZjNode *node, long long fallback) {
    if (node == NULL || node->type != ZJ_NUMBER) return fallback;
    return (long long)node->number;
}

static int json_append(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t amount = strlen(text);
    if (*length + amount + 1 > *capacity) {
        size_t next = *capacity == 0 ? 64 : *capacity;
        while (next < *length + amount + 1) next *= 2;
        char *grown = (char *)realloc(*buffer, next);
        if (grown == NULL) return 0;
        *buffer = grown;
        *capacity = next;
    }
    memcpy(*buffer + *length, text, amount);
    *length += amount;
    (*buffer)[*length] = '\0';
    return 1;
}

static char *stringify_node(const ZjNode *node, int pretty, int indent) {
    (void)indent;
    if (node == NULL) return zeno_strdup("null");
    if (node->type == ZJ_STRING) return zeno_json_escape(node->string != NULL ? node->string : "");
    if (node->type == ZJ_NULL) return zeno_strdup("null");
    if (node->type == ZJ_BOOL) return zeno_strdup(node->boolean ? "true" : "false");
    if (node->type == ZJ_NUMBER) return zeno_format("%.17g", node->number);
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const char *open = node->type == ZJ_ARRAY ? "[" : "{";
    const char *close = node->type == ZJ_ARRAY ? "]" : "}";
    if (!json_append(&result, &length, &capacity, open)) return NULL;
    if (node->type == ZJ_ARRAY) {
        for (size_t index = 0; index < node->count; index++) {
            if (index > 0 && !json_append(&result, &length, &capacity, ",")) goto oom;
            if (pretty && !json_append(&result, &length, &capacity, " ")) goto oom;
            char *item = stringify_node(node->items[index], pretty, indent + 1);
            if (item == NULL || !json_append(&result, &length, &capacity, item)) {
                free(item);
                goto oom;
            }
            free(item);
        }
    } else {
        size_t count = 0;
        for (ZjPair *pair = node->object; pair != NULL; pair = pair->next) count++;
        ZjPair **pairs = count > 0 ? (ZjPair **)calloc(count, sizeof(*pairs)) : NULL;
        size_t index = count;
        for (ZjPair *pair = node->object; pair != NULL; pair = pair->next) pairs[--index] = pair;
        for (index = 0; index < count; index++) {
            if (index > 0 && !json_append(&result, &length, &capacity, ",")) {
                free(pairs);
                goto oom;
            }
            if (pretty && !json_append(&result, &length, &capacity, " ")) {
                free(pairs);
                goto oom;
            }
            char *key = zeno_json_escape(pairs[index]->key);
            char *value = stringify_node(pairs[index]->value, pretty, indent + 1);
            if (key == NULL || value == NULL || !json_append(&result, &length, &capacity, key) ||
                !json_append(&result, &length, &capacity, ":") ||
                (pretty && !json_append(&result, &length, &capacity, " ")) ||
                !json_append(&result, &length, &capacity, value)) {
                free(key);
                free(value);
                free(pairs);
                goto oom;
            }
            free(key);
            free(value);
        }
        free(pairs);
    }
    if (!json_append(&result, &length, &capacity, close)) goto oom;
    return result;
oom:
    free(result);
    return NULL;
}

char *zj_stringify(const ZjNode *node) { return stringify_node(node, 1, 0); }
char *zj_stringify_compact(const ZjNode *node) { return stringify_node(node, 0, 0); }

int zj_object_has(const ZjNode *node, const char *key) { return zj_object_get(node, key) != NULL; }

static ZjNode *json_path_node(const ZjNode *root, const char *path) {
    const ZjNode *current = root;
    const char *cursor = path;
    char token[128];
    while (current != NULL && cursor != NULL && *cursor != '\0') {
        size_t length = 0;
        if (*cursor == '.') cursor++;
        while (*cursor != '\0' && *cursor != '.' && *cursor != '[' && length + 1 < sizeof(token)) {
            token[length++] = *cursor++;
        }
        token[length] = '\0';
        if (length > 0) current = zj_object_get(current, token);
        while (*cursor == '[') {
            cursor++;
            char *end = NULL;
            unsigned long index = strtoul(cursor, &end, 10);
            if (end == cursor || *end != ']') return NULL;
            current = zj_array_get(current, (size_t)index);
            cursor = end + 1;
        }
    }
    return (ZjNode *)current;
}

int zeno_json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char *error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &error);
    free(error);
    if (root == NULL || out == NULL || out_size == 0) {
        zj_free(root);
        if (out != NULL && out_size > 0) out[0] = '\0';
        return 0;
    }
    const char *value = zj_string(zj_object_get(root, key));
    int ok = value != NULL;
    if (ok) zeno_copy_string(out, out_size, value);
    else out[0] = '\0';
    zj_free(root);
    return ok;
}

long zeno_json_get_int(const char *json, const char *key, long fallback) {
    char *error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &error);
    free(error);
    long result = root != NULL ? (long)zj_integer(zj_object_get(root, key), fallback) : fallback;
    zj_free(root);
    return result;
}

int zeno_json_get_bool(const char *json, const char *key, int fallback) {
    char *error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &error);
    free(error);
    int result = root != NULL ? zj_bool(zj_object_get(root, key), fallback) : fallback;
    zj_free(root);
    return result;
}

char *zeno_json_get_path_string(const char *json, const char *path) {
    char *error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &error);
    free(error);
    if (root == NULL) return NULL;
    const char *value = zj_string(json_path_node(root, path));
    char *result = value != NULL ? zeno_strdup(value) : NULL;
    zj_free(root);
    return result;
}

/* Escape with UTF-8 validation: shell and compiler output can carry bytes in
 * the local codepage (e.g. CP850). Invalid sequences become '?' so payloads
 * sent to LLM providers and persisted JSON stay valid UTF-8. */
static int utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80U) return 1;
    if (lead >= 0xC2U && lead <= 0xDFU) return 2;
    if (lead >= 0xE0U && lead <= 0xEFU) return 3;
    if (lead >= 0xF0U && lead <= 0xF4U) return 4;
    return 0;
}

static int utf8_valid(const unsigned char *bytes, int length) {
    for (int index = 1; index < length; index++) if ((bytes[index] & 0xC0U) != 0x80U) return 0;
    return 1;
}

char *zeno_json_escape(const char *text) {
    const unsigned char *cursor = (const unsigned char *)(text != NULL ? text : "");
    size_t length = 2;
    while (*cursor != '\0') {
        int sequence = utf8_sequence_length(*cursor);
        if (sequence == 0) { length += 1; cursor++; continue; }
        if (*cursor == '"' || *cursor == '\\' || *cursor < 0x20U) length += 2;
        else length += (size_t)sequence;
        cursor += sequence;
    }
    char *result = (char *)malloc(length + 1);
    if (result == NULL) return NULL;
    size_t index = 0;
    result[index++] = '"';
    cursor = (const unsigned char *)(text != NULL ? text : "");
    while (*cursor != '\0') {
        int sequence = utf8_sequence_length(*cursor);
        if (sequence == 0 || !utf8_valid(cursor, sequence)) { result[index++] = '?'; cursor += sequence > 0 ? sequence : 1; continue; }
        unsigned char value = *cursor;
        int copied = sequence;
        switch (value) {
            case '"': result[index++] = '\\'; result[index++] = '"'; break;
            case '\\': result[index++] = '\\'; result[index++] = '\\'; break;
            case '\b': result[index++] = '\\'; result[index++] = 'b'; break;
            case '\f': result[index++] = '\\'; result[index++] = 'f'; break;
            case '\n': result[index++] = '\\'; result[index++] = 'n'; break;
            case '\r': result[index++] = '\\'; result[index++] = 'r'; break;
            case '\t': result[index++] = '\\'; result[index++] = 't'; break;
            default:
                if (value < 0x20U) {
                    (void)snprintf(result + index, 7, "\\u%04x", value);
                    index += 6;
                } else for (int byte_index = 0; byte_index < copied; byte_index++) result[index++] = (char)cursor[byte_index];
                break;
        }
        cursor += sequence;
    }
    result[index++] = '"';
    result[index] = '\0';
    return result;
}

char *zeno_strdup(const char *text) {
    const char *source = text != NULL ? text : "";
    size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, source, length + 1);
    return copy;
}

char *zeno_strndup(const char *text, size_t length) {
    const char *source = text != NULL ? text : "";
    size_t actual = strlen(source);
    if (actual > length) actual = length;
    char *copy = (char *)malloc(actual + 1);
    if (copy != NULL) {
        memcpy(copy, source, actual);
        copy[actual] = '\0';
    }
    return copy;
}

char *zeno_format(const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(args);
        return NULL;
    }
    char *result = (char *)malloc((size_t)length + 1);
    if (result != NULL) (void)vsnprintf(result, (size_t)length + 1, format, args);
    va_end(args);
    return result;
}

long long zeno_now_ms(void) {
#ifdef _WIN32
    FILETIME utc; GetSystemTimeAsFileTime(&utc);
    ULARGE_INTEGER raw; raw.LowPart = utc.dwLowDateTime; raw.HighPart = utc.dwHighDateTime;
    return (long long)(raw.QuadPart / 10000ULL) - 11644473600000LL;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return (long long)time(NULL) * 1000LL;
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
#endif
}

void zeno_sleep_ms(int milliseconds) {
    if (milliseconds <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) { }
#endif
}

int zeno_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) return 0;
    const char *value = src != NULL ? src : "";
    size_t length = strlen(value);
    if (length >= dst_size) length = dst_size - 1;
    memcpy(dst, value, length);
    dst[length] = '\0';
    return src != NULL && strlen(src) < dst_size;
}

void zeno_set_error(char *dst, size_t size, const char *format, ...) {
    if (dst == NULL || size == 0) return;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(dst, size, format, args);
    va_end(args);
}

char *zeno_trim_copy(const char *text) {
    const char *value = text != NULL ? text : "";
    while (*value != '\0' && isspace((unsigned char)*value)) value++;
    const char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) end--;
    return zeno_strndup(value, (size_t)(end - value));
}

char *zeno_lower_copy(const char *text) {
    char *result = zeno_strdup(text);
    if (result == NULL) return NULL;
    for (char *cursor = result; *cursor != '\0'; cursor++) *cursor = (char)tolower((unsigned char)*cursor);
    return result;
}

int zeno_contains_ci(const char *text, const char *needle) {
    if (text == NULL || needle == NULL) return 0;
    char *lower_text = zeno_lower_copy(text);
    char *lower_needle = zeno_lower_copy(needle);
    int found = lower_text != NULL && lower_needle != NULL && strstr(lower_text, lower_needle) != NULL;
    free(lower_text);
    free(lower_needle);
    return found;
}

static void normalize_slashes(char *text) {
    if (text == NULL) return;
    for (char *cursor = text; *cursor != '\0'; cursor++) if (*cursor == '/') *cursor = ZENO_PATH_SEP;
}

static int path_has_parent_segment(const char *path) {
    if (path == NULL) return 0;
    const char *cursor = path;
    while (*cursor != '\0') {
        while (*cursor == '/' || *cursor == '\\') cursor++;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') cursor++;
        size_t length = (size_t)(cursor - start);
        if (length == 2 && start[0] == '.' && start[1] == '.') return 1;
    }
    return 0;
}

#ifndef _WIN32
static char *normalize_absolute_path(const char *path) {
    char *result = zeno_strdup("/");
    size_t length = 1;
    if (result == NULL || path == NULL) return result;
    const char *cursor = path;
    while (*cursor != '\0') {
        while (*cursor == '/') cursor++;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/') cursor++;
        size_t part_length = (size_t)(cursor - start);
        if (part_length == 0 || (part_length == 1 && start[0] == '.')) continue;
        if (part_length == 2 && start[0] == '.' && start[1] == '.') {
            if (length > 1) {
                char *last = strrchr(result + 1, '/');
                if (last != NULL) {
                    *last = '\0';
                    length = (size_t)(last - result);
                } else {
                    result[1] = '\0';
                    length = 1;
                }
            }
            continue;
        }
        char *next = length == 1
            ? zeno_format("/%.*s", (int)part_length, start)
            : zeno_format("%s/%.*s", result, (int)part_length, start);
        if (next == NULL) {
            free(result);
            return NULL;
        }
        free(result);
        result = next;
        length = strlen(result);
    }
    return result;
}

static char *resolve_existing_parent(const char *path) {
    char real_path[PATH_MAX];
    if (realpath(path, real_path) != NULL) return zeno_strdup(real_path);
    char *probe = zeno_strdup(path);
    char *suffix = zeno_strdup("");
    if (probe == NULL || suffix == NULL) {
        free(probe);
        free(suffix);
        return NULL;
    }
    for (;;) {
        if (realpath(probe, real_path) != NULL) break;
        char *slash = strrchr(probe, '/');
        if (slash == NULL) {
            free(probe);
            free(suffix);
            return NULL;
        }
        const char *component = slash + 1;
        size_t component_length = strlen(component);
        if (component_length > 0) {
            char *next_suffix = suffix[0] != '\0'
                ? zeno_format("%.*s/%s", (int)component_length, component, suffix)
                : zeno_strndup(component, component_length);
            if (next_suffix == NULL) {
                free(probe);
                free(suffix);
                return NULL;
            }
            free(suffix);
            suffix = next_suffix;
        }
        if (slash == probe) {
            probe[1] = '\0';
        } else {
            *slash = '\0';
        }
    }
    char *result = suffix[0] != '\0'
        ? zeno_format("%s/%s", real_path, suffix)
        : zeno_strdup(real_path);
    free(probe);
    free(suffix);
    return result;
}
#endif

#ifdef _WIN32
static char *windows_final_path(const char *path) {
    if (path == NULL || *path == '\0') return NULL;
    HANDLE handle = CreateFileA(path, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    char buffer[32768];
    DWORD length = GetFinalPathNameByHandleA(handle, buffer, (DWORD)sizeof(buffer), FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (length == 0 || length >= sizeof(buffer)) return NULL;
    buffer[length] = '\0';
    if (strncmp(buffer, "\\\\?\\UNC\\", 8) == 0) {
        char *unc = zeno_format("\\\\%s", buffer + 8);
        if (unc != NULL) normalize_slashes(unc);
        return unc;
    }
    if (strncmp(buffer, "\\\\?\\", 4) == 0) {
        char *normal = zeno_strdup(buffer + 4);
        normalize_slashes(normal);
        return normal;
    }
    normalize_slashes(buffer);
    return zeno_strdup(buffer);
}

static char *windows_resolve_existing_parent(const char *path) {
    char *probe = zeno_strdup(path);
    char *suffix = zeno_strdup("");
    if (probe == NULL || suffix == NULL) { free(probe); free(suffix); return NULL; }
    for (;;) {
        char *final = windows_final_path(probe);
        if (final != NULL) {
            char *result = suffix[0] != '\0' ? zeno_join_path(final, suffix) : zeno_strdup(final);
            free(final); free(probe); free(suffix); return result;
        }
        char *slash = strrchr(probe, '\\');
        char *forward = strrchr(probe, '/');
        if (forward != NULL && (slash == NULL || forward > slash)) slash = forward;
        if (slash == NULL || slash == probe) { free(probe); free(suffix); return NULL; }
        const char *component = slash + 1;
        if (*component != '\0') {
            char *next = suffix[0] != '\0' ? zeno_join_path(component, suffix) : zeno_strdup(component);
            if (next == NULL) { free(probe); free(suffix); return NULL; }
            free(suffix); suffix = next;
        }
        *slash = '\0';
    }
}
#endif

static char *absolute_path(const char *path) {
#ifdef _WIN32
    char buffer[4096];
    if (path == NULL || _fullpath(buffer, path, sizeof(buffer)) == NULL) return NULL;
    normalize_slashes(buffer);
    return windows_resolve_existing_parent(buffer);
#else
    if (path == NULL || *path == '\0') return NULL;
    char cwd[PATH_MAX];
    char *input = NULL;
    if (path[0] == '/') {
        input = zeno_strdup(path);
    } else if (getcwd(cwd, sizeof(cwd)) != NULL) {
        input = zeno_format("%s/%s", cwd, path);
    }
    char *normalized = input != NULL ? normalize_absolute_path(input) : NULL;
    free(input);
    char *resolved = normalized != NULL ? resolve_existing_parent(normalized) : NULL;
    free(normalized);
    return resolved;
#endif
}

int zeno_path_inside(const char *root, const char *candidate, char **resolved) {
    if (resolved != NULL) *resolved = NULL;
    /* Reject parent components before resolving symlinks. Otherwise a path such
     * as link/../file could be normalized before the operating system applies
     * the link, which makes lexical containment unsound. */
    if (path_has_parent_segment(candidate)) return 0;
    char *absolute_root = absolute_path(root != NULL ? root : ".");
    char *absolute_candidate = absolute_path(candidate != NULL ? candidate : ".");
    if (absolute_root == NULL || absolute_candidate == NULL) {
        free(absolute_root);
        free(absolute_candidate);
        return 0;
    }
    size_t root_length = strlen(absolute_root);
    size_t candidate_length = strlen(absolute_candidate);
    int safe = 0;
    if (candidate_length >= root_length) {
#ifdef _WIN32
        int prefix = _strnicmp(absolute_root, absolute_candidate, root_length) == 0;
#else
        int prefix = strncmp(absolute_root, absolute_candidate, root_length) == 0;
#endif
        int boundary = absolute_candidate[root_length] == '\0' || absolute_candidate[root_length] == '/' ||
                       absolute_candidate[root_length] == '\\';
        safe = prefix && boundary;
    }
    if (safe) {
        if (resolved != NULL) *resolved = absolute_candidate;
        else free(absolute_candidate);
    } else {
        free(absolute_candidate);
    }
    free(absolute_root);
    return safe;
}

int zeno_mkdirs(const char *path) {
    if (path == NULL || *path == '\0') return 0;
    char *copy = zeno_strdup(path);
    if (copy == NULL) return 0;
    normalize_slashes(copy);
    size_t length = strlen(copy);
    for (size_t index = 1; index < length; index++) {
        if (copy[index] != ZENO_PATH_SEP) continue;
        copy[index] = '\0';
        if (strlen(copy) > 0) (void)ZENO_MKDIR(copy);
        copy[index] = ZENO_PATH_SEP;
    }
    int result = ZENO_MKDIR(copy) == 0 || errno == EEXIST;
    free(copy);
    return result;
}

char *zeno_join_path(const char *left, const char *right) {
    if (left == NULL || right == NULL) return NULL;
    size_t left_length = strlen(left);
    while (left_length > 0 && (left[left_length - 1] == '/' || left[left_length - 1] == '\\')) left_length--;
    while (*right == '/' || *right == '\\') right++;
    return zeno_format("%.*s%c%s", (int)left_length, left, ZENO_PATH_SEP, right);
}

char *zeno_json_array_append(char *array_json, const char *item_json) {
    const char *array = array_json != NULL ? array_json : "[]";
    const char *item = item_json != NULL ? item_json : "null";
    size_t length = strlen(array);
    while (length > 0 && isspace((unsigned char)array[length - 1])) length--;
    if (length == 1 && array[0] == '[') {
        return zeno_format("[%s]", item);
    }
    if (length < 2 || array[length - 1] != ']') return NULL;
    int empty = length == 2 && array[0] == '[';
    char *result = zeno_format("%.*s%s%s]", (int)(length - 1), array,
                               empty ? "" : ",", item);
    return result;
}

char *zeno_json_object_string(const char *key, const char *value) {
    char *quoted_key = zeno_json_escape(key != NULL ? key : "");
    char *quoted_value = zeno_json_escape(value != NULL ? value : "");
    char *result = quoted_key != NULL && quoted_value != NULL ? zeno_format("{%s:%s}", quoted_key, quoted_value) : NULL;
    free(quoted_key);
    free(quoted_value);
    return result;
}

char *zeno_json_object_merge(const char *base_json, const char *extra_json) {
    char *error = NULL;
    ZjNode *base = zj_parse(base_json != NULL ? base_json : "{}", &error);
    free(error);
    error = NULL;
    ZjNode *extra = zj_parse(extra_json != NULL ? extra_json : "{}", &error);
    free(error);
    if (base == NULL || extra == NULL || base->type != ZJ_OBJECT || extra->type != ZJ_OBJECT) {
        zj_free(base);
        zj_free(extra);
        return NULL;
    }
    for (ZjPair *source = extra->object; source != NULL; source = source->next) {
        ZjPair *target = NULL;
        for (target = base->object; target != NULL; target = target->next) if (strcmp(target->key, source->key) == 0) break;
        if (target != NULL) {
            zj_free(target->value);
            target->value = source->value;
            source->value = NULL;
        } else {
            ZjPair *copy = (ZjPair *)calloc(1, sizeof(*copy));
            if (copy == NULL) continue;
            copy->key = zeno_strdup(source->key);
            char *item = zj_stringify_compact(source->value);
            char *parse_error = NULL;
            copy->value = zj_parse(item != NULL ? item : "null", &parse_error);
            free(item);
            free(parse_error);
            copy->next = base->object;
            base->object = copy;
        }
    }
    char *result = zj_stringify_compact(base);
    zj_free(base);
    zj_free(extra);
    return result;
}

char *zeno_json_array_empty(void) { return zeno_strdup("[]"); }

int zeno_read_all(FILE *file, size_t max_chars, char **output, int *limited) {
    if (output == NULL || file == NULL) return 0;
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (buffer == NULL) return 0;
    if (limited != NULL) *limited = 0;
    int value;
    while ((value = fgetc(file)) != EOF) {
        if (max_chars > 0 && length >= max_chars) {
            if (limited != NULL) *limited = 1;
            break;
        }
        if (length + 1 >= capacity) {
            if (capacity > (size_t)-1 / 2) {
                free(buffer);
                return 0;
            }
            size_t next = capacity * 2;
            char *grown = (char *)realloc(buffer, next);
            if (grown == NULL) {
                free(buffer);
                return 0;
            }
            buffer = grown;
            capacity = next;
        }
        buffer[length++] = (char)value;
    }
    buffer[length] = '\0';
    *output = buffer;
    return 1;
}

int zeno_write_text(FILE *file, const char *text) {
    if (file == NULL || text == NULL) return 0;
    return fputs(text, file) >= 0;
}

char *zeno_timestamp_iso(void) {
    time_t now = time(NULL);
    struct tm value;
#ifdef _WIN32
    gmtime_s(&value, &now);
#else
    gmtime_r(&now, &value);
#endif
    char buffer[32];
    (void)strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &value);
    return zeno_strdup(buffer);
}

char *zeno_timestamp_id(void) {
    return zeno_format("%lld", zeno_now_ms());
}

char *zeno_shell_quote(const char *text) {
    const char *value = text != NULL ? text : "";
#ifdef _WIN32
    size_t extra = 2;
    for (const char *cursor = value; *cursor != '\0'; cursor++) if (*cursor == '"') extra++;
    char *result = (char *)malloc(strlen(value) + extra + 1);
    if (result == NULL) return NULL;
    size_t index = 0;
    result[index++] = '"';
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '"') result[index++] = '\\';
        result[index++] = *cursor;
    }
    result[index++] = '"';
    result[index] = '\0';
    return result;
#else
    size_t extra = 2;
    for (const char *cursor = value; *cursor != '\0'; cursor++) if (*cursor == '\'') extra += 3;
    char *result = (char *)malloc(strlen(value) + extra + 1);
    if (result == NULL) return NULL;
    size_t index = 0;
    result[index++] = '\'';
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '\'') {
            result[index++] = '\''; result[index++] = '\\'; result[index++] = '\''; result[index++] = '\'';
        } else result[index++] = *cursor;
    }
    result[index++] = '\'';
    result[index] = '\0';
    return result;
#endif
}

/* Compact SHA-256 implementation used for cache keys. */
typedef struct Sha256 {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} Sha256;

static const uint32_t sha_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static uint32_t rotr32(uint32_t value, unsigned count) { return (value >> count) | (value << (32U - count)); }
static uint32_t sha_ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t sha_maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }

static void sha_block(Sha256 *sha, const unsigned char *block) {
    uint32_t schedule[64];
    for (size_t index = 0; index < 16; index++) {
        schedule[index] = ((uint32_t)block[index * 4] << 24) | ((uint32_t)block[index * 4 + 1] << 16) |
                          ((uint32_t)block[index * 4 + 2] << 8) | block[index * 4 + 3];
    }
    for (size_t index = 16; index < 64; index++) {
        uint32_t s0 = rotr32(schedule[index - 15], 7) ^ rotr32(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3);
        uint32_t s1 = rotr32(schedule[index - 2], 17) ^ rotr32(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10);
        schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }
    uint32_t a = sha->state[0], b = sha->state[1], c = sha->state[2], d = sha->state[3];
    uint32_t e = sha->state[4], f = sha->state[5], g = sha->state[6], h = sha->state[7];
    for (size_t index = 0; index < 64; index++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t temp1 = h + s1 + sha_ch(e, f, g) + sha_k[index] + schedule[index];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t temp2 = s0 + sha_maj(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}

char *zeno_sha256_hex(const char *text) {
    Sha256 sha = {{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                   0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U}, 0, {0}, 0};
    const unsigned char *data = (const unsigned char *)(text != NULL ? text : "");
    size_t length = strlen((const char *)data);
    sha.bits = (uint64_t)length * 8ULL;
    while (length > 0) {
        size_t amount = length < sizeof(sha.block) - sha.used ? length : sizeof(sha.block) - sha.used;
        memcpy(sha.block + sha.used, data, amount);
        sha.used += amount; data += amount; length -= amount;
        if (sha.used == sizeof(sha.block)) { sha_block(&sha, sha.block); sha.used = 0; }
    }
    sha.block[sha.used++] = 0x80U;
    if (sha.used > 56) {
        while (sha.used < 64) sha.block[sha.used++] = 0;
        sha_block(&sha, sha.block); sha.used = 0;
    }
    while (sha.used < 56) sha.block[sha.used++] = 0;
    for (int index = 7; index >= 0; index--) sha.block[sha.used++] = (unsigned char)(sha.bits >> ((unsigned)index * 8U));
    sha_block(&sha, sha.block);
    char *result = (char *)malloc(65);
    if (result == NULL) return NULL;
    for (size_t index = 0; index < 8; index++) (void)snprintf(result + index * 8, 9, "%08x", sha.state[index]);
    result[64] = '\0';
    return result;
}

void zeno_free(void *ptr) { free(ptr); }
