#include "zeno_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define TEST_RMDIR _rmdir
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define TEST_RMDIR rmdir
#endif

static int failures = 0;
static unsigned long test_counter = 0;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static char *test_root(void) {
#ifdef _WIN32
    unsigned long process_id = (unsigned long)GetCurrentProcessId();
#else
    unsigned long process_id = (unsigned long)getpid();
#endif
    char *root = zeno_format(".zeno_test_tmp_%lu_%lu_%lld", process_id, ++test_counter, zeno_now_ms());
    if (root == NULL || !zeno_mkdirs(root)) { free(root); return NULL; }
    return root;
}

static void remove_tree(const char *path) {
    if (path == NULL) return;
#ifdef _WIN32
    char *pattern = zeno_join_path(path, "*");
    WIN32_FIND_DATAA data;
    HANDLE handle = pattern != NULL ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
            char *child = zeno_join_path(path, data.cFileName);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) remove_tree(child);
            else (void)DeleteFileA(child);
            free(child);
        } while (FindNextFileA(handle, &data) != 0);
        FindClose(handle);
    }
    free(pattern);
    (void)RemoveDirectoryA(path);
#else
    DIR *directory = opendir(path);
    if (directory != NULL) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char *child = zeno_join_path(path, entry->d_name);
            struct stat info;
            if (child != NULL && lstat(child, &info) == 0 && S_ISDIR(info.st_mode)) remove_tree(child);
            else if (child != NULL) (void)remove(child);
            free(child);
        }
        closedir(directory);
    }
    (void)TEST_RMDIR(path);
#endif
}

static void cleanup_root(const char *root) {
    remove_tree(root);
}

static int custom_tool(void *context, const char *args, char **output, char **error) {
    int *calls = (int *)context;
    (*calls)++;
    (void)args;
    (void)error;
    *output = zeno_strdup("custom-ok");
    return 1;
}

typedef struct ModelFixture { int calls; } ModelFixture;

static int model_transport(void *context, const char *base_url, const char *api_key,
                           const char *body, int timeout_ms, char **response,
                           char **error) {
    ModelFixture *fixture = (ModelFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (fixture->calls == 1) {
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":\"custom\",\"arguments\":{}}}]}}]}");
    } else {
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"final answer\"}}]}");
    }
    return 1;
}

static int deny_approval(void *context, const char *request_id, const char *tool_name,
                         const char *args, const char *reason) {
    (void)context; (void)request_id; (void)tool_name; (void)args; (void)reason;
    return 0;
}

static int allow_approval(void *context, const char *request_id, const char *tool_name,
                          const char *args, const char *reason) {
    int *calls = (int *)context;
    (void)request_id; (void)tool_name; (void)args; (void)reason;
    if (calls != NULL) (*calls)++;
    return 1;
}

static int approval_transport(void *context, const char *base_url, const char *api_key,
                              const char *body, int timeout_ms, char **response,
                              char **error) {
    ModelFixture *fixture = (ModelFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    *response = fixture->calls == 1
        ? zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"danger-1\",\"function\":{\"name\":\"danger\",\"arguments\":{}}}]}}]}")
        : zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"should not execute\"}}]}");
    return 1;
}

static int text_transport(void *context, const char *base_url, const char *api_key,
                          const char *body, int timeout_ms, char **response,
                          char **error) {
    ModelFixture *fixture = (ModelFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"hello stream\"}}]}");
    return 1;
}

static int plan_transport(void *context, const char *base_url, const char *api_key,
                          const char *body, int timeout_ms, char **response,
                          char **error) {
    ModelFixture *fixture = (ModelFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"{\\\"objective\\\":\\\"build\\\",\\\"steps\\\":[{\\\"id\\\":1,\\\"description\\\":\\\"step one\\\",\\\"files_affected\\\":[\\\"a.c\\\"],\\\"verification\\\":\\\"run tests\\\"}],\\\"risks\\\":[]}\"}}]}");
    return 1;
}

typedef struct ChunkCollect { char *text; } ChunkCollect;

static void chunk_collect(void *context, const char *chunk) {
    ChunkCollect *collect = (ChunkCollect *)context;
    if (collect->text == NULL) collect->text = zeno_strdup(chunk);
    else { char *next = zeno_format("%s%s", collect->text, chunk); free(collect->text); collect->text = next; }
}

