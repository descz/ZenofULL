/*
 * zeno-bridge.c — Implementação da ponte UI ⇄ ZenoC.
 *
 * Responsabilidades:
 *  - carregar zeno-agent.json + env e montar um runtime ZenoC por run;
 *  - rodar o agente emitindo eventos (thinking/tools/texto) para o servidor;
 *  - mediar a memória compartilhada (ZenoC_memory.md) e os links do grafo;
 *  - listar skills reais da pasta skills/ e modelos do provider.
 *
 * O arquivo usa a API pública do ZenoC (include/zeno.h) e os helpers JSON
 * internos (src/zeno_internal.h) — os mesmos que o runtime usa.
 */
#include "zeno-bridge.h"
#include "zeno.h"
#include "zeno_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define BRIDGE_MEMORY_FALLBACK "{\"turns\":[],\"notes\":[],\"preferences\":[],\"profiles\":[]}"
#define BRIDGE_LINKS_FALLBACK "{\"links\":[]}"
#define BRIDGE_TOOL_OUTPUT_LIMIT 8000
#define BRIDGE_CONFIG_MAX_BYTES 400000
#define BRIDGE_NOTE_CONTENT_LIMIT 60000

typedef struct BridgeConfig {
    char provider[32];
    char base_url[512];
    char api_key[512];
    char model[128];
    char fallback_models[512];
    char workspace[1024];
    char skills_dir[1024];
    int agent_mode;       /* ZENO_AGENT_MODE_* */
    int require_approval;
    char models_json[65536];
    long long models_updated_ms;
} BridgeConfig;

typedef struct BridgeState {
    int ready;
    ZenoMutex lock;
    char base_dir[1024];
    char config_path[1280];
    char memory_path[1280];
    char links_path[1280];
    BridgeConfig cfg;
    ZenoConfig zeno;
    int busy;
    volatile int cancel;
    ZenoAgent *active_agent;
    unsigned long note_sequence;
} BridgeState;

static BridgeState g_bridge;

/* ============================ util ============================ */

static void bridge_copy(char *dst, size_t size, const char *src) {
    zeno_copy_string(dst, size, src);
}

static char *bridge_read_text(const char *path, size_t max_chars) {
    return zeno_read_file(path, max_chars);
}

static char *bridge_join(const char *left, const char *right) {
    return zeno_join_path(left, right);
}

static int bridge_file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

static char *bridge_note_json(const char *id, const char *title, const char *content,
                              const char *kind, const char *scope,
                              const char *tags_json, long long created_ms) {
    char *escaped_id = zeno_json_escape(id != NULL ? id : "");
    char *escaped_title = zeno_json_escape(title != NULL ? title : "");
    char *escaped_content = zeno_json_escape(content != NULL ? content : "");
    char *escaped_kind = zeno_json_escape(kind != NULL && *kind != '\0' ? kind : "note");
    char *escaped_scope = zeno_json_escape(scope != NULL && *scope != '\0' ? scope : "global");
    char *result = (escaped_id && escaped_title && escaped_content && escaped_kind && escaped_scope)
        ? zeno_format("{\"id\":%s,\"title\":%s,\"content\":%s,\"kind\":%s,\"scope\":%s,\"tags\":%s,\"created_ms\":%lld}",
                      escaped_id, escaped_title, escaped_content, escaped_kind, escaped_scope,
                      tags_json != NULL && *tags_json != '\0' ? tags_json : "[]", created_ms)
        : NULL;
    free(escaped_id); free(escaped_title); free(escaped_content);
    free(escaped_kind); free(escaped_scope);
    return result;
}

static char *bridge_gen_id(const char *prefix) {
    unsigned long sequence = ++g_bridge.note_sequence;
    return zeno_format("%s_%lld_%lu", prefix, zeno_now_ms(), sequence);
}

/* Monta um objeto JSON campo a campo; cada etapa libera o intermediário. */
static char *bridge_obj_append(char *base, const char *key, const char *value) {
    char *piece = zeno_json_object_string(key, value);
    if (piece == NULL) return base;
    char *merged = zeno_json_object_merge(base, piece);
    free(piece);
    if (merged == NULL) return base;
    free(base);
    return merged;
}

static char *bridge_obj_merge_raw(char *base, const char *raw_object) {
    char *merged = zeno_json_object_merge(base, raw_object);
    if (merged == NULL) return base;
    free(base);
    return merged;
}

static char *bridge_json_get_optional_string(ZjNode *root, const char *key, const char *fallback) {
    const char *value = root != NULL ? zj_string(zj_object_get(root, key)) : NULL;
    return zeno_strdup(value != NULL ? value : fallback);
}

/* ========================== configuração ========================== */

static void bridge_defaults(void) {
    memset(&g_bridge.cfg, 0, sizeof(g_bridge.cfg));
    bridge_copy(g_bridge.cfg.provider, sizeof(g_bridge.cfg.provider), "openai");
    bridge_copy(g_bridge.cfg.base_url, sizeof(g_bridge.cfg.base_url), "https://api.openai.com/v1");
    bridge_copy(g_bridge.cfg.model, sizeof(g_bridge.cfg.model), "gpt-4o-mini");
    bridge_copy(g_bridge.cfg.workspace, sizeof(g_bridge.cfg.workspace), ".");
    bridge_copy(g_bridge.cfg.skills_dir, sizeof(g_bridge.cfg.skills_dir), "skills");
    g_bridge.cfg.agent_mode = ZENO_AGENT_MODE_FULL;
    g_bridge.cfg.require_approval = 0;
    bridge_copy(g_bridge.cfg.models_json, sizeof(g_bridge.cfg.models_json), "[]");
    g_bridge.cfg.models_updated_ms = 0;
}

static void bridge_apply_env(void) {
    const char *value = NULL;
    if ((value = getenv("OPENAI_BASE_URL")) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.base_url, sizeof(g_bridge.cfg.base_url), value);
    if ((value = getenv("OPENAI_API_KEY")) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.api_key, sizeof(g_bridge.cfg.api_key), value);
    if ((value = getenv("MODEL_ID")) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.model, sizeof(g_bridge.cfg.model), value);
    if ((value = getenv("FALLBACK_MODELS")) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.fallback_models, sizeof(g_bridge.cfg.fallback_models), value);
    if ((value = getenv("WORKSPACE_ROOT")) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.workspace, sizeof(g_bridge.cfg.workspace), value);
    if ((value = getenv("ZENO_SKILLS_DIR")) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.skills_dir, sizeof(g_bridge.cfg.skills_dir), value);
    if (getenv("ZENO_AGENT_MODE") != NULL && zeno_contains_ci(getenv("ZENO_AGENT_MODE"), "minimal")) g_bridge.cfg.agent_mode = ZENO_AGENT_MODE_MINIMAL;
    if ((value = getenv("REQUIRE_APPROVAL")) != NULL && (strcmp(value, "1") == 0 || zeno_contains_ci(value, "true"))) g_bridge.cfg.require_approval = 1;
}

