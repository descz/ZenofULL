/*
 * Zeno Agent — Servidor HTTP em C
 *
 * Serve os arquivos da UI e persiste as configuracoes (zeno-config.json).
 * O painel Browser abre uma guia/janela dedicada "ZENO AGENT" no proprio
 * navegador do usuario (window.open com name) — nao ha navegador headless
 * nem proxy aqui.
 *
 * Compilar (Windows/MinGW): gcc -O2 -o zeno-server.exe server.c -lws2_32
 * Compilar (Linux):         gcc -O2 -o zeno-server server.c -lpthread
 * Uso:                      zeno-server.exe [porta]   (padrao 8080)
 */

#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
  #define CLOSESOCK closesocket
  #define MSG_NOSIGNAL 0
#else
  #define _POSIX_C_SOURCE 200809L
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <signal.h>
  #include <pthread.h>
  #define CLOSESOCK close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "zeno-bridge.h"

#define BUF_SIZE 65536
#define MAX_URL  8192

static volatile int running = 1;

#ifdef _WIN32
#include <process.h>
typedef HANDLE MUTEX_T;
#define MUTEX_INIT(m) ((m) = CreateMutex(NULL, FALSE, NULL))
#define MUTEX_LOCK(m) WaitForSingleObject((m), INFINITE)
#define MUTEX_UNLOCK(m) ReleaseMutex(m)
#else
typedef pthread_mutex_t MUTEX_T;
#define MUTEX_INIT(m) pthread_mutex_init(&(m), NULL)
#define MUTEX_LOCK(m) pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m))
#endif

static MUTEX_T cfg_mutex;

/* ================= util ================= */
static char *str_case_str(const char *h, const char *n) {
    if (!n[0]) return (char *)h;
    size_t nl = strlen(n);
    for (; *h; h++)
        if (tolower((unsigned char)*h) == tolower((unsigned char)n[0])) {
            size_t i = 1;
            while (i < nl && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i])) i++;
            if (i == nl) return (char *)h;
        }
    return NULL;
}