static void test_json_and_helpers(void) {
    CHECK(strcmp(zeno_sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    char *escaped = zeno_json_escape("a\n\"b");
    CHECK(escaped != NULL && strcmp(escaped, "\"a\\n\\\"b\"") == 0);
    free(escaped);
    char nested_json[300];
    size_t nested_length = 0;
    for (size_t depth = 0; depth < 130U; depth++) nested_json[nested_length++] = '[';
    nested_json[nested_length++] = '0';
    for (size_t depth = 0; depth < 130U; depth++) nested_json[nested_length++] = ']';
    nested_json[nested_length] = '\0';
    char *nested_error = NULL;
    ZjNode *too_deep = zj_parse(nested_json, &nested_error);
    CHECK(too_deep == NULL && nested_error != NULL);
    zj_free(too_deep); free(nested_error);
    ZenoConfig config;
    zeno_config_default(&config);
    CHECK(config.sandbox_allow_shell_operators == 0);
    zeno_copy_string(config.openai_base_url, sizeof(config.openai_base_url), "https://provider.test/\\\"quoted");
    zeno_copy_string(config.ollama_model, sizeof(config.ollama_model), "model\\\\name");
    char *public_config = zeno_config_public_json(&config);
    char *config_error = NULL;
    ZjNode *config_node = zj_parse(public_config != NULL ? public_config : "", &config_error);
    CHECK(config_node != NULL && zj_string(zj_object_get(config_node, "OPENAI_BASE_URL")) != NULL);
    free(config_error);
    zj_free(config_node);
    free(public_config);
    char *summary = zeno_build_caveman_summary("  one   two  ", "completed", "a.c, b.h", 2, 12.4);
    CHECK(summary != NULL && strstr(summary, "SUMMARY task=one two") != NULL);
    free(summary);
    char *completion = NULL; char *calls = NULL;
    CHECK(zeno_parse_completion("{\"choices\":[{\"message\":{\"content\":\"ok\",\"tool_calls\":[{\"id\":\"1\",\"function\":{\"name\":\"x\",\"arguments\":{}}}]}}]}", &completion, &calls));
    CHECK(completion != NULL && strcmp(completion, "ok") == 0);
    CHECK(calls != NULL && strstr(calls, "\"name\":\"x\"") != NULL);
    free(completion); free(calls);
}

static void test_persistence_and_memory(const char *root) {
    char *path = zeno_join_path(root, "data.md");
    CHECK(zeno_markdown_write_json(path, "Test", "{\"a\":[1,2],\"ok\":true}"));
    char *json = zeno_markdown_read_json(path, "{}");
    CHECK(json != NULL && strstr(json, "\"a\"") != NULL);
    free(json); free(path);
    char *memory_path = zeno_join_path(root, "memory.md");
    ZenoMemory *memory = zeno_memory_create(memory_path);
    CHECK(memory != NULL);
    CHECK(zeno_memory_add_turn(memory, "s1", "user", "build the C agent"));
    CHECK(zeno_memory_add_turn(memory, "s1", "assistant", "implemented and tested"));
    char *turns = zeno_memory_search_conversations(memory, "agent", "s1", 5);
    CHECK(turns != NULL && strstr(turns, "build the C agent") != NULL);
    free(turns);
    char *id = NULL;
    CHECK(zeno_memory_add_note(memory, "Important", "Use CMake for reproducible builds", "note", "global", "[\"build\"]", &id));
    CHECK(id != NULL);
    free(id);
    CHECK(zeno_memory_set_preference(memory, "language", "pt", "global"));
    char *preference = zeno_memory_get_preference(memory, "language");
    CHECK(preference != NULL && strcmp(preference, "pt") == 0);
    free(preference);
    char *prompt = zeno_memory_context_prompt(memory, "s1", "u1", "build", 10, 2000);
    CHECK(prompt != NULL && strstr(prompt, "MEMORY") != NULL);
    free(prompt); zeno_memory_destroy(memory);
    memory = zeno_memory_create(memory_path);
    CHECK(memory != NULL);
    char *notes = zeno_memory_search_notes(memory, "Important", 5);
    CHECK(notes != NULL && strstr(notes, "Important") != NULL);
    free(notes); zeno_memory_destroy(memory); free(memory_path);
}

static void test_cache_trace_sandbox(const char *root) {
    char *cache_path = zeno_join_path(root, "cache.md");
    ZenoCache *cache = zeno_cache_create(cache_path, 3600, 4);
    CHECK(zeno_cache_set(cache, "key", "value"));
    char *value = NULL; CHECK(zeno_cache_get(cache, "key", &value)); CHECK(value != NULL && strcmp(value, "value") == 0); free(value);
    size_t hits = 0, misses = 0, size = 0; zeno_cache_stats(cache, &hits, &misses, &size); CHECK(hits == 1 && size == 1); zeno_cache_destroy(cache); free(cache_path);
    char *trace_path = zeno_join_path(root, "trace.jsonl"); ZenoTrace *trace = zeno_trace_create(trace_path); char *span = zeno_trace_start(trace, "run", "run", "t1", NULL, "{}"); zeno_trace_end(trace, span, "ok", "{\"turns\":1}"); char *traces = zeno_trace_get_json(trace, 10); CHECK(traces != NULL && strstr(traces, "run") != NULL); free(traces); free(span); zeno_trace_destroy(trace); char *trace_file = zeno_read_file(trace_path, 0); CHECK(trace_file != NULL && strstr(trace_file, "span_id") != NULL); free(trace_file); free(trace_path);
    ZenoSandboxPolicy policy = {root, 0, "", 1, 2000, 4096, 1000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy); CHECK(sandbox != NULL); char reason[256]; CHECK(zeno_sandbox_validate(sandbox, "echo safe", root, reason, sizeof(reason))); CHECK(!zeno_sandbox_validate(sandbox, "rm -rf /", root, reason, sizeof(reason))); CHECK(!zeno_sandbox_validate(sandbox, "curl https://example.com", root, reason, sizeof(reason))); CHECK(!zeno_sandbox_validate(sandbox, "echo safe", "..", reason, sizeof(reason))); ZenoExecResult execution = zeno_sandbox_execute(sandbox, "echo zeno", root, 8000); CHECK(execution.ok && execution.output != NULL && strstr(execution.output, "zeno") != NULL); zeno_exec_result_free(&execution); zeno_sandbox_destroy(sandbox);
}

static void test_registry_and_agent(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy); ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create(); CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root)); CHECK(zeno_registry_has(registry, "read_text_file")); CHECK(zeno_registry_is_read_only(registry, "read_text_file")); ZenoToolResult missing = zeno_registry_execute(registry, "read_text_file", "{}"); CHECK(!missing.ok && missing.error != NULL); zeno_tool_result_free(&missing); { ZenoToolResult unknown_tool = zeno_registry_execute(registry, "definitely_not_a_tool", "{}"); CHECK(!unknown_tool.ok && unknown_tool.error_code == ZENO_TOOL_ERROR_NOT_FOUND); zeno_tool_result_free(&unknown_tool); } CHECK(zeno_tool_error_code(NULL) == ZENO_TOOL_OK && zeno_tool_error_code("some_future_error") == ZENO_TOOL_ERROR_GENERIC); ZenoToolResult assertion = zeno_registry_execute(registry, "assert_contains", "{\"condition_description\":\"text\",\"value\":\"hello\",\"substring\":\"ell\"}"); CHECK(assertion.ok && strstr(assertion.output, "OK") != NULL); zeno_tool_result_free(&assertion);
    ZenoToolResult bad_type = zeno_registry_execute(registry, "write_text_file", "{\"relative_path\":42,\"content\":\"x\"}");
    CHECK(!bad_type.ok && bad_type.error != NULL && strstr(bad_type.output, "must be a string") != NULL && bad_type.error_code == ZENO_TOOL_ERROR_INVALID_ARGUMENTS);
    zeno_tool_result_free(&bad_type);
    int custom_calls = 0; ZenoToolDefinition custom = {"custom", "Custom deterministic tool", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0}; CHECK(zeno_registry_register(registry, custom, custom_tool, &custom_calls));
    ZenoToolDefinition invalid_schema = {"invalid_schema", "Invalid schema", "{\"type\":\"object\",\"properties\":{\"value\":{\"type\":\"unsupported\"}}}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    CHECK(!zeno_registry_register(registry, invalid_schema, custom_tool, &custom_calls));
    ZenoToolResult write = zeno_registry_execute(registry, "write_text_file", "{\"relative_path\":\"nested/value.txt\",\"content\":\"one\"}"); CHECK(write.ok); zeno_tool_result_free(&write);
    ZenoToolResult pre_append_read = zeno_registry_execute(registry, "read_text_file", "{\"relative_path\":\"nested/value.txt\"}"); CHECK(pre_append_read.ok); zeno_tool_result_free(&pre_append_read); /* read-before-edit ledger */
    ZenoToolResult append = zeno_registry_execute(registry, "append_text_file", "{\"relative_path\":\"nested/value.txt\",\"content\":\" two\"}"); CHECK(append.ok); zeno_tool_result_free(&append);
    ZenoToolResult read = zeno_registry_execute(registry, "read_text_file", "{\"relative_path\":\"nested/value.txt\"}"); CHECK(read.ok && strstr(read.output, "one two") != NULL); zeno_tool_result_free(&read);
    ZenoToolResult replace = zeno_registry_execute(registry, "replace_in_file", "{\"relative_path\":\"nested/value.txt\",\"old_text\":\"two\",\"new_text\":\"three\"}"); CHECK(replace.ok); zeno_tool_result_free(&replace);
    ZenoToolResult search = zeno_registry_execute(registry, "search_workspace", "{\"query\":\"three\"}"); CHECK(search.ok && strstr(search.output, "value.txt") != NULL); zeno_tool_result_free(&search);
    ZenoToolResult glob = zeno_registry_execute(registry, "glob_workspace", "{\"pattern\":\"*.txt\"}"); CHECK(glob.ok && strstr(glob.output, "value.txt") != NULL); zeno_tool_result_free(&glob);
    char *escape_write = zeno_workspace_write(root, "../outside.txt", "must not escape");
    char *escape_search = zeno_workspace_search(root, "", "..", 10);
    char *escape_glob = zeno_workspace_glob(root, "*", "..", 10);
    CHECK(escape_write != NULL && strstr(escape_write, "escapes") != NULL);
    CHECK(escape_search != NULL && strstr(escape_search, "escapes") != NULL);
    CHECK(escape_glob != NULL && strstr(escape_glob, "escapes") != NULL);
    free(escape_write); free(escape_search); free(escape_glob);
    ModelFixture fixture = {0}; ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000}; CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, model_transport, &fixture);
    ZenoToolDefinition danger = {"danger", "Approval gated tool", "{\"type\":\"object\"}", ZENO_EFFECT_PROCESS, 1, 1000, 0}; CHECK(zeno_registry_register(registry, danger, custom_tool, &custom_calls));
    char *runs_path = zeno_join_path(root, "runs"); ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 1, 0, 4, 120000, 20000}; ZenoAgent *agent = zeno_agent_create(&agent_options); ZenoRunOptions run_options = {"test-model", 4, 1000, 0.2, 0, 1, 0, NULL, deny_approval, NULL, NULL, NULL, NULL}; ZenoAgentResult result = zeno_agent_run(agent, "s1", "perform custom", &run_options); CHECK(result.approval_required == 0 && strcmp(result.status, "completed") == 0); CHECK(custom_calls == 1 && fixture.calls >= 2); zeno_agent_result_free(&result);
     ZenoToolDefinition gated_read = {"gated_read", "Approval-gated external read", "{\"type\":\"object\"}", ZENO_EFFECT_READ_EXTERNAL, 1, 1000, 0}; CHECK(zeno_registry_register(registry, gated_read, custom_tool, &custom_calls));
     int approval_calls = 0; ZenoRunOptions gated_options = run_options; gated_options.require_approval = 0; gated_options.approve = allow_approval; gated_options.approve_context = &approval_calls;
     char *gated_logs = NULL; int gated_success = 0; int gated_failed = 0; int gated_waiting = 0;
     CHECK(zeno_execute_tool_calls(agent, "s1", "gated-run", "[{\"name\":\"gated_read\",\"arguments\":{}}]", &gated_options, &gated_logs, &gated_success, &gated_failed, &gated_waiting));
     CHECK(approval_calls == 1 && gated_success == 1 && gated_failed == 0 && gated_waiting == 0);
     free(gated_logs);
    ModelFixture approval_fixture = {0}; zeno_router_set_transport(router, approval_transport, &approval_fixture); ZenoAgentResult approval_result = zeno_agent_run(agent, "s2", "perform dangerous action", &run_options); char *approval_context = zeno_approval_get(approval, approval_result.run_id); char *checkpoint = zeno_format("%s/%s.json", runs_path, approval_result.run_id); CHECK(approval_result.approval_required && strcmp(approval_result.status, "waiting_approval") == 0); CHECK(approval_context != NULL); CHECK(custom_calls == 2); free(approval_context); if (checkpoint != NULL) { (void)remove(checkpoint); free(checkpoint); } zeno_agent_result_free(&approval_result);
    CHECK(zeno_agent_can_parallelize(registry, "[{\"name\":\"read_text_file\",\"arguments\":{\"relative_path\":\"a\"}},{\"name\":\"read_text_file\",\"arguments\":{\"relative_path\":\"b\"}}]")); CHECK(!zeno_agent_can_parallelize(registry, "[{\"name\":\"write_text_file\",\"arguments\":{\"relative_path\":\"a\",\"content\":\"1\"}},{\"name\":\"write_text_file\",\"arguments\":{\"relative_path\":\"a\",\"content\":\"2\"}}]"));
    ModelFixture stream_fixture = {0}; zeno_router_set_transport(router, text_transport, &stream_fixture); ChunkCollect collect = {0}; ZenoRunOptions stream_options = {"test-model", 4, 1000, 0.2, 0, 1, 1, NULL, NULL, NULL, NULL, chunk_collect, &collect}; ZenoAgentResult stream_result = zeno_agent_run(agent, "s3", "stream me", &stream_options); CHECK(strcmp(stream_result.status, "completed") == 0); CHECK(collect.text != NULL && strstr(collect.text, "hello stream") != NULL); free(collect.text); zeno_agent_result_free(&stream_result);
    ModelFixture plan_fixture = {0}; zeno_router_set_transport(router, plan_transport, &plan_fixture); char *plan_json = NULL; char *plan_result = zeno_agent_run_with_plan(agent, "s4", "build the module", &run_options, &plan_json); CHECK(plan_json != NULL && strstr(plan_json, "step one") != NULL); CHECK(plan_result != NULL && strstr(plan_result, "\"success\":true") != NULL); free(plan_json); free(plan_result);
    CHECK(zeno_memory_update_user_profile(memory, "u1", "seja mais direto e valide com teste", "ok", "[{\"tool\":\"read_text_file\"}]")); char *profile = zeno_memory_get_or_create_user(memory, "u1"); CHECK(profile != NULL && strstr(profile, "u1") != NULL); free(profile); char *profile_context = zeno_memory_user_profile_context(memory, "u1"); CHECK(profile_context != NULL && strstr(profile_context, "USER PROFILE") != NULL); free(profile_context);
    zeno_memory_flush(memory);
     ZenoMemory *reloaded_memory = zeno_memory_create(memory_path);
     char *reloaded_context = reloaded_memory != NULL ? zeno_memory_user_profile_context(reloaded_memory, "u1") : NULL;
     CHECK(reloaded_memory != NULL && reloaded_context != NULL && strstr(reloaded_context, "read_text_file") != NULL);
     free(reloaded_context);
     zeno_memory_destroy(reloaded_memory);
     char *tests_json = zeno_run_tests_execute(root, 1000); CHECK(tests_json != NULL && strstr(tests_json, "framework") != NULL); free(tests_json);
    zeno_agent_destroy(agent); free(runs_path); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

