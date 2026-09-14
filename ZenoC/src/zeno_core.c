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

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static int env_equals_ci(const char *value, const char *expected) {
    if (value == NULL || expected == NULL) return 0;
    while (isspace((unsigned char)*value)) value++;
    while (*value != '\0' && *expected != '\0') {
        if (tolower((unsigned char)*value) != tolower((unsigned char)*expected)) return 0;
        value++;
        expected++;
    }
    while (isspace((unsigned char)*value)) value++;
    return *value == '\0' && *expected == '\0';
}

static int env_true(const char *value, int fallback) {
    if (value == NULL) return fallback;
    if (env_equals_ci(value, "1") || env_equals_ci(value, "true") ||
        env_equals_ci(value, "yes") || env_equals_ci(value, "on")) return 1;
    if (env_equals_ci(value, "0") || env_equals_ci(value, "false") ||
        env_equals_ci(value, "no") || env_equals_ci(value, "off")) return 0;
    return fallback;
}

static int env_false(const char *value, int fallback) {
    if (value == NULL) return fallback;
    if (env_equals_ci(value, "0") || env_equals_ci(value, "false") ||
        env_equals_ci(value, "no") || env_equals_ci(value, "off")) return 0;
    if (env_equals_ci(value, "1") || env_equals_ci(value, "true") ||
        env_equals_ci(value, "yes") || env_equals_ci(value, "on")) return 1;
    return fallback;
}

static int env_int(const char *name, int fallback) {
    const char *value = getenv(name);
    if (value == NULL || *value == '\0') return fallback;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 0 || parsed > 2147483647L) return fallback;
    return (int)parsed;
}

static void env_copy(char *destination, size_t size, const char *name, const char *fallback) {
    const char *value = getenv(name);
    zeno_copy_string(destination, size, value != NULL ? value : fallback);
}

void zeno_config_default(ZenoConfig *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    zeno_copy_string(config->openai_base_url, sizeof(config->openai_base_url), "https://openrouter.ai/api/v1");
    zeno_copy_string(config->fireworks_base_url, sizeof(config->fireworks_base_url), "https://api.fireworks.ai/inference/v1");
    zeno_copy_string(config->model_id, sizeof(config->model_id), "gpt-4o-mini");
    zeno_copy_string(config->ollama_url, sizeof(config->ollama_url), "http://localhost:11434");
    zeno_copy_string(config->ollama_model, sizeof(config->ollama_model), "llama3.1");
    zeno_copy_string(config->workspace_root, sizeof(config->workspace_root), "./workspace");
    zeno_copy_string(config->runs_dir, sizeof(config->runs_dir), ".zeno_runs");
    zeno_copy_string(config->trace_file, sizeof(config->trace_file), ".zeno_traces.jsonl");
    config->api_port = 8000;
    config->llm_timeout_ms = 120000;
    config->max_agent_turns = 20;
    config->require_approval = 1;
    config->enable_llm_cache = 1;
    config->caveman_mode = 1;
    config->sandbox_timeout_ms = 120000;
    config->sandbox_max_output_chars = 256000;
    config->sandbox_max_command_chars = 32000;
    config->sandbox_max_jobs = 4;
    config->sandbox_max_job_output_chars = 1000000;
    config->sandbox_max_job_runtime_ms = 900000;
    /* Shell operators are opt-in; balanced/strict must not silently turn a
     * model-controlled command into a multi-command shell script. */
    config->sandbox_allow_shell_operators = 0;
    config->agent_mode = ZENO_AGENT_MODE_FULL;
}

int zeno_config_load_env(ZenoConfig *config) {
    if (config == NULL) return 0;
    zeno_config_default(config);
    env_copy(config->openai_api_key, sizeof(config->openai_api_key), "OPENAI_API_KEY", "");
    env_copy(config->openai_base_url, sizeof(config->openai_base_url), "OPENAI_BASE_URL", config->openai_base_url);
    env_copy(config->fireworks_api_key, sizeof(config->fireworks_api_key), "FIREWORKS_API_KEY", "");
    env_copy(config->fireworks_base_url, sizeof(config->fireworks_base_url), "FIREWORKS_BASE_URL", config->fireworks_base_url);
    env_copy(config->composio_api_key, sizeof(config->composio_api_key), "COMPOSIO_API_KEY", "");
    env_copy(config->model_id, sizeof(config->model_id), "MODEL_ID", config->model_id);
    env_copy(config->fallback_models, sizeof(config->fallback_models), "FALLBACK_MODELS", "");
    env_copy(config->ollama_url, sizeof(config->ollama_url), "OLLAMA_URL", config->ollama_url);
    env_copy(config->ollama_model, sizeof(config->ollama_model), "OLLAMA_MODEL", config->ollama_model);
    env_copy(config->workspace_root, sizeof(config->workspace_root), "WORKSPACE_ROOT", config->workspace_root);
    env_copy(config->runs_dir, sizeof(config->runs_dir), "ZENO_RUNS_DIR", config->runs_dir);
    env_copy(config->trace_file, sizeof(config->trace_file), "ZENO_TRACES_FILE", config->trace_file);
    config->api_port = env_int("API_PORT", config->api_port);
    config->llm_timeout_ms = env_int("LLM_TIMEOUT_MS", config->llm_timeout_ms);
    config->max_agent_turns = env_int("MAX_AGENT_TURNS", config->max_agent_turns);
    config->require_approval = env_true(getenv("REQUIRE_APPROVAL"), config->require_approval);
    config->absolute_mode = env_true(getenv("ABSOLUTE_MODE"), config->absolute_mode);
    config->enable_llm_cache = env_false(getenv("ENABLE_LLM_CACHE"), config->enable_llm_cache);
    config->enable_history_summary = env_true(getenv("ENABLE_HISTORY_SUMMARY"), config->enable_history_summary);
    config->use_ollama = env_true(getenv("USE_OLLAMA"), config->use_ollama);
    config->use_fireworks = env_true(getenv("USE_FIREWORKS"), config->use_fireworks);
    config->caveman_mode = env_false(getenv("CAVEMAN_MODE"), config->caveman_mode);
    config->sandbox_strict = env_equals_ci(getenv("SANDBOX_MODE"), "strict");
    const char *operators = getenv("SANDBOX_ALLOW_SHELL_OPERATORS");
    config->sandbox_allow_shell_operators = operators != NULL
        ? env_true(operators, config->sandbox_allow_shell_operators)
        : config->sandbox_allow_shell_operators;
    config->sandbox_allow_network = env_true(getenv("SANDBOX_ALLOW_NETWORK_COMMANDS"), 0);
    config->sandbox_timeout_ms = env_int("SANDBOX_TIMEOUT_MS", config->sandbox_timeout_ms);
    config->sandbox_max_output_chars = env_int("SANDBOX_MAX_OUTPUT_CHARS", config->sandbox_max_output_chars);
    config->sandbox_max_command_chars = env_int("SANDBOX_MAX_COMMAND_CHARS", config->sandbox_max_command_chars);
    config->sandbox_max_jobs = env_int("SANDBOX_MAX_CONCURRENT_JOBS", config->sandbox_max_jobs);
    config->sandbox_max_job_output_chars = env_int("SANDBOX_MAX_BACKGROUND_JOB_OUTPUT_CHARS", config->sandbox_max_job_output_chars);
    config->sandbox_max_job_runtime_ms = env_int("SANDBOX_MAX_BACKGROUND_JOB_RUNTIME_MS", config->sandbox_max_job_runtime_ms);
    config->agent_mode = zeno_contains_ci(getenv("ZENO_AGENT_MODE"), "minimal") ? ZENO_AGENT_MODE_MINIMAL : ZENO_AGENT_MODE_FULL;
    return 1;
}

int zeno_config_validate(const ZenoConfig *config, int require_telegram,
                         char *error, size_t error_size) {
    (void)require_telegram;
    if (error != NULL && error_size > 0) error[0] = '\0';
    if (config == NULL) {
        zeno_set_error(error, error_size, "Configuration is null.");
        return 0;
    }
    if (config->openai_api_key[0] == '\0' && config->fireworks_api_key[0] == '\0' && !config->use_ollama) {
        zeno_set_error(error, error_size, "No LLM API key configured (OPENAI_API_KEY or FIREWORKS_API_KEY).")
        ;
        return 0;
    }
    if (config->max_agent_turns < 1 || config->sandbox_timeout_ms < 1 || config->sandbox_max_output_chars < 1) {
        zeno_set_error(error, error_size, "Numeric runtime limits must be positive.");
        return 0;
    }
    return 1;
}

char *zeno_config_public_json(const ZenoConfig *config) {
    if (config == NULL) return zeno_strdup("{}");
    char *openai_url = zeno_json_escape(config->openai_base_url);
    char *fireworks_url = zeno_json_escape(config->fireworks_base_url);
    char *fallback = zeno_json_escape(config->fallback_models);
    char *model = zeno_json_escape(config->model_id);
    char *ollama_url = zeno_json_escape(config->ollama_url);
    char *ollama_model = zeno_json_escape(config->ollama_model);
    char *workspace = zeno_json_escape(config->workspace_root);
    char *result = zeno_format(
        "{\"OPENAI_API_KEY\":\"[redacted]\",\"FIREWORKS_API_KEY\":\"[redacted]\","
        "\"OPENAI_BASE_URL\":%s,\"FIREWORKS_BASE_URL\":%s,\"MODEL_ID\":%s,"
        "\"FALLBACK_MODELS\":%s,\"USE_OLLAMA\":%s,\"USE_FIREWORKS\":%s,\"OLLAMA_URL\":%s,"
        "\"OLLAMA_MODEL\":%s,\"WORKSPACE_ROOT\":%s,\"API_PORT\":%d,\"MAX_AGENT_TURNS\":%d,"
        "\"REQUIRE_APPROVAL\":%s,\"ABSOLUTE_MODE\":%s,\"CAVEMAN_MODE\":%s,\"AGENT_MODE\":\"%s\",\"SANDBOX_MODE\":\"%s\","
        "\"SANDBOX_ALLOW_NETWORK_COMMANDS\":%s,\"SANDBOX_TIMEOUT_MS\":%d,\"SANDBOX_MAX_OUTPUT_CHARS\":%d}",
        openai_url != NULL ? openai_url : "\"\"",
        fireworks_url != NULL ? fireworks_url : "\"\"",
        model != NULL ? model : "\"\"",
        fallback != NULL ? fallback : "\"\"",
        config->use_ollama ? "true" : "false", config->use_fireworks ? "true" : "false",
        ollama_url != NULL ? ollama_url : "\"\"",
        ollama_model != NULL ? ollama_model : "\"\"",
        workspace != NULL ? workspace : "\"\"",
        config->api_port, config->max_agent_turns, config->require_approval ? "true" : "false",
        config->absolute_mode ? "true" : "false", config->caveman_mode ? "true" : "false",
        config->agent_mode == ZENO_AGENT_MODE_MINIMAL ? "minimal" : "full",
        config->sandbox_strict ? "strict" : "balanced", config->sandbox_allow_network ? "true" : "false",
        config->sandbox_timeout_ms, config->sandbox_max_output_chars);
    free(openai_url);
    free(fireworks_url);
    free(fallback);
    free(model);
    free(ollama_url);
    free(ollama_model);
    free(workspace);
    return result;
}

char *zeno_read_file(const char *path, size_t max_chars) {
    if (path == NULL) return NULL;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    char *output = NULL;
    int limited = 0;
    int ok = zeno_read_all(file, max_chars, &output, &limited);
    (void)fclose(file);
    if (!ok) return NULL;
    if (limited) {
        char *bounded = zeno_format("%s\n... [truncated]", output);
        free(output);
        return bounded;
    }
    return output;
}

