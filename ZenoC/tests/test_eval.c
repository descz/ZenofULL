/* ZenoC offline evaluation harness.
 *
 * Golden behavioral scenarios against deterministic mock transports.
 * One JSON metrics line per scenario; non-zero exit on any failed
 * expectation. zenoc_tests covers units; zenoc_eval covers behavior. */

#include "zeno_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static int eval_failures = 0;
static unsigned long eval_counter = 0;

#define EVAL_CHECK(condition) do { if (!(condition)) { fprintf(stderr, "EVAL FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); eval_failures++; } } while (0)

static char *eval_root(void) {
#ifdef _WIN32
    unsigned long process_id = (unsigned long)GetCurrentProcessId();
#else
    unsigned long process_id = (unsigned long)getpid();
#endif
    char *root = zeno_format(".zeno_eval_tmp_%lu_%lu_%lld", process_id, ++eval_counter, zeno_now_ms());
    if (root == NULL || !zeno_mkdirs(root)) { free(root); return NULL; }
    return root;
}

static void eval_remove_tree(const char *path) {
    if (path == NULL) return;
#ifdef _WIN32
    char *pattern = zeno_join_path(path, "*");
    WIN32_FIND_DATAA data;
    HANDLE handle = pattern != NULL ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
            char *child = zeno_join_path(path, data.cFileName);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) eval_remove_tree(child);
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
            if (child != NULL && lstat(child, &info) == 0 && S_ISDIR(info.st_mode)) eval_remove_tree(child);
            else if (child != NULL) (void)remove(child);
            free(child);
        }
        closedir(directory);
    }
    (void)rmdir(path);
#endif
}

static void eval_report(const char *scenario, int ok, const ZenoAgentResult *result) {
    printf("{\"scenario\":\"%s\",\"verdict\":\"%s\",\"run_status\":\"%s\",\"turns\":%d,\"tool_calls\":%d,"
           "\"successful_tools\":%d,\"failed_tools\":%d,\"tokens_in\":%lld,\"tokens_out\":%lld,\"duration_ms\":%lld}\n",
           scenario, ok ? "pass" : "fail",
           result != NULL && result->status != NULL ? result->status : "n/a",
           result != NULL ? result->turns : -1,
           result != NULL ? result->tool_calls : -1,
           result != NULL ? result->successful_tools : -1,
           result != NULL ? result->failed_tools : -1,
           result != NULL ? result->tokens_in : -1,
           result != NULL ? result->tokens_out : -1,
           result != NULL ? result->duration_ms : -1);
}

typedef struct EvalFixture { int calls; int saw_gate; int saw_notice; int saw_tail; int saw_sub_report; } EvalFixture;

static char *eval_response_write(const char *id, const char *path, const char *content) {
    return zeno_format("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"%s\",\"type\":\"function\",\"function\":{\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"%s\\\",\\\"content\\\":\\\"%s\\\"}\"}}]}}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}", id, path, content);
}

static char *eval_response_final(const char *text) {
    return zeno_format("{\"choices\":[{\"message\":{\"content\":\"%s\"}}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}", text);
}

static ZenoSandbox *eval_sandbox_new(const char *root) {
    ZenoSandboxPolicy policy = {root, 0, "", 1, 3000, 4096, 2000, 2, 10000, 10000, 0};
    return zeno_sandbox_create(&policy);
}

/* Transports ---------------------------------------------------------------- */

static int eval_transport_gate(void *context, const char *base_url, const char *api_key,
                               const char *body, int timeout_ms, char **response,
                               char **error) {
    EvalFixture *fixture = (EvalFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL && strstr(body, "RUNTIME VERIFICATION GATE") != NULL) fixture->saw_gate = 1;
    *response = fixture->calls == 1
        ? eval_response_write("g1", "nested/eval.txt", "eval body")
        : eval_response_final("done");
    return 1;
}

static int eval_big_tool(void *context, const char *args, char **output, char **error) {
    (void)context; (void)args; (void)error;
    char *filler = (char *)calloc(1201, 1);
    if (filler != NULL) memset(filler, 'x', 1200);
    *output = zeno_format("HEADMARKER_ABCDEFGHIJKLMNOP\n%s\nTAILMARKER_ZYXWVUTSRQPONMLK", filler != NULL ? filler : "");
    free(filler);
    return 1;
}