static void test_jobs_persistence(const char *root) {
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0};
    ZenoSandbox *first = zeno_sandbox_create(&policy); CHECK(first != NULL);
    char *started = zeno_sandbox_start_job(first, "cmd /c ping -n 2 127.0.0.1 > nul", "slow-job", root);
    CHECK(started != NULL && strstr(started, "Job started") != NULL); free(started);
    zeno_sandbox_destroy(first);
    ZenoSandbox *second = zeno_sandbox_create(&policy); CHECK(second != NULL);
    char *listed = zeno_sandbox_list_jobs(second, 1);
    CHECK(listed != NULL && strstr(listed, "slow-job") != NULL && (strstr(listed, "killed") != NULL || strstr(listed, "failed") != NULL)); free(listed);
    char *jobs_index = zeno_join_path(root, ".Zeno_sandbox/jobs/index.md");
    char *index = jobs_index != NULL ? zeno_read_file(jobs_index, 0) : NULL;
    CHECK(index != NULL && strstr(index, "slow-job") != NULL); free(index); free(jobs_index);
    zeno_sandbox_destroy(second);
}

/* --- Parallelism, squad and modes --- */

typedef struct SyncFixture { ZenoMutex lock; int calls; int active; int max_active; } SyncFixture;
typedef struct SlowFixture { ZenoMutex lock; int active; int max_active; } SlowFixture;

static void sync_fixture_init(SyncFixture *fixture) { memset(fixture, 0, sizeof(*fixture)); }
static void slow_fixture_init(SlowFixture *fixture) { memset(fixture, 0, sizeof(*fixture)); }

static int parallel_transport(void *context, const char *base_url, const char *api_key,
                              const char *body, int timeout_ms, char **response,
                              char **error) {
    SyncFixture *fixture = (SyncFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    zeno_mutex_lock(&fixture->lock);
    fixture->calls++;
    fixture->active++;
    if (fixture->active > fixture->max_active) fixture->max_active = fixture->active;
    zeno_mutex_unlock(&fixture->lock);
    zeno_sleep_ms(180);
    zeno_mutex_lock(&fixture->lock);
    fixture->active--;
    zeno_mutex_unlock(&fixture->lock);
    *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"parallel contribution\"}}]}");
    return 1;
}

static int slow_tool(void *context, const char *args, char **output, char **error) {
    SlowFixture *fixture = (SlowFixture *)context;
    (void)args; (void)error;
    zeno_mutex_lock(&fixture->lock);
    fixture->active++;
    if (fixture->active > fixture->max_active) fixture->max_active = fixture->active;
    zeno_mutex_unlock(&fixture->lock);
    zeno_sleep_ms(250);
    zeno_mutex_lock(&fixture->lock);
    fixture->active--;
    zeno_mutex_unlock(&fixture->lock);
    *output = zeno_strdup("slow-ok");
    return 1;
}

static void test_parallel_tool_calls(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    SlowFixture slow_fixture; slow_fixture_init(&slow_fixture);
    ZenoToolDefinition slow = {"slowread", "Deterministic slow read tool", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    CHECK(zeno_registry_register(registry, slow, slow_tool, &slow_fixture));
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, NULL, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 1, 0, 4, 120000, 20000, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 4, 1000, 0.2, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    char *logs = NULL; int successful = 0; int failed = 0; int waiting = 0;
    CHECK(zeno_execute_tool_calls(agent, "parallel-tools", "run_parallel_tools",
        "[{\"id\":\"p1\",\"name\":\"slowread\",\"arguments\":{}},{\"id\":\"p2\",\"name\":\"slowread\",\"arguments\":{}}]",
        &run_options, &logs, &successful, &failed, &waiting));
    CHECK(successful == 2 && failed == 0 && waiting == 0);
    CHECK(logs != NULL && strstr(logs, "slow-ok") != NULL);
    CHECK(slow_fixture.max_active >= 2);
    free(logs); free(runs_path);
    zeno_agent_destroy(agent); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

static void test_parallel_agents_and_squad(const char *root) {
    char *memory_path = zeno_join_path(root, "squad-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    SyncFixture fixture; sync_fixture_init(&fixture);
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, parallel_transport, &fixture);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 1, 0, 4, 120000, 20000, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 4, 1000, 0.2, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};

    char *parallel = zeno_agent_run_parallel(agent, "parallel-session",
        "[\"first task\",\"second task\",\"third task\"]", &run_options);
    CHECK(parallel != NULL);
    CHECK(strstr(parallel, "\"status\":\"completed\"") != NULL);
    CHECK(fixture.max_active >= 2); /* transports really overlapped */
    int completed_count = 0;
    for (const char *cursor = parallel; (cursor = strstr(cursor, "\"status\":\"completed\"")) != NULL; cursor++) completed_count++;
    CHECK(completed_count == 3);
    free(parallel);

    fixture.max_active = 0; fixture.calls = 0;
    char *squad = zeno_agent_run_squad(agent, "squad-session", "ship the feature",
                                       "coder,reviewer,tester", &run_options);
    CHECK(squad != NULL);
    CHECK(strstr(squad, "\"role\":\"coder\"") != NULL);
    CHECK(strstr(squad, "\"role\":\"reviewer\"") != NULL);
    CHECK(strstr(squad, "\"role\":\"tester\"") != NULL);
    CHECK(strstr(squad, "\"synthesis\"") != NULL);
    CHECK(strstr(squad, "\"success\":true") != NULL);
    CHECK(strstr(squad, "\"parallel\":true") != NULL);
    CHECK(fixture.max_active >= 2); /* roles really ran concurrently */
    CHECK(fixture.calls == 4); /* 3 roles + 1 synthesis */
    free(squad);
    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

static int minimal_transport(void *context, const char *base_url, const char *api_key,
                             const char *body, int timeout_ms, char **response,
                             char **error) {
    ModelFixture *fixture = (ModelFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (fixture->calls == 1) {
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"m1\",\"function\":{\"name\":\"write_text_file\",\"arguments\":{\"relative_path\":\"nested/minimal.txt\",\"content\":\"minimal body\"}}}]}}]}");
    } else {
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"done minimal\"}}]}");
    }
    return 1;
}

static void test_minimal_mode(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_minimal(registry, sandbox, memory, root));
    CHECK(registry->count == 4);
    CHECK(zeno_registry_has(registry, "read_text_file"));
    CHECK(zeno_registry_has(registry, "write_text_file"));
    CHECK(zeno_registry_has(registry, "replace_in_file"));
    CHECK(zeno_registry_has(registry, "run_command"));
    CHECK(!zeno_registry_has(registry, "search_workspace"));
    CHECK(!zeno_registry_has(registry, "memory_search"));
    ModelFixture fixture = {0};
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, minimal_transport, &fixture);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 0, 0, 0, ZENO_AGENT_MODE_MINIMAL};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    CHECK(agent->options.max_turns == 12);
    CHECK(agent->options.max_history_chars == 40000);
    CHECK(agent->options.max_tool_output_chars == 8000);
    char *health = zeno_agent_health_json(agent);
    CHECK(health != NULL && strstr(health, "\"mode\":\"minimal\"") != NULL && strstr(health, "\"tool_count\":4") != NULL);
    free(health);
    char *system_prompt = zeno_build_system_prompt(agent, "minimal-session", "write the file");
    CHECK(system_prompt != NULL && strstr(system_prompt, "PRECISION CODING MODE (minimal)") != NULL);
    CHECK(system_prompt != NULL && strstr(system_prompt, "read_text_file") != NULL);
    free(system_prompt);
    ZenoRunOptions run_options = {"test-model", 0, 0, 0.0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    ZenoAgentResult result = zeno_agent_run(agent, "minimal-session", "create the file", &run_options);
    CHECK(strcmp(result.status, "completed") == 0);
    CHECK(result.tool_calls == 1 && result.successful_tools == 1);
    char *written = zeno_workspace_read(root, "nested/minimal.txt", 200);
    CHECK(written != NULL && strstr(written, "minimal body") != NULL);
    free(written);
    zeno_agent_result_free(&result);
    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

/* --- Squad tool scoping, caveman flag, MCP bridge --- */

typedef struct ScopeFixture { ZenoMutex lock; int calls; } ScopeFixture;

static int scoping_transport(void *context, const char *base_url, const char *api_key,
                             const char *body, int timeout_ms, char **response,
                             char **error) {
    (void)context; (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    int wants_write = body != NULL && strstr(body, "YOUR ROLE:") != NULL && strstr(body, "nested/scoped.txt") == NULL;
    *response = wants_write
        ? zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"sc1\",\"function\":{\"name\":\"write_text_file\",\"arguments\":{\"relative_path\":\"nested/scoped.txt\",\"content\":\"scoped body\"}}}]}}]}")
        : zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"scope contribution\"}}]}");
    return 1;
}