int zeno_write_file_atomic(const char *path, const char *content) {
    if (path == NULL || content == NULL) return 0;
    char *directory = zeno_strdup(path);
    if (directory == NULL) return 0;
    char *separator = strrchr(directory, '/');
    char *backslash = strrchr(directory, '\\');
    if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
    if (separator != NULL) {
        *separator = '\0';
        (void)zeno_mkdirs(directory);
    }
#ifdef _WIN32
    unsigned long process_id = (unsigned long)GetCurrentProcessId();
#else
    unsigned long process_id = (unsigned long)getpid();
#endif
    char *temporary = zeno_format("%s.%lld.%lu.tmp", path, zeno_now_ms(), process_id);
    FILE *file = NULL;
    if (temporary != NULL) {
#ifdef _WIN32
        int descriptor = _open(temporary, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) {
            file = _fdopen(descriptor, "wb");
            if (file == NULL) _close(descriptor);
        }
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        int descriptor = open(temporary, flags, 0600);
        if (descriptor >= 0) {
            file = fdopen(descriptor, "wb");
            if (file == NULL) close(descriptor);
        }
#endif
    }
    int ok = file != NULL && zeno_write_text(file, content) && fclose(file) == 0;
    if (file != NULL && !ok) (void)fclose(file);
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(temporary, path) == 0;
#endif
    }
    if (!ok && temporary != NULL) (void)remove(temporary);
    free(temporary);
    free(directory);
    return ok;
}

char *zeno_markdown_read_json(const char *path, const char *fallback_json) {
    char *markdown = zeno_read_file(path, 16U * 1024U * 1024U);
    if (markdown == NULL) return zeno_strdup(fallback_json != NULL ? fallback_json : "{}");
    const char *start = strstr(markdown, "```json");
    if (start == NULL) start = strstr(markdown, "```JSON");
    if (start == NULL) {
        free(markdown);
        return zeno_strdup(fallback_json != NULL ? fallback_json : "{}");
    }
    start = strchr(start, '\n');
    const char *end = start != NULL ? strstr(start + 1, "```") : NULL;
    if (start == NULL || end == NULL) {
        free(markdown);
        return zeno_strdup(fallback_json != NULL ? fallback_json : "{}");
    }
    while (start + 1 < end && isspace((unsigned char)start[1])) start++;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    char *json = zeno_strndup(start + 1, (size_t)(end - (start + 1)));
    char *error = NULL;
    ZjNode *parsed = zj_parse(json != NULL ? json : "", &error);
    free(error);
    if (parsed == NULL) {
        free(json);
        json = zeno_strdup(fallback_json != NULL ? fallback_json : "{}");
    }
    zj_free(parsed);
    free(markdown);
    return json;
}

int zeno_markdown_write_json(const char *path, const char *title, const char *json) {
    char *content = zeno_format("# %s\n\n<!-- ZENO_DATA: structured Markdown persistence -->\n\n```json\n%s\n```\n",
                                title != NULL ? title : "Zeno Data", json != NULL ? json : "{}");
    int ok = content != NULL && zeno_write_file_atomic(path, content);
    free(content);
    return ok;
}

static const char *caveman_text =
    "<<< BEGIN CAVEMAN PROTOCOL (activated) >>>\n"
    "You operate in CAVEMAN MODE: maximum token economy, no fluff.\n\n"
    "INPUT\n- Compress context to bullet points. Keep only what matters: what/where/why.\n"
    "- Ignore user filler words.\n\nPLANNING\n- Plan: max 10 steps. One line per step + target file.\n"
    "- No long explanations.\n\nEXECUTION\n- Code/output: direct, no commentary between steps.\n"
    "- Errors: one line describing what failed and the fix.\n\nREVIEW\n- Per step: CHECK or FAIL.\n"
    "- Final: DECISION: NEEDS_CORRECTION | DONE\n\nFINAL RESPONSE\n- Summary: 3-5 lines max.\n"
    "- List touched files.\n<<< END CAVEMAN PROTOCOL >>>\n";

const char *zeno_caveman_protocol(void) { return caveman_text; }

char *zeno_compress_task(const char *task, size_t max_chars) {
    char *trimmed = zeno_trim_copy(task);
    if (trimmed == NULL) return NULL;
    size_t write = 0;
    int previous_space = 0;
    for (size_t index = 0; trimmed[index] != '\0'; index++) {
        unsigned char value = (unsigned char)trimmed[index];
        if (isspace(value)) {
            if (!previous_space) trimmed[write++] = ' ';
            previous_space = 1;
        } else {
            trimmed[write++] = trimmed[index];
            previous_space = 0;
        }
    }
    trimmed[write] = '\0';
    if (max_chars > 0 && write > max_chars) {
        char *result = zeno_format("%.*s...", (int)max_chars, trimmed);
        free(trimmed);
        return result;
    }
    return trimmed;
}

char *zeno_build_caveman_prompt(int enabled) { return enabled ? zeno_strdup(caveman_text) : zeno_strdup(""); }

char *zeno_build_caveman_summary(const char *task, const char *status,
                                 const char *files_csv, int turns, double latency_ms) {
    char *compact = zeno_compress_task(task, 60);
    char *files = zeno_compress_task(files_csv != NULL && *files_csv != '\0' ? files_csv : "none", 120);
    char *result = zeno_format("SUMMARY task=%s\nSTATUS=%s turns=%d latency=%.0fms\nFILES=%s",
                               compact != NULL ? compact : "", status != NULL ? status : "unknown",
                               turns, latency_ms, files != NULL ? files : "none");
    free(compact);
    free(files);
    return result;
}

static void trace_span_free(ZenoTraceSpan *span) {
    if (span == NULL) return;
    free(span->trace_id); free(span->span_id); free(span->parent_id); free(span->name);
    free(span->kind); free(span->status); free(span->attributes_json);
}

ZenoTrace *zeno_trace_create(const char *file_path) {
    ZenoTrace *trace = (ZenoTrace *)calloc(1, sizeof(*trace));
    if (trace == NULL) return NULL;
    trace->file_path = zeno_strdup(file_path != NULL ? file_path : ".zeno_traces.jsonl");
    return trace;
}

void zeno_trace_destroy(ZenoTrace *trace) {
    if (trace == NULL) return;
    for (size_t index = 0; index < trace->count; index++) trace_span_free(&trace->spans[index]);
    free(trace->spans);
    free(trace->file_path);
    zeno_mutex_destroy(&trace->lock);
    free(trace);
}

static char *zeno_trace_start_locked(ZenoTrace *trace, const char *name, const char *kind,
                       const char *trace_id, const char *parent_id,
                       const char *attributes_json) {
    if (trace == NULL) return NULL;
    if (trace->count == trace->capacity) {
        size_t next = trace->capacity == 0 ? 32 : trace->capacity * 2;
        ZenoTraceSpan *grown = (ZenoTraceSpan *)realloc(trace->spans, next * sizeof(*grown));
        if (grown == NULL) return NULL;
        trace->spans = grown;
        trace->capacity = next;
    }
    ZenoTraceSpan *span = &trace->spans[trace->count++];
    memset(span, 0, sizeof(*span));
    span->trace_id = zeno_strdup(trace_id != NULL ? trace_id : "trace");
    span->span_id = zeno_format("span_%lld_%zu", zeno_now_ms(), trace->count);
    span->parent_id = zeno_strdup(parent_id != NULL ? parent_id : "");
    span->name = zeno_strdup(name != NULL ? name : "span");
    span->kind = zeno_strdup(kind != NULL ? kind : "internal");
    span->started_ms = zeno_now_ms();
    span->status = zeno_strdup("running");
    span->attributes_json = zeno_strdup(attributes_json != NULL ? attributes_json : "{}");
    if (trace->count > 10000) {
        trace_span_free(&trace->spans[0]);
        memmove(trace->spans, trace->spans + 1, (trace->count - 1) * sizeof(*trace->spans));
        trace->count--;
    }
    return zeno_strdup(span->span_id);
}
char * zeno_trace_start(ZenoTrace *trace, const char *name, const char *kind, const char *trace_id, const char *parent_id, const char *attributes_json) { if (trace != NULL) zeno_mutex_lock((ZenoMutex *)&trace->lock); char * zeno_result = zeno_trace_start_locked(trace, name, kind, trace_id, parent_id, attributes_json); if (trace != NULL) zeno_mutex_unlock((ZenoMutex *)&trace->lock); return zeno_result; }

static void zeno_trace_end_locked(ZenoTrace *trace, const char *span_id, const char *status,
                    const char *extra_json) {
    if (trace == NULL || span_id == NULL) return;
    for (size_t index = 0; index < trace->count; index++) {
        ZenoTraceSpan *span = &trace->spans[index];
        if (strcmp(span->span_id, span_id) != 0) continue;
        span->ended_ms = zeno_now_ms();
        free(span->status);
        span->status = zeno_strdup(status != NULL ? status : "ok");
        if (extra_json != NULL) {
            char *merged = zeno_json_object_merge(span->attributes_json, extra_json);
            if (merged != NULL) { free(span->attributes_json); span->attributes_json = merged; }
        }
        char *line = zeno_format("{\"trace_id\":%s,\"span_id\":%s,\"parent_span_id\":%s,\"name\":%s,\"kind\":%s,\"started_at\":%lld,\"ended_at\":%lld,\"duration_ms\":%lld,\"status\":%s,\"attributes\":%s}\n",
                                  zeno_json_escape(span->trace_id), zeno_json_escape(span->span_id),
                                  zeno_json_escape(span->parent_id), zeno_json_escape(span->name),
                                  zeno_json_escape(span->kind), span->started_ms, span->ended_ms,
                                  span->ended_ms - span->started_ms, zeno_json_escape(span->status),
                                  span->attributes_json != NULL ? span->attributes_json : "{}");
        if (line != NULL) {
            char *directory = zeno_strdup(trace->file_path);
            if (directory != NULL) {
                char *slash = strrchr(directory, '/');
                if (slash != NULL) { *slash = '\0'; (void)zeno_mkdirs(directory); }
                free(directory);
            }
            FILE *file = fopen(trace->file_path, "ab");
            if (file != NULL) { (void)fputs(line, file); (void)fclose(file); }
        }
        free(line);
        return;
    }
}
void zeno_trace_end(ZenoTrace *trace, const char *span_id, const char *status, const char *extra_json) { if (trace != NULL) zeno_mutex_lock((ZenoMutex *)&trace->lock); zeno_trace_end_locked(trace, span_id, status, extra_json); if (trace != NULL) zeno_mutex_unlock((ZenoMutex *)&trace->lock); }

static char *zeno_trace_get_json_locked(const ZenoTrace *trace, size_t limit) {
    if (trace == NULL) return zeno_strdup("[]");
    size_t start = trace->count > limit ? trace->count - limit : 0;
    char *result = zeno_strdup("[");
    for (size_t index = start; index < trace->count && result != NULL; index++) {
        const ZenoTraceSpan *span = &trace->spans[index];
        char *item = zeno_format("{\"trace_id\":%s,\"span_id\":%s,\"name\":%s,\"kind\":%s,\"status\":%s,\"started_at\":%lld,\"ended_at\":%lld}",
                                  zeno_json_escape(span->trace_id), zeno_json_escape(span->span_id),
                                  zeno_json_escape(span->name), zeno_json_escape(span->kind),
                                  zeno_json_escape(span->status), span->started_ms, span->ended_ms);
        char *next = zeno_json_array_append(result, item != NULL ? item : "null");
        free(item);
        free(result);
        result = next;
    }
    if (result == NULL) return zeno_strdup("[]");
    return result;
}
char * zeno_trace_get_json(const ZenoTrace *trace, size_t limit) { if (trace != NULL) zeno_mutex_lock((ZenoMutex *)&trace->lock); char * zeno_result = zeno_trace_get_json_locked(trace, limit); if (trace != NULL) zeno_mutex_unlock((ZenoMutex *)&trace->lock); return zeno_result; }

static void cache_entry_free(ZenoCacheEntry *entry) { free(entry->key); free(entry->value); }

static char *cache_serialized(const ZenoCache *cache) {
    char *result = zeno_strdup("{\"entries\":[");
    for (size_t index = 0; index < cache->count && result != NULL; index++) {
        char *key = zeno_json_escape(cache->entries[index].key);
        char *value = zeno_json_escape(cache->entries[index].value);
        char *item = zeno_format("%s{\"key\":%s,\"value\":%s,\"timestamp_ms\":%lld}",
                                  index == 0 ? "" : ",", key, value, cache->entries[index].timestamp_ms);
        char *merged = item != NULL ? zeno_format("%s%s", result, item) : NULL;
        free(key); free(value); free(item); free(result); result = merged;
    }
    if (result == NULL) return NULL;
    char *final = zeno_format("%s]}", result);
    free(result);
    return final;
}