static void bridge_load_config_file(void) {
    char *text = bridge_read_text(g_bridge.config_path, BRIDGE_CONFIG_MAX_BYTES);
    if (text == NULL) return;
    char *error = NULL;
    ZjNode *root = zj_parse(text, &error);
    free(error);
    free(text);
    if (root == NULL || root->type != ZJ_OBJECT) { zj_free(root); return; }
    const char *value = NULL;
    if ((value = zj_string(zj_object_get(root, "provider"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.provider, sizeof(g_bridge.cfg.provider), value);
    if ((value = zj_string(zj_object_get(root, "base_url"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.base_url, sizeof(g_bridge.cfg.base_url), value);
    if ((value = zj_string(zj_object_get(root, "api_key"))) != NULL) bridge_copy(g_bridge.cfg.api_key, sizeof(g_bridge.cfg.api_key), value);
    if ((value = zj_string(zj_object_get(root, "model"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.model, sizeof(g_bridge.cfg.model), value);
    if ((value = zj_string(zj_object_get(root, "fallback_models"))) != NULL) bridge_copy(g_bridge.cfg.fallback_models, sizeof(g_bridge.cfg.fallback_models), value);
    if ((value = zj_string(zj_object_get(root, "workspace"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.workspace, sizeof(g_bridge.cfg.workspace), value);
    if ((value = zj_string(zj_object_get(root, "skills_dir"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.skills_dir, sizeof(g_bridge.cfg.skills_dir), value);
    if (zj_object_has(root, "agent_mode")) {
        const char *mode = zj_string(zj_object_get(root, "agent_mode"));
        g_bridge.cfg.agent_mode = (mode != NULL && zeno_contains_ci(mode, "minimal")) ? ZENO_AGENT_MODE_MINIMAL : ZENO_AGENT_MODE_FULL;
    }
    if (zj_object_has(root, "require_approval")) g_bridge.cfg.require_approval = zj_bool(zj_object_get(root, "require_approval"), 0);
    ZjNode *models = zj_object_get(root, "models");
    if (models != NULL && models->type == ZJ_ARRAY) {
        char *serialized = zj_stringify_compact(models);
        if (serialized != NULL) {
            bridge_copy(g_bridge.cfg.models_json, sizeof(g_bridge.cfg.models_json), serialized);
            free(serialized);
        }
    }
    g_bridge.cfg.models_updated_ms = zj_integer(zj_object_get(root, "models_updated_ms"), 0);
    zj_free(root);
}

static int bridge_write_config_file(char **error) {
    char *json = zeno_strdup("{}");
    char *piece = NULL;
    #define BRIDGE_CONFIG_APPEND(KEY, VALUE) \
        do { piece = zeno_json_object_string((KEY), (VALUE)); \
             if (piece != NULL) { char *merged = zeno_json_object_merge(json, piece); free(json); json = merged; free(piece); piece = NULL; } \
        } while (0)
    BRIDGE_CONFIG_APPEND("provider", g_bridge.cfg.provider);
    BRIDGE_CONFIG_APPEND("base_url", g_bridge.cfg.base_url);
    BRIDGE_CONFIG_APPEND("api_key", g_bridge.cfg.api_key);
    BRIDGE_CONFIG_APPEND("model", g_bridge.cfg.model);
    BRIDGE_CONFIG_APPEND("fallback_models", g_bridge.cfg.fallback_models);
    BRIDGE_CONFIG_APPEND("workspace", g_bridge.cfg.workspace);
    BRIDGE_CONFIG_APPEND("skills_dir", g_bridge.cfg.skills_dir);
    BRIDGE_CONFIG_APPEND("agent_mode", g_bridge.cfg.agent_mode == ZENO_AGENT_MODE_MINIMAL ? "minimal" : "full");
    #undef BRIDGE_CONFIG_APPEND
    if (json == NULL) { if (error != NULL) *error = zeno_strdup("Falha ao serializar a configuração."); return 0; }
    char *extra = zeno_format("{\"require_approval\":%s,\"models\":%s,\"models_updated_ms\":%lld}",
                              g_bridge.cfg.require_approval ? "true" : "false",
                              g_bridge.cfg.models_json[0] != '\0' ? g_bridge.cfg.models_json : "[]",
                              g_bridge.cfg.models_updated_ms);
    if (extra != NULL) {
        char *merged = zeno_json_object_merge(json, extra);
        free(extra);
        if (merged != NULL) { free(json); json = merged; }
    }
    if (!zeno_write_file_atomic(g_bridge.config_path, json)) {
        free(json);
        if (error != NULL) *error = zeno_format("Não foi possível escrever %s.", g_bridge.config_path);
        return 0;
    }
    free(json);
    return 1;
}

static void bridge_refresh_paths(void) {
    char *memory = bridge_join(g_bridge.cfg.workspace, "ZenoC_memory.md");
    char *links = bridge_join(g_bridge.cfg.workspace, "ZenoC_memory_links.md");
    if (memory != NULL) bridge_copy(g_bridge.memory_path, sizeof(g_bridge.memory_path), memory);
    if (links != NULL) bridge_copy(g_bridge.links_path, sizeof(g_bridge.links_path), links);
    free(memory);
    free(links);
}

static void bridge_apply_zeno_config(void) {
    zeno_config_default(&g_bridge.zeno);
    bridge_copy(g_bridge.zeno.openai_base_url, sizeof(g_bridge.zeno.openai_base_url), g_bridge.cfg.base_url);
    bridge_copy(g_bridge.zeno.openai_api_key, sizeof(g_bridge.zeno.openai_api_key), g_bridge.cfg.api_key);
    bridge_copy(g_bridge.zeno.model_id, sizeof(g_bridge.zeno.model_id), g_bridge.cfg.model);
    bridge_copy(g_bridge.zeno.fallback_models, sizeof(g_bridge.zeno.fallback_models), g_bridge.cfg.fallback_models);
    bridge_copy(g_bridge.zeno.workspace_root, sizeof(g_bridge.zeno.workspace_root), g_bridge.cfg.workspace);
    bridge_copy(g_bridge.zeno.runs_dir, sizeof(g_bridge.zeno.runs_dir), ".zeno_runs");
    g_bridge.zeno.require_approval = g_bridge.cfg.require_approval;
    g_bridge.zeno.enable_llm_cache = 0;
    g_bridge.zeno.caveman_mode = 0;
    g_bridge.zeno.agent_mode = g_bridge.cfg.agent_mode;
    g_bridge.zeno.sandbox_allow_shell_operators = 1;
    g_bridge.zeno.llm_timeout_ms = 300000;
}

void zeno_bridge_init(const char *base_dir) {
    if (g_bridge.ready) return;
    zeno_mutex_lock(&g_bridge.lock);
    if (g_bridge.ready) { zeno_mutex_unlock(&g_bridge.lock); return; }
    bridge_copy(g_bridge.base_dir, sizeof(g_bridge.base_dir), base_dir != NULL && *base_dir != '\0' ? base_dir : ".");
    char *config = bridge_join(g_bridge.base_dir, "zeno-agent.json");
    if (config != NULL) bridge_copy(g_bridge.config_path, sizeof(g_bridge.config_path), config);
    free(config);
    bridge_defaults();
    bridge_apply_env();
    bridge_load_config_file();
    bridge_refresh_paths();
    bridge_apply_zeno_config();
    g_bridge.ready = 1;
    zeno_mutex_unlock(&g_bridge.lock);
}

/* ============================ runtime ============================ */

typedef struct BridgeRunContext {
    ZenoBridgeEmit emit;
    void *emit_context;
    const char *session_id;
    const char *model;
    ZenoMutex emit_lock; /* serializa emitters de hooks paralelos */
} BridgeRunContext;

static void bridge_emit_event(BridgeRunContext *context, const char *event_json) {
    if (context == NULL || context->emit == NULL || event_json == NULL) return;
    zeno_mutex_lock(&context->emit_lock);
    context->emit(context->emit_context, event_json);
    zeno_mutex_unlock(&context->emit_lock);
}

static void bridge_emit_simple(BridgeRunContext *context, const char *type, const char *key, const char *value) {
    char *escaped = zeno_json_escape(value != NULL ? value : "");
    char *escaped_type = zeno_json_escape(type != NULL ? type : "");
    char *escaped_key = zeno_json_escape(key != NULL ? key : "");
    if (escaped != NULL && escaped_type != NULL && escaped_key != NULL) {
        char *event = zeno_format("{\"type\":%s,%s:%s}", escaped_type, escaped_key, escaped);
        if (event != NULL) bridge_emit_event(context, event);
        free(event);
    }
    free(escaped);
    free(escaped_type);
    free(escaped_key);
}

/* Assinatura dos hooks v1.3.0 do ZenoC. */
static int bridge_pre_tool(void *context, const char *tool_name, const char *arguments_json) {
    BridgeRunContext *run = (BridgeRunContext *)context;
    char *escaped_tool = zeno_json_escape(tool_name != NULL ? tool_name : "tool");
    char *args = arguments_json != NULL && *arguments_json != '\0' ? zeno_strdup(arguments_json) : zeno_strdup("{}");
    if (escaped_tool != NULL && args != NULL) {
        char *event = zeno_format("{\"type\":\"tool_started\",\"tool\":%s,\"args\":%s}", escaped_tool, args);
        if (event != NULL) bridge_emit_event(run, event);
        free(event);
    }
    free(escaped_tool);
    free(args);
    return 1;
}

static void bridge_post_tool(void *context, const char *tool_name, const char *arguments_json,
                             const char *result_text, int ok) {
    (void)arguments_json;
    BridgeRunContext *run = (BridgeRunContext *)context;
    char *escaped_tool = zeno_json_escape(tool_name != NULL ? tool_name : "tool");
    char *bounded = NULL;
    if (result_text != NULL) {
        size_t length = strlen(result_text);
        if (length > BRIDGE_TOOL_OUTPUT_LIMIT) {
            bounded = zeno_format("%.*s\n... [truncado]", BRIDGE_TOOL_OUTPUT_LIMIT, result_text);
        } else {
            bounded = zeno_strdup(result_text);
        }
    }
    char *escaped_output = zeno_json_escape(bounded != NULL ? bounded : "");
    if (escaped_tool != NULL && escaped_output != NULL) {
        char *event = zeno_format("{\"type\":\"tool_completed\",\"tool\":%s,\"ok\":%s,\"output\":%s}",
                                  escaped_tool, ok ? "true" : "false", escaped_output);
        if (event != NULL) bridge_emit_event(run, event);
        free(event);
    }
    free(escaped_tool);
    free(escaped_output);
    free(bounded);
}

static void bridge_stream_chunk(void *context, const char *chunk) {
    if (chunk == NULL || *chunk == '\0') return;
    BridgeRunContext *run = (BridgeRunContext *)context;
    char *escaped = zeno_json_escape(chunk);
    if (escaped == NULL) return;
    char *event = zeno_format("{\"type\":\"text\",\"delta\":%s}", escaped);
    if (event != NULL) bridge_emit_event(run, event);
    free(escaped);
    free(event);
}

static void bridge_agent_event(void *context, const char *event, const char *run_id, const char *session_id) {
    (void)run_id;
    (void)session_id;
    if (event == NULL) return;
    BridgeRunContext *run = (BridgeRunContext *)context;
    if (strcmp(event, "llm_started") == 0) {
        bridge_emit_simple(run, "thinking", "text", "Analisando o contexto e planejando os próximos passos…");
    } else if (strcmp(event, "run_started") == 0) {
        /* O bridge emite o seu próprio run_started com modelo e sessão. */
    }
}

static int bridge_should_cancel(void *context) {
    (void)context;
    return g_bridge.cancel;
}

static int bridge_auto_approve(void *context, const char *request_id, const char *tool_name,
                               const char *args_json, const char *reason) {
    (void)request_id; (void)args_json; (void)reason;
    BridgeRunContext *run = (BridgeRunContext *)context;
    char *message = zeno_format("Aprovando automaticamente: %s", tool_name != NULL ? tool_name : "ferramenta");
    if (message != NULL) {
        bridge_emit_simple(run, "thinking", "text", message);
        free(message);
    }
    return 1;
}

/* Ferramenta extra: liga duas notas da memória (grafo do workspace). */
static char *bridge_link_add_locked(const char *from, const char *to, const char *label, char **error);
static int bridge_memory_link_tool(void *context, const char *args_json, char **output, char **error) {
    (void)context;
    char *parse_error = NULL;
    ZjNode *root = zj_parse(args_json != NULL ? args_json : "{}", &parse_error);
    free(parse_error);
    const char *from = root != NULL ? zj_string(zj_object_get(root, "from")) : NULL;
    const char *to = root != NULL ? zj_string(zj_object_get(root, "to")) : NULL;
    const char *label = root != NULL ? zj_string(zj_object_get(root, "label")) : NULL;
    if (from == NULL || *from == '\0' || to == NULL || *to == '\0') {
        if (output != NULL) *output = zeno_strdup("Error: from and to note ids are required.");
        if (error != NULL) *error = zeno_strdup("invalid link arguments");
        zj_free(root);
        return 0;
    }
    zeno_mutex_lock(&g_bridge.lock);
    char *link_error = NULL;
    char *result = bridge_link_add_locked(from, to, label, &link_error);
    zeno_mutex_unlock(&g_bridge.lock);
    if (result == NULL) {
        if (output != NULL) *output = zeno_format("Error: %s", link_error != NULL ? link_error : "could not link notes");
        free(link_error);
        zj_free(root);
        return 0;
    }
    if (output != NULL) *output = zeno_format("Linked %s -> %s", from, to);
    free(result);
    free(link_error);
    zj_free(root);
    return 1;
}

/* Registra memory_link em um registry (usado no run e na listagem de tools). */
static int bridge_register_memory_link_tool(ZenoRegistry *registry, char **error) {
    ZenoToolDefinition definition;
    memset(&definition, 0, sizeof(definition));
    definition.name = "memory_link";
    definition.description = "Link two durable memory notes in the workspace knowledge graph (from/to are note ids returned by memory_remember or memory_list).";
    definition.parameters_json = "{\"type\":\"object\",\"properties\":{\"from\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"},\"label\":{\"type\":\"string\"}},\"required\":[\"from\",\"to\"]}";
    definition.effect = ZENO_EFFECT_WRITE_LOCAL;
    definition.requires_approval = 0;
    definition.timeout_ms = 5000;
    definition.max_retries = 0;
    if (!zeno_registry_register(registry, definition, bridge_memory_link_tool, NULL)) {
        if (error != NULL) *error = zeno_strdup("Falha ao registrar a ferramenta memory_link.");
        return 0;
    }
    return 1;
}

/* ================= plugins (modificador do Zeno, backend em C) ============ */
/* Um plugin pode: registrar ferramentas de código no agente (templates de */
/* comando shell com placeholders {{var}}), criar botões que injetam prompts */
/* na UI, criar abas na sidebar e campos custom nas configurações. */
typedef struct PluginToolContext {
    ZenoSandbox *sandbox;
    char *workspace;
    char *command;
} PluginToolContext;

static char *bridge_plugins_path(void) {
    char *dir = bridge_join(g_bridge.cfg.workspace, ".zeno");
    if (dir == NULL) return NULL;
    (void)zeno_mkdirs(dir);
    char *path = bridge_join(dir, "plugins.json");
    free(dir);
    return path;
}

static char *bridge_plugins_read_raw(void) {
    const char *fallback = "{\"plugins\":[]}";
    char *path = bridge_plugins_path();
    if (path == NULL) return zeno_strdup(fallback);
    FILE *file = fopen(path, "rb");
    if (file == NULL) { free(path); return zeno_strdup(fallback); }
    char *buffer = (char *)malloc(1024 * 1024);
    size_t read = 0;
    if (buffer != NULL) read = fread(buffer, 1, 1024 * 1024 - 1, file);
    fclose(file);
    if (buffer == NULL) { free(path); return zeno_strdup(fallback); }
    buffer[read] = 0;
    free(path);
    return buffer;
}

static int bridge_plugins_write_raw(const char *json) {
    char *path = bridge_plugins_path();
    if (path == NULL || json == NULL) { free(path); return 0; }
    FILE *file = fopen(path, "wb");
    if (file == NULL) { free(path); return 0; }
    size_t written = fwrite(json, 1, strlen(json), file);
    fclose(file);
    free(path);
    return written == strlen(json);
}

/* Expande {{placeholders}} do template com os valores de args_json; */
/* placeholder sem valor vira string vazia. */
static char *bridge_replace_all(const char *text, const char *needle, const char *replacement) {
    if (text == NULL || needle == NULL || replacement == NULL) return NULL;
    size_t needle_len = strlen(needle);
    size_t repl_len = strlen(replacement);
    size_t count = 0;
    const char *cursor = text;
    while ((cursor = strstr(cursor, needle)) != NULL) { count++; cursor += needle_len; }
    if (count == 0) return zeno_strdup(text);
    size_t text_len = strlen(text);
    char *result = (char *)malloc(text_len + count * (repl_len > needle_len ? repl_len - needle_len : 0) + 16);
    size_t at = 0;
    cursor = text;
    while (*cursor != 0) {
        if (needle_len > 0 && strncmp(cursor, needle, needle_len) == 0) {
            memcpy(result + at, replacement, repl_len); at += repl_len; cursor += needle_len;
        } else {
            result[at++] = *cursor++;
        }
    }
    result[at] = 0;
    return result;
}

static char *bridge_plugin_expand(const char *tpl, const char *args_json) {
    char *result = zeno_strdup(tpl != NULL ? tpl : "");
    for (int pass = 0; pass < 64; pass++) {
        const char *open = strstr(result, "{{");
        if (open == NULL) break;
        const char *close = strstr(open, "}}");
        if (close == NULL) break;
        size_t length = (size_t)(close - open - 2);
        char *key = zeno_strndup(open + 2, length);
        char *name = key != NULL ? zeno_trim_copy(key) : NULL;
        free(key);
        char *value = name != NULL && *name != 0 ? zeno_json_get_path_string(args_json, name) : NULL;
        char *safe = value != NULL ? zeno_strdup(value) : zeno_strdup("");
        free(value);
        char *next = zeno_format("%.*s%s%s", (int)(open - result), result, safe, close + 2);
        free(result); free(name); free(safe); result = next;
    }
    return result;
}
static int bridge_plugin_tool_handler(void *context, const char *args, char **output, char **error) {
    PluginToolContext *ctx = (PluginToolContext *)context;
    if (ctx == NULL || ctx->sandbox == NULL) {
        if (error != NULL) *error = zeno_strdup("Plugin tool sem sandbox.");
        return 0;
    }
    char *command = bridge_plugin_expand(ctx->command, args);
    ZenoExecResult result;
    memset(&result, 0, sizeof(result));
    result = zeno_sandbox_execute(ctx->sandbox, command != NULL ? command : "", ctx->workspace, 120000);
    int ok = result.ok;
    *output = ok ? zeno_strdup(result.output) : zeno_format("%s%s", result.blocked ? "SANDBOX BLOCKED: " : "Error: ", result.reason != NULL ? result.reason : "plugin command failed");
    if (!ok && error != NULL) *error = zeno_strdup(result.reason != NULL ? result.reason : "plugin command failed");
    zeno_exec_result_free(&result);
    free(command);
    return *output != NULL && ok;
}

/* Schema derivado dos placeholders únicos do template. */
static char *bridge_plugin_schema(const char *tpl) {
    char *schema = zeno_strdup("{\"type\":\"object\",\"properties\":{}}");
    const char *cursor = tpl != NULL ? tpl : "";
    while ((cursor = strstr(cursor, "{{")) != NULL) {
        const char *end = strstr(cursor, "}}");
        if (end == NULL) break;
        size_t length = (size_t)(end - cursor - 2);
        char *key = zeno_strndup(cursor + 2, length);
        char *name = key != NULL ? zeno_trim_copy(key) : NULL;
        free(key);
        if (name != NULL && *name != 0 && strstr(schema, name) == NULL) {
            char *prop = zeno_format("\"%s\":{\"type\":\"string\"}", name);
            char *before = strstr(schema, "\"properties\":{");
            if (before != NULL) {
                size_t head = (size_t)(before - schema) + strlen("\"properties\":{");
                char *merged = zeno_format("%.*s%s%s", (int)head, schema, prop, schema + head);
                free(schema); schema = merged;
            }
            free(prop);
        }
        free(name);
        cursor = end;
    }
    return schema;
}

/* Remove todas as ferramentas plugin_* do registry. */
static void bridge_unregister_plugin_tools(ZenoRegistry *registry) {
    char *list = zeno_registry_list_json(registry);
    char *parse_error = NULL;
    ZjNode *root = zj_parse(list != NULL ? list : "[]", &parse_error);
    free(parse_error); free(list);
    if (root != NULL && root->type == ZJ_ARRAY) {
        for (size_t index = 0; index < root->count; index++) {
            ZjNode *item = zj_array_get(root, index);
            const char *name = zj_string(zj_object_get(item, "name"));
            if (name != NULL && strncmp(name, "plugin_", 7) == 0) zeno_registry_unregister(registry, name);
        }
    }
    zj_free(root);
}

/* Registra as tools dos plugins habilitados; chamar após builtins. */
static void bridge_register_plugin_tools(ZenoRegistry *registry, ZenoSandbox *sandbox) {
    bridge_unregister_plugin_tools(registry);
    char *json = bridge_plugins_read_raw();
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error); free(json);
    ZjNode *plugins = root != NULL && root->type == ZJ_OBJECT ? zj_object_get(root, "plugins") : NULL;
    if (plugins == NULL || plugins->type != ZJ_ARRAY) { zj_free(root); return; }
    for (size_t index = 0; index < plugins->count; index++) {
        ZjNode *plugin = zj_array_get(plugins, index);
        const char *id = zj_string(zj_object_get(plugin, "id"));
        int enabled = zj_bool(zj_object_get(plugin, "enabled"), 0);
        if (id == NULL || *id == 0 || !enabled) continue;
        ZjNode *tools = zj_object_get(plugin, "tools");
        if (tools == NULL || tools->type != ZJ_ARRAY) continue;
        for (size_t t = 0; t < tools->count; t++) {
            ZjNode *tool = zj_array_get(tools, t);
            const char *name = zj_string(zj_object_get(tool, "name"));
            const char *description = zj_string(zj_object_get(tool, "description"));
            const char *command = zj_string(zj_object_get(tool, "command"));
            if (name == NULL || *name == 0 || command == NULL || *command == 0) continue;
            char *tool_name = zeno_format("plugin_%s_%s", id, name);
            for (char *c = tool_name; *c != 0; c++) {
                if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) *c = '_';
            }
            char *schema = bridge_plugin_schema(command);
            ZenoToolDefinition definition;
            memset(&definition, 0, sizeof(definition));
            definition.name = tool_name;
            definition.description = description != NULL && *description != 0 ? description : "Plugin code tool (shell command template).";
            definition.parameters_json = schema;
            definition.effect = ZENO_EFFECT_WRITE_EXTERNAL;
            definition.requires_approval = 0;
            definition.timeout_ms = 120000;
            definition.max_retries = 0;
            PluginToolContext *ctx = (PluginToolContext *)calloc(1, sizeof(PluginToolContext));
            if (ctx != NULL) {
                ctx->sandbox = sandbox;
                ctx->workspace = zeno_strdup(g_bridge.cfg.workspace);
                ctx->command = zeno_strdup(command);
                (void)zeno_registry_register(registry, definition, bridge_plugin_tool_handler, ctx);
            }
            free(tool_name); free(schema);
        }
    }
    zj_free(root);
}

/* Troca o literal true/false do campo "enabled" no JSON do plugin. */
static char *bridge_plugin_toggle_flag(const char *plugin_json, int enabled) {
    const char *key = strstr(plugin_json, "\"enabled\"");
    if (key == NULL) return NULL;
    const char *colon = strchr(key, ':');
    if (colon == NULL) return NULL;
    const char *cursor = colon + 1;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    const char *token = cursor;
    const char *false_hit = strstr(token, "false");
    const char *true_hit = strstr(token, "true");
    const char *hit = NULL;
    size_t token_len = 0;
    if (true_hit != NULL && (false_hit == NULL || true_hit < false_hit)) { hit = true_hit; token_len = 4; }
    else if (false_hit != NULL) { hit = false_hit; token_len = 5; }
    if (hit == NULL) return NULL;
    char *replacement = enabled ? "true" : "false";
    return zeno_format("%.*s%s%s", (int)(hit - plugin_json), plugin_json, replacement, hit + token_len);
}

/* Reconstrói o store aplicando uma operação: */
/* op 0 = apenas leitura; 1 = upsert (upsert_id + upsert_json); */
/* 2 = delete (upsert_id usado como id alvo); 3 = toggle. */
static char *bridge_plugins_store_maintain(int op, const char *upsert_id, const char *upsert_json, int toggle_enabled, char **error) {
    const char *delete_id = op == 2 ? upsert_id : NULL;
    const char *toggle_id = op == 3 ? upsert_id : NULL;
    char *store_raw = bridge_plugins_read_raw();
    char *parse_error = NULL;
    ZjNode *store = zj_parse(store_raw != NULL ? store_raw : "{}", &parse_error);
    free(parse_error); free(store_raw);
    ZjNode *plugins = store != NULL && store->type == ZJ_OBJECT ? zj_object_get(store, "plugins") : NULL;
    char *array = zeno_strdup("[");
    int found = 0;
    if (plugins != NULL && plugins->type == ZJ_ARRAY) {
        for (size_t index = 0; index < plugins->count; index++) {
            ZjNode *plugin = zj_array_get(plugins, index);
            const char *pid = zj_string(zj_object_get(plugin, "id"));
            if (delete_id != NULL && pid != NULL && strcmp(pid, delete_id) == 0) { found = 1; continue; }
            if (op == 1 && upsert_id != NULL && pid != NULL && strcmp(pid, upsert_id) == 0) { found = 1; continue; }
            if (op == 3 && toggle_id != NULL && pid != NULL && strcmp(pid, toggle_id) == 0) {
                found = 1;
                char *plugin_json = zj_stringify_compact(plugin);
                char *patched = plugin_json != NULL ? bridge_plugin_toggle_flag(plugin_json, toggle_enabled) : NULL;
                free(plugin_json);
                if (patched == NULL) { free(array); zj_free(store); if (error != NULL) *error = zeno_strdup("Plugin não encontrado ou sem campo enabled."); return NULL; }
                char *grown = patched != NULL ? zeno_json_array_append(array, patched) : NULL;
                free(patched);
                if (grown != NULL) { free(array); array = grown; }
                continue;
            }
            char *plugin_json = zj_stringify_compact(plugin);
            char *grown = plugin_json != NULL ? zeno_json_array_append(array, plugin_json) : NULL;
            free(plugin_json);
            if (grown != NULL) { free(array); array = grown; }
        }
    }
    zj_free(store);
    if (op == 1 && upsert_json != NULL && *upsert_json != 0) {
        char *grown = zeno_json_array_append(array, upsert_json);
        if (grown != NULL) { free(array); array = grown; }
        found = 1;
    }
    if ((op == 2 || op == 3) && !found) {
        free(array);
        if (error != NULL) *error = zeno_strdup("Plugin não encontrado.");
        return NULL;
    }
    return zeno_format("{\"plugins\":%s}", array);
}

char *zeno_bridge_plugins_json(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    char *json = bridge_plugins_read_raw();
    zeno_mutex_unlock(&g_bridge.lock);
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error); free(json);
    if (root != NULL && root->type == ZJ_OBJECT && zj_object_get(root, "plugins") == NULL) { zj_free(root); return zeno_strdup("{}"); }
    char *serialized = root != NULL ? zj_stringify_compact(root) : NULL;
    zj_free(root);
    return serialized != NULL ? serialized : zeno_strdup("{}");
}

/* Salva (upsert) um plugin e re-registra as tools. */
/* Retorna {"ok":true,"id":"..."} ou NULL com *error. */
char *zeno_bridge_plugins_save(const char *json, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    if (json == NULL || *json == 0) {
        if (error != NULL) *error = zeno_strdup("JSON vazio.");
        return NULL;
    }
    char *parse_error = NULL;
    ZjNode *incoming = zj_parse(json, &parse_error);
    free(parse_error);
    if (incoming == NULL || incoming->type != ZJ_OBJECT) {
        zj_free(incoming);
        if (error != NULL) *error = zeno_strdup("JSON de plugin inválido.");
        return NULL;
    }
    const char *raw_id = zj_string(zj_object_get(incoming, "id"));
    char *plugin_id = raw_id != NULL && *raw_id != 0 ? zeno_strdup(raw_id) : zeno_format("plg_%lld", zeno_now_ms());
    zj_free(incoming);
    char *payload = zeno_strdup(json);
    if (strstr(json, "\"enabled\"") == NULL) {
        /* injeta id (se faltar) e enabled=true: plugins novos nascem ativos */
        char *patched = zeno_format("{\"id\":\"%s\",\"enabled\":true,%.*s", plugin_id, (int)(strlen(json) - 1), json + 1);
        free(payload); payload = patched;
    } else if (raw_id == NULL || *raw_id == 0) {
        char *patched = zeno_format("{\"id\":\"%s\",%.*s", plugin_id, (int)(strlen(json) - 1), json + 1);
        free(payload); payload = patched;
    }
    char *store_json = bridge_plugins_store_maintain(1, plugin_id, payload, 0, error);
    int ok = store_json != NULL && bridge_plugins_write_raw(store_json);
    free(store_json); free(payload);
    if (!ok) {
        free(plugin_id);
        if (error != NULL && *error == NULL) *error = zeno_strdup("Não foi possível salvar o plugin.");
        return NULL;
    }
    char *result = zeno_format("{\"ok\":true,\"id\":\"%s\"}", plugin_id);
    free(plugin_id);
    return result;
}

/* Remove um plugin por id. */
char *zeno_bridge_plugins_delete(const char *id, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    if (id == NULL || *id == 0) {
        if (error != NULL) *error = zeno_strdup("id é obrigatório.");
        return NULL;
    }
    char *store_json = bridge_plugins_store_maintain(2, id, NULL, 0, error);
    int ok = store_json != NULL && bridge_plugins_write_raw(store_json);
    free(store_json);
    if (!ok) {
        if (error != NULL && *error == NULL) *error = zeno_strdup("Não foi possível salvar o store.");
        return NULL;
    }
    return zeno_strdup("{\"ok\":true}");
}

/* Liga/desliga um plugin por id. */
char *zeno_bridge_plugins_toggle(const char *id, int enabled, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    if (id == NULL || *id == 0) {
        if (error != NULL) *error = zeno_strdup("id é obrigatório.");
        return NULL;
    }
    char *store_json = bridge_plugins_store_maintain(3, id, NULL, enabled, error);
    int ok = store_json != NULL && bridge_plugins_write_raw(store_json);
    free(store_json);
    if (!ok) {
        if (error != NULL && *error == NULL) *error = zeno_strdup("Não foi possível salvar o store.");
        return NULL;
    }
    return zeno_strdup("{\"ok\":true}");
}

static ZenoRouter *bridge_build_router(void) {
    ZenoRouter *router = zeno_router_create();
    if (router == NULL) return NULL;
    if (g_bridge.cfg.api_key[0] != '\0') {
        ZenoProviderConfig provider;
        memset(&provider, 0, sizeof(provider));
        provider.id = g_bridge.cfg.provider[0] != '\0' ? g_bridge.cfg.provider : "openai";
        provider.base_url = g_bridge.cfg.base_url;
        provider.api_key = g_bridge.cfg.api_key;
        provider.models_csv = g_bridge.cfg.model;
        provider.priority = 1;
        provider.max_errors = 3;
        provider.timeout_ms = g_bridge.zeno.llm_timeout_ms > 0 ? g_bridge.zeno.llm_timeout_ms : 180000;
        (void)zeno_router_add_provider(router, provider);
    }
    if (g_bridge.cfg.fallback_models[0] != '\0') zeno_router_set_fallback_models(router, g_bridge.cfg.fallback_models);
    return router;
}

typedef struct BridgeRuntime {
    ZenoSandbox *sandbox;
    ZenoMemory *memory;
    ZenoRegistry *registry;
    ZenoApproval *approval;
    ZenoRouter *router;
    ZenoAgent *agent;
} BridgeRuntime;

static void bridge_runtime_destroy(BridgeRuntime *runtime) {
    if (runtime == NULL) return;
    if (runtime->agent != NULL) zeno_agent_destroy(runtime->agent);
    if (runtime->router != NULL) zeno_router_destroy(runtime->router);
    if (runtime->approval != NULL) zeno_approval_destroy(runtime->approval);
    if (runtime->registry != NULL) zeno_registry_destroy(runtime->registry);
    if (runtime->memory != NULL) zeno_memory_destroy(runtime->memory);
    if (runtime->sandbox != NULL) zeno_sandbox_destroy(runtime->sandbox);
    memset(runtime, 0, sizeof(*runtime));
}

static int bridge_runtime_build(BridgeRuntime *runtime, char **error) {
    memset(runtime, 0, sizeof(*runtime));
    ZenoSandboxPolicy policy;
    policy.workspace_root = g_bridge.cfg.workspace;
    policy.strict = 0;
    policy.allowed_executables_csv = "";
    policy.allow_shell_operators = 1;
    policy.default_timeout_ms = g_bridge.zeno.sandbox_timeout_ms > 0 ? g_bridge.zeno.sandbox_timeout_ms : 120000;
    policy.max_output_chars = g_bridge.zeno.sandbox_max_output_chars > 0 ? g_bridge.zeno.sandbox_max_output_chars : 256000;
    policy.max_command_chars = g_bridge.zeno.sandbox_max_command_chars > 0 ? g_bridge.zeno.sandbox_max_command_chars : 32000;
    policy.max_concurrent_jobs = g_bridge.zeno.sandbox_max_jobs > 0 ? g_bridge.zeno.sandbox_max_jobs : 4;
    policy.max_background_output_chars = g_bridge.zeno.sandbox_max_job_output_chars;
    policy.max_background_runtime_ms = g_bridge.zeno.sandbox_max_job_runtime_ms;
    policy.allow_network_commands = 0;
    runtime->sandbox = zeno_sandbox_create(&policy);
    runtime->memory = zeno_memory_create(g_bridge.memory_path);
    runtime->registry = zeno_registry_create();
    runtime->approval = zeno_approval_create();
    runtime->router = bridge_build_router();
    if (runtime->sandbox == NULL || runtime->memory == NULL || runtime->registry == NULL ||
        runtime->approval == NULL || runtime->router == NULL) {
        if (error != NULL) *error = zeno_strdup("Falha ao inicializar o runtime ZenoC.");
        bridge_runtime_destroy(runtime);
        return 0;
    }
    int registered = g_bridge.cfg.agent_mode == ZENO_AGENT_MODE_MINIMAL
        ? zeno_registry_register_minimal(runtime->registry, runtime->sandbox, runtime->memory, g_bridge.cfg.workspace)
        : zeno_registry_register_builtins(runtime->registry, runtime->sandbox, runtime->memory, g_bridge.cfg.workspace);
    if (registered && !bridge_register_memory_link_tool(runtime->registry, error)) {
        bridge_runtime_destroy(runtime);
        return 0;
    }
    /* Plugins do usuário: ferramentas de código, botões, abas e settings. */
    if (registered) bridge_register_plugin_tools(runtime->registry, runtime->sandbox);
    if (!registered) {
        if (error != NULL) *error = zeno_strdup("Falha ao registrar as ferramentas do ZenoC.");
        bridge_runtime_destroy(runtime);
        return 0;
    }
    ZenoAgentOptions options;
    memset(&options, 0, sizeof(options));
    options.registry = runtime->registry;
    options.router = runtime->router;
    options.memory = runtime->memory;
    options.sandbox = runtime->sandbox;
    options.cache = NULL;
    options.trace = NULL;
    options.approval = runtime->approval;
    options.model = g_bridge.cfg.model;
    options.runs_dir = g_bridge.zeno.runs_dir;
    options.require_approval = g_bridge.cfg.require_approval;
    options.absolute_mode = 0;
    options.max_turns = g_bridge.zeno.max_agent_turns > 0 ? g_bridge.zeno.max_agent_turns : 20;
    options.max_history_chars = 120000;
    options.max_tool_output_chars = 20000;
    options.mode = g_bridge.cfg.agent_mode;
    options.caveman_off = 1;
    runtime->agent = zeno_agent_create(&options);
    if (runtime->agent == NULL) {
        if (error != NULL) *error = zeno_strdup("Falha ao criar o agente ZenoC.");
        bridge_runtime_destroy(runtime);
        return 0;
    }
    {
        char *skills_dir = bridge_join(g_bridge.cfg.workspace, g_bridge.cfg.skills_dir);
        if (skills_dir != NULL) {
            if (bridge_file_exists(skills_dir)) (void)zeno_agent_attach_skills(runtime->agent, runtime->registry, skills_dir);
            free(skills_dir);
        }
    }
    return 1;
}

static char *bridge_run_result_json(const ZenoAgentResult *result, const char *model) {
    char *escaped_response = zeno_json_escape(result->response != NULL ? result->response : "");
    char *escaped_status = zeno_json_escape(result->status != NULL ? result->status : "failed");
    char *escaped_run = zeno_json_escape(result->run_id != NULL ? result->run_id : "");
    char *escaped_model = zeno_json_escape(model != NULL ? model : "");
    char *json = (escaped_response && escaped_status && escaped_run && escaped_model)
        ? zeno_format("{\"status\":%s,\"response\":%s,\"run_id\":%s,\"model\":%s,\"turns\":%d,\"tool_calls\":%d,\"successful_tools\":%d,\"failed_tools\":%d,\"tokens_in\":%lld,\"tokens_out\":%lld,\"tokens_cached\":%lld,\"duration_ms\":%lld}",
                      escaped_status, escaped_response, escaped_run, escaped_model, result->turns,
                      result->tool_calls, result->successful_tools, result->failed_tools,
                      result->tokens_in, result->tokens_out, result->tokens_cached, result->duration_ms)
        : NULL;
    free(escaped_response); free(escaped_status); free(escaped_run); free(escaped_model);
    return json;
}

static void bridge_sanitize_session(const char *input, char *output, size_t size) {
    size_t write = 0;
    if (input == NULL) input = "";
    for (size_t index = 0; input[index] != '\0' && write + 1 < size; index++) {
        char c = input[index];
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (safe) output[write++] = c;
    }
    if (write == 0) {
        const char *fallback = "gui";
        for (size_t index = 0; fallback[index] != '\0' && write + 1 < size; index++) output[write++] = fallback[index];
    }
    output[write] = '\0';
}

char *zeno_bridge_run(const char *session_id, const char *message,
                      const char *model, ZenoBridgeEmit emit, void *emit_context,
                      char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    if (message == NULL || *message == '\0') {
        if (error != NULL) *error = zeno_strdup("Mensagem vazia.");
        return NULL;
    }
    char session[160];
    bridge_sanitize_session(session_id, session, sizeof(session));
    zeno_mutex_lock(&g_bridge.lock);
    if (g_bridge.busy) {
        zeno_mutex_unlock(&g_bridge.lock);
        if (error != NULL) *error = zeno_strdup("Um run do agente já está em andamento.");
        return NULL;
    }
    if (g_bridge.cfg.api_key[0] == '\0') {
        zeno_mutex_unlock(&g_bridge.lock);
        if (error != NULL) *error = zeno_strdup("Nenhum provider configurado. Abra Settings → Models e informe a API key.");
        return NULL;
    }
    g_bridge.busy = 1;
    g_bridge.cancel = 0;
    BridgeRuntime runtime;
    char *build_error = NULL;
    if (!bridge_runtime_build(&runtime, &build_error)) {
        g_bridge.busy = 0;
        zeno_mutex_unlock(&g_bridge.lock);
        if (error != NULL) *error = build_error != NULL ? build_error : zeno_strdup("Falha ao preparar o runtime.");
        else free(build_error);
        return NULL;
    }
    g_bridge.active_agent = runtime.agent;
    const char *run_model = model != NULL && *model != '\0' ? model : g_bridge.cfg.model;
    BridgeRunContext context;
    memset(&context, 0, sizeof(context));
    context.emit = emit;
    context.emit_context = emit_context;
    context.session_id = session;
    context.model = run_model;
    zeno_mutex_unlock(&g_bridge.lock);

    {
        char *escaped_session = zeno_json_escape(session);
        char *escaped_model = zeno_json_escape(run_model);
        if (escaped_session != NULL && escaped_model != NULL) {
            char *event = zeno_format("{\"type\":\"run_started\",\"session_id\":%s,\"model\":%s}", escaped_session, escaped_model);
            if (event != NULL) bridge_emit_event(&context, event);
            free(event);
        }
        free(escaped_session);
        free(escaped_model);
    }

    ZenoRunOptions run_options;
    memset(&run_options, 0, sizeof(run_options));
    run_options.model = run_model;
    run_options.max_turns = g_bridge.zeno.max_agent_turns > 0 ? g_bridge.zeno.max_agent_turns : 20;
    run_options.max_tokens = 4096;
    run_options.temperature = 0.3;
    run_options.use_cache = 0;
    run_options.require_approval = g_bridge.cfg.require_approval;
    run_options.stream = 0; /* sem curl o stream entrega o texto final no chunk */
    run_options.resume_run_id = NULL;
    run_options.approve = bridge_auto_approve;
    run_options.approve_context = &context;
    run_options.event = bridge_agent_event;
    run_options.chunk = bridge_stream_chunk;
    run_options.callback_context = &context;
    run_options.should_cancel = bridge_should_cancel;
    run_options.cancel_context = &g_bridge;
    run_options.wall_clock_budget_ms = 0;
    run_options.pre_tool_hook = bridge_pre_tool;
    run_options.post_tool_hook = bridge_post_tool;
    run_options.hook_context = &context;

    ZenoAgentResult result = zeno_agent_run(runtime.agent, session, message, &run_options);
    char *result_json = bridge_run_result_json(&result, run_model);
    if (result_json != NULL && (result.status == NULL || strcmp(result.status, "completed") != 0)) {
        char *stats = zeno_router_stats_json(runtime.router);
        if (stats != NULL) {
            char *parse_error = NULL;
            ZjNode *root = zj_parse(stats, &parse_error);
            free(parse_error);
            const char *last_error = NULL;
            if (root != NULL && root->type == ZJ_ARRAY) {
                ZjNode *provider0 = zj_array_get(root, 0);
                last_error = provider0 != NULL ? zj_string(zj_object_get(provider0, "last_error")) : NULL;
            }
            if (last_error != NULL && *last_error != '\0') {
                result_json = bridge_obj_append(result_json, "provider_error", last_error);
            }
            zj_free(root);
            free(stats);
        }
    }
    {
        char *escaped_status = zeno_json_escape(result.status != NULL ? result.status : "failed");
        char *event = escaped_status != NULL
            ? zeno_format("{\"type\":\"run_completed\",\"status\":%s,\"turns\":%d,\"tool_calls\":%d,\"tokens_in\":%lld,\"tokens_out\":%lld,\"duration_ms\":%lld}",
                          escaped_status, result.turns, result.tool_calls, result.tokens_in, result.tokens_out, result.duration_ms)
            : NULL;
        if (event != NULL) bridge_emit_event(&context, event);
        free(event);
        free(escaped_status);
    }
    if (result.response == NULL || *result.response == '\0') {
        char *event = zeno_strdup("{\"type\":\"error\",\"message\":\"O agente terminou sem resposta. Verifique a API key e o modelo.\"}");
        if (event != NULL) bridge_emit_event(&context, event);
        free(event);
    }
    zeno_agent_result_free(&result);
    bridge_runtime_destroy(&runtime);

    zeno_mutex_lock(&g_bridge.lock);
    g_bridge.active_agent = NULL;
    g_bridge.busy = 0;
    g_bridge.cancel = 0;
    zeno_mutex_unlock(&g_bridge.lock);
    return result_json;
}

char *zeno_bridge_chat(const char *request_json, ZenoBridgeEmit emit,
                       void *emit_context, char **error) {
    if (error != NULL) *error = NULL;
    char *parse_error = NULL;
    ZjNode *root = zj_parse(request_json != NULL ? request_json : "{}", &parse_error);
    free(parse_error);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        if (error != NULL) *error = zeno_strdup("JSON de chat inválido.");
        return NULL;
    }
    char *message = bridge_json_get_optional_string(root, "message", "");
    char *model = bridge_json_get_optional_string(root, "model", NULL);
    char *session = bridge_json_get_optional_string(root, "session", NULL);
    zj_free(root);
    if (message == NULL || *message == '\0') {
        free(message); free(model); free(session);
        if (error != NULL) *error = zeno_strdup("Campo 'message' é obrigatório.");
        return NULL;
    }
    char *result = zeno_bridge_run(session, message, model, emit, emit_context, error);
    free(message);
    free(model);
    free(session);
    return result;
}

void zeno_bridge_cancel(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    g_bridge.cancel = 1;
    if (g_bridge.active_agent != NULL) zeno_agent_request_cancel(g_bridge.active_agent);
    zeno_mutex_unlock(&g_bridge.lock);
}

int zeno_bridge_busy(void) {
    zeno_bridge_init(NULL);
    return g_bridge.busy;
}

/* ============================ memória ============================ */

static char *bridge_memory_read_locked(void) {
    char *json = zeno_markdown_read_json(g_bridge.memory_path, BRIDGE_MEMORY_FALLBACK);
    return json != NULL ? json : zeno_strdup(BRIDGE_MEMORY_FALLBACK);
}

static int bridge_memory_write_locked(const char *json) {
    return zeno_markdown_write_json(g_bridge.memory_path, "Zeno Memory Store", json);
}

static ZjNode *bridge_object_ensure_array(ZjNode *root, const char *key) {
    if (root == NULL || root->type != ZJ_OBJECT) return NULL;
    ZjNode *array = zj_object_get(root, key);
    if (array != NULL && array->type == ZJ_ARRAY) return array;
    if (array != NULL) return NULL;
    array = (ZjNode *)calloc(1, sizeof(ZjNode));
    ZjPair *pair = (ZjPair *)calloc(1, sizeof(ZjPair));
    if (array == NULL || pair == NULL) { free(array); free(pair); return NULL; }
    array->type = ZJ_ARRAY;
    pair->key = zeno_strdup(key);
    pair->value = array;
    pair->next = root->object;
    root->object = pair;
    return array;
}

static int bridge_array_remove_id(ZjNode *array, const char *id) {
    if (array == NULL || array->type != ZJ_ARRAY || id == NULL) return 0;
    for (size_t index = 0; index < array->count; index++) {
        ZjNode *item = array->items[index];
        const char *item_id = item != NULL ? zj_string(zj_object_get(item, "id")) : NULL;
        if (item_id != NULL && strcmp(item_id, id) == 0) {
            zj_free(item);
            if (index + 1 < array->count) {
                memmove(&array->items[index], &array->items[index + 1], (array->count - index - 1) * sizeof(*array->items));
            }
            array->count--;
            return 1;
        }
    }
    return 0;
}

static ZjNode *bridge_array_find_id(ZjNode *array, const char *id) {
    if (array == NULL || array->type != ZJ_ARRAY || id == NULL) return NULL;
    for (size_t index = 0; index < array->count; index++) {
        ZjNode *item = array->items[index];
        const char *item_id = item != NULL ? zj_string(zj_object_get(item, "id")) : NULL;
        if (item_id != NULL && strcmp(item_id, id) == 0) return item;
    }
    return NULL;
}

static int bridge_array_append(ZjNode *array, ZjNode *item) {
    if (array == NULL || array->type != ZJ_ARRAY || item == NULL) return 0;
    ZjNode **grown = (ZjNode **)realloc(array->items, (array->count + 1) * sizeof(*grown));
    if (grown == NULL) return 0;
    array->items = grown;
    array->items[array->count++] = item;
    return 1;
}

char *zeno_bridge_memory_notes_json(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    char *json = bridge_memory_read_locked();
    zeno_mutex_unlock(&g_bridge.lock);
    if (json == NULL) return zeno_strdup("[]");
    char *error = NULL;
    ZjNode *root = zj_parse(json, &error);
    free(error);
    free(json);
    if (root == NULL) return zeno_strdup("[]");
    ZjNode *notes = zj_object_get(root, "notes");
    if (notes == NULL || notes->type != ZJ_ARRAY) { zj_free(root); return zeno_strdup("[]"); }
    /* A UI mostra memórias/notas do usuário; kinds internos do runtime
     * (session, tool_sequence) ficam no store para o agente, mas não entram
     * no grafo da interface. */
    char *filtered = zeno_strdup("[");
    if (filtered != NULL) {
        for (size_t i = 0; i < notes->count; i++) {
            ZjNode *note = zj_array_get(notes, i);
            if (note == NULL) continue;
            const char *kind = zj_string(zj_object_get(note, "kind"));
            if (kind != NULL && (!strcmp(kind, "session") || !strcmp(kind, "tool_sequence"))) continue;
            char *item = zj_stringify_compact(note);
            char *next = item != NULL ? zeno_json_array_append(filtered, item) : NULL;
            free(item);
            if (next != NULL) filtered = next;
        }
    }
    zj_free(root);
    return filtered != NULL ? filtered : zeno_strdup("[]");
}

static char *bridge_note_add_locked(const char *title, const char *content, const char *kind,
                                    const char *scope, const char *tags, char **error) {
    if (title == NULL || *title == '\0') { if (error != NULL) *error = zeno_strdup("Título é obrigatório."); return NULL; }
    if (content == NULL) content = "";
    if (strlen(content) > BRIDGE_NOTE_CONTENT_LIMIT) { if (error != NULL) *error = zeno_strdup("Conteúdo da nota excede o limite."); return NULL; }
    char *id = bridge_gen_id("mem");
    if (id == NULL) { if (error != NULL) *error = zeno_strdup("Sem memória."); return NULL; }
    long long created = zeno_now_ms();
    char *note_json = bridge_note_json(id, title, content, kind, scope,
                                       tags != NULL && *tags != '\0' ? tags : "[]", created);
    free(id);
    if (note_json == NULL) { if (error != NULL) *error = zeno_strdup("Nota inválida."); return NULL; }
    char *json = bridge_memory_read_locked();
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : BRIDGE_MEMORY_FALLBACK, &parse_error);
    free(parse_error);
    free(json);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root); free(note_json);
        if (error != NULL) *error = zeno_strdup("Store de memória corrompido.");
        return NULL;
    }
    ZjNode *notes = bridge_object_ensure_array(root, "notes");
    char *node_error = NULL;
    ZjNode *note_node = zj_parse(note_json, &node_error);
    free(node_error);
    if (notes == NULL || note_node == NULL || !bridge_array_append(notes, note_node)) {
        zj_free(note_node); zj_free(root); free(note_json);
        if (error != NULL) *error = zeno_strdup("Não foi possível adicionar a nota.");
        return NULL;
    }
    char *serialized = zj_stringify(root);
    zj_free(root);
    if (serialized == NULL || !bridge_memory_write_locked(serialized)) {
        free(serialized); free(note_json);
        if (error != NULL) *error = zeno_strdup("Não foi possível salvar a memória.");
        return NULL;
    }
    free(serialized);
    return note_json;
}

char *zeno_bridge_memory_note_add(const char *json, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        if (error != NULL) *error = zeno_strdup("JSON inválido.");
        return NULL;
    }
    char *title = bridge_json_get_optional_string(root, "title", "");
    char *content = bridge_json_get_optional_string(root, "content", "");
    char *kind = bridge_json_get_optional_string(root, "kind", "note");
    char *scope = bridge_json_get_optional_string(root, "scope", "global");
    char *tags = bridge_json_get_optional_string(root, "tags_json", "[]");
    zj_free(root);
    zeno_mutex_lock(&g_bridge.lock);
    char *note = bridge_note_add_locked(title, content, kind, scope, tags, error);
    zeno_mutex_unlock(&g_bridge.lock);
    free(title); free(content); free(kind); free(scope); free(tags);
    return note;
}

char *zeno_bridge_memory_note_update(const char *json, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        if (error != NULL) *error = zeno_strdup("JSON inválido.");
        return NULL;
    }
    char *id = bridge_json_get_optional_string(root, "id", "");
    char *title = bridge_json_get_optional_string(root, "title", NULL);
    char *content = bridge_json_get_optional_string(root, "content", NULL);
    char *kind = bridge_json_get_optional_string(root, "kind", NULL);
    char *scope = bridge_json_get_optional_string(root, "scope", NULL);
    char *tags = bridge_json_get_optional_string(root, "tags_json", NULL);
    zj_free(root);
    if (id == NULL || *id == '\0') {
        free(id); free(title); free(content); free(kind); free(scope); free(tags);
        if (error != NULL) *error = zeno_strdup("id é obrigatório.");
        return NULL;
    }
    zeno_mutex_lock(&g_bridge.lock);
    char *store = bridge_memory_read_locked();
    char *store_error = NULL;
    ZjNode *store_root = zj_parse(store != NULL ? store : BRIDGE_MEMORY_FALLBACK, &store_error);
    free(store_error); free(store);
    ZjNode *notes = store_root != NULL ? zj_object_get(store_root, "notes") : NULL;
    ZjNode *existing = bridge_array_find_id(notes, id);
    if (existing == NULL) {
        zj_free(store_root);
        zeno_mutex_unlock(&g_bridge.lock);
        free(id); free(title); free(content); free(kind); free(scope); free(tags);
        if (error != NULL) *error = zeno_strdup("Nota não encontrada.");
        return NULL;
    }
    char *current_title = bridge_json_get_optional_string(existing, "title", "");
    char *current_content = bridge_json_get_optional_string(existing, "content", "");
    char *current_kind = bridge_json_get_optional_string(existing, "kind", "note");
    char *current_scope = bridge_json_get_optional_string(existing, "scope", "global");
    char *current_tags = bridge_json_get_optional_string(existing, "tags", "[]");
    long long created = zj_integer(zj_object_get(existing, "created_ms"), zeno_now_ms());
    char *updated_json = bridge_note_json(id,
        title != NULL ? title : current_title,
        content != NULL ? content : current_content,
        kind != NULL ? kind : current_kind,
        scope != NULL ? scope : current_scope,
        tags != NULL ? tags : current_tags,
        created);
    free(current_title); free(current_content); free(current_kind); free(current_scope); free(current_tags);
    if (updated_json == NULL) {
        zj_free(store_root);
        zeno_mutex_unlock(&g_bridge.lock);
        free(id); free(title); free(content); free(kind); free(scope); free(tags);
        if (error != NULL) *error = zeno_strdup("Nota inválida.");
        return NULL;
    }
    char *node_error = NULL;
    ZjNode *replacement = zj_parse(updated_json, &node_error);
    free(node_error);
    if (replacement == NULL) {
        zj_free(store_root); free(updated_json);
        zeno_mutex_unlock(&g_bridge.lock);
        free(id); free(title); free(content); free(kind); free(scope); free(tags);
        if (error != NULL) *error = zeno_strdup("Falha ao serializar a nota.");
        return NULL;
    }
    for (size_t index = 0; index < notes->count; index++) {
        if (notes->items[index] == existing) {
            zj_free(existing);
            notes->items[index] = replacement;
            break;
        }
    }
    char *serialized = zj_stringify(store_root);
    zj_free(store_root);
    int saved = serialized != NULL && bridge_memory_write_locked(serialized);
    free(serialized);
    zeno_mutex_unlock(&g_bridge.lock);
    free(id); free(title); free(content); free(kind); free(scope); free(tags);
    if (!saved) {
        free(updated_json);
        if (error != NULL) *error = zeno_strdup("Não foi possível salvar a memória.");
        return NULL;
    }
    return updated_json;
}

char *zeno_bridge_memory_note_delete(const char *id, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    if (id == NULL || *id == '\0') { if (error != NULL) *error = zeno_strdup("id é obrigatório."); return NULL; }
    zeno_mutex_lock(&g_bridge.lock);
    char *store = bridge_memory_read_locked();
    char *parse_error = NULL;
    ZjNode *root = zj_parse(store != NULL ? store : BRIDGE_MEMORY_FALLBACK, &parse_error);
    free(parse_error); free(store);
    ZjNode *notes = root != NULL ? zj_object_get(root, "notes") : NULL;
    int removed = bridge_array_remove_id(notes, id);
    char *serialized = removed ? zj_stringify(root) : NULL;
    zj_free(root);
    int saved = serialized != NULL && bridge_memory_write_locked(serialized);
    free(serialized);
    zeno_mutex_unlock(&g_bridge.lock);
    if (!removed) {
        if (error != NULL) *error = zeno_strdup("Nota não encontrada.");
        return NULL;
    }
    if (!saved) {
        if (error != NULL) *error = zeno_strdup("Não foi possível salvar a memória.");
        return NULL;
    }
    char *result = zeno_strdup("{\"ok\":true}");
    result = bridge_obj_append(result, "id", id);
    return result;
}

/* ============================ links ============================ */

static char *bridge_links_read_locked(void) {
    char *json = zeno_markdown_read_json(g_bridge.links_path, BRIDGE_LINKS_FALLBACK);
    return json != NULL ? json : zeno_strdup(BRIDGE_LINKS_FALLBACK);
}

static char *bridge_link_add_locked(const char *from, const char *to, const char *label, char **error) {
    char *store = bridge_links_read_locked();
    char *parse_error = NULL;
    ZjNode *root = zj_parse(store != NULL ? store : BRIDGE_LINKS_FALLBACK, &parse_error);
    free(parse_error); free(store);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        if (error != NULL) *error = zeno_strdup("Store de links corrompido.");
        return NULL;
    }
    ZjNode *links = bridge_object_ensure_array(root, "links");
    if (links == NULL) { zj_free(root); if (error != NULL) *error = zeno_strdup("Falha ao preparar os links."); return NULL; }
    for (size_t index = 0; index < links->count; index++) {
        ZjNode *item = links->items[index];
        const char *a = zj_string(zj_object_get(item, "from"));
        const char *b = zj_string(zj_object_get(item, "to"));
        if (a != NULL && b != NULL && strcmp(a, from) == 0 && strcmp(b, to) == 0) {
            char *existing = zj_stringify_compact(item);
            zj_free(root);
            return existing;
        }
    }
    char *escaped_from = zeno_json_escape(from);
    char *escaped_to = zeno_json_escape(to);
    char *escaped_label = zeno_json_escape(label != NULL ? label : "");
    char *link_json = (escaped_from && escaped_to && escaped_label)
        ? zeno_format("{\"from\":%s,\"to\":%s,\"label\":%s}", escaped_from, escaped_to, escaped_label)
        : NULL;
    free(escaped_from); free(escaped_to); free(escaped_label);
    if (link_json == NULL) { zj_free(root); if (error != NULL) *error = zeno_strdup("Link inválido."); return NULL; }
    char *node_error = NULL;
    ZjNode *node = zj_parse(link_json, &node_error);
    free(node_error);
    if (node == NULL || !bridge_array_append(links, node)) {
        zj_free(node); zj_free(root); free(link_json);
        if (error != NULL) *error = zeno_strdup("Falha ao adicionar o link.");
        return NULL;
    }
    char *serialized = zj_stringify(root);
    zj_free(root);
    if (serialized == NULL || !zeno_markdown_write_json(g_bridge.links_path, "Zeno Memory Links", serialized)) {
        free(serialized); free(link_json);
        if (error != NULL) *error = zeno_strdup("Não foi possível salvar os links.");
        return NULL;
    }
    free(serialized);
    return link_json;
}

char *zeno_bridge_memory_links_json(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    char *store = bridge_links_read_locked();
    zeno_mutex_unlock(&g_bridge.lock);
    char *error = NULL;
    ZjNode *root = zj_parse(store != NULL ? store : BRIDGE_LINKS_FALLBACK, &error);
    free(error); free(store);
    if (root == NULL) return zeno_strdup("[]");
    ZjNode *links = zj_object_get(root, "links");
    char *result = links != NULL && links->type == ZJ_ARRAY ? zj_stringify_compact(links) : zeno_strdup("[]");
    zj_free(root);
    return result != NULL ? result : zeno_strdup("[]");
}

char *zeno_bridge_memory_link_add(const char *json, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        if (error != NULL) *error = zeno_strdup("JSON inválido.");
        return NULL;
    }
    char *from = bridge_json_get_optional_string(root, "from", "");
    char *to = bridge_json_get_optional_string(root, "to", "");
    char *label = bridge_json_get_optional_string(root, "label", "");
    zj_free(root);
    if (*from == '\0' || *to == '\0') {
        free(from); free(to); free(label);
        if (error != NULL) *error = zeno_strdup("from e to são obrigatórios.");
        return NULL;
    }
    zeno_mutex_lock(&g_bridge.lock);
    char *link = bridge_link_add_locked(from, to, label, error);
    zeno_mutex_unlock(&g_bridge.lock);
    free(from); free(to); free(label);
    return link;
}

char *zeno_bridge_memory_link_delete(const char *json, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    char *parse_error = NULL;
    ZjNode *request = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error);
    if (request == NULL || request->type != ZJ_OBJECT) {
        zj_free(request);
        if (error != NULL) *error = zeno_strdup("JSON inválido.");
        return NULL;
    }
    char *from = bridge_json_get_optional_string(request, "from", "");
    char *to = bridge_json_get_optional_string(request, "to", "");
    zj_free(request);
    if (*from == '\0' || *to == '\0') {
        free(from); free(to);
        if (error != NULL) *error = zeno_strdup("from e to são obrigatórios.");
        return NULL;
    }
    zeno_mutex_lock(&g_bridge.lock);
    char *store = bridge_links_read_locked();
    char *store_error = NULL;
    ZjNode *root = zj_parse(store != NULL ? store : BRIDGE_LINKS_FALLBACK, &store_error);
    free(store_error); free(store);
    ZjNode *links = root != NULL ? zj_object_get(root, "links") : NULL;
    int removed = 0;
    if (links != NULL && links->type == ZJ_ARRAY) {
        for (size_t index = 0; index < links->count; index++) {
            ZjNode *item = links->items[index];
            const char *a = zj_string(zj_object_get(item, "from"));
            const char *b = zj_string(zj_object_get(item, "to"));
            if (a != NULL && b != NULL && strcmp(a, from) == 0 && strcmp(b, to) == 0) {
                zj_free(item);
                if (index + 1 < links->count) memmove(&links->items[index], &links->items[index + 1], (links->count - index - 1) * sizeof(*links->items));
                links->count--;
                removed = 1;
                break;
            }
        }
    }
    char *serialized = removed ? zj_stringify(root) : NULL;
    zj_free(root);
    int saved = serialized != NULL && zeno_markdown_write_json(g_bridge.links_path, "Zeno Memory Links", serialized);
    free(serialized);
    zeno_mutex_unlock(&g_bridge.lock);
    free(from); free(to);
    if (!removed) {
        if (error != NULL) *error = zeno_strdup("Link não encontrado.");
        return NULL;
    }
    if (!saved) {
        if (error != NULL) *error = zeno_strdup("Não foi possível salvar os links.");
        return NULL;
    }
    return zeno_strdup("{\"ok\":true}");
}

/* ============================ skills ============================ */

#ifdef _WIN32
typedef struct BridgeDirList { char **names; size_t count; } BridgeDirList;
static BridgeDirList bridge_list_dirs(const char *path) {
    BridgeDirList list;
    memset(&list, 0, sizeof(list));
    char *pattern = zeno_format("%s\\*", path);
    if (pattern == NULL) return list;
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE) return list;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        char **grown = (char **)realloc(list.names, (list.count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        list.names = grown;
        list.names[list.count] = zeno_strdup(data.cFileName);
        if (list.names[list.count] == NULL) break;
        list.count++;
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return list;
}
static void bridge_dirlist_free(BridgeDirList *list) {
    for (size_t index = 0; index < list->count; index++) free(list->names[index]);
    free(list->names);
    memset(list, 0, sizeof(*list));
}
#else
typedef struct BridgeDirList { char **names; size_t count; } BridgeDirList;
static BridgeDirList bridge_list_dirs(const char *path) {
    BridgeDirList list;
    memset(&list, 0, sizeof(list));
    DIR *dir = opendir(path);
    if (dir == NULL) return list;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char *full = bridge_join(path, entry->d_name);
        if (full == NULL) continue;
        struct stat info;
        int is_dir = stat(full, &info) == 0 && S_ISDIR(info.st_mode);
        free(full);
        if (!is_dir) continue;
        char **grown = (char **)realloc(list.names, (list.count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        list.names = grown;
        list.names[list.count] = zeno_strdup(entry->d_name);
        if (list.names[list.count] == NULL) break;
        list.count++;
    }
    closedir(dir);
    return list;
}
static void bridge_dirlist_free(BridgeDirList *list) {
    for (size_t index = 0; index < list->count; index++) free(list->names[index]);
    free(list->names);
    memset(list, 0, sizeof(*list));
}
#endif

static char *bridge_trimmed_slice(const char *text, size_t length) {
    char *raw = zeno_strndup(text, length);
    if (raw == NULL) return NULL;
    char *trimmed = zeno_trim_copy(raw);
    free(raw);
    return trimmed;
}

static void bridge_skill_meta(const char *skill_md, char **name, char **description) {
    *name = NULL;
    *description = NULL;
    char *text = bridge_read_text(skill_md, 12000);
    if (text == NULL) return;
    int in_frontmatter = 0;
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *line_end = strchr(cursor, '\n');
        size_t length = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);
        if (length >= 3 && strncmp(cursor, "---", 3) == 0) {
            in_frontmatter = !in_frontmatter;
        } else if (in_frontmatter) {
            if (*name == NULL && length > 5 && strncmp(cursor, "name:", 5) == 0) {
                *name = bridge_trimmed_slice(cursor + 5, length - 5);
            } else if (*description == NULL && length > 12 && strncmp(cursor, "description:", 12) == 0) {
                *description = bridge_trimmed_slice(cursor + 12, length - 12);
            }
        } else if (*name == NULL && length > 2 && cursor[0] == '#') {
            *name = bridge_trimmed_slice(cursor + 1, length - 1);
        }
        if (line_end == NULL) break;
        cursor = line_end + 1;
    }
    free(text);
}

static int bridge_skills_count(void);

static char *bridge_skills_json_locked(void) {
    char *root = bridge_join(g_bridge.cfg.workspace, g_bridge.cfg.skills_dir);
    if (root == NULL) return zeno_strdup("[]");
    char *result = zeno_strdup("[]");
    BridgeDirList top = bridge_list_dirs(root);
    for (size_t index = 0; index < top.count; index++) {
        char *category_path = bridge_join(root, top.names[index]);
        if (category_path == NULL) continue;
        char *direct = bridge_join(category_path, "SKILL.md");
        if (direct != NULL && bridge_file_exists(direct)) {
            char *name = NULL; char *description = NULL;
            bridge_skill_meta(direct, &name, &description);
            char *item = zeno_strdup("{}");
            item = bridge_obj_append(item, "id", top.names[index]);
            item = bridge_obj_append(item, "name", name != NULL ? name : top.names[index]);
            item = bridge_obj_append(item, "description", description != NULL ? description : "");
            item = bridge_obj_append(item, "category", "");
            item = bridge_obj_append(item, "path", category_path);
            if (item != NULL) {
                char *grown = zeno_json_array_append(result, item);
                free(item);
                if (grown != NULL) { free(result); result = grown; }
            }
            free(name); free(description);
        } else {
            BridgeDirList children = bridge_list_dirs(category_path);
            for (size_t child = 0; child < children.count; child++) {
                char *skill_path = bridge_join(category_path, children.names[child]);
                if (skill_path == NULL) continue;
                char *skill_md = bridge_join(skill_path, "SKILL.md");
                if (skill_md != NULL && bridge_file_exists(skill_md)) {
                    char *name = NULL; char *description = NULL;
                    bridge_skill_meta(skill_md, &name, &description);
                    char *item = zeno_strdup("{}");
                    item = bridge_obj_append(item, "id", children.names[child]);
                    item = bridge_obj_append(item, "name", name != NULL ? name : children.names[child]);
                    item = bridge_obj_append(item, "description", description != NULL ? description : "");
                    item = bridge_obj_append(item, "category", top.names[index]);
                    item = bridge_obj_append(item, "path", skill_path);
                    if (item != NULL) {
                        char *grown = zeno_json_array_append(result, item);
                        free(item);
                        if (grown != NULL) { free(result); result = grown; }
                    }
                    free(name); free(description);
                }
                free(skill_md);
                free(skill_path);
            }
            bridge_dirlist_free(&children);
        }
        free(direct);
        free(category_path);
    }
    bridge_dirlist_free(&top);
    free(root);
    return result;
}

static int bridge_skills_count(void) {
    char *json = bridge_skills_json_locked();
    if (json == NULL) return 0;
    char *error = NULL;
    ZjNode *root = zj_parse(json, &error);
    free(error);
    free(json);
    if (root == NULL || root->type != ZJ_ARRAY) { zj_free(root); return 0; }
    int count = (int)root->count;
    zj_free(root);
    return count;
}

char *zeno_bridge_skills_json(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    char *result = bridge_skills_json_locked();
    zeno_mutex_unlock(&g_bridge.lock);
    return result != NULL ? result : zeno_strdup("[]");
}

/* ============================ status/tools/modelos ============================ */

char *zeno_bridge_status_json(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    int notes = 0;
    {
        char *store = bridge_memory_read_locked();
        char *error = NULL;
        ZjNode *root = zj_parse(store != NULL ? store : BRIDGE_MEMORY_FALLBACK, &error);
        free(error); free(store);
        ZjNode *array = root != NULL ? zj_object_get(root, "notes") : NULL;
        if (array != NULL && array->type == ZJ_ARRAY) notes = (int)array->count;
        zj_free(root);
    }
    int skills = bridge_skills_count();
    char *json = zeno_strdup("{}");
    json = bridge_obj_append(json, "version", ZENO_VERSION);
    json = bridge_obj_append(json, "adapter", "zenoc");
    json = bridge_obj_append(json, "provider", g_bridge.cfg.provider);
    json = bridge_obj_append(json, "base_url", g_bridge.cfg.base_url);
    json = bridge_obj_append(json, "api_key", g_bridge.cfg.api_key[0] != '\0' ? "configured" : "");
    json = bridge_obj_append(json, "model", g_bridge.cfg.model);
    json = bridge_obj_append(json, "workspace", g_bridge.cfg.workspace);
    json = bridge_obj_append(json, "agent_mode", g_bridge.cfg.agent_mode == ZENO_AGENT_MODE_MINIMAL ? "minimal" : "full");
    json = bridge_obj_append(json, "memory_path", g_bridge.memory_path);
    {
        char *extra = zeno_format("{\"require_approval\":%s,\"memory_notes\":%d,\"skills\":%d,\"busy\":%s}",
                                  g_bridge.cfg.require_approval ? "true" : "false", notes, skills,
                                  g_bridge.busy ? "true" : "false");
        if (extra != NULL) { json = bridge_obj_merge_raw(json, extra); free(extra); }
    }
    zeno_mutex_unlock(&g_bridge.lock);
    return json != NULL ? json : zeno_strdup("{}");
}

char *zeno_bridge_tools_json(void) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    ZenoSandboxPolicy policy;
    memset(&policy, 0, sizeof(policy));
    policy.workspace_root = g_bridge.cfg.workspace;
    policy.allow_shell_operators = 1;
    policy.default_timeout_ms = 120000;
    policy.max_output_chars = 256000;
    ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoMemory *memory = zeno_memory_create(g_bridge.memory_path);
    ZenoRegistry *registry = zeno_registry_create();
    char *result = NULL;
    if (registry != NULL && sandbox != NULL && memory != NULL) {
        int registered = g_bridge.cfg.agent_mode == ZENO_AGENT_MODE_MINIMAL
            ? zeno_registry_register_minimal(registry, sandbox, memory, g_bridge.cfg.workspace)
            : zeno_registry_register_builtins(registry, sandbox, memory, g_bridge.cfg.workspace);
        /* memory_link também faz parte do arsenal do agente. */
        if (registered && !bridge_register_memory_link_tool(registry, NULL)) registered = 0;
        if (registered) bridge_register_plugin_tools(registry, sandbox);
        if (registered) result = zeno_registry_list_json(registry);
    }
    zeno_registry_destroy(registry);
    zeno_memory_destroy(memory);
    zeno_sandbox_destroy(sandbox);
    zeno_mutex_unlock(&g_bridge.lock);
    return result != NULL ? result : zeno_strdup("[]");
}

/* GET usado para listar modelos. O provider é configurado pelo usuário, então
 * aceitamos hosts locais (Ollama/LM Studio); a ferramenta http_request do
 * agente continua restrita a URLs públicas pelo próprio ZenoC. */
static int bridge_http_get(const char *url, const char *api_key, long *status_out, char **body_out, char **error) {
    *status_out = 0;
    *body_out = NULL;
    *error = NULL;
#ifdef _WIN32
    if (url == NULL || *url == '\0') { *error = zeno_strdup("URL vazia."); return 0; }
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    if (wide_length <= 0 || wide_length > 8192) { *error = zeno_strdup("URL inválida."); return 0; }
    wchar_t *wide_url = (wchar_t *)malloc((size_t)wide_length * sizeof(wchar_t));
    if (wide_url == NULL) { *error = zeno_strdup("Sem memória."); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wide_url, wide_length);
    URL_COMPONENTS components;
    memset(&components, 0, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = (DWORD)-1;
    components.dwUrlPathLength = (DWORD)-1;
    components.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wide_url, 0, 0, &components)) { free(wide_url); *error = zeno_strdup("URL não pôde ser interpretada."); return 0; }
    wchar_t host[256];
    wchar_t path[4096];
    size_t host_length = (size_t)components.dwHostNameLength;
    size_t path_length = (size_t)components.dwUrlPathLength + (size_t)components.dwExtraInfoLength;
    if (host_length == 0 || host_length >= sizeof(host) / sizeof(host[0]) || path_length >= sizeof(path) / sizeof(path[0])) {
        free(wide_url); *error = zeno_strdup("URL muito longa."); return 0;
    }
    memcpy(host, components.lpszHostName, host_length * sizeof(wchar_t)); host[host_length] = L'\0';
    if (path_length == 0) { path[0] = L'/'; path[1] = L'\0'; }
    else { memcpy(path, components.lpszUrlPath, path_length * sizeof(wchar_t)); path[path_length] = L'\0'; }
    int secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    INTERNET_PORT port = components.nPort;
    free(wide_url);
    HINTERNET session = WinHttpOpen(L"ZenoC-Bridge/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        *error = zeno_strdup("Sessão WinHTTP indisponível.");
        if (getenv("ZENO_BRIDGE_DEBUG") != NULL) fprintf(stderr, "[bridge] GET %s session failed: %lu\n", url, (unsigned long)GetLastError());
        return 0;
    }
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000);
    HINTERNET connection = WinHttpConnect(session, host, port, 0);
    if (connection == NULL && getenv("ZENO_BRIDGE_DEBUG") != NULL) fprintf(stderr, "[bridge] GET %s connect failed: %lu\n", url, (unsigned long)GetLastError());
    HINTERNET request = connection != NULL ? WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
    int ok = 0;
    char *buffer = NULL;
    size_t capacity = 0, length = 0;
    if (request == NULL) {
        *error = zeno_strdup("Requisição WinHTTP inválida.");
        if (getenv("ZENO_BRIDGE_DEBUG") != NULL) fprintf(stderr, "[bridge] GET %s open-request failed: %lu\n", url, (unsigned long)GetLastError());
        goto bridge_http_cleanup;
    }
    if (api_key != NULL && *api_key != '\0') {
        char *authorization = zeno_format("Authorization: Bearer %s", api_key);
        if (authorization != NULL) {
            int wide_auth_length = MultiByteToWideChar(CP_UTF8, 0, authorization, -1, NULL, 0);
            wchar_t *wide_auth = wide_auth_length > 0 ? (wchar_t *)malloc((size_t)wide_auth_length * sizeof(wchar_t)) : NULL;
            if (wide_auth != NULL) {
                MultiByteToWideChar(CP_UTF8, 0, authorization, -1, wide_auth, wide_auth_length);
                (void)WinHttpAddRequestHeaders(request, wide_auth, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                free(wide_auth);
            }
            free(authorization);
        }
    }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, NULL)) {
        *error = zeno_format("Falha na conexão (%lu).", (unsigned long)GetLastError());
        if (getenv("ZENO_BRIDGE_DEBUG") != NULL) fprintf(stderr, "[bridge] GET %s send/receive failed: %s\n", url, *error);
        goto bridge_http_cleanup;
    }
    {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        (void)WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        *status_out = (long)status;
    }
    for (;;) {
        DWORD read = 0;
        char chunk[8192];
        if (!WinHttpReadData(request, chunk, sizeof(chunk), &read)) { *error = zeno_strdup("Falha ao ler a resposta."); goto bridge_http_cleanup; }
        if (read == 0) break;
        if (length + read + 1 > capacity) {
            size_t next = capacity == 0 ? 8192 : capacity * 2;
            while (next < length + read + 1) next *= 2;
            if (next > 4U * 1024U * 1024U) break;
            char *grown = (char *)realloc(buffer, next);
            if (grown == NULL) { *error = zeno_strdup("Sem memória."); goto bridge_http_cleanup; }
            buffer = grown;
            capacity = next;
        }
        memcpy(buffer + length, chunk, read);
        length += read;
        buffer[length] = '\0';
    }
    if (buffer == NULL) {
        buffer = zeno_strdup("");
        if (buffer == NULL) { *error = zeno_strdup("Sem memória."); goto bridge_http_cleanup; }
    }
    *body_out = buffer;
    buffer = NULL;
    ok = 1;