static void test_squad_scoping_caveman_mcp(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    CHECK(zeno_registry_has(registry, "mcp_call"));
    ScopeFixture fixture; memset(&fixture, 0, sizeof(fixture));
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, scoping_transport, &fixture);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 4, 120000, 20000, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 4, 1000, 0.2, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};

    /* Squad: both roles receive the same write instruction; the coder succeeds,
     * the reviewer fails because write tools are absent from its scope. */
    char *squad = zeno_agent_run_squad(agent, "scope-session", "write the file", "coder,reviewer", &run_options);
    CHECK(squad != NULL);
    CHECK(strstr(squad, "\"role\":\"coder\"") != NULL);
    CHECK(strstr(squad, "\"role\":\"reviewer\"") != NULL);
    CHECK(strstr(squad, "\"successful_tools\":1") != NULL);
    CHECK(strstr(squad, "\"failed_tools\":1") != NULL);
    CHECK(strstr(squad, "\"success\":true") != NULL);
    char *written = zeno_workspace_read(root, "nested/scoped.txt", 200);
    CHECK(written != NULL && strstr(written, "scoped body") != NULL);
    free(written);
    free(squad);

    /* Caveman flag: on by default, off when requested. */
    char *prompt_on = zeno_build_system_prompt(agent, "caveman", "task");
    CHECK(prompt_on != NULL && strstr(prompt_on, "CAVEMAN PROTOCOL") != NULL);
    free(prompt_on);
    ZenoAgentOptions quiet_options = agent_options;
    quiet_options.caveman_off = 1;
    ZenoAgent *quiet = zeno_agent_create(&quiet_options);
    char *prompt_off = zeno_build_system_prompt(quiet, "caveman", "task");
    CHECK(prompt_off != NULL && strstr(prompt_off, "CAVEMAN PROTOCOL") == NULL);
    free(prompt_off);
    zeno_agent_destroy(quiet);

    /* MCP bridge: available with libcurl, fails cleanly on unreachable servers. */
    int mcp_available = zeno_mcp_adapter_available();
     CHECK(mcp_available == 0 || mcp_available == 1);
    char *mcp = zeno_mcp_call("http://127.0.0.1:9/mcp", "demo", "{}");
    CHECK(mcp != NULL && (mcp_available ? strncmp(mcp, "MCP error", 9) == 0 : strstr(mcp, "not enabled") != NULL));
    free(mcp);
    char *listed = zeno_mcp_list_tools("http://127.0.0.1:9/mcp");
    CHECK(listed != NULL && (mcp_available ? strncmp(listed, "MCP error", 9) == 0 : strstr(listed, "not enabled") != NULL));
    free(listed);
    char *blocked_url = NULL;
     CHECK(!zeno_http_request("http://127.0.0.1:80/metadata", "GET", NULL, NULL, 1000, 1000, &blocked_url));
     CHECK(blocked_url != NULL && strstr(blocked_url, "public HTTP(S)") != NULL);
     free(blocked_url);
     char *injected_url = NULL;
      CHECK(!zeno_http_request("https://example.com/\r\nX-Injected: yes", "GET", NULL, NULL, 1000, 1000, &injected_url));
      CHECK(injected_url != NULL && strstr(injected_url, "public HTTP(S)") != NULL);
      free(injected_url);
      char *composio = zeno_composio_status("key");
    CHECK(composio != NULL && strstr(composio, "\"bridge\":\"mcp\"") != NULL);
    free(composio);

    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

typedef struct MultiFixture { int calls; } MultiFixture;

static int multi_turn_transport(void *context, const char *base_url, const char *api_key,
                                const char *body, int timeout_ms, char **response,
                                char **error) {
    MultiFixture *fixture = (MultiFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (fixture->calls == 1)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"m1\",\"type\":\"function\",\"function\":{\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"nested/multi1.txt\\\",\\\"content\\\":\\\"one\\\"}\"}}]}}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}");
    else if (fixture->calls == 2)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"m2\",\"type\":\"function\",\"function\":{\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"nested/multi2.txt\\\",\\\"content\\\":\\\"two\\\"}\"}}]}}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}");
    else
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"multi done\"}}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}");
    return 1;
}

static void test_multi_tool_turns(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    MultiFixture fixture = {0};
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, multi_turn_transport, &fixture);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 6, 120000, 20000, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 6, 1000, 0.2, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    ZenoAgentResult result = zeno_agent_run(agent, "multi", "write two files then stop", &run_options);
    CHECK(strcmp(result.status, "completed") == 0);
    CHECK(result.tool_calls == 2 && result.successful_tools == 2);
    CHECK(fixture.calls == 4); /* two writes + one verification-gate round trip + final */
    CHECK(result.tokens_in == 40 && result.tokens_out == 20); /* provider usage aggregated across all four LLM turns */
    char *one = zeno_workspace_read(root, "nested/multi1.txt", 100);
    char *two = zeno_workspace_read(root, "nested/multi2.txt", 100);
    CHECK(one != NULL && strstr(one, "one") != NULL);
    CHECK(two != NULL && strstr(two, "two") != NULL);
    free(one); free(two);
    zeno_agent_result_free(&result);
    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

/* --- Senior-loop behavior: bounded truncation keeps tails, loops teach --- */

#define LOOP_TEST_HEAD_MARKER "HEADMARKER_ABCDEFGHIJKLMNOP"
#define LOOP_TEST_TAIL_MARKER "TAILMARKER_ZYXWVUTSRQPONMLK"

static int big_output_tool(void *context, const char *args, char **output, char **error) {
    int *calls = (int *)context;
    (void)args; (void)error;
    (*calls)++;
    size_t fill = 1200;
    char *filler = (char *)calloc(fill + 1, 1);
    if (filler != NULL) memset(filler, 'x', fill);
    *output = zeno_format("%s\n%s\n%s", LOOP_TEST_HEAD_MARKER, filler != NULL ? filler : "", LOOP_TEST_TAIL_MARKER);
    free(filler);
    return 1;
}

typedef struct LoopFixture { int calls; int saw_tail; int saw_notice; } LoopFixture;

static int loop_transport(void *context, const char *base_url, const char *api_key,
                          const char *body, int timeout_ms, char **response,
                          char **error) {
    LoopFixture *fixture = (LoopFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL) {
        if (strstr(body, LOOP_TEST_TAIL_MARKER) != NULL) fixture->saw_tail = 1;
        if (strstr(body, "RUNTIME LOOP NOTICE") != NULL) fixture->saw_notice = 1;
    }
    *response = fixture->calls <= 4
        ? zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"lp1\",\"type\":\"function\",\"function\":{\"name\":\"echo_big\",\"arguments\":{}}}]}}]}")
        : zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"loop handled\"}}]}");
    return 1;
}

static void test_loop_notice_and_truncation(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path);
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    ZenoToolDefinition echo_big = {"echo_big", "Return a large diagnostic output.", "{}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    int big_calls = 0;
    CHECK(zeno_registry_register(registry, echo_big, big_output_tool, &big_calls));
    LoopFixture fixture = {0};
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, loop_transport, &fixture);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 8, 120000, 300, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 8, 500, 0.1, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    ZenoAgentResult result = zeno_agent_run(agent, "loop-session", "repeat a diagnostic read", &run_options);
    CHECK(strcmp(result.status, "completed") == 0);
    CHECK(fixture.calls == 5);
    CHECK(fixture.saw_tail == 1);   /* the model sees the END of oversized tool output */
    CHECK(fixture.saw_notice == 1); /* the model gets corrective feedback instead of a hard failure */
    CHECK(big_calls == 4);          /* stopped well before the hard-fail threshold */
    CHECK(result.tool_calls == 4 && result.successful_tools == 4);
    zeno_agent_result_free(&result);
    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

/* --- Phase 2: read-before-edit ledger, change summaries, verification gate --- */

typedef struct GateFixture { int calls; int saw_gate; } GateFixture;

static int gate_transport(void *context, const char *base_url, const char *api_key,
                          const char *body, int timeout_ms, char **response,
                          char **error) {
    GateFixture *fixture = (GateFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL && strstr(body, "RUNTIME VERIFICATION GATE") != NULL) fixture->saw_gate = 1;
    *response = fixture->calls == 1
        ? zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"g1\",\"type\":\"function\",\"function\":{\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"nested/gate.txt\\\",\\\"content\\\":\\\"gate body\\\"}\"}}]}}]}")
        : zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"gate done\"}}]}");
    return 1;
}