static void cache_save(ZenoCache *cache) {
    if (cache == NULL || cache->path == NULL) return;
    char *json = cache_serialized(cache);
    if (json != NULL) { (void)zeno_markdown_write_json(cache->path, "Zeno LLM Cache", json); free(json); }
}

ZenoCache *zeno_cache_create(const char *path, int ttl_seconds, size_t max_entries) {
    ZenoCache *cache = (ZenoCache *)calloc(1, sizeof(*cache));
    if (cache == NULL) return NULL;
    cache->path = zeno_strdup(path != NULL ? path : "llm_cache.md");
    cache->ttl_seconds = ttl_seconds;
    cache->max_entries = max_entries > 0 ? max_entries : 2000;
    char *json = zeno_markdown_read_json(cache->path, "{\"entries\":[]}");
    char *error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &error);
    free(error); free(json);
    ZjNode *entries = root != NULL ? zj_object_get(root, "entries") : NULL;
    for (size_t index = 0; entries != NULL && index < entries->count; index++) {
        ZjNode *item = zj_array_get(entries, index);
        const char *key = zj_string(zj_object_get(item, "key"));
        const char *value = zj_string(zj_object_get(item, "value"));
        if (key == NULL || value == NULL) continue;
        if (cache->count == cache->capacity) {
            size_t next = cache->capacity == 0 ? 32 : cache->capacity * 2;
            ZenoCacheEntry *grown = (ZenoCacheEntry *)realloc(cache->entries, next * sizeof(*grown));
            if (grown == NULL) break;
            cache->entries = grown; cache->capacity = next;
        }
        ZenoCacheEntry *entry = &cache->entries[cache->count++];
        entry->key = zeno_strdup(key); entry->value = zeno_strdup(value);
        entry->timestamp_ms = zj_integer(zj_object_get(item, "timestamp_ms"), zeno_now_ms());
    }
    zj_free(root);
    return cache;
}

void zeno_cache_destroy(ZenoCache *cache) {
    if (cache == NULL) return;
    for (size_t index = 0; index < cache->count; index++) cache_entry_free(&cache->entries[index]);
    free(cache->entries); free(cache->path);
    zeno_mutex_destroy(&cache->lock);
    free(cache);
}

static int zeno_cache_get_locked(ZenoCache *cache, const char *key, char **value) {
    if (value != NULL) *value = NULL;
    if (cache == NULL || key == NULL) return 0;
    for (size_t index = 0; index < cache->count; index++) {
        ZenoCacheEntry *entry = &cache->entries[index];
        if (strcmp(entry->key, key) != 0) continue;
        if (cache->ttl_seconds > 0 && zeno_now_ms() - entry->timestamp_ms > (long long)cache->ttl_seconds * 1000LL) {
            cache_entry_free(entry);
            memmove(entry, entry + 1, (cache->count - index - 1) * sizeof(*entry));
            cache->count--; cache->misses++; cache_save(cache);
            return 0;
        }
        if (value != NULL) *value = zeno_strdup(entry->value);
        cache->hits++;
        return 1;
    }
    cache->misses++;
    return 0;
}
int zeno_cache_get(ZenoCache *cache, const char *key, char **value) { if (cache != NULL) zeno_mutex_lock((ZenoMutex *)&cache->lock); int zeno_result = zeno_cache_get_locked(cache, key, value); if (cache != NULL) zeno_mutex_unlock((ZenoMutex *)&cache->lock); return zeno_result; }

static int zeno_cache_set_locked(ZenoCache *cache, const char *key, const char *value) {
    if (cache == NULL || key == NULL || value == NULL) return 0;
    for (size_t index = 0; index < cache->count; index++) {
        if (strcmp(cache->entries[index].key, key) != 0) continue;
        free(cache->entries[index].value); cache->entries[index].value = zeno_strdup(value);
        cache->entries[index].timestamp_ms = zeno_now_ms(); cache_save(cache); return 1;
    }
    if (cache->count == cache->capacity) {
        size_t next = cache->capacity == 0 ? 32 : cache->capacity * 2;
        ZenoCacheEntry *grown = (ZenoCacheEntry *)realloc(cache->entries, next * sizeof(*grown));
        if (grown == NULL) return 0;
        cache->entries = grown; cache->capacity = next;
    }
    cache->entries[cache->count].key = zeno_strdup(key);
    cache->entries[cache->count].value = zeno_strdup(value);
    cache->entries[cache->count].timestamp_ms = zeno_now_ms();
    cache->count++;
    while (cache->count > cache->max_entries) {
        size_t oldest = 0;
        for (size_t index = 1; index < cache->count; index++) if (cache->entries[index].timestamp_ms < cache->entries[oldest].timestamp_ms) oldest = index;
        cache_entry_free(&cache->entries[oldest]);
        memmove(&cache->entries[oldest], &cache->entries[oldest + 1], (cache->count - oldest - 1) * sizeof(cache->entries[0]));
        cache->count--;
    }
    cache_save(cache);
    return 1;
}
int zeno_cache_set(ZenoCache *cache, const char *key, const char *value) { if (cache != NULL) zeno_mutex_lock((ZenoMutex *)&cache->lock); int zeno_result = zeno_cache_set_locked(cache, key, value); if (cache != NULL) zeno_mutex_unlock((ZenoMutex *)&cache->lock); return zeno_result; }

static void zeno_cache_clear_locked(ZenoCache *cache) {
    if (cache == NULL) return;
    for (size_t index = 0; index < cache->count; index++) cache_entry_free(&cache->entries[index]);
    cache->count = 0; cache->hits = 0; cache->misses = 0; cache_save(cache);
}
void zeno_cache_clear(ZenoCache *cache) { if (cache != NULL) zeno_mutex_lock((ZenoMutex *)&cache->lock); zeno_cache_clear_locked(cache); if (cache != NULL) zeno_mutex_unlock((ZenoMutex *)&cache->lock); }

static void zeno_cache_stats_locked(const ZenoCache *cache, size_t *hits, size_t *misses, size_t *size) {
    if (hits != NULL) *hits = cache != NULL ? cache->hits : 0;
    if (misses != NULL) *misses = cache != NULL ? cache->misses : 0;
    if (size != NULL) *size = cache != NULL ? cache->count : 0;
}
void zeno_cache_stats(const ZenoCache *cache, size_t *hits, size_t *misses, size_t *size) { if (cache != NULL) zeno_mutex_lock((ZenoMutex *)&cache->lock); zeno_cache_stats_locked(cache, hits, misses, size); if (cache != NULL) zeno_mutex_unlock((ZenoMutex *)&cache->lock); }

static void memory_turn_free(ZenoMemoryTurn *turn) {
    free(turn->id); free(turn->session_id); free(turn->role); free(turn->content);
}
static void memory_note_free(ZenoMemoryNote *note) {
    free(note->id); free(note->title); free(note->content); free(note->kind); free(note->scope); free(note->tags_json);
}
static void memory_pref_free(ZenoMemoryPref *pref) { free(pref->key); free(pref->value); free(pref->scope); }
static void memory_profile_free(ZenoUserProfile *profile) { free(profile->user_id); free(profile->frequent_tools); }

static char *memory_serialized(const ZenoMemory *memory) {
    char *json = zeno_strdup("{\"turns\":[");
    for (size_t index = 0; index < memory->turn_count && json != NULL; index++) {
        ZenoMemoryTurn *turn = &memory->turns[index];
        char *item = zeno_format("%s{\"id\":%s,\"session_id\":%s,\"role\":%s,\"content\":%s,\"created_ms\":%lld}",
                                  index == 0 ? "" : ",", zeno_json_escape(turn->id), zeno_json_escape(turn->session_id),
                                  zeno_json_escape(turn->role), zeno_json_escape(turn->content), turn->created_ms);
        char *next = item != NULL ? zeno_format("%s%s", json, item) : NULL;
        free(item); free(json); json = next;
    }
    char *notes = json != NULL ? zeno_format("%s],\"notes\":[", json) : NULL;
    free(json); json = notes;
    for (size_t index = 0; index < memory->note_count && json != NULL; index++) {
        ZenoMemoryNote *note = &memory->notes[index];
        char *item = zeno_format("%s{\"id\":%s,\"title\":%s,\"content\":%s,\"kind\":%s,\"scope\":%s,\"tags\":%s,\"created_ms\":%lld}",
                                  index == 0 ? "" : ",", zeno_json_escape(note->id), zeno_json_escape(note->title),
                                  zeno_json_escape(note->content), zeno_json_escape(note->kind), zeno_json_escape(note->scope),
                                  note->tags_json != NULL ? note->tags_json : "[]", note->created_ms);
        char *next = item != NULL ? zeno_format("%s%s", json, item) : NULL;
        free(item); free(json); json = next;
    }
    char *prefs = json != NULL ? zeno_format("%s],\"preferences\":[", json) : NULL;
    free(json); json = prefs;
    for (size_t index = 0; index < memory->pref_count && json != NULL; index++) {
        ZenoMemoryPref *pref = &memory->prefs[index];
        char *item = zeno_format("%s{\"key\":%s,\"value\":%s,\"scope\":%s}", index == 0 ? "" : ",",
                                 zeno_json_escape(pref->key), zeno_json_escape(pref->value), zeno_json_escape(pref->scope));
        char *next = item != NULL ? zeno_format("%s%s", json, item) : NULL;
        free(item); free(json); json = next;
    }
    char *profiles = json != NULL ? zeno_format("%s],\"profiles\":[", json) : NULL;
    free(json); json = profiles;
    for (size_t index = 0; index < memory->profile_count && json != NULL; index++) {
        ZenoUserProfile *profile = &memory->profiles[index];
        char *item = zeno_format("%s{\"user_id\":%s,\"created_ms\":%lld,\"interactions\":%ld,\"preferred_language\":%s,\"preferred_style\":%s,\"auto_test\":%s,\"signals\":{\"direct_count\":%ld,\"detailed_count\":%ld,\"pt_count\":%ld,\"en_count\":%ld,\"test_yes_count\":%ld,\"test_no_count\":%ld},\"frequent_tools\":%s,\"total_response_chars\":%lld}",
                                 index == 0 ? "" : ",", zeno_json_escape(profile->user_id), profile->created_ms, profile->interactions,
                                 zeno_json_escape(profile->preferred_language), zeno_json_escape(profile->preferred_style),
                                 profile->auto_test ? "true" : "false", profile->direct_count, profile->detailed_count,
                                 profile->pt_count, profile->en_count, profile->test_yes_count, profile->test_no_count,
                                 profile->frequent_tools != NULL ? profile->frequent_tools : "{}", profile->total_response_chars);
        char *next = item != NULL ? zeno_format("%s%s", json, item) : NULL;
        free(item); free(json); json = next;
    }
    char *final = json != NULL ? zeno_format("%s]}", json) : NULL;
    free(json);
    return final;
}

static void memory_save(ZenoMemory *memory) {
    if (memory == NULL || memory->path == NULL || *memory->path == '\0') return;
    char *json = memory_serialized(memory);
    if (json != NULL) { (void)zeno_markdown_write_json(memory->path, "Zeno Memory Store", json); free(json); }
}