static int eval_transport_loop_recovery(void *context, const char *base_url, const char *api_key,
                                        const char *body, int timeout_ms, char **response,
                                        char **error) {
    EvalFixture *fixture = (EvalFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL) {
        if (strstr(body, "TAILMARKER_ZYXWVUTSRQPONMLK") != NULL) fixture->saw_tail = 1;
        if (strstr(body, "RUNTIME LOOP NOTICE") != NULL) fixture->saw_notice = 1;
    }
    *response = fixture->calls <= 4
        ? zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"lr1\",\"type\":\"function\",\"function\":{\"name\":\"big_tool\",\"arguments\":{}}}]}}]}")
        : eval_response_final("recovered");
    return 1;
}

static int eval_transport_loop_stuck(void *context, const char *base_url, const char *api_key,
                                     const char *body, int timeout_ms, char **response,
                                     char **error) {
    (void)context; (void)base_url; (void)api_key; (void)body; (void)timeout_ms; (void)error;
    *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"ls1\",\"type\":\"function\",\"function\":{\"name\":\"big_tool\",\"arguments\":{}}}]}}]}");
    return 1;
}

static int eval_transport_subagent(void *context, const char *base_url, const char *api_key,
                                   const char *body, int timeout_ms, char **response,
                                   char **error) {
    EvalFixture *fixture = (EvalFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL && strstr(body, "SUB_REPORT_OK") != NULL) fixture->saw_sub_report = 1;
    if (fixture->calls == 1)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"sa1\",\"type\":\"function\",\"function\":{\"name\":\"spawn_subagent\",\"arguments\":\"{\\\"task\\\":\\\"research the frobnicator\\\"}\"}}]}}]}");
    else if (fixture->calls == 2)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"SUB_REPORT_OK\"}}]}");
    else
        *response = eval_response_final("orchestration done");
    return 1;
}

static int eval_transport_edit_chain(void *context, const char *base_url, const char *api_key,
                                     const char *body, int timeout_ms, char **response,
                                     char **error) {
    EvalFixture *fixture = (EvalFixture *)context;
    (void)base_url; (void)api_key; (void)timeout_ms; (void)error;
    fixture->calls++;
    if (body != NULL && strstr(body, "RUNTIME VERIFICATION GATE") != NULL) fixture->saw_gate = 1;
    if (fixture->calls == 1)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"ec1\",\"type\":\"function\",\"function\":{\"name\":\"replace_in_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"nested/chain.txt\\\",\\\"old_text\\\":\\\"beta\\\",\\\"new_text\\\":\\\"BETA\\\"}\"}}]}}]}");
    else if (fixture->calls == 2)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"ec2\",\"type\":\"function\",\"function\":{\"name\":\"read_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"nested/chain.txt\\\"}\"}}]}}]}");
    else if (fixture->calls == 3)
        *response = zeno_strdup("{\"choices\":[{\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"ec3\",\"type\":\"function\",\"function\":{\"name\":\"replace_in_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"nested/chain.txt\\\",\\\"old_text\\\":\\\"beta\\\",\\\"new_text\\\":\\\"BETA\\\"}\"}}]}}]}");
    else
        *response = eval_response_final("chain done");
    return 1;
}

/* Scenarios ----------------------------------------------------------------- */

static ZenoRouter *eval_router_new(void *transport_context, ZenoTransport transport) {
    ZenoRouter *router = zeno_router_create();
    ZenoProviderConfig provider = {"test", "mock://test", "", "test-model", 1, 3, 1000};
    zeno_router_add_provider(router, provider);
    zeno_router_set_transport(router, transport, transport_context);
    return router;
}

static void eval_agent_options(ZenoAgentOptions *options, ZenoRegistry *registry, ZenoRouter *router,
                               ZenoMemory *memory, ZenoSandbox *sandbox, ZenoApproval *approval,
                               const char *runs_path, int max_turns, size_t max_tool_output) {
    memset(options, 0, sizeof(*options));
    options->registry = registry; options->router = router; options->memory = memory;
    options->sandbox = sandbox; options->approval = approval; options->model = "test-model";
    options->runs_dir = runs_path; options->max_turns = max_turns;
    options->max_history_chars = 120000; options->max_tool_output_chars = max_tool_output;
}

static ZenoRunOptions eval_run_options(int max_turns) {
    ZenoRunOptions run; memset(&run, 0, sizeof(run));
    run.model = "test-model"; run.max_turns = max_turns; run.max_tokens = 500; run.temperature = 0.1;
    return run;
}