static void test_senior_edit_discipline(const char *root) {
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));

    /* Read-before-edit: an existing file cannot be mutated unread. */
    char *seeded = zeno_workspace_write(root, "nested/discipline.txt", "alpha\nbeta\ngamma");
    CHECK(seeded != NULL && strstr(seeded, "Error") == NULL); free(seeded);
    ZenoToolResult blocked = zeno_registry_execute(registry, "replace_in_file", "{\"relative_path\":\"nested/discipline.txt\",\"old_text\":\"beta\",\"new_text\":\"BETA\"}");
    CHECK(!blocked.ok && blocked.error != NULL && strcmp(blocked.error, "read_before_edit") == 0);
    CHECK(blocked.error_code == ZENO_TOOL_ERROR_READ_BEFORE_EDIT);
    CHECK(blocked.output != NULL && strstr(blocked.output, "read-before-edit policy") != NULL);
    zeno_tool_result_free(&blocked);

    /* After a real read the same edit succeeds and reports a change summary. */
    ZenoToolResult read = zeno_registry_execute(registry, "read_text_file", "{\"relative_path\":\"nested/discipline.txt\"}"); CHECK(read.ok); zeno_tool_result_free(&read);
    ZenoToolResult replace = zeno_registry_execute(registry, "replace_in_file", "{\"relative_path\":\"nested/discipline.txt\",\"old_text\":\"beta\",\"new_text\":\"BETA\"}");
    CHECK(replace.ok && replace.output != NULL && strstr(replace.output, "[change] +1/-1 lines") != NULL);
    zeno_tool_result_free(&replace);

    /* Overwriting an existing-but-unread file is also blocked; new files are free. */
    char *second = zeno_workspace_write(root, "nested/unread.txt", "never read");
    CHECK(second != NULL && strstr(second, "Error") == NULL); free(second);
    ZenoToolResult overwrite = zeno_registry_execute(registry, "write_text_file", "{\"relative_path\":\"nested/unread.txt\",\"content\":\"x\"}");
    CHECK(!overwrite.ok && overwrite.error != NULL && strcmp(overwrite.error, "read_before_edit") == 0);
    zeno_tool_result_free(&overwrite);
    ZenoToolResult fresh = zeno_registry_execute(registry, "write_text_file", "{\"relative_path\":\"nested/fresh.txt\",\"content\":\"one\\ntwo\"}");
    CHECK(fresh.ok && fresh.output != NULL && strstr(fresh.output, "[change] created file") != NULL);
    zeno_tool_result_free(&fresh);

    /* Verification gate: a run that writes and then declares "done" gets one
     * structured reminder with extra budget instead of silently completing. */
    GateFixture gate = {0};
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, gate_transport, &gate);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 6, 120000, 20000, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 6, 500, 0.1, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    ZenoAgentResult result = zeno_agent_run(agent, "gate-session", "write then finish", &run_options);
    CHECK(strcmp(result.status, "completed") == 0);
    CHECK(gate.calls == 3);          /* write -> gate reminder -> final */
    CHECK(gate.saw_gate == 1);       /* the model actually received the gate text */
    CHECK(result.turns >= 3);        /* extra budget was really granted */
    char *written = zeno_workspace_read(root, "nested/gate.txt", 200);
    CHECK(written != NULL && strstr(written, "gate body") != NULL);
    free(written);
    zeno_agent_result_free(&result);
    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

/* --- Phase 3: live plan, scoped subagents, cooperative cancellation --- */

typedef struct OrchFixture { int calls; int saw_sub_report; } OrchFixture;

static int orch_transport(void *context, const char *base_url, const char *api_key,
                          const char *body, int timeout_ms, char **response,
                          char **error) {
    OrchFixture *fixture = (OrchFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL && strstr(body, "SUB_REPORT_OK") != NULL) fixture->saw_sub_report = 1;
    if (fixture->calls == 1)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"o1\",\"type\":\"function\",\"function\":{\"name\":\"spawn_subagent\",\"arguments\":\"{\\\"task\\\":\\\"research the frobnicator\\\"}\"}}]}}]}");
    else if (fixture->calls == 2)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"SUB_REPORT_OK\"}}]}");
    else
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"orchestration done\"}}]}");
    return 1;
}

typedef struct CancelFixture { int calls; } CancelFixture;

static int cancel_counting_transport(void *context, const char *base_url, const char *api_key,
                                     const char *body, int timeout_ms, char **response,
                                     char **error) {
    CancelFixture *fixture = (CancelFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":\"custom\",\"arguments\":{}}}]}}]}");
    return 1;
}

static int cancel_when_two(void *context) {
    CancelFixture *fixture = (CancelFixture *)context;
    return fixture->calls >= 2;
}