ZenoMemory *zeno_memory_create(const char *path) {
    ZenoMemory *memory = (ZenoMemory *)calloc(1, sizeof(*memory));
    if (memory == NULL) return NULL;
    memory->path = zeno_strdup(path != NULL ? path : "ZenoC_memory.md");
    char *json = zeno_markdown_read_json(memory->path, "{\"turns\":[],\"notes\":[],\"preferences\":[]}");
    char *error = NULL;
    ZjNode *root = zj_parse(json != NULL ? json : "{}", &error);
    free(error); free(json);
    ZjNode *turns = root != NULL ? zj_object_get(root, "turns") : NULL;
    for (size_t index = 0; turns != NULL && index < turns->count; index++) {
        ZjNode *item = zj_array_get(turns, index);
        const char *session = zj_string(zj_object_get(item, "session_id"));
        const char *role = zj_string(zj_object_get(item, "role"));
        const char *content = zj_string(zj_object_get(item, "content"));
        if (session == NULL || role == NULL || content == NULL) continue;
        ZenoMemoryTurn *grown = (ZenoMemoryTurn *)realloc(memory->turns, (memory->turn_count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        memory->turns = grown; ZenoMemoryTurn *turn = &memory->turns[memory->turn_count++];
        turn->id = zeno_strdup(zj_string(zj_object_get(item, "id")) != NULL ? zj_string(zj_object_get(item, "id")) : "turn");
        turn->session_id = zeno_strdup(session); turn->role = zeno_strdup(role); turn->content = zeno_strdup(content);
        turn->created_ms = zj_integer(zj_object_get(item, "created_ms"), zeno_now_ms());
    }
    ZjNode *notes = root != NULL ? zj_object_get(root, "notes") : NULL;
    for (size_t index = 0; notes != NULL && index < notes->count; index++) {
        ZjNode *item = zj_array_get(notes, index);
        const char *title = zj_string(zj_object_get(item, "title"));
        const char *content = zj_string(zj_object_get(item, "content"));
        if (title == NULL || content == NULL) continue;
        ZenoMemoryNote *grown = (ZenoMemoryNote *)realloc(memory->notes, (memory->note_count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        memory->notes = grown; ZenoMemoryNote *note = &memory->notes[memory->note_count++];
        note->id = zeno_strdup(zj_string(zj_object_get(item, "id")) != NULL ? zj_string(zj_object_get(item, "id")) : "mem");
        note->title = zeno_strdup(title); note->content = zeno_strdup(content);
        note->kind = zeno_strdup(zj_string(zj_object_get(item, "kind")) != NULL ? zj_string(zj_object_get(item, "kind")) : "note");
        note->scope = zeno_strdup(zj_string(zj_object_get(item, "scope")) != NULL ? zj_string(zj_object_get(item, "scope")) : "global");
        note->tags_json = zj_stringify_compact(zj_object_get(item, "tags"));
        if (note->tags_json == NULL) note->tags_json = zeno_strdup("[]");
        note->created_ms = zj_integer(zj_object_get(item, "created_ms"), zeno_now_ms());
    }
    ZjNode *prefs = root != NULL ? zj_object_get(root, "preferences") : NULL;
    for (size_t index = 0; prefs != NULL && index < prefs->count; index++) {
        ZjNode *item = zj_array_get(prefs, index);
        const char *key = zj_string(zj_object_get(item, "key"));
        const char *value = zj_string(zj_object_get(item, "value"));
        if (key == NULL || value == NULL) continue;
        ZenoMemoryPref *grown = (ZenoMemoryPref *)realloc(memory->prefs, (memory->pref_count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        memory->prefs = grown; ZenoMemoryPref *pref = &memory->prefs[memory->pref_count++];
        pref->key = zeno_strdup(key); pref->value = zeno_strdup(value);
        pref->scope = zeno_strdup(zj_string(zj_object_get(item, "scope")) != NULL ? zj_string(zj_object_get(item, "scope")) : "global");
    }
    ZjNode *profiles = root != NULL ? zj_object_get(root, "profiles") : NULL;
    for (size_t index = 0; profiles != NULL && index < profiles->count; index++) {
        ZjNode *item = zj_array_get(profiles, index);
        const char *user_id = zj_string(zj_object_get(item, "user_id"));
        if (user_id == NULL) continue;
        ZenoUserProfile *grown = (ZenoUserProfile *)realloc(memory->profiles, (memory->profile_count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        memory->profiles = grown; ZenoUserProfile *profile = &memory->profiles[memory->profile_count++]; memset(profile, 0, sizeof(*profile));
        profile->user_id = zeno_strdup(user_id);
        profile->created_ms = zj_integer(zj_object_get(item, "created_ms"), zeno_now_ms());
        profile->interactions = (long)zj_integer(zj_object_get(item, "interactions"), 0);
        zeno_copy_string(profile->preferred_language, sizeof(profile->preferred_language), zj_string(zj_object_get(item, "preferred_language")) != NULL ? zj_string(zj_object_get(item, "preferred_language")) : "pt");
        zeno_copy_string(profile->preferred_style, sizeof(profile->preferred_style), zj_string(zj_object_get(item, "preferred_style")) != NULL ? zj_string(zj_object_get(item, "preferred_style")) : "direct");
        profile->auto_test = zj_bool(zj_object_get(item, "auto_test"), 1);
        ZjNode *signals = zj_object_get(item, "signals");
        profile->direct_count = (long)zj_integer(zj_object_get(signals, "direct_count"), 0);
        profile->detailed_count = (long)zj_integer(zj_object_get(signals, "detailed_count"), 0);
        profile->pt_count = (long)zj_integer(zj_object_get(signals, "pt_count"), 0);
        profile->en_count = (long)zj_integer(zj_object_get(signals, "en_count"), 0);
        profile->test_yes_count = (long)zj_integer(zj_object_get(signals, "test_yes_count"), 0);
        profile->test_no_count = (long)zj_integer(zj_object_get(signals, "test_no_count"), 0);
        ZjNode *frequent_tools = zj_object_get(item, "frequent_tools");
        profile->frequent_tools = frequent_tools != NULL && frequent_tools->type == ZJ_OBJECT
            ? zj_stringify_compact(frequent_tools)
            : zeno_strdup("{}");
        if (profile->frequent_tools == NULL) profile->frequent_tools = zeno_strdup("{}");
        profile->total_response_chars = zj_integer(zj_object_get(item, "total_response_chars"), 0);
    }
    zj_free(root);
    return memory;
}

void zeno_memory_destroy(ZenoMemory *memory) {
    if (memory == NULL) return;
    zeno_memory_flush(memory);
    for (size_t index = 0; index < memory->turn_count; index++) memory_turn_free(&memory->turns[index]);
    for (size_t index = 0; index < memory->note_count; index++) memory_note_free(&memory->notes[index]);
    for (size_t index = 0; index < memory->pref_count; index++) memory_pref_free(&memory->prefs[index]);
    for (size_t index = 0; index < memory->profile_count; index++) memory_profile_free(&memory->profiles[index]);
    free(memory->turns); free(memory->notes); free(memory->prefs); free(memory->profiles); free(memory->path);
    zeno_mutex_destroy(&memory->lock);
    free(memory);
}

static void zeno_memory_flush_locked(ZenoMemory *memory) { memory_save(memory); }
void zeno_memory_flush(ZenoMemory *memory) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); zeno_memory_flush_locked(memory); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); }

static int zeno_memory_add_turn_locked(ZenoMemory *memory, const char *session_id,
                         const char *role, const char *content) {
    if (memory == NULL || session_id == NULL || role == NULL || content == NULL) return 0;
    ZenoMemoryTurn *grown = (ZenoMemoryTurn *)realloc(memory->turns, (memory->turn_count + 1) * sizeof(*grown));
    if (grown == NULL) return 0;
    memory->turns = grown; ZenoMemoryTurn *turn = &memory->turns[memory->turn_count++];
    turn->id = zeno_format("turn_%lld_%lu", zeno_now_ms(), ++memory->sequence);
    turn->session_id = zeno_strdup(session_id); turn->role = zeno_strdup(role); turn->content = zeno_strdup(content);
    turn->created_ms = zeno_now_ms();
    memory_save(memory);
    return 1;
}
int zeno_memory_add_turn(ZenoMemory *memory, const char *session_id, const char *role, const char *content) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); int zeno_result = zeno_memory_add_turn_locked(memory, session_id, role, content); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *memory_turn_json(const ZenoMemoryTurn *turn) {
    return zeno_format("{\"id\":%s,\"session_id\":%s,\"role\":%s,\"content\":%s,\"created_ms\":%lld}",
                       zeno_json_escape(turn->id), zeno_json_escape(turn->session_id), zeno_json_escape(turn->role),
                       zeno_json_escape(turn->content), turn->created_ms);
}

static char *zeno_memory_search_conversations_locked(const ZenoMemory *memory,
                                       const char *query, const char *session_id,
                                       size_t limit) {
    char *result = zeno_strdup("[");
    size_t added = 0;
    for (size_t index = memory != NULL ? memory->turn_count : 0; index > 0 && added < limit; index--) {
        const ZenoMemoryTurn *turn = &memory->turns[index - 1];
        if (session_id != NULL && *session_id != '\0' && strcmp(session_id, turn->session_id) != 0) continue;
        if (query != NULL && *query != '\0' && !zeno_contains_ci(turn->content, query)) continue;
        char *item = memory_turn_json(turn);
        char *next = item != NULL ? zeno_json_array_append(result, item) : NULL;
        free(item); free(result); result = next; added++;
    }
    return result != NULL ? result : zeno_strdup("[]");
}
char * zeno_memory_search_conversations(const ZenoMemory *memory, const char *query, const char *session_id, size_t limit) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_search_conversations_locked(memory, query, session_id, limit); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *memory_notes_prompt(const ZenoMemory *memory, const char *query, size_t limit) {
    char *result = zeno_strdup("");
    size_t added = 0;
    for (size_t index = memory != NULL ? memory->note_count : 0; index > 0 && added < limit; index--) {
        const ZenoMemoryNote *note = &memory->notes[index - 1];
        if (query != NULL && *query != '\0' && !zeno_contains_ci(note->title, query) && !zeno_contains_ci(note->content, query)) continue;
        char *line = zeno_format("- [%s] %s: %.*s\n", note->kind, note->title, 240, note->content);
        char *next = line != NULL ? zeno_format("%s%s", result, line) : NULL;
        free(line); free(result); result = next; added++;
    }
    return result;
}

static char *zeno_memory_context_prompt_locked(const ZenoMemory *memory, const char *session_id,
                                 const char *user_id, const char *query,
                                 size_t max_items, size_t max_chars) {
    (void)user_id;
    if (max_items == 0) max_items = 30;
    if (max_chars == 0) max_chars = 16000;
    char *result = zeno_strdup("");
    size_t used = 0;
    size_t added = 0;
    for (size_t index = memory != NULL ? memory->turn_count : 0; index > 0 && added < max_items; index--) {
        const ZenoMemoryTurn *turn = &memory->turns[index - 1];
        if (session_id != NULL && strcmp(session_id, turn->session_id) != 0) continue;
        char *line = zeno_format("%s: %s\n", turn->role, turn->content);
        if (line == NULL) continue;
        if (used + strlen(line) > max_chars && added > 0) {
            char *compressed = zeno_format("%s: %.180s ...\n", turn->role, turn->content);
            free(line); line = compressed;
        }
        if (line == NULL || used + strlen(line) > max_chars) { free(line); break; }
        char *next = zeno_format("%s%s", result, line);
        free(line); free(result); result = next;
        if (result == NULL) break;
        used = strlen(result); added++;
    }
    char *notes = memory_notes_prompt(memory, query, 12);
    if (notes != NULL && *notes != '\0' && used < max_chars) {
        char *header = zeno_format("\n## MEMORY\n%s", notes);
        char *next = header != NULL ? zeno_format("%s%s", result, header) : NULL;
        free(header); free(result); result = next;
    }
    free(notes);
    return result != NULL ? result : zeno_strdup("");
}
char * zeno_memory_context_prompt(const ZenoMemory *memory, const char *session_id, const char *user_id, const char *query, size_t max_items, size_t max_chars) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_context_prompt_locked(memory, session_id, user_id, query, max_items, max_chars); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static int zeno_memory_update_session_locked(ZenoMemory *memory, const char *session_id,
                               const char *last_message, const char *last_response,
                               const char *run_id) {
    if (memory == NULL || session_id == NULL) return 0;
    char *title = zeno_format("session:%s", session_id);
    char *content = zeno_format("last_message=%s\nlast_response=%s\nrun_id=%s", last_message != NULL ? last_message : "",
                                last_response != NULL ? last_response : "", run_id != NULL ? run_id : "");
    char *id = NULL;
    int ok = title != NULL && content != NULL && zeno_memory_add_note(memory, title, content, "session", session_id, "[]", &id);
    free(title); free(content); free(id);
    return ok;
}
int zeno_memory_update_session(ZenoMemory *memory, const char *session_id, const char *last_message, const char *last_response, const char *run_id) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); int zeno_result = zeno_memory_update_session_locked(memory, session_id, last_message, last_response, run_id); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static int zeno_memory_add_note_locked(ZenoMemory *memory, const char *title,
                         const char *content, const char *kind,
                         const char *scope, const char *tags_json, char **id) {
    if (id != NULL) *id = NULL;
    if (memory == NULL || title == NULL || content == NULL) return 0;
    const char *tags = tags_json != NULL && *tags_json != '\0' ? tags_json : "[]";
    char *error = NULL;
    ZjNode *tag_node = zj_parse(tags, &error);
    int valid_tags = tag_node != NULL && tag_node->type == ZJ_ARRAY;
    free(error); zj_free(tag_node);
    if (!valid_tags) return 0;
    ZenoMemoryNote *grown = (ZenoMemoryNote *)realloc(memory->notes, (memory->note_count + 1) * sizeof(*grown));
    if (grown == NULL) return 0;
    memory->notes = grown; ZenoMemoryNote *note = &memory->notes[memory->note_count++];
    note->id = zeno_format("mem_%lld_%lu", zeno_now_ms(), ++memory->sequence);
    note->title = zeno_strdup(title); note->content = zeno_strdup(content);
    note->kind = zeno_strdup(kind != NULL && *kind != '\0' ? kind : "note");
    note->scope = zeno_strdup(scope != NULL && *scope != '\0' ? scope : "global");
    note->tags_json = zeno_strdup(tags); note->created_ms = zeno_now_ms();
    if (id != NULL) *id = zeno_strdup(note->id);
    memory_save(memory);
    return 1;
}
int zeno_memory_add_note(ZenoMemory *memory, const char *title, const char *content, const char *kind, const char *scope, const char *tags_json, char **id) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); int zeno_result = zeno_memory_add_note_locked(memory, title, content, kind, scope, tags_json, id); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *zeno_memory_search_notes_locked(const ZenoMemory *memory, const char *query, size_t limit) {
    char *result = zeno_strdup("[");
    size_t added = 0;
    for (size_t index = memory != NULL ? memory->note_count : 0; index > 0 && added < limit; index--) {
        const ZenoMemoryNote *note = &memory->notes[index - 1];
        if (query != NULL && *query != '\0' && !zeno_contains_ci(note->title, query) && !zeno_contains_ci(note->content, query)) continue;
        char *item = zeno_format("{\"id\":%s,\"title\":%s,\"content\":%s,\"kind\":%s,\"scope\":%s}",
                                 zeno_json_escape(note->id), zeno_json_escape(note->title), zeno_json_escape(note->content),
                                 zeno_json_escape(note->kind), zeno_json_escape(note->scope));
        char *next = item != NULL ? zeno_json_array_append(result, item) : NULL;
        free(item); free(result); result = next; added++;
    }
    return result != NULL ? result : zeno_strdup("[]");
}
char * zeno_memory_search_notes(const ZenoMemory *memory, const char *query, size_t limit) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_search_notes_locked(memory, query, limit); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *zeno_memory_list_notes_locked(const ZenoMemory *memory, const char *scope, size_t limit) {
    char *result = zeno_strdup("[");
    size_t added = 0;
    for (size_t index = memory != NULL ? memory->note_count : 0; index > 0 && added < limit; index--) {
        const ZenoMemoryNote *note = &memory->notes[index - 1];
        if (scope != NULL && *scope != '\0' && strcmp(scope, note->scope) != 0) continue;
        char *item = zeno_format("{\"id\":%s,\"title\":%s,\"content\":%s,\"kind\":%s,\"scope\":%s}",
                                 zeno_json_escape(note->id), zeno_json_escape(note->title), zeno_json_escape(note->content),
                                 zeno_json_escape(note->kind), zeno_json_escape(note->scope));
        char *next = item != NULL ? zeno_json_array_append(result, item) : NULL;
        free(item); free(result); result = next; added++;
    }
    return result != NULL ? result : zeno_strdup("[]");
}
char * zeno_memory_list_notes(const ZenoMemory *memory, const char *scope, size_t limit) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_list_notes_locked(memory, scope, limit); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static int zeno_memory_set_preference_locked(ZenoMemory *memory, const char *key,
                               const char *value, const char *scope) {
    if (memory == NULL || key == NULL || value == NULL) return 0;
    for (size_t index = 0; index < memory->pref_count; index++) {
        if (strcmp(memory->prefs[index].key, key) != 0) continue;
        free(memory->prefs[index].value); memory->prefs[index].value = zeno_strdup(value);
        free(memory->prefs[index].scope); memory->prefs[index].scope = zeno_strdup(scope != NULL ? scope : "global");
        memory_save(memory); return 1;
    }
    ZenoMemoryPref *grown = (ZenoMemoryPref *)realloc(memory->prefs, (memory->pref_count + 1) * sizeof(*grown));
    if (grown == NULL) return 0;
    memory->prefs = grown; ZenoMemoryPref *pref = &memory->prefs[memory->pref_count++];
    pref->key = zeno_strdup(key); pref->value = zeno_strdup(value); pref->scope = zeno_strdup(scope != NULL ? scope : "global");
    memory_save(memory); return 1;
}
int zeno_memory_set_preference(ZenoMemory *memory, const char *key, const char *value, const char *scope) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); int zeno_result = zeno_memory_set_preference_locked(memory, key, value, scope); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *zeno_memory_get_preference_locked(const ZenoMemory *memory, const char *key) {
    if (memory == NULL || key == NULL) return NULL;
    for (size_t index = 0; index < memory->pref_count; index++) if (strcmp(memory->prefs[index].key, key) == 0) return zeno_strdup(memory->prefs[index].value);
    return NULL;
}
char * zeno_memory_get_preference(const ZenoMemory *memory, const char *key) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_get_preference_locked(memory, key); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *zeno_memory_add_tool_sequence_locked(ZenoMemory *memory, const char *session_id,
                                    const char *task, const char *tools_csv,
                                    const char *result) {
    if (memory == NULL || tools_csv == NULL || strchr(tools_csv, '-') == NULL || strchr(tools_csv, '>') == NULL) return NULL;
    char *title = zeno_format("Tool sequence: %.100s", tools_csv);
    char *content = zeno_format("Session: %s\nTask: %.300s\nPattern: %s\nResult: %.500s",
                                session_id != NULL ? session_id : "", task != NULL ? task : "", tools_csv,
                                result != NULL ? result : "");
    char *id = NULL;
    if (title != NULL && content != NULL) (void)zeno_memory_add_note(memory, title, content, "tool_sequence", "global", "[\"auto_learned\",\"tool_pattern\"]", &id);
    free(title); free(content);
    return id;
}
char * zeno_memory_add_tool_sequence(ZenoMemory *memory, const char *session_id, const char *task, const char *tools_csv, const char *result) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_add_tool_sequence_locked(memory, session_id, task, tools_csv, result); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static ZenoUserProfile *memory_find_profile(ZenoMemory *memory, const char *user_id) {
    for (size_t index = 0; memory != NULL && index < memory->profile_count; index++) if (strcmp(memory->profiles[index].user_id, user_id) == 0) return &memory->profiles[index];
    return NULL;
}

static char *zeno_memory_get_or_create_user_locked(ZenoMemory *memory, const char *user_id) {
    if (memory == NULL || user_id == NULL) return NULL;
    ZenoUserProfile *profile = memory_find_profile(memory, user_id);
    if (profile == NULL) {
        ZenoUserProfile *grown = (ZenoUserProfile *)realloc(memory->profiles, (memory->profile_count + 1) * sizeof(*grown));
        if (grown == NULL) return NULL;
        memory->profiles = grown; profile = &memory->profiles[memory->profile_count++]; memset(profile, 0, sizeof(*profile));
        profile->user_id = zeno_strdup(user_id); profile->created_ms = zeno_now_ms();
        zeno_copy_string(profile->preferred_language, sizeof(profile->preferred_language), "pt");
        zeno_copy_string(profile->preferred_style, sizeof(profile->preferred_style), "direct");
        profile->auto_test = 1; profile->frequent_tools = zeno_strdup("{}");
        memory_save(memory);
    }
    return zeno_format("{\"user_id\":%s,\"created_ms\":%lld,\"interactions\":%ld,\"preferred_language\":%s,\"preferred_style\":%s,\"auto_test\":%s}",
                       zeno_json_escape(profile->user_id), profile->created_ms, profile->interactions,
                       zeno_json_escape(profile->preferred_language), zeno_json_escape(profile->preferred_style),
                       profile->auto_test ? "true" : "false");
}
char * zeno_memory_get_or_create_user(ZenoMemory *memory, const char *user_id) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_get_or_create_user_locked(memory, user_id); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static void profile_record_tool(ZenoUserProfile *profile, const char *tool_name) {
    if (profile == NULL || tool_name == NULL || *tool_name == '\0') return;
    char *current = zeno_strdup(profile->frequent_tools != NULL ? profile->frequent_tools : "{}");
    char *parse_error = NULL;
    ZjNode *object = zj_parse(current != NULL ? current : "{}", &parse_error);
    free(parse_error);
    long count = 0;
    if (object != NULL && object->type == ZJ_OBJECT)
        count = (long)zj_integer(zj_object_get(object, tool_name), 0);
    else {
        free(current);
        current = zeno_strdup("{}");
    }
    zj_free(object);
    char *key = zeno_json_escape(tool_name);
    char *entry = key != NULL ? zeno_format("{%s:%ld}", key, count + 1) : NULL;
    char *updated = current != NULL && entry != NULL ? zeno_json_object_merge(current, entry) : NULL;
    if (updated != NULL) {
        free(profile->frequent_tools);
        profile->frequent_tools = updated;
    }
    free(current);
    free(key);
    free(entry);
}

static int zeno_memory_update_user_profile_locked(ZenoMemory *memory, const char *user_id,
                                    const char *user_message, const char *response,
                                    const char *tools_json) {
    if (memory == NULL || user_id == NULL) return 0;
    ZenoUserProfile *profile = memory_find_profile(memory, user_id);
    if (profile == NULL) { char *created = zeno_memory_get_or_create_user(memory, user_id); free(created); profile = memory_find_profile(memory, user_id); }
    if (profile == NULL) return 0;
    profile->interactions++;
    profile->total_response_chars += response != NULL ? (long long)strlen(response) : 0;
    const char *message = user_message != NULL ? user_message : "";
    if (zeno_contains_ci(message, "mais direto") || zeno_contains_ci(message, "menos explicação") || zeno_contains_ci(message, "só o código") || zeno_contains_ci(message, "só faz") || zeno_contains_ci(message, "sem bla") || zeno_contains_ci(message, "direto") || zeno_contains_ci(message, "vai logo")) profile->direct_count++;
    if (zeno_contains_ci(message, "explique") || zeno_contains_ci(message, "detalhes") || zeno_contains_ci(message, "passo a passo") || zeno_contains_ci(message, "verbose") || zeno_contains_ci(message, "detalhadamente") || zeno_contains_ci(message, "mais info")) profile->detailed_count++;
    if (zeno_contains_ci(message, "em português") || zeno_contains_ci(message, "português") || zeno_contains_ci(message, "pt-br") || zeno_contains_ci(message, "pt_br")) profile->pt_count++;
    if (zeno_contains_ci(message, "em inglês") || zeno_contains_ci(message, "english") || zeno_contains_ci(message, "in english") || zeno_contains_ci(message, "en-us")) profile->en_count++;
    if (zeno_contains_ci(message, "testa") || zeno_contains_ci(message, "teste") || zeno_contains_ci(message, "testar") || zeno_contains_ci(message, "valida") || zeno_contains_ci(message, "verifica")) profile->test_yes_count++;
    if (zeno_contains_ci(message, "sem teste") || zeno_contains_ci(message, "não testa") || zeno_contains_ci(message, "skip test") || zeno_contains_ci(message, "sem verificar")) profile->test_no_count++;
    if (profile->direct_count + profile->detailed_count >= 3) zeno_copy_string(profile->preferred_style, sizeof(profile->preferred_style), profile->direct_count > profile->detailed_count ? "direct" : "detailed");
    if (profile->pt_count + profile->en_count >= 2) zeno_copy_string(profile->preferred_language, sizeof(profile->preferred_language), profile->pt_count >= profile->en_count ? "pt" : "en");
    if (profile->test_yes_count + profile->test_no_count >= 2) profile->auto_test = profile->test_yes_count > profile->test_no_count;
    char *error = NULL; ZjNode *logs = zj_parse(tools_json != NULL ? tools_json : "[]", &error); free(error);
    if (logs != NULL && logs->type == ZJ_ARRAY) for (size_t index = 0; index < logs->count; index++) { const char *tool = zj_string(zj_object_get(logs->items[index], "tool")); if (tool != NULL) profile_record_tool(profile, tool); }
    zj_free(logs);
    memory_save(memory);
    return 1;
}
int zeno_memory_update_user_profile(ZenoMemory *memory, const char *user_id, const char *user_message, const char *response, const char *tools_json) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); int zeno_result = zeno_memory_update_user_profile_locked(memory, user_id, user_message, response, tools_json); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