bridge_http_cleanup:
    free(buffer);
    if (request != NULL) WinHttpCloseHandle(request);
    if (connection != NULL) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
#else
    char *response = NULL;
    char *headers = api_key != NULL && *api_key != '\0' ? zeno_format("{\"Authorization\":\"Bearer %s\"}", api_key) : NULL;
    int ok = zeno_http_request(url, "GET", headers, NULL, 30000, 4000000, &response);
    free(headers);
    if (!ok) { *error = response != NULL ? response : zeno_strdup("Falha na requisição."); return 0; }
    const char *body = response != NULL ? strstr(response, "\n\n") : NULL;
    body = body != NULL ? body + 2 : response;
    *body_out = zeno_strdup(body != NULL ? body : "");
    *status_out = 200;
    free(response);
    return 1;
#endif
}

static char *bridge_models_from_cache(void) {
    char *json = zeno_strdup(g_bridge.cfg.models_json);
    if (json == NULL || *json == '\0') { free(json); json = zeno_strdup("[]"); }
    return json;
}

static void bridge_models_cache_store(const char *models_json) {
    bridge_copy(g_bridge.cfg.models_json, sizeof(g_bridge.cfg.models_json), models_json != NULL ? models_json : "[]");
    g_bridge.cfg.models_updated_ms = zeno_now_ms();
    char *error = NULL;
    (void)bridge_write_config_file(&error);
    free(error);
}