static void test_phase3_orchestration(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path);
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    int custom_calls = 0;
    ZenoToolDefinition custom = {"custom", "Custom deterministic tool", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    CHECK(zeno_registry_register(registry, custom, custom_tool, &custom_calls));

    /* Live plan: persisted by the tool, injected into the system prompt. */
    ZenoToolResult plan = zeno_registry_execute(registry, "update_plan", "{\"plan_markdown\":\"- [ ] step one\\n- [x] step two\"}");
    CHECK(plan.ok && plan.output != NULL && strstr(plan.output, "Plan saved") != NULL);
    zeno_tool_result_free(&plan);

    OrchFixture orch = {0};
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, orch_transport, &orch);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 8, 120000, 20000, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);

    char *prompt = zeno_build_system_prompt(agent, "orch", "any task");
    CHECK(prompt != NULL && strstr(prompt, "CURRENT PLAN") != NULL && strstr(prompt, "- [ ] step one") != NULL);
    free(prompt);

    /* Scoped subagent: parent delegates research, sub report reaches parent. */
    ZenoRunOptions run_options = {"test-model", 6, 500, 0.1, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    ZenoAgentResult result = zeno_agent_run(agent, "orch-session", "delegate then report", &run_options);
    CHECK(strcmp(result.status, "completed") == 0);
    CHECK(orch.calls == 3);            /* parent ask -> sub report -> parent final */
    CHECK(orch.saw_sub_report == 1);   /* sub output really flowed back to the parent */

    /* Cooperative cancellation via run options at a turn boundary. */
    CancelFixture cancel = {0};
    ZenoRouter *cancel_router = zeno_router_create();
    CHECK(zeno_router_add_provider(cancel_router, provider)); zeno_router_set_transport(cancel_router, cancel_counting_transport, &cancel);
    ZenoAgentOptions cancel_options = {registry, cancel_router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 6, 120000, 20000, 0, 0};
    ZenoAgent *cancel_agent = zeno_agent_create(&cancel_options);
    ZenoRunOptions cancel_run = {"test-model", 6, 500, 0.1, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    cancel_run.should_cancel = cancel_when_two;
    cancel_run.cancel_context = &cancel;
    ZenoAgentResult cancelled = zeno_agent_run(cancel_agent, "cancel-session", "keep calling custom", &cancel_run);
    CHECK(strcmp(cancelled.status, "cancelled") == 0);
    CHECK(cancelled.turns >= 2);

    /* Agent-level cancellation flag stops the very next run instantly. */
    zeno_agent_request_cancel(cancel_agent);
    ZenoRunOptions flagged_run = {"test-model", 6, 500, 0.1, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    ZenoAgentResult flagged = zeno_agent_run(cancel_agent, "flagged-session", "should stop immediately", &flagged_run);
    CHECK(strcmp(flagged.status, "cancelled") == 0);
    CHECK(flagged.turns == 0); /* stopped before consuming any turn */

    zeno_agent_result_free(&result);
    zeno_agent_result_free(&cancelled);
    zeno_agent_result_free(&flagged);
    free(runs_path);
    zeno_agent_destroy(cancel_agent); zeno_agent_destroy(agent);
    zeno_router_destroy(cancel_router); zeno_router_destroy(router);
    zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

/* --- Phase 4 hardening: deterministic fuzzing of the JSON parser --- */

static uint64_t fuzz_next(uint64_t *state) {
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(*state >> 33);
}

static void test_json_fuzz(void) {
    static const char *seeds[] = {
        "{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true,\"e\":null}",
        "[[{\"k\":\"v\\n\\t\\u0041\"},3.14e-2,false]]",
        "{\"tool_calls\":[{\"id\":\"c1\",\"function\":{\"name\":\"read_text_file\",\"arguments\":\"{}\"}}]}",
        "\"escaped \\\" quote \\\\ slash \\u00e9\"",
        "{}",
        "[]",
    };
    uint64_t state = 0x5EED1234ULL;
    char buffer[512];
    for (int round = 0; round < 5000; round++) {
        const char *seed = seeds[fuzz_next(&state) % (sizeof(seeds) / sizeof(seeds[0]))];
        size_t len = strlen(seed);
        if (len == 0 || len >= sizeof(buffer)) continue;
        memcpy(buffer, seed, len + 1);
        int mutations = (int)(fuzz_next(&state) % 7U);
        for (int m = 0; m < mutations; m++) {
            size_t position = fuzz_next(&state) % len;
            switch (fuzz_next(&state) % 4U) {
                case 0: buffer[position] = (char)(32U + fuzz_next(&state) % 95U); break;
                case 1: memmove(&buffer[position], &buffer[position + 1], len - position); len--; buffer[len] = '\0'; if (len == 0) break; break;
                case 2:
                    if (len + 1 < sizeof(buffer)) {
                        memmove(&buffer[position + 1], &buffer[position], len - position);
                        buffer[position] = (char)(32U + fuzz_next(&state) % 95U);
                        len++;
                        buffer[len] = '\0';
                    }
                    break;
                default: buffer[position] = "{}[]\":,0ntf"[(fuzz_next(&state) % 10U)]; break;
            }
            if (len == 0) break;
        }
        char *error = NULL;
        ZjNode *node = zj_parse(buffer, &error);
        /* Core invariants: never crash or hang; failure always yields an
         * error message; freeing any tree (even partial-looking) is safe. */
        if (node != NULL) {
            char *serialized = zj_stringify_compact(node);
            if (serialized != NULL) {
                char *reerror = NULL;
                ZjNode *reparsed = zj_parse(serialized, &reerror);
                if (reparsed != NULL) {
                    char *again = zj_stringify_compact(reparsed);
                    CHECK(again != NULL && strcmp(again, serialized) == 0);
                    free(again);
                } else {
                    CHECK(reerror != NULL);
                }
                free(reerror);
                zj_free(reparsed);
            }
            free(serialized);
            zj_free(node);
            CHECK(error == NULL);
        } else {
            CHECK(error != NULL);
        }
        free(error);
    }

    /* Depth limit: nesting beyond the cap must fail cleanly, not crash. */
    char deep[401];
    memset(deep, '[', 200);
    memset(deep + 200, ']', 200);
    deep[400] = '\0';
    char *deep_error = NULL;
    ZjNode *deep_node = zj_parse(deep, &deep_error);
    CHECK(deep_node == NULL);
    CHECK(deep_error != NULL && strstr(deep_error, "maximum depth") != NULL);
    free(deep_error);

    char shallow[65];
    memset(shallow, '[', 32);
    memset(shallow + 32, ']', 32);
    shallow[64] = '\0';
    char *shallow_error = NULL;
    ZjNode *shallow_node = zj_parse(shallow, &shallow_error);
    CHECK(shallow_node != NULL && shallow_error == NULL);
    free(shallow_error);
    zj_free(shallow_node);
}

/* Deadline: a wall-clock budget stops the run at a turn boundary. */
typedef struct SlowCallFixture { int calls; } SlowCallFixture;

static int slow_call_transport(void *context, const char *base_url, const char *api_key,
                               const char *body, int timeout_ms, char **response,
                               char **error) {
    SlowCallFixture *fixture = (SlowCallFixture *)context;
    (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    fixture->calls++;
    zeno_sleep_ms(120);
    *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"sc1\",\"type\":\"function\",\"function\":{\"name\":\"custom\",\"arguments\":{}}}]}}]}");
    return 1;
}

static void test_run_deadline(const char *root) {
    char *memory_path = zeno_join_path(root, "agent-memory.md"); ZenoMemory *memory = zeno_memory_create(memory_path);
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0}; ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    int custom_calls = 0;
    ZenoToolDefinition custom = {"custom", "Custom deterministic tool", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    CHECK(zeno_registry_register(registry, custom, custom_tool, &custom_calls));
    SlowCallFixture fixture = {0};
    ZenoRouter *router = zeno_router_create(); ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider)); zeno_router_set_transport(router, slow_call_transport, &fixture);
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, router, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 8, 120000, 20000, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    ZenoRunOptions run_options = {"test-model", 8, 500, 0.1, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    run_options.wall_clock_budget_ms = 300;
    ZenoAgentResult result = zeno_agent_run(agent, "deadline-session", "keep calling custom slowly", &run_options);
    CHECK(strcmp(result.status, "deadline_exceeded") == 0);
    CHECK(result.turns >= 1 && result.turns <= 3);
    CHECK(custom_calls >= 1 && custom_calls <= 3);
    zeno_agent_result_free(&result);
    free(runs_path);
    zeno_agent_destroy(agent); zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry); zeno_memory_destroy(memory); zeno_sandbox_destroy(sandbox); free(memory_path);
}

#if defined(_WIN32)
#define zeno_test_putenv _putenv
#else
#define zeno_test_putenv putenv
#endif

static void test_ecosystem_v13(const char *root) {
    /* --- MCP over stdio: real spawned server, real handshake --- */
    char *server_path = zeno_join_path(root, "mcp_server.py");
    CHECK(server_path != NULL);
    FILE *server_file = fopen(server_path, "wb");
    CHECK(server_file != NULL);
    static const char *server_source[] = {
        "import json, sys\n",
        "for line in sys.stdin:\n",
        "    line = line.strip()\n",
        "    if not line: continue\n",
        "    try: request = json.loads(line)\n",
        "    except ValueError: continue\n",
        "    method = request.get('method', ''); rid = request.get('id')\n",
        "    if method == 'notifications/initialized': continue\n",
        "    elif method == 'initialize':\n",
        "        result = {'protocolVersion': '2024-11-05', 'capabilities': {}, 'serverInfo': {'name': 't', 'version': '1'}}\n",
        "    elif method == 'tools/list':\n",
        "        result = {'tools': [{'name': 'add', 'description': 'Add two numbers'}, {'name': 'echo', 'description': 'Echo'}]}\n",
        "    elif method == 'tools/call':\n",
        "        p = request.get('params', {}); args = p.get('arguments', {})\n",
        "        if p.get('name') == 'add': text = str(args.get('a', 0)) + ' + ' + str(args.get('b', 0)) + ' = ' + str(args.get('a', 0) + args.get('b', 0))\n",
        "        elif p.get('name') == 'echo': text = str(args.get('text', ''))\n",
        "        else:\n",
        "            sys.stdout.write(json.dumps({'jsonrpc': '2.0', 'id': rid, 'error': {'code': -32602, 'message': 'unknown tool'}}) + chr(10)); sys.stdout.flush(); continue\n",
        "        result = {'content': [{'type': 'text', 'text': text}]}\n",
        "    else:\n",
        "        sys.stdout.write(json.dumps({'jsonrpc': '2.0', 'id': rid, 'error': {'code': -32601, 'message': 'unknown method'}}) + chr(10)); sys.stdout.flush(); continue\n",
        "    sys.stdout.write(json.dumps({'jsonrpc': '2.0', 'id': rid, 'result': result}) + chr(10)); sys.stdout.flush()\n",
    };
    for (size_t index = 0; index < sizeof(server_source) / sizeof(server_source[0]); index++)
        fputs(server_source[index], server_file);
    (void)fclose(server_file);

    /* Gate on python availability so the suite stays deterministic anywhere. */
    ZenoExecResult probe;
    memset(&probe, 0, sizeof(probe));
    int have_python = zeno_process_command_stdin("python -c \"print(1)\"", root, "", 10000, 2000, &probe);
    free(probe.output); free(probe.reason);
    if (have_python && probe.exit_code == 0) {
        char *server_command = zeno_format("python \"%s\"", server_path);
        CHECK(server_command != NULL);
        char *sum = zeno_mcp_call_stdio(server_command, "add", "{\"a\": 2, \"b\": 3}");
        CHECK(sum != NULL && strstr(sum, "2 + 3 = 5") != NULL);
        free(sum);
        char *echoed = zeno_mcp_call_stdio(server_command, "echo", "{\"text\": \"zeno-stdio\"}");
        CHECK(echoed != NULL && strstr(echoed, "zeno-stdio") != NULL);
        free(echoed);
        char *bad_tool = zeno_mcp_call_stdio(server_command, "nope", "{}");
        CHECK(bad_tool != NULL && strncmp(bad_tool, "MCP error", 9) == 0);
        free(bad_tool);
        char *tools = zeno_mcp_list_tools_stdio(server_command);
        CHECK(tools != NULL && strstr(tools, "add") != NULL && strstr(tools, "echo") != NULL);
        free(tools);
        free(server_command);
        fprintf(stderr, "[v13] stage-mcp-done\n");
    } else {
        puts("note: python not available; stdio MCP cases skipped");
    }
    free(server_path);

    /* --- Skills: index in prompt, load_skill tool, traversal rejected --- */
    char *skills_dir = zeno_join_path(root, "skills");
    char *alpha_dir = zeno_join_path(skills_dir, "alpha");
    char *alpha_path = zeno_join_path(alpha_dir, "SKILL.md");
    CHECK(zeno_mkdirs(alpha_dir));
    FILE *alpha_file = fopen(alpha_path, "wb");
    CHECK(alpha_file != NULL);
    fputs("---\nname: alpha\ndescription: Alpha skill for the ecosystem test.\n---\nALPHA BODY MARKER\n", alpha_file);
    (void)fclose(alpha_file);
    char *memory_path = zeno_join_path(root, "agent-memory.md");
    ZenoMemory *memory = zeno_memory_create(memory_path);
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0};
    ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoRegistry *registry = zeno_registry_create();
    ZenoApproval *approval = zeno_approval_create();
    CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    char *runs_path = zeno_join_path(root, "runs");
    ZenoAgentOptions agent_options = {registry, NULL, memory, sandbox, NULL, NULL, approval, "test-model", runs_path, 0, 0, 4, 120000, 20000, 0, 0};
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    CHECK(zeno_agent_attach_skills(agent, registry, skills_dir));
    CHECK(zeno_registry_has(registry, "load_skill"));
    char *prompt = zeno_build_system_prompt(agent, "skills-session", "task");
    CHECK(prompt != NULL && strstr(prompt, "alpha: Alpha skill") != NULL && strstr(prompt, "AVAILABLE SKILLS") != NULL);
    free(prompt);
    ZenoToolResult loaded = zeno_registry_execute(registry, "load_skill", "{\"name\":\"alpha\"}");
    CHECK(loaded.ok && strstr(loaded.output, "ALPHA BODY MARKER") != NULL);
    zeno_tool_result_free(&loaded);
    ZenoToolResult escaped = zeno_registry_execute(registry, "load_skill", "{\"name\":\"../../secret\"}");
    CHECK(!escaped.ok);
    zeno_tool_result_free(&escaped);
    fprintf(stderr, "[v13] stage-skills-done\n");
    zeno_agent_destroy(agent);

    /* --- Agent definitions: AI Suite schema drives the squad --- */
    char *agents_dir = zeno_join_path(root, "agents");
    CHECK(zeno_mkdirs(agents_dir));
    char *def_path = zeno_join_path(agents_dir, "coder.md");
    FILE *def_file = fopen(def_path, "wb");
    CHECK(def_file != NULL);
    fputs("---\nname: coder\ndescription: >-\n  Writes scoped changes.\ntools: read_text_file, write_text_file\nmodel: test-model\n---\nAGENT DEF BRIEF MARKER\n", def_file);
    (void)fclose(def_file);
    free(def_path);
    ZenoAgentDef *defs = NULL;
    size_t def_count = 0;
    CHECK(zeno_load_agent_defs(agents_dir, &defs, &def_count) == 1);
    CHECK(def_count == 1);
    CHECK(defs[0].name != NULL && strcmp(defs[0].name, "coder") == 0);
    CHECK(defs[0].description != NULL && strstr(defs[0].description, "Writes scoped changes.") != NULL);
    CHECK(defs[0].tools != NULL && strcmp(defs[0].tools, "read_text_file, write_text_file") == 0);
    CHECK(defs[0].model != NULL && strcmp(defs[0].model, "test-model") == 0);
    CHECK(defs[0].system_prompt != NULL && strstr(defs[0].system_prompt, "AGENT DEF BRIEF MARKER") != NULL);
    zeno_agent_defs_free(defs, def_count);
    zeno_free(agents_dir);
    fprintf(stderr, "[v13] stage-defs-done\n");

    ScopeFixture fixture;
    memset(&fixture, 0, sizeof(fixture));
    ZenoRouter *router = zeno_router_create();
    ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    CHECK(zeno_router_add_provider(router, provider));
    zeno_router_set_transport(router, scoping_transport, &fixture);
    ZenoAgentOptions squad_options = agent_options;
    squad_options.router = router;
    ZenoAgent *squad_agent = zeno_agent_create(&squad_options);
    ZenoRunOptions run_options = {"test-model", 4, 1000, 0.2, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
    agents_dir = zeno_join_path(root, "agents");
    char *squad = zeno_agent_run_squad_ex(squad_agent, "eco-session", "write the file", "coder,reviewer", agents_dir, &run_options);
    CHECK(squad != NULL && strstr(squad, "\"agents_dir\"") != NULL);
    CHECK(strstr(squad, "\"role\":\"coder\"") != NULL && strstr(squad, "\"role\":\"reviewer\"") != NULL);
    CHECK(strstr(squad, "\"success\":true") != NULL);
    /* The coder's def-scoped registry still allowed the write (tools csv). */
    char *written = zeno_workspace_read(root, "nested/scoped.txt", 200);
    CHECK(written != NULL && strstr(written, "scoped body") != NULL);
    free(written);
    free(squad);
    zeno_agent_destroy(squad_agent);
    zeno_router_destroy(router);
    zeno_free(agents_dir);
    fprintf(stderr, "[v13] stage-squadex-done\n");

    /* --- Hooks: pre_tool exit 2 blocks; post_tool runs for side effects --- */
    char *hooks_dir = zeno_join_path(root, ".zeno");
    CHECK(zeno_mkdirs(hooks_dir));
    char *hooks_path = zeno_join_path(hooks_dir, "hooks.json");
    char *marker_path = zeno_join_path(root, "hook-marker.txt");
    char *marker_command = zeno_format("echo hook-marker-ok>\"%s\"", marker_path);
    FILE *hooks_file = fopen(hooks_path, "wb");
    CHECK(hooks_file != NULL && marker_command != NULL);
    fprintf(hooks_file, "{\"hooks\":[{\"event\":\"pre_tool\",\"command\":\"exit 2\",\"timeout_ms\":3000}]}");
    (void)fclose(hooks_file);
    {
        char saved[1024];
        const char *profile = getenv("USERPROFILE");
#ifndef _WIN32
        if (profile == NULL || *profile == '\0') profile = getenv("HOME");
#endif
        if (profile != NULL) { strncpy(saved, profile, sizeof(saved) - 1); saved[sizeof(saved) - 1] = '\0'; } else saved[0] = '\0';
        /* hooks_load reads <home>/.zeno/hooks.json, so the home override is the
         * case root. putenv retains the string on POSIX: deliberately leaked. */
        char *override = zeno_format("USERPROFILE=%s", root);
        CHECK(override != NULL && zeno_test_putenv(override) == 0);
        ZenoAgent *hook_agent = zeno_agent_create(&agent_options);
        char *calls = zeno_strdup("[{\"id\":\"call_1\",\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"hook-target.txt\\\",\\\"content\\\":\\\"blocked body\\\"}\"}]");
        char *logs = NULL;
        int successful = 0, failed = 0, waiting = 0;
        ZenoRunOptions hook_options = {"test-model", 4, 1000, 0.2, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL};
        (void)zeno_execute_tool_calls(hook_agent, "hooks-session", "run_1", calls, &hook_options, &logs, &successful, &failed, &waiting);
        CHECK(failed == 1 && successful == 0);
        CHECK(logs != NULL && strstr(logs, "hook_denied") != NULL);
        /* workspace_read returns a non-NULL error text for missing files. */
        char *target = zeno_workspace_read(root, "hook-target.txt", 200);
        CHECK(target != NULL && strstr(target, "File not found") != NULL && strstr(target, "blocked body") == NULL);
        free(target);
        free(logs);
        free(calls);
        zeno_agent_destroy(hook_agent);

        /* post_tool hook allowed and observable. */
        FILE *hooks_file2 = fopen(hooks_path, "wb");
        CHECK(hooks_file2 != NULL);
        char *marker_json = marker_command != NULL ? zeno_json_escape(marker_command) : NULL;
        fprintf(hooks_file2, "{\"hooks\":[{\"event\":\"post_tool\",\"command\":%s,\"timeout_ms\":3000}]}",
                marker_json != NULL ? marker_json : "\"echo\"");
        free(marker_json);
        (void)fclose(hooks_file2);
        ZenoAgent *hook_agent2 = zeno_agent_create(&agent_options);
        char *calls2 = zeno_strdup("[{\"id\":\"call_2\",\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"hook-ok.txt\\\",\\\"content\\\":\\\"ok body\\\"}\"}]");
        char *logs2 = NULL;
        int successful2 = 0, failed2 = 0, waiting2 = 0;
        (void)zeno_execute_tool_calls(hook_agent2, "hooks-session", "run_2", calls2, &hook_options, &logs2, &successful2, &failed2, &waiting2);
        CHECK(successful2 == 1 && failed2 == 0);
        char *marker = zeno_workspace_read(root, "hook-marker.txt", 100);
        CHECK(marker != NULL && strstr(marker, "hook-marker-ok") != NULL);
        free(marker);
        free(logs2);
        free(calls2);
        zeno_agent_destroy(hook_agent2);

        if (saved[0] != '\0') {
            char *restore = zeno_format("USERPROFILE=%s", saved);
            if (restore != NULL) (void)zeno_test_putenv(restore);
            /* restore deliberately leaked: POSIX putenv retains the pointer. */
        }
    }
    fprintf(stderr, "[v13] stage-hooks-done\n");
    free(marker_command);
    free(marker_path);
    zeno_registry_destroy(registry);
    zeno_approval_destroy(approval);
    zeno_memory_destroy(memory);
    zeno_sandbox_destroy(sandbox);
    free(memory_path);
    free(runs_path);
    free(skills_dir);
    free(alpha_dir);
    free(alpha_path);
    free(hooks_path);
    free(hooks_dir);
    fprintf(stderr, "[v13] stage-cleanup-done\n");
}

static char *studio_test_field(const char *json, const char *key) {
    char *error = NULL;
    ZjNode *holder = zj_parse(json != NULL ? json : "{}", &error);
    const char *value;
    char *copy = NULL;
    free(error);
    if (holder == NULL) return NULL;
    value = zj_string(zj_object_get(holder, key));
    if (value != NULL) copy = zeno_strdup(value);
    zj_free(holder);
    return copy;
}

static void test_studio_backend(const char *root) {
    /* --- voice: Deepgram-first, OpenAI optional, auto greeting, no chat --- */
    ZenoVoiceConfig voice;
    char *text = NULL;
    char *url = NULL;
    char *sample = NULL;
    zeno_voice_config_default(&voice);
    CHECK(strcmp(voice.provider, "deepgram") == 0);
    CHECK(strcmp(voice.language, "pt-BR") == 0);
    CHECK(voice.auto_speak == 1);
    CHECK(zeno_voice_config_load_env(&voice));
    url = zeno_voice_stt_url(&voice, NULL);
    CHECK(url != NULL && strstr(url, "deepgram.com/v1/listen") != NULL && strstr(url, "nova-3") != NULL);
    free(url);
    url = zeno_voice_tts_url(&voice, NULL);
    CHECK(url != NULL && strstr(url, "deepgram.com/v1/speak") != NULL);
    free(url);
    text = zeno_voice_greeting(&voice);
    CHECK(text != NULL && *text != '\0');
    free(text);
    sample = zeno_strdup("{\"results\":{\"channels\":[{\"alternatives\":[{\"transcript\":\"ola mundo\"}]}]}}");
    text = zeno_voice_transcript_extract(sample);
    CHECK(text != NULL && strcmp(text, "ola mundo") == 0);
    free(sample);
    free(text);
    sample = zeno_strdup("{\"text\":\"hello there\"}");
    text = zeno_voice_transcript_extract(sample);
    CHECK(text != NULL && strcmp(text, "hello there") == 0);
    free(sample);
    free(text);
    CHECK(zeno_voice_transcript_extract("{\"results\":{}}") == NULL);
    {
        ZenoVoiceConfig openai;
        zeno_voice_config_default(&openai);
        zeno_copy_string(openai.provider, sizeof(openai.provider), "openai");
        url = zeno_voice_stt_url(&openai, "en-US");
        CHECK(url != NULL && strstr(url, "openai.com/v1/audio/transcriptions") != NULL);
        free(url);
        url = zeno_voice_tts_url(&openai, NULL);
        CHECK(url != NULL && strstr(url, "openai.com/v1/audio/speech") != NULL);
        free(url);
        text = zeno_voice_describe(&voice);
        CHECK(text != NULL && strstr(text, "stt_url") != NULL && strstr(text, "greeting") != NULL);
        free(text);
    }

    /* --- settings: Chat/Notifications/Shortcuts/Voice/Appearance --- */
    {
        char *settings_path = zeno_join_path(root, "settings.md");
        char *listed = NULL;
        char *value = NULL;
        CHECK(settings_path != NULL);
        listed = zeno_settings_list(settings_path);
        CHECK(listed != NULL && strstr(listed, "\"voice\"") != NULL && strstr(listed, "\"chat\"") != NULL);
        free(listed);
        CHECK(zeno_settings_set(settings_path, "voice", "provider", "\"openai\""));
        value = zeno_settings_get(settings_path, "voice", "provider");
        CHECK(value != NULL && strstr(value, "openai") != NULL);
        free(value);
        CHECK(zeno_settings_set(settings_path, "chat", "font_size", "large"));
        value = zeno_settings_get(settings_path, "chat", "font_size");
        CHECK(value != NULL && strstr(value, "large") != NULL);
        free(value);
        CHECK(zeno_settings_set(settings_path, "notifications", "sound", "false"));
        value = zeno_settings_get(settings_path, "notifications", "sound");
        CHECK(value != NULL && strstr(value, "false") != NULL);
        free(value);
        CHECK(!zeno_settings_set(settings_path, "bad section!", "key", "value"));
        free(settings_path);
    }

    /* --- projects: editable blocks (create/rename/folders/delete) --- */
    {
        char *projects_path = zeno_join_path(root, "projects.md");
        char *created = NULL;
        char *pid = NULL;
        char *listed = NULL;
        CHECK(projects_path != NULL);
        created = zeno_project_create(projects_path, "Website da oficina", "GUI Zeno,Docs");
        CHECK(created != NULL && strstr(created, "Website da oficina") != NULL);
        pid = studio_test_field(created, "id");
        CHECK(pid != NULL);
        free(created);
        listed = zeno_projects_list(projects_path);
        CHECK(listed != NULL && strstr(listed, "Website da oficina") != NULL);
        free(listed);
        CHECK(zeno_project_rename(projects_path, pid, "Site novo"));
        listed = zeno_projects_list(projects_path);
        CHECK(listed != NULL && strstr(listed, "Site novo") != NULL);
        free(listed);
        CHECK(zeno_project_set_folders(projects_path, pid, "A,B"));
        CHECK(!zeno_project_rename(projects_path, "nope", "x"));
        CHECK(zeno_project_delete(projects_path, pid));
        listed = zeno_projects_list(projects_path);
        CHECK(listed != NULL && strstr(listed, "Site novo") == NULL);
        free(listed);
        free(pid);
        free(projects_path);
    }

    /* --- remotes: VPS, VM, Colab --- */
    {
        char *remotes_path = zeno_join_path(root, "remotes.md");
        char *added = NULL;
        char *rid = NULL;
        char *info = NULL;
        char *listed = NULL;
        CHECK(remotes_path != NULL);
        added = zeno_remote_add(remotes_path, "vps", "prod", "dev@192.168.1.10:2222");
        CHECK(added != NULL && strstr(added, "192.168.1.10") != NULL);
        rid = studio_test_field(added, "id");
        CHECK(rid != NULL);
        free(added);
        info = zeno_remote_connect_cmd(remotes_path, rid);
        CHECK(info != NULL && strstr(info, "ssh -p 2222 dev@192.168.1.10") != NULL);
        free(info);
        CHECK(zeno_remote_rename(remotes_path, rid, "production"));
        added = zeno_remote_add(remotes_path, "colab", "gpu", "https://colab.research.google.com/drive/abc");
        CHECK(added != NULL && strstr(added, "colab") != NULL);
        {
            char *cid = studio_test_field(added, "id");
            CHECK(cid != NULL);
            info = zeno_remote_connect_cmd(remotes_path, cid);
            CHECK(info != NULL && strstr(info, "colab") != NULL);
            free(info);
            CHECK(zeno_remote_remove(remotes_path, cid));
            free(cid);
        }
        free(added);
        CHECK(zeno_remote_add(remotes_path, "mainframe", "x", "y") == NULL);
        CHECK(zeno_remote_remove(remotes_path, rid));
        listed = zeno_remotes_list(remotes_path);
        CHECK(listed != NULL && strstr(listed, "production") == NULL);
        free(listed);
        free(rid);
        free(remotes_path);
    }

    /* --- plugins: JSON lifecycle + agent auto-save --- */
    {
        char err[256];
        char *plugin = zeno_plugin_create("resume emails rapidamente", "Email Helper");
        char *plugins_dir = zeno_join_path(root, "plugins");
        char *listed = NULL;
        char *applied = NULL;
        char *pid = NULL;
        CHECK(plugin != NULL);
        CHECK(zeno_plugin_validate(plugin, err, sizeof(err)));
        CHECK(!zeno_plugin_validate("{\"id\":\"BAD ID!\"}", err, sizeof(err)));
        CHECK(plugins_dir != NULL);
        CHECK(zeno_plugin_save(plugins_dir, plugin));
        pid = studio_test_field(plugin, "id");
        CHECK(pid != NULL);
        free(plugin);
        listed = zeno_plugin_list(plugins_dir);
        CHECK(listed != NULL && strstr(listed, pid) != NULL);
        free(listed);
        CHECK(zeno_plugin_set_enabled(plugins_dir, pid, 0));
        listed = zeno_plugin_list(plugins_dir);
        CHECK(listed != NULL && strstr(listed, "\"enabled\":false") != NULL);
        free(listed);
        CHECK(zeno_plugin_set_enabled(plugins_dir, pid, 1));
        CHECK(zeno_plugin_rename(plugins_dir, pid, "Email Super Helper"));
        listed = zeno_plugin_list(plugins_dir);
        CHECK(listed != NULL && strstr(listed, "Email Super Helper") != NULL);
        free(listed);
        applied = zeno_plugin_apply(plugins_dir);
        CHECK(applied != NULL && strstr(applied, "functions") != NULL && strstr(applied, "buttons") != NULL);
        free(applied);
        CHECK(zeno_plugin_guide() != NULL && strstr(zeno_plugin_guide(), "create_plugin") != NULL);
        free(pid);
        free(plugins_dir);
    }

    /* --- agent tool: create_plugin generates + saves automatically --- */
    {
        ZenoRegistry *registry = zeno_registry_create();
        char *plugins_dir = zeno_join_path(root, "agent-plugins");
        char *args = NULL;
        CHECK(registry != NULL && plugins_dir != NULL);
        CHECK(zeno_register_studio_tools(registry, plugins_dir));
        CHECK(zeno_registry_has(registry, "create_plugin"));
        args = NULL;
        {
            char *dir_esc = zeno_json_escape(plugins_dir);
            CHECK(dir_esc != NULL);
            args = zeno_format("{\"request\":\"organiza tarefas do dia\",\"name_hint\":\"Tasks\","
                               "\"plugins_dir\":%s}",
                               dir_esc);
            free(dir_esc);
        }
        if (args != NULL) {
            ZenoToolResult pres = zeno_registry_execute(registry, "create_plugin", args);
            CHECK(pres.ok && pres.output != NULL && strstr(pres.output, "\"id\"") != NULL);
            zeno_tool_result_free(&pres);
            free(args);
        }
        zeno_registry_destroy(registry);
        free(plugins_dir);
    }
    fprintf(stderr, "[studio] backend-cases-done\n");
}

#define RUN_ISOLATED_CASE(function_name) do { \
    char *case_root = test_root(); \
    CHECK(case_root != NULL); \
    if (case_root != NULL) { function_name(case_root); cleanup_root(case_root); free(case_root); } \
} while (0)