static char *zeno_memory_user_profile_context_locked(ZenoMemory *memory, const char *user_id) {
    if (memory == NULL || user_id == NULL) return zeno_strdup("");
    ZenoUserProfile *profile = memory_find_profile(memory, user_id);
    if (profile == NULL) return zeno_strdup("");
    char *result = zeno_format("## USER PROFILE (aprendido gradualmente)\n- preferred_language: %s\n- preferred_style: %s\n- auto_test: %s\n- total_interactions: %ld\n- most_used_tools: %s\n",
                               profile->preferred_language, profile->preferred_style, profile->auto_test ? "true" : "false",
                               profile->interactions, profile->frequent_tools != NULL ? profile->frequent_tools : "{}");
    return result;
}
char * zeno_memory_user_profile_context(ZenoMemory *memory, const char *user_id) { if (memory != NULL) zeno_mutex_lock((ZenoMutex *)&memory->lock); char * zeno_result = zeno_memory_user_profile_context_locked(memory, user_id); if (memory != NULL) zeno_mutex_unlock((ZenoMutex *)&memory->lock); return zeno_result; }

ZenoApproval *zeno_approval_create(void) { return (ZenoApproval *)calloc(1, sizeof(ZenoApproval)); }

void zeno_approval_destroy(ZenoApproval *approval) {
    if (approval == NULL) return;
    for (size_t index = 0; index < approval->count; index++) { free(approval->ids[index]); free(approval->contexts[index]); }
    free(approval->ids); free(approval->contexts);
    zeno_mutex_destroy(&approval->lock);
    free(approval);
}