static void eval_teardown(ZenoMemory *memory, ZenoSandbox *sandbox, ZenoRegistry *registry,
                          ZenoRouter *router, ZenoApproval *approval, char *memory_path, char *runs_path) {
    zeno_router_destroy(router); zeno_approval_destroy(approval); zeno_registry_destroy(registry);
    zeno_sandbox_destroy(sandbox); zeno_memory_destroy(memory); free(memory_path); free(runs_path);
}

/* S1: writes then declares done -> verification gate gives one extra round. */
static void scenario_verification_gate(const char *root) {
    char *memory_path = zeno_join_path(root, "m.md"), *runs_path = zeno_join_path(root, "runs");
    ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandbox *sandbox = eval_sandbox_new(root);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    EVAL_CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    EvalFixture fixture = {0};
    ZenoRouter *router = eval_router_new(&fixture, eval_transport_gate);
    ZenoAgentOptions options; eval_agent_options(&options, registry, router, memory, sandbox, approval, runs_path, 8, 20000);
    ZenoAgent *agent = zeno_agent_create(&options);
    ZenoRunOptions run = eval_run_options(8);
    ZenoAgentResult result = zeno_agent_run(agent, "s1", "write then finish", &run);
    int ok = strcmp(result.status, "completed") == 0 && fixture.calls == 3 && fixture.saw_gate == 1 && result.turns >= 3;
    eval_report("verification_gate", ok, &result); EVAL_CHECK(ok);
    zeno_agent_result_free(&result); zeno_agent_destroy(agent);
    eval_teardown(memory, sandbox, registry, router, approval, memory_path, runs_path);
}