int main(void) {
    test_json_and_helpers();
    test_json_fuzz();
    RUN_ISOLATED_CASE(test_persistence_and_memory);
    RUN_ISOLATED_CASE(test_cache_trace_sandbox);
    RUN_ISOLATED_CASE(test_registry_and_agent);
    RUN_ISOLATED_CASE(test_jobs_persistence);
    RUN_ISOLATED_CASE(test_parallel_tool_calls);
    RUN_ISOLATED_CASE(test_parallel_agents_and_squad);
    RUN_ISOLATED_CASE(test_minimal_mode);
    RUN_ISOLATED_CASE(test_multi_tool_turns);
    RUN_ISOLATED_CASE(test_loop_notice_and_truncation);
    RUN_ISOLATED_CASE(test_senior_edit_discipline);
    RUN_ISOLATED_CASE(test_phase3_orchestration);
    RUN_ISOLATED_CASE(test_run_deadline);
    RUN_ISOLATED_CASE(test_squad_scoping_caveman_mcp);
    RUN_ISOLATED_CASE(test_ecosystem_v13);
    RUN_ISOLATED_CASE(test_studio_backend);
    if (failures != 0) { fprintf(stderr, "%d test assertion(s) failed.\n", failures); return 1; }
    puts("ZenoC tests: all assertions passed");
    return 0;
}