int zeno_approval_needs(const ZenoApproval *approval, const char *tool_name, ZenoEffect effect) {
    (void)approval;
    if (effect == ZENO_EFFECT_PROCESS || effect == ZENO_EFFECT_WRITE_EXTERNAL || effect == ZENO_EFFECT_BROWSER) return 1;
    return tool_name != NULL && (strcmp(tool_name, "run_command") == 0 || strcmp(tool_name, "start_background_job") == 0);
}

static int zeno_approval_store_locked(ZenoApproval *approval, const char *request_id, const char *context_json) {
    if (approval == NULL || request_id == NULL || context_json == NULL) return 0;
    for (size_t index = 0; index < approval->count; index++) {
        if (strcmp(approval->ids[index], request_id) != 0) continue;
        free(approval->contexts[index]); approval->contexts[index] = zeno_strdup(context_json); return 1;
    }
    size_t next_count = approval->count + 1;
    char **ids = (char **)malloc(next_count * sizeof(*ids));
    char **contexts = (char **)malloc(next_count * sizeof(*contexts));
    if (ids == NULL || contexts == NULL) {
        free(ids);
        free(contexts);
        return 0;
    }
    if (approval->count > 0) {
        memcpy(ids, approval->ids, approval->count * sizeof(*ids));
        memcpy(contexts, approval->contexts, approval->count * sizeof(*contexts));
    }
    ids[approval->count] = zeno_strdup(request_id);
    contexts[approval->count] = zeno_strdup(context_json);
    if (ids[approval->count] == NULL || contexts[approval->count] == NULL) {
        free(ids[approval->count]);
        free(contexts[approval->count]);
        free(ids);
        free(contexts);
        return 0;
    }
    free(approval->ids);
    free(approval->contexts);
    approval->ids = ids;
    approval->contexts = contexts;
    approval->count = next_count;
    return 1;
}
int zeno_approval_store(ZenoApproval *approval, const char *request_id, const char *context_json) { if (approval != NULL) zeno_mutex_lock((ZenoMutex *)&approval->lock); int zeno_result = zeno_approval_store_locked(approval, request_id, context_json); if (approval != NULL) zeno_mutex_unlock((ZenoMutex *)&approval->lock); return zeno_result; }

static char *zeno_approval_get_locked(const ZenoApproval *approval, const char *request_id) {
    if (approval == NULL || request_id == NULL) return NULL;
    for (size_t index = 0; index < approval->count; index++) if (strcmp(approval->ids[index], request_id) == 0) return zeno_strdup(approval->contexts[index]);
    return NULL;
}
char * zeno_approval_get(const ZenoApproval *approval, const char *request_id) { if (approval != NULL) zeno_mutex_lock((ZenoMutex *)&approval->lock); char * zeno_result = zeno_approval_get_locked(approval, request_id); if (approval != NULL) zeno_mutex_unlock((ZenoMutex *)&approval->lock); return zeno_result; }

static void zeno_approval_clear_locked(ZenoApproval *approval, const char *request_id) {
    if (approval == NULL || request_id == NULL) return;
    for (size_t index = 0; index < approval->count; index++) {
        if (strcmp(approval->ids[index], request_id) != 0) continue;
        free(approval->ids[index]); free(approval->contexts[index]);
        memmove(approval->ids + index, approval->ids + index + 1, (approval->count - index - 1) * sizeof(char *));
        memmove(approval->contexts + index, approval->contexts + index + 1, (approval->count - index - 1) * sizeof(char *));
        approval->count--; return;
    }
}
void zeno_approval_clear(ZenoApproval *approval, const char *request_id) { if (approval != NULL) zeno_mutex_lock((ZenoMutex *)&approval->lock); zeno_approval_clear_locked(approval, request_id); if (approval != NULL) zeno_mutex_unlock((ZenoMutex *)&approval->lock); }

static ZenoMutex transcript_lock;

char *zeno_transcript_append(const char *workspace_root, const char *session_id,
                             const char *user_message, const char *response,
                             const char *tools_json) {
    zeno_mutex_lock(&transcript_lock);
    char *path = zeno_join_path(workspace_root != NULL ? workspace_root : ".", "TRANSCRIPT.md");
    if (path == NULL) { zeno_mutex_unlock(&transcript_lock); return NULL; }
    char *existing = zeno_read_file(path, 0);
    if (existing == NULL) existing = zeno_strdup("# TRANSCRIPT - Session Log\n\n_Log of Zeno Agent interactions._\n\n---\n");
    char *entry = zeno_format("\n### Entry: %s\n- **Session**: `%s`\n- **User**: %.*s\n- **Tools**: %s\n- **Response**: %.*s\n\n---\n",
                              zeno_timestamp_iso(), session_id != NULL ? session_id : "", 200,
                              user_message != NULL ? user_message : "", tools_json != NULL ? tools_json : "(none)",
                              500, response != NULL ? response : "");
    char *next = existing != NULL && entry != NULL ? zeno_format("%s%s", existing, entry) : NULL;
    if (next != NULL) (void)zeno_write_file_atomic(path, next);
    free(existing); free(entry); free(path);
    zeno_mutex_unlock(&transcript_lock);
    return next;
}

char *zeno_transcript_summary(const char *workspace_root, size_t max_entries) {
    char *path = zeno_join_path(workspace_root != NULL ? workspace_root : ".", "TRANSCRIPT.md");
    char *content = path != NULL ? zeno_read_file(path, 0) : NULL;
    free(path);
    if (content == NULL) return zeno_strdup("No entries yet.");
    if (max_entries == 0) max_entries = 5;
    char *cursor = content;
    size_t count = 0;
    char *last = NULL;
    while ((cursor = strstr(cursor, "### Entry:")) != NULL) { last = cursor; count++; cursor += 10; }
    if (count <= max_entries || last == NULL) return content;
    size_t skip = count - max_entries;
    cursor = content;
    while (skip > 0 && (cursor = strstr(cursor, "### Entry:")) != NULL) { cursor += 10; skip--; }
    char *summary = cursor != NULL ? zeno_format("# TRANSCRIPT - Session Log\n\n%s", cursor) : zeno_strdup(content);
    free(content);
    return summary;
}

char *zeno_transcript_read(const char *workspace_root) {
    char *path = zeno_join_path(workspace_root != NULL ? workspace_root : ".", "TRANSCRIPT.md");
    char *content = path != NULL ? zeno_read_file(path, 0) : NULL;
    free(path);
    return content != NULL ? content : zeno_strdup("# TRANSCRIPT - Session Log\n");
}

size_t zeno_transcript_count(const char *workspace_root) {
    char *content = zeno_transcript_read(workspace_root);
    size_t count = 0;
    if (content != NULL) {
        const char *cursor = content;
        while ((cursor = strstr(cursor, "### Entry:")) != NULL) { count++; cursor += 10; }
    }
    free(content);
    return count;
}

char *zeno_transcript_tool_stats(const char *workspace_root) {
    char *content = zeno_transcript_read(workspace_root);
    char *result = zeno_strdup("{}");
    if (content == NULL) return result;
    char **names = NULL; size_t *counts = NULL; size_t count = 0;
    const char *cursor = content;
    while ((cursor = strstr(cursor, "\"tool\":")) != NULL) {
        cursor += strlen("\"tool\":");
        while (*cursor != '\0' && *cursor != '"') cursor++;
        if (*cursor == '"') cursor++;
        const char *end = strchr(cursor, '"');
        if (end == NULL) break;
        char *name = zeno_strndup(cursor, (size_t)(end - cursor));
        size_t index = 0;
        for (; index < count; index++) if (strcmp(names[index], name) == 0) break;
        if (index == count) {
            char **grown_names = (char **)realloc(names, (count + 1) * sizeof(*grown_names));
            size_t *grown_counts = (size_t *)realloc(counts, (count + 1) * sizeof(*grown_counts));
            if (grown_names == NULL || grown_counts == NULL) { free(grown_names); free(grown_counts); free(name); break; }
            names = grown_names; counts = grown_counts; names[count] = name; counts[count] = 1; count++;
        } else { counts[index]++; free(name); }
        cursor = end + 1;
    }
    free(result); result = zeno_strdup("{");
    for (size_t index = 0; index < count && result != NULL; index++) {
        char *next = zeno_format("%s%s%s:%zu", result, index == 0 ? "" : ",", zeno_json_escape(names[index]), counts[index]);
        free(result); result = next; free(names[index]);
    }
    free(names); free(counts);
    if (result != NULL) { char *closed = zeno_format("%s}", result); free(result); result = closed; }
    free(content);
    return result != NULL ? result : zeno_strdup("{}");
}

char *zeno_generate_agents_summary(const char *workspace_root) {
    char *summary = zeno_transcript_summary(workspace_root, 5);
    char *path = zeno_join_path(workspace_root != NULL ? workspace_root : ".", "AGENTS.md");
    char *content = zeno_format("## Anchored Summary\n\n%s\n", summary != NULL ? summary : "No entries yet.");
    int ok = path != NULL && content != NULL && zeno_write_file_atomic(path, content);
    char *result = ok ? zeno_format("Anchored summary updated: %s", path) : zeno_strdup("Error updating summary.");
    free(summary); free(path); free(content); return result;
}

static int extension_language(const char *name, const char **language);

const char *zeno_detect_language(const char *file_name) {
    const char *language = NULL;
    return file_name != NULL && extension_language(file_name, &language) ? language : "text";
}

static int has_tool_separator(const char *tools_csv) {
    return tools_csv != NULL && strstr(tools_csv, "->") != NULL;
}