static const char *mime_type(const char *path) {
    const char *e = strrchr(path, '.');
    if (!e) return "application/octet-stream";
    if (!strcasecmp(e, ".html") || !strcasecmp(e, ".htm")) return "text/html; charset=utf-8";
    if (!strcasecmp(e, ".css"))  return "text/css; charset=utf-8";
    if (!strcasecmp(e, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcasecmp(e, ".json")) return "application/json; charset=utf-8";
    if (!strcasecmp(e, ".png"))  return "image/png";
    if (!strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(e, ".svg"))  return "image/svg+xml";
    if (!strcasecmp(e, ".ico"))  return "image/x-icon";
    if (!strcasecmp(e, ".woff2")) return "font/woff2";
    if (!strcasecmp(e, ".woff")) return "font/woff";
    return "application/octet-stream";
}

static int send_all(int fd, const char *d, size_t l) {
    size_t s = 0;
    while (s < l) {
        int n = send(fd, d + s, (int)(l - s), MSG_NOSIGNAL);
        if (n <= 0) return -1;
        s += (size_t)n;
    }
    return 0;
}

static void send_response(int fd, int code, const char *status, const char *ctype,
                          const unsigned char *body, size_t blen, const char *extra) {
    char h[1024];
    int hl = snprintf(h, sizeof(h),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n%s\r\n",
        code, status, ctype, blen, extra ? extra : "");
    send_all(fd, h, (size_t)hl);
    if (body && blen) send_all(fd, (const char *)body, blen);
}

static void send_json(int fd, const char *json) {
    const char *body = json != NULL ? json : "{}";
    send_response(fd, 200, "OK", "application/json; charset=utf-8",
                  (const unsigned char *)body, strlen(body), NULL);
}

static void send_json_error(int fd, int code, const char *status, const char *message) {
    char escaped[900];
    size_t write = 0;
    const char *text = message != NULL ? message : "error";
    for (const char *cursor = text; *cursor != '\0' && write + 7 < sizeof(escaped); cursor++) {
        if (*cursor == '"' || *cursor == '\\') { escaped[write++] = '\\'; escaped[write++] = *cursor; }
        else if (*cursor == '\n') { escaped[write++] = '\\'; escaped[write++] = 'n'; }
        else if (*cursor == '\r') continue;
        else escaped[write++] = *cursor;
    }
    escaped[write] = '\0';
    char body[1024];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", escaped);
    send_response(fd, code, status, "application/json; charset=utf-8",
                  (const unsigned char *)body, strlen(body), NULL);
}

/* ================= SSE (chat do agente) ================= */
typedef struct SseSink { int fd; int failed; } SseSink;

static void sse_start(int fd) {
    const char *headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n\r\n";
    send_all(fd, headers, strlen(headers));
}

static int sse_write_chunk(int fd, const char *data, size_t length) {
    char head[32];
    int head_length = snprintf(head, sizeof(head), "%zx\r\n", length);
    return send_all(fd, head, (size_t)head_length) == 0 &&
           send_all(fd, data, length) == 0 &&
           send_all(fd, "\r\n", 2) == 0 ? 0 : -1;
}

static void sse_send_event(void *context, const char *event_json) {
    SseSink *sink = (SseSink *)context;
    if (sink == NULL || sink->failed || event_json == NULL) return;
    size_t length = strlen(event_json);
    char *payload = (char *)malloc(length + 8);
    if (payload == NULL) { sink->failed = 1; return; }
    memcpy(payload, "data: ", 6);
    memcpy(payload + 6, event_json, length);
    payload[6 + length] = '\n';
    payload[7 + length] = '\n';
    if (sse_write_chunk(sink->fd, payload, length + 8) < 0) sink->failed = 1;
    free(payload);
}

static void sse_end(int fd) {
    send_all(fd, "0\r\n\r\n", 5);
}

/* ================= config ================= */
static void handle_config(int fd, const char *method, const char *body, size_t blen) {
    const char *path = "zeno-config.json";
    MUTEX_LOCK(cfg_mutex);
    if (!strcmp(method, "POST")) {
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(body, 1, blen, f); fclose(f); }
        MUTEX_UNLOCK(cfg_mutex);
        send_response(fd, 200, "OK", "application/json", (const unsigned char *)"{\"ok\":true}", 11, NULL);
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        MUTEX_UNLOCK(cfg_mutex);
        send_response(fd, 200, "OK", "application/json", (const unsigned char *)"{}", 2, NULL);
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (buf) { fread(buf, 1, (size_t)len, f); buf[len] = 0; }
    fclose(f);
    MUTEX_UNLOCK(cfg_mutex);
    send_response(fd, 200, "OK", "application/json", (const unsigned char *)(buf ? buf : "{}"), (size_t)len, NULL);
    free(buf);
}

/* ================= estaticos ================= */
static void handle_static(int fd, const char *path) {
    char safe[MAX_URL];
    if (strstr(path, "..")) { send_response(fd, 403, "Forbidden", "text/plain", (const unsigned char *)"Forbidden", 9, NULL); return; }
    if (path[0] == '/') path++;
    if (!path[0]) path = "index.html";
    snprintf(safe, sizeof(safe), "%s", path);
    FILE *f = fopen(safe, "rb");
    if (!f) {
        f = fopen("index.html", "rb");
        if (!f) { send_response(fd, 404, "Not Found", "text/plain", (const unsigned char *)"Not found", 9, NULL); return; }
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (buf) { fread(buf, 1, (size_t)len, f); buf[len] = 0; }
    fclose(f);
    send_response(fd, 200, "OK", mime_type(safe), (const unsigned char *)buf, (size_t)len, NULL);
    free(buf);
}

/* ================= zenoc (ponte com o agente ZenoC) ================= */
static char *json_body_copy(const char *body, size_t blen) {
    char *copy = (char *)malloc(blen + 1);
    if (copy == NULL) return NULL;
    if (body != NULL && blen > 0) memcpy(copy, body, blen);
    copy[blen] = '\0';
    return copy;
}

static void sse_send_error(void *context, const char *message) {
    char escaped[1024];
    size_t write = 0;
    const char *text = message != NULL ? message : "Erro desconhecido.";
    for (const char *cursor = text; *cursor != '\0' && write + 7 < sizeof(escaped); cursor++) {
        if (*cursor == '"' || *cursor == '\\') { escaped[write++] = '\\'; escaped[write++] = *cursor; }
        else if (*cursor == '\n') { escaped[write++] = '\\'; escaped[write++] = 'n'; }
        else if (*cursor == '\r') continue;
        else escaped[write++] = *cursor;
    }
    escaped[write] = '\0';
    char event[1200];
    snprintf(event, sizeof(event), "{\"type\":\"error\",\"message\":\"%s\"}", escaped);
    sse_send_event(context, event);
}

static void handle_zenoc(int fd, const char *method, const char *uri, const char *body, size_t blen, int refresh_models) {
    /* Status do runtime */
    if (!strcmp(uri, "/api/zenoc/status") && !strcmp(method, "GET")) {
        char *json = zeno_bridge_status_json();
        send_json(fd, json);
        free(json);
        return;
    }
    /* Ferramentas registradas */
    if (!strcmp(uri, "/api/zenoc/tools") && !strcmp(method, "GET")) {
        char *json = zeno_bridge_tools_json();
        send_json(fd, json);
        free(json);
        return;
    }
    /* Modelos do provider (refresh=1 consulta /models) */
    if (!strcmp(uri, "/api/zenoc/models") && !strcmp(method, "GET")) {
        char *json = zeno_bridge_models_json(refresh_models);
        send_json(fd, json);
        free(json);
        return;
    }
    /* Skills reais da pasta skills/ */
    if (!strcmp(uri, "/api/zenoc/skills") && !strcmp(method, "GET")) {
        char *json = zeno_bridge_skills_json();
        send_json(fd, json);
        free(json);
        return;
    }
    /* Configuração do provider */
    if (!strcmp(uri, "/api/zenoc/config") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char *error = NULL;
        int ok = json != NULL && zeno_bridge_config_save(json, &error);
        if (ok) send_json(fd, "{\"ok\":true}");
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Não foi possível salvar a configuração.");
        free(json);
        free(error);
        return;
    }
    /* Memória: notas */
    if (!strcmp(uri, "/api/zenoc/memory/notes")) {
        if (!strcmp(method, "GET")) {
            char *json = zeno_bridge_memory_notes_json();
            send_json(fd, json);
            free(json);
            return;
        }
        if (!strcmp(method, "POST")) {
            char *json = json_body_copy(body, blen);
            char *error = NULL;
            char *note = json != NULL ? zeno_bridge_memory_note_add(json, &error) : NULL;
            if (note != NULL) send_json(fd, note);
            else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Nota inválida.");
            free(json);
            free(note);
            free(error);
            return;
        }
    }
    if (!strcmp(uri, "/api/zenoc/memory/notes/update") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char *error = NULL;
        char *note = json != NULL ? zeno_bridge_memory_note_update(json, &error) : NULL;
        if (note != NULL) send_json(fd, note);
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Nota inválida.");
        free(json);
        free(note);
        free(error);
        return;
    }
    if (!strcmp(uri, "/api/zenoc/memory/notes/delete") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char *error = NULL;
        char id[160];
        id[0] = '\0';
        if (json != NULL) {
            const char *start = strstr(json, "\"id\"");
            if (start != NULL) {
                start = strchr(start + 4, '"');
                if (start != NULL) {
                    start++;
                    const char *end = strchr(start, '"');
                    if (end != NULL && (size_t)(end - start) < sizeof(id)) {
                        memcpy(id, start, (size_t)(end - start));
                        id[end - start] = '\0';
                    }
                }
            }
        }
        char *result = id[0] != '\0' ? zeno_bridge_memory_note_delete(id, &error) : NULL;
        if (result != NULL) send_json(fd, result);
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Nota não encontrada.");
        free(json);
        free(result);
        free(error);
        return;
    }
    /* Memória: links do grafo */
    if (!strcmp(uri, "/api/zenoc/memory/links")) {
        if (!strcmp(method, "GET")) {
            char *json = zeno_bridge_memory_links_json();
            send_json(fd, json);
            free(json);
            return;
        }
        if (!strcmp(method, "POST")) {
            char *json = json_body_copy(body, blen);
            char *error = NULL;
            char *link = json != NULL ? zeno_bridge_memory_link_add(json, &error) : NULL;
            if (link != NULL) send_json(fd, link);
            else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Link inválido.");
            free(json);
            free(link);
            free(error);
            return;
        }
    }
    if (!strcmp(uri, "/api/zenoc/memory/links/delete") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char *error = NULL;
        char *result = json != NULL ? zeno_bridge_memory_link_delete(json, &error) : NULL;
        if (result != NULL) send_json(fd, result);
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Link não encontrado.");
        free(json);
        free(result);
        free(error);
        return;
    }
    /* Cancela o run ativo */
    if (!strcmp(uri, "/api/zenoc/plugins") && !strcmp(method, "GET")) {
        char *json = zeno_bridge_plugins_json();
        send_json(fd, json);
        free(json);
        return;
    }
    if (!strcmp(uri, "/api/zenoc/plugins/save") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char *error = NULL;
        char *result = json != NULL ? zeno_bridge_plugins_save(json, &error) : NULL;
        if (result != NULL) send_json(fd, result);
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Plugin inválido.");
        free(json);
        free(result);
        free(error);
        return;
    }
    if (!strcmp(uri, "/api/zenoc/plugins/delete") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char id[160];
        id[0] = '\0';
        if (json != NULL) {
            const char *start = strstr(json, "\"id\"");
            if (start != NULL) {
                start = strchr(start + 4, '"');
                if (start != NULL) {
                    start++;
                    const char *end = strchr(start, '"');
                    if (end != NULL && (size_t)(end - start) < sizeof(id)) {
                        memcpy(id, start, (size_t)(end - start));
                        id[end - start] = '\0';
                    }
                }
            }
        }
        char *error = NULL;
        char *result = id[0] != '\0' ? zeno_bridge_plugins_delete(id, &error) : NULL;
        if (result != NULL) send_json(fd, result);
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Plugin inválido.");
        free(json);
        free(result);
        free(error);
        return;
    }
    if (!strcmp(uri, "/api/zenoc/plugins/toggle") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char id[160];
        id[0] = '\0';
        int enabled = 0;
        if (json != NULL) {
            const char *start = strstr(json, "\"id\"");
            if (start != NULL) {
                start = strchr(start + 4, '"');
                if (start != NULL) {
                    start++;
                    const char *end = strchr(start, '"');
                    if (end != NULL && (size_t)(end - start) < sizeof(id)) {
                        memcpy(id, start, (size_t)(end - start));
                        id[end - start] = '\0';
                    }
                }
            }
            enabled = strstr(json, "\"enabled\":true") != NULL || strstr(json, "\"enabled\": true") != NULL;
        }
        char *error = NULL;
        char *result = id[0] != '\0' ? zeno_bridge_plugins_toggle(id, enabled, &error) : NULL;
        if (result != NULL) send_json(fd, result);
        else send_json_error(fd, 400, "Bad Request", error != NULL ? error : "Plugin inválido.");
        free(json);
        free(result);
        free(error);
        return;
    }
    if (!strcmp(uri, "/api/zenoc/cancel") && !strcmp(method, "POST")) {
        zeno_bridge_cancel();
        send_json(fd, "{\"ok\":true}");
        return;
    }
    /* Chat: executa o agente e transmite eventos via SSE */
    if (!strcmp(uri, "/api/zenoc/chat") && !strcmp(method, "POST")) {
        char *json = json_body_copy(body, blen);
        char *error = NULL;
        SseSink sink;
        sink.fd = fd;
        sink.failed = 0;
        sse_start(fd);
        char *result = json != NULL ? zeno_bridge_chat(json, sse_send_event, &sink, &error) : NULL;
        if (result == NULL) {
            sse_send_error(&sink, error != NULL ? error : "Falha ao executar o agente.");
        } else {
            size_t result_length = strlen(result);
            char *wrapped = (char *)malloc(result_length + 32);
            if (wrapped != NULL) {
                snprintf(wrapped, result_length + 32, "{\"type\":\"result\",\"result\":%s}", result);
                sse_send_event(&sink, wrapped);
                free(wrapped);
            }
        }
        sse_end(fd);
        free(json);
        free(result);
        free(error);
        return;
    }
    send_json_error(fd, 404, "Not Found", "Rota ZenoC desconhecida.");
}

/* ================= HTTP loop ================= */
static void handle_client(int fd) {
    char *buf = (char *)malloc(BUF_SIZE);
    if (!buf) { CLOSESOCK(fd); return; }
    int total = 0;
    while (total < BUF_SIZE - 1) {
        int n = recv(fd, buf + total, BUF_SIZE - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    if (total <= 0) { free(buf); CLOSESOCK(fd); return; }

    char method[16] = {0}, uri[MAX_URL] = {0};
    sscanf(buf, "%15s %8191s", method, uri);

    size_t declared = 0;
    char *cl = str_case_str(buf, "Content-Length:");
    if (cl) declared = (size_t)atoll(cl + 15);
    size_t blen = 0;
    char *body = strstr(buf, "\r\n\r\n");
    char *body_copy = NULL;
    if (body) {
        body += 4;
        size_t have = (size_t)(buf + total - body);
        if (have < declared) {
            body_copy = (char *)malloc(declared + 1);
            if (body_copy) {
                memcpy(body_copy, body, have);
                size_t got = have;
                while (got < declared) {
                    int r = recv(fd, body_copy + got, (int)(declared - got), 0);
                    if (r <= 0) break;
                    got += (size_t)r;
                }
                body_copy[got] = '\0';
                body = body_copy;
                blen = got;
            }
        } else {
            blen = declared > 0 && declared <= have ? declared : have;
        }
    }

    int refresh_models = 0;
    char *q = strchr(uri, '?');
    if (q) {
        if (strstr(q, "refresh=1") != NULL) refresh_models = 1;
        *q = '\0';
    }

    if (!strcmp(method, "OPTIONS")) {
        send_response(fd, 204, "No Content", "text/plain", NULL, 0, NULL);
    } else if (!strcmp(uri, "/api/config")) {
        handle_config(fd, method, body, blen);
    } else if (!strncmp(uri, "/api/zenoc/", 11)) {
        handle_zenoc(fd, method, uri, body, blen, refresh_models);
    } else {
        handle_static(fd, uri);
    }

    free(body_copy);
    free(buf);
    CLOSESOCK(fd);
}

#ifdef _WIN32
static DWORD WINAPI client_thread(LPVOID arg) {
    int fd = *(int *)arg;
    free(arg);
    handle_client(fd);
    return 0;
}
static BOOL WINAPI console_handler(DWORD sig) {
    if (sig == CTRL_C_EVENT) { running = 0; return TRUE; }
    return FALSE;
}
#else
static void *client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);
    handle_client(fd);
    return NULL;
}
static void sigint_handler(int s) { (void)s; running = 0; }
#endif

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 8080;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
#endif
    MUTEX_INIT(cfg_mutex);

    int srv = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(srv, 128) < 0) { perror("listen"); return 1; }

    zeno_bridge_init(".");

    printf("[Zeno Server C] http://localhost:%d\n", port);
    printf("[Zeno Server C] o painel Browser abre a guia \"ZENO AGENT\" no proprio navegador\n");
    printf("[Zeno Server C] agente ZenoC: /api/zenoc/status | /api/zenoc/chat (SSE) | /api/zenoc/memory/notes\n");
    fflush(stdout);

    while (running) {
        struct sockaddr_in c;
        socklen_t cl = sizeof(c);
        int fd = accept(srv, (struct sockaddr *)&c, &cl);
        if (fd < 0) continue;
        int *pfd = (int *)malloc(sizeof(int));
        *pfd = fd;
#ifdef _WIN32
        CloseHandle(CreateThread(NULL, 0, client_thread, pfd, 0, NULL));
#else
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, pfd);
        pthread_detach(tid);
#endif
    }

    CLOSESOCK(srv);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