/* S2: blind loop of identical reads -> corrective notice -> recovery. */
static void scenario_loop_recovery(const char *root) {
    char *memory_path = zeno_join_path(root, "m.md"), *runs_path = zeno_join_path(root, "runs");
    ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandbox *sandbox = eval_sandbox_new(root);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    EVAL_CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    ZenoToolDefinition big = {"big_tool", "Large diagnostic output.", "{}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    EVAL_CHECK(zeno_registry_register(registry, big, eval_big_tool, NULL));
    EvalFixture fixture = {0};
    ZenoRouter *router = eval_router_new(&fixture, eval_transport_loop_recovery);
    ZenoAgentOptions options; eval_agent_options(&options, registry, router, memory, sandbox, approval, runs_path, 8, 300);
    ZenoAgent *agent = zeno_agent_create(&options);
    ZenoRunOptions run = eval_run_options(8);
    ZenoAgentResult result = zeno_agent_run(agent, "s2", "repeat a diagnostic read", &run);
    int ok = strcmp(result.status, "completed") == 0 && fixture.calls == 5 && fixture.saw_tail == 1 && fixture.saw_notice == 1;
    eval_report("loop_recovery", ok, &result); EVAL_CHECK(ok);
    zeno_agent_result_free(&result); zeno_agent_destroy(agent);
    eval_teardown(memory, sandbox, registry, router, approval, memory_path, runs_path);
}

/* S3: endless blind loop -> runtime stops it at the threshold, inside budget. */
static void scenario_loop_stuck(const char *root) {
    char *memory_path = zeno_join_path(root, "m.md"), *runs_path = zeno_join_path(root, "runs");
    ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandbox *sandbox = eval_sandbox_new(root);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    EVAL_CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    ZenoToolDefinition big = {"big_tool", "Large diagnostic output.", "{}", ZENO_EFFECT_READ_LOCAL, 0, 1000, 0};
    EVAL_CHECK(zeno_registry_register(registry, big, eval_big_tool, NULL));
    EvalFixture fixture = {0};
    ZenoRouter *router = eval_router_new(&fixture, eval_transport_loop_stuck);
    ZenoAgentOptions options; eval_agent_options(&options, registry, router, memory, sandbox, approval, runs_path, 12, 20000);
    ZenoAgent *agent = zeno_agent_create(&options);
    ZenoRunOptions run = eval_run_options(12);
    ZenoAgentResult result = zeno_agent_run(agent, "s3", "loop forever", &run);
    int ok = strcmp(result.status, "failed") == 0 && result.response != NULL && strstr(result.response, "Loop detected") != NULL && result.turns <= 6;
    eval_report("loop_stuck", ok, &result); EVAL_CHECK(ok);
    zeno_agent_result_free(&result); zeno_agent_destroy(agent);
    eval_teardown(memory, sandbox, registry, router, approval, memory_path, runs_path);
}

/* S4: edit-before-read blocked, self-corrects, verification gate follows. */
static void scenario_edit_discipline(const char *root) {
    char *memory_path = zeno_join_path(root, "m.md"), *runs_path = zeno_join_path(root, "runs");
    ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandbox *sandbox = eval_sandbox_new(root);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    EVAL_CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    char *seeded = zeno_workspace_write(root, "nested/chain.txt", "alpha\nbeta\ngamma");
    EVAL_CHECK(seeded != NULL); free(seeded);
    EvalFixture fixture = {0};
    ZenoRouter *router = eval_router_new(&fixture, eval_transport_edit_chain);
    ZenoAgentOptions options; eval_agent_options(&options, registry, router, memory, sandbox, approval, runs_path, 10, 20000);
    ZenoAgent *agent = zeno_agent_create(&options);
    ZenoRunOptions run = eval_run_options(10);
    ZenoAgentResult result = zeno_agent_run(agent, "s4", "edit the file", &run);
    int ok = strcmp(result.status, "completed") == 0 && result.failed_tools == 1 && result.successful_tools == 2 && fixture.saw_gate == 1;
    eval_report("edit_discipline", ok, &result); EVAL_CHECK(ok);
    zeno_agent_result_free(&result); zeno_agent_destroy(agent);
    eval_teardown(memory, sandbox, registry, router, approval, memory_path, runs_path);
}

/* S5: parent delegates research to a scoped subagent; report flows back. */
static void scenario_subagent_delegation(const char *root) {
    char *memory_path = zeno_join_path(root, "m.md"), *runs_path = zeno_join_path(root, "runs");
    ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandbox *sandbox = eval_sandbox_new(root);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    EVAL_CHECK(zeno_registry_register_builtins(registry, sandbox, memory, root));
    EvalFixture fixture = {0};
    ZenoRouter *router = eval_router_new(&fixture, eval_transport_subagent);
    ZenoAgentOptions options; eval_agent_options(&options, registry, router, memory, sandbox, approval, runs_path, 8, 20000);
    ZenoAgent *agent = zeno_agent_create(&options);
    ZenoRunOptions run = eval_run_options(8);
    ZenoAgentResult result = zeno_agent_run(agent, "s5", "delegate then report", &run);
    int ok = strcmp(result.status, "completed") == 0 && fixture.calls == 3 && fixture.saw_sub_report == 1;
    eval_report("subagent_delegation", ok, &result); EVAL_CHECK(ok);
    zeno_agent_result_free(&result); zeno_agent_destroy(agent);
    eval_teardown(memory, sandbox, registry, router, approval, memory_path, runs_path);
}

/* S6: minimal profile still completes the precision contract end-to-end. */
static void scenario_minimal_profile(const char *root) {
    char *memory_path = zeno_join_path(root, "m.md"), *runs_path = zeno_join_path(root, "runs");
    ZenoMemory *memory = zeno_memory_create(memory_path); ZenoSandbox *sandbox = eval_sandbox_new(root);
    ZenoRegistry *registry = zeno_registry_create(); ZenoApproval *approval = zeno_approval_create();
    EVAL_CHECK(zeno_registry_register_minimal(registry, sandbox, memory, root));
    EvalFixture fixture = {0};
    ZenoRouter *router = eval_router_new(&fixture, eval_transport_gate);
    ZenoAgentOptions options; eval_agent_options(&options, registry, router, memory, sandbox, approval, runs_path, 0, 0);
    options.mode = ZENO_AGENT_MODE_MINIMAL;
    ZenoAgent *agent = zeno_agent_create(&options);
    ZenoRunOptions run = eval_run_options(0);
    ZenoAgentResult result = zeno_agent_run(agent, "s6", "create the file", &run);
    int ok = strcmp(result.status, "completed") == 0 && result.tool_calls == 1 && fixture.calls >= 3;
    eval_report("minimal_profile", ok, &result); EVAL_CHECK(ok);
    zeno_agent_result_free(&result); zeno_agent_destroy(agent);
    eval_teardown(memory, sandbox, registry, router, approval, memory_path, runs_path);
}

int main(void) {
    static void (*scenarios[])(const char *) = {
        scenario_verification_gate,
        scenario_loop_recovery,
        scenario_loop_stuck,
        scenario_edit_discipline,
        scenario_subagent_delegation,
        scenario_minimal_profile,
    };
    for (size_t index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]); index++) {
        char *root = eval_root();
        if (root == NULL) { eval_failures++; continue; }
        scenarios[index](root);
        eval_remove_tree(root);
        free(root);
    }
    if (eval_failures != 0) { fprintf(stderr, "%d eval expectation(s) failed.\n", eval_failures); return 1; }
    puts("ZenoC eval: all scenarios passed");
    return 0;
}