static int bridge_model_in_list(const char *list_json, const char *model) {
    if (list_json == NULL || model == NULL || *model == '\0') return 0;
    char *error = NULL;
    ZjNode *root = zj_parse(list_json, &error);
    free(error);
    if (root == NULL || root->type != ZJ_ARRAY) { zj_free(root); return 0; }
    int found = 0;
    for (size_t index = 0; index < root->count && !found; index++) {
        const char *id = zj_string(root->items[index]);
        if (id != NULL && strcmp(id, model) == 0) found = 1;
    }
    zj_free(root);
    return found;
}

char *zeno_bridge_models_json(int refresh) {
    zeno_bridge_init(NULL);
    zeno_mutex_lock(&g_bridge.lock);
    char *error = NULL;
    if (refresh && g_bridge.cfg.base_url[0] != '\0') {
        char *url = zeno_format("%s%s", g_bridge.cfg.base_url,
                                g_bridge.cfg.base_url[strlen(g_bridge.cfg.base_url) - 1] == '/' ? "models" : "/models");
        char *response = NULL;
        long http_status = 0;
        char *http_error = NULL;
        int fetched = url != NULL && bridge_http_get(url, g_bridge.cfg.api_key, &http_status, &response, &http_error);
        if (fetched && http_status >= 400) {
            fetched = 0;
            char *trimmed = response != NULL ? zeno_strndup(response, 200) : NULL;
            if (http_error != NULL) free(http_error);
            http_error = zeno_format("API respondeu %ld: %s", http_status, trimmed != NULL ? trimmed : "");
            free(trimmed);
            free(response);
            response = NULL;
        }
        if (fetched) {
            const char *body = response;
            char *parse_error = NULL;
            ZjNode *root = zj_parse(body != NULL ? body : "{}", &parse_error);
            free(parse_error);
            ZjNode *data = root != NULL ? zj_object_get(root, "data") : NULL;
            if (data != NULL && data->type == ZJ_ARRAY) {
                char *list = zeno_strdup("[]");
                for (size_t index = 0; index < data->count; index++) {
                    const char *id = zj_string(zj_object_get(data->items[index], "id"));
                    if (id == NULL || *id == '\0') continue;
                    char *escaped = zeno_json_escape(id);
                    if (escaped == NULL) continue;
                    char *grown = zeno_json_array_append(list, escaped);
                    free(escaped);
                    if (grown != NULL) { free(list); list = grown; }
                }
                if (list != NULL) bridge_models_cache_store(list);
                free(list);
            } else {
                error = zeno_strdup("O provider respondeu sem a lista de modelos.");
            }
            zj_free(root);
        } else if (error == NULL) {
            error = http_error != NULL ? http_error : zeno_format("Não foi possível consultar %s.", url != NULL ? url : "o provider");
            http_error = NULL;
        }
        free(http_error);
        free(url);
        free(response);
    }
    char *models = bridge_models_from_cache();
    if (g_bridge.cfg.model[0] != '\0' && !bridge_model_in_list(models, g_bridge.cfg.model)) {
        char *item = zeno_json_escape(g_bridge.cfg.model);
        if (item != NULL) {
            char *grown = zeno_json_array_append(models, item);
            free(item);
            if (grown != NULL) { free(models); models = grown; }
        }
    }
    if (g_bridge.cfg.fallback_models[0] != '\0') {
        char *copy = zeno_strdup(g_bridge.cfg.fallback_models);
        char *token = copy != NULL ? strtok(copy, ",") : NULL;
        while (token != NULL) {
            char *trimmed = zeno_trim_copy(token);
            if (trimmed != NULL && *trimmed != '\0' && !bridge_model_in_list(models, trimmed)) {
                char *item = zeno_json_escape(trimmed);
                if (item != NULL) {
                    char *grown = zeno_json_array_append(models, item);
                    free(item);
                    if (grown != NULL) { free(models); models = grown; }
                }
            }
            free(trimmed);
            token = strtok(NULL, ",");
        }
        free(copy);
    }
    char *escaped_provider = zeno_json_escape(g_bridge.cfg.provider);
    char *escaped_base = zeno_json_escape(g_bridge.cfg.base_url);
    char *escaped_model = zeno_json_escape(g_bridge.cfg.model);
    char *escaped_error = zeno_json_escape(error != NULL ? error : "");
    char *result = (escaped_provider && escaped_base && escaped_model && escaped_error)
        ? zeno_format("{\"provider\":%s,\"base_url\":%s,\"model\":%s,\"models\":%s,\"source\":%s,\"updated_ms\":%lld,\"error\":%s}",
                      escaped_provider, escaped_base, escaped_model, models,
                      refresh ? "\"remote\"" : "\"cache\"", g_bridge.cfg.models_updated_ms, escaped_error)
        : NULL;
    free(escaped_provider); free(escaped_base); free(escaped_model); free(escaped_error);
    free(models); free(error);
    zeno_mutex_unlock(&g_bridge.lock);
    return result != NULL ? result : zeno_strdup("{\"models\":[],\"error\":\"out of memory\"}");
}