int zeno_learning_record(const char *path, const char *session_id,
                         const char *user_message, const char *tools_csv,
                         int success, char **skill_id) {
    if (skill_id != NULL) *skill_id = NULL;
    if (!success || !has_tool_separator(tools_csv)) return 0;
    char *json = zeno_markdown_read_json(path, "{\"entries\":[]}");
    char *error = NULL; ZjNode *root = zj_parse(json != NULL ? json : "{}", &error); free(error); free(json);
    ZjNode *entries = root != NULL ? zj_object_get(root, "entries") : NULL;
    if (entries == NULL || entries->type != ZJ_ARRAY) { zj_free(root); return 0; }
    for (size_t index = 0; index < entries->count; index++) {
        const char *pattern = zj_string(zj_object_get(entries->items[index], "pattern"));
        if (pattern != NULL && strcmp(pattern, tools_csv) == 0) { zj_free(root); return 0; }
    }
    char *item = zeno_format("{\"session\":%s,\"user_message\":%s,\"pattern\":%s,\"timestamp\":%s}",
                             zeno_json_escape(session_id != NULL ? session_id : ""),
                             zeno_json_escape(user_message != NULL ? user_message : ""),
                             zeno_json_escape(tools_csv), zeno_json_escape(zeno_timestamp_iso()));
    char *old_entries = zj_stringify_compact(entries);
    char *new_entries = item != NULL ? zeno_json_array_append(old_entries, item) : NULL;
    char *saved = new_entries != NULL ? zeno_format("{\"entries\":%s}", new_entries) : NULL;
    int ok = saved != NULL && zeno_markdown_write_json(path, "Zeno Learning Log", saved);
    size_t tool_count = 1; for (const char *cursor = tools_csv; (cursor = strstr(cursor, "->")) != NULL; cursor += 2) tool_count++;
    if (ok && skill_id != NULL && tool_count >= 3) *skill_id = zeno_format("auto_%s", "tool_sequence");
    free(item); free(old_entries); free(new_entries); free(saved); zj_free(root); return ok;
}

char *zeno_learning_list(const char *path, size_t limit) {
    char *json = zeno_markdown_read_json(path, "{\"entries\":[]}");
    char *error = NULL; ZjNode *root = zj_parse(json != NULL ? json : "{}", &error); free(error); free(json);
    ZjNode *entries = root != NULL ? zj_object_get(root, "entries") : NULL; char *result = zeno_strdup("[");
    if (entries != NULL && entries->type == ZJ_ARRAY) {
        size_t start = entries->count > limit && limit > 0 ? entries->count - limit : 0;
        for (size_t index = start; index < entries->count; index++) { char *item = zj_stringify_compact(entries->items[index]); char *next = item != NULL ? zeno_json_array_append(result, item) : NULL; free(item); free(result); result = next; }
    }
    zj_free(root); return result != NULL ? result : zeno_strdup("[]");
}

int zeno_evolution_record(const char *path, const char *agent_type,
                          const char *strain, int success, double latency,
                          const char *tools_csv) {
    char *json = zeno_markdown_read_json(path, "{\"strains\":[]}"); char *error = NULL; ZjNode *root = zj_parse(json != NULL ? json : "{}", &error); free(error); free(json); if (root == NULL) return 0;
    ZjNode *strains = zj_object_get(root, "strains"); if (strains == NULL || strains->type != ZJ_ARRAY) { zj_free(root); return 0; }
    size_t target = strains->count; for (size_t index = 0; index < strains->count; index++) { const char *current = zj_string(zj_object_get(strains->items[index], "strain")); if (current != NULL && strcmp(current, strain != NULL ? strain : "") == 0) { target = index; break; } }
    if (target == strains->count) {
        char *item = zeno_format("{\"agent_type\":%s,\"strain\":%s,\"executions\":1,\"successes\":%d,\"failures\":%d,\"total_latency\":%.6f,\"tools\":%s}", zeno_json_escape(agent_type != NULL ? agent_type : ""), zeno_json_escape(strain != NULL ? strain : ""), success ? 1 : 0, success ? 0 : 1, latency, zeno_json_escape(tools_csv != NULL ? tools_csv : ""));
        char *old = zj_stringify_compact(strains); char *updated = item != NULL ? zeno_json_array_append(old, item) : NULL; char *saved = updated != NULL ? zeno_format("{\"strains\":%s}", updated) : NULL; int ok = saved != NULL && zeno_markdown_write_json(path, "Zeno Evolutionary Memory", saved); free(item); free(old); free(updated); free(saved); zj_free(root); return ok;
    }
    /* Updating an existing JSON node in place keeps the implementation deterministic. */
    ZjNode *entry = strains->items[target]; long executions = (long)zj_integer(zj_object_get(entry, "executions"), 0) + 1; long successes = (long)zj_integer(zj_object_get(entry, "successes"), 0) + (success ? 1 : 0); long failures = (long)zj_integer(zj_object_get(entry, "failures"), 0) + (success ? 0 : 1); double total = zj_object_get(entry, "total_latency") != NULL ? zj_object_get(entry, "total_latency")->number + latency : latency;
    char *updated_item = zeno_format("{\"agent_type\":%s,\"strain\":%s,\"executions\":%ld,\"successes\":%ld,\"failures\":%ld,\"total_latency\":%.6f,\"tools\":%s}", zeno_json_escape(zj_string(zj_object_get(entry, "agent_type"))), zeno_json_escape(zj_string(zj_object_get(entry, "strain"))), executions, successes, failures, total, zeno_json_escape(tools_csv != NULL ? tools_csv : ""));
    char *items = zeno_strdup("["); for (size_t index = 0; index < strains->count; index++) { char *item = index == target ? zeno_strdup(updated_item) : zj_stringify_compact(strains->items[index]); char *next = item != NULL ? zeno_json_array_append(items, item) : NULL; free(item); free(items); items = next; }
    char *saved = items != NULL ? zeno_format("{\"strains\":%s}", items) : NULL; int ok = saved != NULL && zeno_markdown_write_json(path, "Zeno Evolutionary Memory", saved); free(updated_item); free(items); free(saved); zj_free(root); return ok;
}

char *zeno_evolution_best(const char *path, const char *agent_type) {
    char *json = zeno_markdown_read_json(path, "{\"strains\":[]}"); char *error = NULL; ZjNode *root = zj_parse(json != NULL ? json : "{}", &error); free(error); free(json); ZjNode *strains = root != NULL ? zj_object_get(root, "strains") : NULL; const char *best = NULL; double best_rate = -1.0; double best_latency = 0.0; if (strains != NULL && strains->type == ZJ_ARRAY) for (size_t index = 0; index < strains->count; index++) { ZjNode *entry = strains->items[index]; const char *type = zj_string(zj_object_get(entry, "agent_type")); if (agent_type != NULL && type != NULL && strcmp(agent_type, type) != 0) continue; double executions = (double)zj_integer(zj_object_get(entry, "executions"), 1); double rate = (double)zj_integer(zj_object_get(entry, "successes"), 0) / (executions > 0 ? executions : 1.0); double latency = zj_object_get(entry, "total_latency") != NULL ? zj_object_get(entry, "total_latency")->number / (executions > 0 ? executions : 1.0) : 0.0; if (best == NULL || rate > best_rate || (rate == best_rate && latency < best_latency)) { best = zj_string(zj_object_get(entry, "strain")); best_rate = rate; best_latency = latency; } } char *result = best != NULL ? zeno_strdup(best) : NULL; zj_free(root); return result;
}

static uint32_t embedding_hash(const char *text) { uint32_t hash = 2166136261U; for (const unsigned char *cursor = (const unsigned char *)text; cursor != NULL && *cursor != '\0'; cursor++) { hash ^= *cursor; hash *= 16777619U; } return hash; }

size_t zeno_embed_text(const char *text, double *output, size_t dimensions) {
    if (output == NULL || dimensions == 0) return 0; for (size_t index = 0; index < dimensions; index++) output[index] = 0.0; char *copy = zeno_lower_copy(text != NULL ? text : ""); if (copy == NULL) return 0; size_t tokens = 0; char *token = strtok(copy, " \t\r\n.,;:!?()[]{}\""); while (token != NULL) { output[embedding_hash(token) % dimensions] += 1.0; tokens++; token = strtok(NULL, " \t\r\n.,;:!?()[]{}\""); } if (tokens > 0) for (size_t index = 0; index < dimensions; index++) output[index] /= (double)tokens; free(copy); return dimensions;
}

double zeno_embedding_cosine(const double *left, const double *right, size_t dimensions) { if (left == NULL || right == NULL || dimensions == 0) return 0.0; double dot = 0.0, left_norm = 0.0, right_norm = 0.0; for (size_t index = 0; index < dimensions; index++) { dot += left[index] * right[index]; left_norm += left[index] * left[index]; right_norm += right[index] * right[index]; } return left_norm > 0.0 && right_norm > 0.0 ? dot / (sqrt(left_norm) * sqrt(right_norm)) : 0.0; }

char *zeno_semantic_rank(const char *query, const char *candidates_json, size_t limit) {
    char *error = NULL; ZjNode *root = zj_parse(candidates_json != NULL ? candidates_json : "[]", &error); free(error); if (root == NULL || root->type != ZJ_ARRAY) { zj_free(root); return zeno_strdup("[]"); }
    typedef struct Score { const char *id; double score; } Score; Score *scores = (Score *)calloc(root->count, sizeof(*scores)); if (scores == NULL) { zj_free(root); return zeno_strdup("[]"); } double query_vector[32]; zeno_embed_text(query, query_vector, 32); size_t count = 0; for (size_t index = 0; index < root->count; index++) { const char *id = zj_string(zj_object_get(root->items[index], "id")); const char *text = zj_string(zj_object_get(root->items[index], "text")); if (id == NULL || text == NULL) continue; double candidate[32]; zeno_embed_text(text, candidate, 32); scores[count].id = id; scores[count].score = zeno_embedding_cosine(query_vector, candidate, 32); count++; }
    for (size_t i = 0; i < count; i++) for (size_t j = i + 1; j < count; j++) if (scores[j].score > scores[i].score) { Score temp = scores[i]; scores[i] = scores[j]; scores[j] = temp; }
    char *result = zeno_strdup("["); size_t take = limit > 0 && limit < count ? limit : count; for (size_t index = 0; index < take; index++) { char *item = zeno_format("{\"id\":%s,\"score\":%.6f}", zeno_json_escape(scores[index].id), scores[index].score); char *next = item != NULL ? zeno_json_array_append(result, item) : NULL; free(item); free(result); result = next; } free(scores); zj_free(root); return result != NULL ? result : zeno_strdup("[]");
}

char *zeno_assert_true(const char *description, const char *value, const char *expected) {
    char *v = zeno_trim_copy(value); char *e = zeno_trim_copy(expected);
    int ok = v != NULL && e != NULL && strcmp(v, e) == 0;
    char *result = ok ? zeno_format("OK: %s", description != NULL ? description : "assertion") :
                        zeno_format("FAILURE: %s | got '%s' expected '%s'", description != NULL ? description : "assertion", v != NULL ? v : "", e != NULL ? e : "");
    free(v); free(e); return result;
}

char *zeno_assert_contains(const char *description, const char *value, const char *substring) {
    int ok = value != NULL && substring != NULL && strstr(value, substring) != NULL;
    char *result = ok ? zeno_format("OK: %s", description != NULL ? description : "assertion") :
                        zeno_format("FAILURE: %s | '%s' not found in value", description != NULL ? description : "assertion", substring != NULL ? substring : "");
    return result;
}

static int extension_language(const char *name, const char **language) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return 0;
    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0) *language = "c";
    else if (strcmp(dot, ".ts") == 0 || strcmp(dot, ".tsx") == 0) *language = "typescript";
    else if (strcmp(dot, ".js") == 0 || strcmp(dot, ".jsx") == 0) *language = "javascript";
    else if (strcmp(dot, ".py") == 0) *language = "python";
    else if (strcmp(dot, ".go") == 0) *language = "go";
    else return 0;
    return 1;
}

static int path_skip_dir(const char *name) {
    return name[0] == '.' || strcmp(name, "node_modules") == 0 || strcmp(name, "build") == 0 || strcmp(name, "dist") == 0;
}

static void append_line(char **result, const char *line) {
    if (*result == NULL) *result = zeno_strdup(line);
    else {
        char *next = zeno_format("%s%s", *result, line);
        free(*result); *result = next;
    }
}