int zeno_bridge_config_save(const char *json, char **error) {
    if (error != NULL) *error = NULL;
    zeno_bridge_init(NULL);
    char *parse_error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &parse_error);
    free(parse_error);
    if (root == NULL || root->type != ZJ_OBJECT) {
        zj_free(root);
        if (error != NULL) *error = zeno_strdup("JSON de configuração inválido.");
        return 0;
    }
    zeno_mutex_lock(&g_bridge.lock);
    const char *value = NULL;
    if ((value = zj_string(zj_object_get(root, "provider"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.provider, sizeof(g_bridge.cfg.provider), value);
    if ((value = zj_string(zj_object_get(root, "base_url"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.base_url, sizeof(g_bridge.cfg.base_url), value);
    if (zj_object_has(root, "api_key")) {
        value = zj_string(zj_object_get(root, "api_key"));
        bridge_copy(g_bridge.cfg.api_key, sizeof(g_bridge.cfg.api_key), value != NULL ? value : "");
    }
    if ((value = zj_string(zj_object_get(root, "model"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.model, sizeof(g_bridge.cfg.model), value);
    if (zj_object_has(root, "fallback_models")) {
        value = zj_string(zj_object_get(root, "fallback_models"));
        bridge_copy(g_bridge.cfg.fallback_models, sizeof(g_bridge.cfg.fallback_models), value != NULL ? value : "");
    }
    if ((value = zj_string(zj_object_get(root, "workspace"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.workspace, sizeof(g_bridge.cfg.workspace), value);
    if ((value = zj_string(zj_object_get(root, "skills_dir"))) != NULL && *value != '\0') bridge_copy(g_bridge.cfg.skills_dir, sizeof(g_bridge.cfg.skills_dir), value);
    if (zj_object_has(root, "agent_mode")) {
        value = zj_string(zj_object_get(root, "agent_mode"));
        g_bridge.cfg.agent_mode = (value != NULL && zeno_contains_ci(value, "minimal")) ? ZENO_AGENT_MODE_MINIMAL : ZENO_AGENT_MODE_FULL;
    }
    if (zj_object_has(root, "require_approval")) g_bridge.cfg.require_approval = zj_bool(zj_object_get(root, "require_approval"), 0);
    zj_free(root);
    bridge_refresh_paths();
    bridge_apply_zeno_config();
    int ok = bridge_write_config_file(error);
    zeno_mutex_unlock(&g_bridge.lock);
    return ok;
}