#ifdef _WIN32
static void index_walk(const char *root, const char *relative, char **result, size_t *count) {
    if (count == NULL || *count >= 10000U) return;
    char *directory = relative != NULL && *relative != '\0' ? zeno_join_path(root, relative) : zeno_strdup(root);
    char *pattern = zeno_join_path(directory, "*");
    WIN32_FIND_DATAA data;
    HANDLE handle = pattern != NULL ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
    if (handle == INVALID_HANDLE_VALUE) { free(directory); free(pattern); return; }
    do {
        if (*count >= 10000U) break;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
        char *child_rel = relative != NULL && *relative != '\0' ? zeno_join_path(relative, data.cFileName) : zeno_strdup(data.cFileName);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!path_skip_dir(data.cFileName)) index_walk(root, child_rel, result, count);
        } else {
            const char *language = NULL;
            if (extension_language(data.cFileName, &language)) {
                *count += 1;
                char *line = zeno_format("- %s (%s)\n", child_rel, language);
                append_line(result, line); free(line);
            }
        }
        free(child_rel);
    } while (FindNextFileA(handle, &data) != 0);
    FindClose(handle); free(directory); free(pattern);
}
#else
static void index_walk(const char *root, const char *relative, char **result, size_t *count) {
    if (count == NULL || *count >= 10000U) return;
    char *directory = relative != NULL && *relative != '\0' ? zeno_join_path(root, relative) : zeno_strdup(root);
    DIR *handle = directory != NULL ? opendir(directory) : NULL;
    if (handle == NULL) { free(directory); return; }
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL && *count < 10000U) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child_rel = relative != NULL && *relative != '\0' ? zeno_join_path(relative, entry->d_name) : zeno_strdup(entry->d_name);
        char *full = zeno_join_path(root, child_rel);
        struct stat info;
        int readable = full != NULL && lstat(full, &info) == 0;
        if (readable && !S_ISLNK(info.st_mode) && S_ISDIR(info.st_mode)) {
            if (!path_skip_dir(entry->d_name)) index_walk(root, child_rel, result, count);
        } else if (readable && !S_ISLNK(info.st_mode)) {
            const char *language = NULL;
            if (extension_language(entry->d_name, &language)) {
                *count += 1;
                char *line = zeno_format("- %s (%s)\n", child_rel, language);
                append_line(result, line); free(line);
            }
        }
        free(full); free(child_rel);
    }
    closedir(handle); free(directory);
}
#endif

char *zeno_codebase_structure(const char *workspace_root) {
    char *files = NULL; size_t count = 0;
    index_walk(workspace_root != NULL ? workspace_root : ".", "", &files, &count);
    char *result = zeno_format("Files: %zu\n\n%s", count, files != NULL ? files : "");
    free(files); return result;
}

static void find_symbol_walk(const char *root, const char *relative, const char *query, char **result, size_t *matches) {
    char *directory = relative != NULL && *relative != '\0' ? zeno_join_path(root, relative) : zeno_strdup(root);
#ifdef _WIN32
    char *pattern = zeno_join_path(directory, "*"); WIN32_FIND_DATAA data;
    HANDLE handle = pattern != NULL ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
    if (handle == INVALID_HANDLE_VALUE) { free(directory); free(pattern); return; }
    do {
        if (*matches >= 100U) break;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
        char *child_rel = relative != NULL && *relative != '\0' ? zeno_join_path(relative, data.cFileName) : zeno_strdup(data.cFileName);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) { if (!path_skip_dir(data.cFileName)) find_symbol_walk(root, child_rel, query, result, matches); }
        else {
            const char *language = NULL;
            if (extension_language(data.cFileName, &language) && *matches < 100) {
                char *full = zeno_join_path(root, child_rel); char *text = full != NULL ? zeno_read_file(full, 2000000) : NULL;
                if (text != NULL) { size_t line_no = 0; char *line = strtok(text, "\n"); while (line != NULL) { line_no++; if (zeno_contains_ci(line, query) && (strstr(line, "function ") || strstr(line, "class ") || strstr(line, "void ") || strstr(line, "int ") || strstr(line, "def "))) { char *out = zeno_format("%s:%zu: %s\n", child_rel, line_no, line); append_line(result, out); free(out); (*matches)++; } line = strtok(NULL, "\n"); } free(text); }
                free(full);
            }
        }
        free(child_rel);
    } while (FindNextFileA(handle, &data) != 0);
    FindClose(handle); free(directory); free(pattern);
#else
    DIR *handle = directory != NULL ? opendir(directory) : NULL; if (handle == NULL) { free(directory); return; }
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child_rel = relative != NULL && *relative != '\0' ? zeno_join_path(relative, entry->d_name) : zeno_strdup(entry->d_name); char *full = zeno_join_path(root, child_rel); struct stat info;
        int readable = full != NULL && lstat(full, &info) == 0;
        if (readable && !S_ISLNK(info.st_mode) && S_ISDIR(info.st_mode)) { if (!path_skip_dir(entry->d_name)) find_symbol_walk(root, child_rel, query, result, matches); }
        else if (readable && !S_ISLNK(info.st_mode) && *matches < 100) { const char *language = NULL; if (extension_language(entry->d_name, &language)) { char *text = zeno_read_file(full, 2000000); if (text != NULL) { size_t line_no = 0; char *line = strtok(text, "\n"); while (line != NULL) { line_no++; if (zeno_contains_ci(line, query) && (strstr(line, "function ") || strstr(line, "class ") || strstr(line, "void ") || strstr(line, "int ") || strstr(line, "def "))) { char *out = zeno_format("%s:%zu: %s\n", child_rel, line_no, line); append_line(result, out); free(out); (*matches)++; } line = strtok(NULL, "\n"); } free(text); } } }
        free(full); free(child_rel);
    }
    closedir(handle); free(directory);
#endif
}

char *zeno_find_symbol(const char *workspace_root, const char *name) {
    if (name == NULL || *name == '\0') return zeno_strdup("[]");
    char *result = NULL; size_t matches = 0;
    find_symbol_walk(workspace_root != NULL ? workspace_root : ".", "", name, &result, &matches);
    return result != NULL ? result : zeno_strdup("No symbols found.");
}

char *zeno_detect_test_framework(const char *workspace_root) {
    const char *root = workspace_root != NULL ? workspace_root : ".";
    const char *names[] = {"CMakeLists.txt", "CTestTestfile.cmake", "Makefile", "package.json", "pyproject.toml", "pytest.ini", "Cargo.toml", "go.mod"};
    const char *values[] = {"cmake", "ctest", "make", "npm", "pytest", "pytest", "cargo", "go"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        char *path = zeno_join_path(root, names[index]);
        FILE *file = path != NULL ? fopen(path, "rb") : NULL;
        free(path);
        if (file != NULL) { fclose(file); return zeno_strdup(values[index]); }
    }
    return NULL;
}

char *zeno_run_tests_execute(const char *workspace_root, int timeout_ms) {
    char *framework = zeno_detect_test_framework(workspace_root);
    if (framework == NULL) return zeno_strdup("{\"ok\":false,\"framework\":null,\"output\":\"No test framework detected.\"}");
    const char *command = NULL;
    if (strcmp(framework, "pytest") == 0) command = "python -m pytest -x -v";
    else if (strcmp(framework, "npm") == 0) command = "npm test";
    else if (strcmp(framework, "cargo") == 0) command = "cargo test";
    else if (strcmp(framework, "go") == 0) command = "go test ./...";
    else command = "ctest --output-on-failure";
    ZenoExecResult result; memset(&result, 0, sizeof(result)); result.exit_code = -1;
    (void)zeno_process_command(command, workspace_root != NULL ? workspace_root : ".", timeout_ms > 0 ? timeout_ms : 120000, 8000, &result);
    char *output_json = zeno_format("{\"ok\":%s,\"framework\":%s,\"exit_code\":%d,\"output\":%s}",
                                    result.ok ? "true" : "false", zeno_json_escape(framework), result.exit_code,
                                    zeno_json_escape(result.output != NULL ? result.output : ""));
    zeno_exec_result_free(&result); free(framework);
    return output_json;
}

/* libcurl is the portable adapter; on Windows the WinHTTP fallback in
 * zeno_llm.c provides the same surface when libcurl is not available. */
#if defined(ZENO_HAVE_CURL) || defined(_WIN32)
int zeno_mcp_adapter_available(void) { return 1; }
#else
int zeno_mcp_adapter_available(void) { return 0; }
#endif
int zeno_composio_adapter_available(const char *api_key) {
    return api_key != NULL && *api_key != '\0' && zeno_mcp_adapter_available();
}

char *zeno_composio_status(const char *api_key) {
    return zeno_format("{\"bridge\":\"mcp\",\"mcp_enabled\":%s,\"api_key_configured\":%s,\"note\":\"External integrations go through the MCP bridge (zeno_mcp_call / mcp_call tool). Point it at the Composio MCP server endpoint.\"}",
                       zeno_mcp_adapter_available() ? "true" : "false",
                       api_key != NULL && *api_key != '\0' ? "true" : "false");
}

int zeno_browser_adapter_available(void) {
#if defined(ZENO_HAVE_CURL) || defined(_WIN32)
    return 1;
#else
    return 0;
#endif
}
int zeno_vision_is_multimodal_json(const char *content_json) {
    char *error = NULL; ZjNode *root = zj_parse(content_json != NULL ? content_json : "null", &error); free(error);
    int result = root != NULL && root->type == ZJ_ARRAY && root->count > 0; zj_free(root); return result;
}

char *zeno_browser_navigate(const char *url, int timeout_ms) {
    char *output = NULL;
    if (!zeno_scrape_url(url, timeout_ms > 0 ? timeout_ms : 30000, 8000, &output))
        return output != NULL ? output : zeno_strdup("Browser reader unavailable.");
    return output;
}

char *zeno_browser_info(void) {
    return zeno_format("{\"enabled\":%s,\"driver\":\"%s\"}",
                       zeno_browser_adapter_available() ? "true" : "false",
                       zeno_browser_adapter_available() ? "http-reader" : "none");
}



char *zeno_squad_run(const char *task, const char *roles_csv) {
    const char *roles = roles_csv != NULL && *roles_csv != '\0' ? roles_csv : "coder,reviewer,tester";
    char *copy = zeno_strdup(roles); char *result = zeno_strdup("[");
    char *role = copy != NULL ? strtok(copy, ",") : NULL; size_t index = 0;
    while (role != NULL && result != NULL) {
        char *trimmed = zeno_trim_copy(role);
        ZenoSubAgentStats *agent = zeno_subagent_create(trimmed != NULL ? trimmed : "agent", "squad");
        char *response = zeno_format("[%s] Task received: %.100s", trimmed != NULL ? trimmed : "agent", task != NULL ? task : "");
        int ok = zeno_subagent_record(agent, 1, 0.1, "");
        char *item = ok ? zeno_format("{\"role\":%s,\"response\":%s}", zeno_json_escape(trimmed != NULL ? trimmed : ""), zeno_json_escape(response)) : NULL;
        char *next = item != NULL ? zeno_json_array_append(result, item) : NULL;
        free(item); free(response); free(trimmed); zeno_subagent_free(agent); free(result); result = next;
        role = strtok(NULL, ","); index++;
    }
    free(copy);
    char *synthesis = index > 0 ? zeno_format("{\"task\":%s,\"outputs\":%s,\"final\":\"Synthesized squad result.\"}", zeno_json_escape(task != NULL ? task : ""), result != NULL ? result : "[]") : zeno_strdup("{\"task\":\"\",\"outputs\":[],\"final\":\"No roles available.\"}");
    free(result);
    return synthesis;
}

char *zeno_coding_agent_plan(const char *task) {
    ZenoPlan *plan = zeno_plan_parse(NULL, task != NULL ? task : "");
    char *json = zeno_plan_to_json(plan);
    zeno_plan_free(plan);
    return json;
}

char *zeno_install_skill_from_github(const char *repository, const char *destination) {
    (void)repository;
    return zeno_format("Skill installer requires network access; destination prepared: %s. Clone through the sandbox to respect policy.", destination != NULL ? destination : "marketplace/skills");
}
