#ifndef ZENO_H
#define ZENO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZENO_VERSION "1.3.0"

typedef struct ZenoRegistry ZenoRegistry;
typedef struct ZenoSandbox ZenoSandbox;
typedef struct ZenoMemory ZenoMemory;
typedef struct ZenoCache ZenoCache;
typedef struct ZenoTrace ZenoTrace;
typedef struct ZenoApproval ZenoApproval;
typedef struct ZenoRouter ZenoRouter;
typedef struct ZenoAgent ZenoAgent;
typedef struct ZenoMarketplace ZenoMarketplace;

typedef enum ZenoEffect {
    ZENO_EFFECT_READ_LOCAL = 0,
    ZENO_EFFECT_WRITE_LOCAL,
    ZENO_EFFECT_READ_EXTERNAL,
    ZENO_EFFECT_WRITE_EXTERNAL,
    ZENO_EFFECT_PROCESS,
    ZENO_EFFECT_BROWSER
} ZenoEffect;

typedef enum ZenoRunStatus {
    ZENO_RUN_COMPLETED = 0,
    ZENO_RUN_WAITING_APPROVAL,
    ZENO_RUN_FAILED,
    ZENO_RUN_CANCELLED,
    ZENO_RUN_DEADLINE
} ZenoRunStatus;

/* Minimal mode is the precision coding profile: a four-tool surface
 * (read/write/replace/run), tighter budgets and a terse system prompt for
 * speed and low error margin. Full mode registers every builtin tool. */
typedef enum ZenoAgentMode {
    ZENO_AGENT_MODE_FULL = 0,
    ZENO_AGENT_MODE_MINIMAL = 1
} ZenoAgentMode;

typedef struct ZenoConfig {
    char openai_api_key[512];
    char openai_base_url[512];
    char fireworks_api_key[512];
    char fireworks_base_url[512];
    char composio_api_key[512];
    char model_id[128];
    char fallback_models[512];
    char ollama_url[512];
    char ollama_model[128];
    char workspace_root[1024];
    char runs_dir[1024];
    char trace_file[1024];
    int api_port;
    int llm_timeout_ms;
    int max_agent_turns;
    int require_approval;
    int absolute_mode;
    int enable_llm_cache;
    int enable_history_summary;
    int use_ollama;
    int use_fireworks;
    int caveman_mode;
    int sandbox_strict;
    int sandbox_allow_shell_operators;
    int sandbox_allow_network;
    int sandbox_timeout_ms;
    int sandbox_max_output_chars;
    int sandbox_max_command_chars;
    int sandbox_max_jobs;
    int sandbox_max_job_output_chars;
    int sandbox_max_job_runtime_ms;
    int agent_mode;
} ZenoConfig;

void zeno_config_default(ZenoConfig *config);
int zeno_config_load_env(ZenoConfig *config);
int zeno_config_validate(const ZenoConfig *config, int require_telegram,
                        char *error, size_t error_size);
char *zeno_config_public_json(const ZenoConfig *config);

void zeno_free(void *ptr);
char *zeno_read_file(const char *path, size_t max_chars);
int zeno_write_file_atomic(const char *path, const char *content);
char *zeno_markdown_read_json(const char *path, const char *fallback_json);
int zeno_markdown_write_json(const char *path, const char *title,
                             const char *json);
char *zeno_compress_task(const char *task, size_t max_chars);
const char *zeno_caveman_protocol(void);
char *zeno_build_caveman_prompt(int enabled);
char *zeno_build_caveman_summary(const char *task, const char *status,
                                 const char *files_csv, int turns,
                                 double latency_ms);

ZenoTrace *zeno_trace_create(const char *file_path);
void zeno_trace_destroy(ZenoTrace *trace);
char *zeno_trace_start(ZenoTrace *trace, const char *name, const char *kind,
                       const char *trace_id, const char *parent_id,
                       const char *attributes_json);
void zeno_trace_end(ZenoTrace *trace, const char *span_id, const char *status,
                    const char *extra_json);
char *zeno_trace_get_json(const ZenoTrace *trace, size_t limit);

ZenoCache *zeno_cache_create(const char *path, int ttl_seconds, size_t max_entries);
void zeno_cache_destroy(ZenoCache *cache);
int zeno_cache_get(ZenoCache *cache, const char *key, char **value);
int zeno_cache_set(ZenoCache *cache, const char *key, const char *value);
void zeno_cache_clear(ZenoCache *cache);
void zeno_cache_stats(const ZenoCache *cache, size_t *hits, size_t *misses,
                      size_t *size);
char *zeno_sha256_hex(const char *text);

ZenoMemory *zeno_memory_create(const char *path);
void zeno_memory_destroy(ZenoMemory *memory);
void zeno_memory_flush(ZenoMemory *memory);
int zeno_memory_add_turn(ZenoMemory *memory, const char *session_id,
                         const char *role, const char *content);
char *zeno_memory_search_conversations(const ZenoMemory *memory,
                                       const char *query, const char *session_id,
                                       size_t limit);
char *zeno_memory_context_prompt(const ZenoMemory *memory, const char *session_id,
                                 const char *user_id, const char *query,
                                 size_t max_items, size_t max_chars);
int zeno_memory_update_session(ZenoMemory *memory, const char *session_id,
                               const char *last_message, const char *last_response,
                               const char *run_id);
int zeno_memory_add_note(ZenoMemory *memory, const char *title,
                         const char *content, const char *kind,
                         const char *scope, const char *tags_json, char **id);
char *zeno_memory_search_notes(const ZenoMemory *memory, const char *query,
                               size_t limit);
char *zeno_memory_list_notes(const ZenoMemory *memory, const char *scope,
                             size_t limit);
int zeno_memory_set_preference(ZenoMemory *memory, const char *key,
                                const char *value, const char *scope);
char *zeno_memory_get_preference(const ZenoMemory *memory, const char *key);
char *zeno_memory_add_tool_sequence(ZenoMemory *memory, const char *session_id,
                                    const char *task, const char *tools_csv,
                                    const char *result);
char *zeno_memory_get_or_create_user(ZenoMemory *memory, const char *user_id);
int zeno_memory_update_user_profile(ZenoMemory *memory, const char *user_id,
                                    const char *user_message, const char *response,
                                    const char *tools_json);
char *zeno_memory_user_profile_context(ZenoMemory *memory, const char *user_id);

char *zeno_workspace_list(const char *workspace_root, const char *relative_path,
                          size_t max_entries);
char *zeno_workspace_read(const char *workspace_root, const char *relative_path,
                          size_t max_chars);
char *zeno_workspace_write(const char *workspace_root, const char *relative_path,
                           const char *content);
char *zeno_workspace_append(const char *workspace_root, const char *relative_path,
                            const char *content);
char *zeno_workspace_replace(const char *workspace_root, const char *relative_path,
                             const char *old_text, const char *new_text,
                             int replace_all);
char *zeno_workspace_create_directory(const char *workspace_root,
                                      const char *relative_path);
char *zeno_workspace_search(const char *workspace_root, const char *query,
                            const char *relative_path, size_t max_results);
char *zeno_workspace_glob(const char *workspace_root, const char *pattern,
                          const char *relative_path, size_t max_results);

ZenoApproval *zeno_approval_create(void);
void zeno_approval_destroy(ZenoApproval *approval);
int zeno_approval_needs(const ZenoApproval *approval, const char *tool_name,
                        ZenoEffect effect);
int zeno_approval_store(ZenoApproval *approval, const char *request_id,
                        const char *context_json);
char *zeno_approval_get(const ZenoApproval *approval, const char *request_id);
void zeno_approval_clear(ZenoApproval *approval, const char *request_id);

typedef struct ZenoSandboxPolicy {
    const char *workspace_root;
    int strict;
    const char *allowed_executables_csv;
    int allow_shell_operators;
    int default_timeout_ms;
    int max_output_chars;
    int max_command_chars;
    int max_concurrent_jobs;
    int max_background_output_chars;
    int max_background_runtime_ms;
    int allow_network_commands;
} ZenoSandboxPolicy;

typedef struct ZenoExecResult {
    int ok;
    char *output;
    int exit_code;
    int timed_out;
    int cancelled;
    int output_limited;
    int blocked;
    long long duration_ms;
    char *reason;
} ZenoExecResult;

ZenoSandbox *zeno_sandbox_create(const ZenoSandboxPolicy *policy);
void zeno_sandbox_destroy(ZenoSandbox *sandbox);
int zeno_sandbox_validate(const ZenoSandbox *sandbox, const char *command,
                         const char *cwd, char *reason, size_t reason_size);
ZenoExecResult zeno_sandbox_execute(ZenoSandbox *sandbox, const char *command,
                                     const char *cwd, int timeout_ms);
void zeno_exec_result_free(ZenoExecResult *result);
char *zeno_sandbox_health_json(const ZenoSandbox *sandbox);
char *zeno_sandbox_start_job(ZenoSandbox *sandbox, const char *command,
                             const char *name, const char *cwd);
char *zeno_sandbox_list_jobs(const ZenoSandbox *sandbox, int include_completed);
char *zeno_sandbox_tail_job(const ZenoSandbox *sandbox, const char *job_id,
                            size_t max_lines);
char *zeno_sandbox_cancel_job(ZenoSandbox *sandbox, const char *job_id);

typedef struct ZenoToolDefinition {
    const char *name;
    const char *description;
    const char *parameters_json;
    ZenoEffect effect;
    int requires_approval;
    int timeout_ms;
    int max_retries;
} ZenoToolDefinition;

typedef int (*ZenoToolHandler)(void *context, const char *args_json,
                               char **output, char **error);

/* Stable error taxonomy: consumers branch on codes, never on strings.
 * zeno_tool_error_code maps any error string (current or future) onto the
 * enum; unknown strings degrade to ZENO_TOOL_ERROR_GENERIC. */
typedef enum ZenoToolError {
    ZENO_TOOL_OK = 0,
    ZENO_TOOL_ERROR_GENERIC,
    ZENO_TOOL_ERROR_NOT_FOUND,
    ZENO_TOOL_ERROR_INVALID_ARGUMENTS,
    ZENO_TOOL_ERROR_TIMEOUT,
    ZENO_TOOL_ERROR_APPROVAL_REQUIRED,
    ZENO_TOOL_ERROR_READ_BEFORE_EDIT,
    ZENO_TOOL_ERROR_SANDBOX_BLOCKED,
    ZENO_TOOL_ERROR_SUBAGENT_LIMIT,
    ZENO_TOOL_ERROR_OUT_OF_MEMORY,
    ZENO_TOOL_ERROR_UNAVAILABLE
} ZenoToolError;

int zeno_tool_error_code(const char *error_string);

typedef struct ZenoToolResult {
    char *name;
    char *output;
    char *error;
    int ok;
    int attempts;
    long long latency_ms;
    int error_code; /* ZenoToolError; 0 (OK) when ok */
} ZenoToolResult;

ZenoRegistry *zeno_registry_create(void);
void zeno_registry_destroy(ZenoRegistry *registry);
int zeno_registry_register(ZenoRegistry *registry, ZenoToolDefinition definition,
                          ZenoToolHandler handler, void *context);
int zeno_registry_unregister(ZenoRegistry *registry, const char *name);
int zeno_registry_has(const ZenoRegistry *registry, const char *name);
int zeno_registry_requires_approval(const ZenoRegistry *registry,
                                    const char *name);
int zeno_registry_is_read_only(const ZenoRegistry *registry, const char *name);
char *zeno_registry_list_json(const ZenoRegistry *registry);
char *zeno_registry_openai_schema_json(const ZenoRegistry *registry);
ZenoToolResult zeno_registry_execute(ZenoRegistry *registry, const char *name,
                                     const char *args_json);
void zeno_tool_result_free(ZenoToolResult *result);
int zeno_registry_register_builtins(ZenoRegistry *registry,
                                    ZenoSandbox *sandbox, ZenoMemory *memory,
                                    const char *workspace_root);
int zeno_registry_register_minimal(ZenoRegistry *registry,
                                   ZenoSandbox *sandbox, ZenoMemory *memory,
                                   const char *workspace_root);

typedef struct ZenoProviderConfig {
    const char *id;
    const char *base_url;
    const char *api_key;
    const char *models_csv;
    int priority;
    int max_errors;
    int timeout_ms;
} ZenoProviderConfig;

typedef struct ZenoProviderStats {
    char id[128];
    size_t total_calls;
    size_t success_calls;
    size_t error_calls;
    double average_latency;
    size_t consecutive_errors;
    long long cooldown_until_ms;
    char last_error[256];
} ZenoProviderStats;

typedef int (*ZenoTransport)(void *context, const char *base_url,
                            const char *api_key, const char *body_json,
                            int timeout_ms, char **response_json,
                            char **error_message);
typedef void (*ZenoChunkCallback)(void *context, const char *chunk);

ZenoRouter *zeno_router_create(void);
void zeno_router_destroy(ZenoRouter *router);
int zeno_router_add_provider(ZenoRouter *router, ZenoProviderConfig provider);
void zeno_router_set_fallback_models(ZenoRouter *router, const char *models_csv);
void zeno_router_set_transport(ZenoRouter *router, ZenoTransport transport,
                               void *context);
void zeno_set_request_session(const char *session_id);
int zeno_router_has_providers(const ZenoRouter *router);
int zeno_router_complete(ZenoRouter *router, const char *preferred_model,
                         const char *messages_json, const char *tools_json,
                         double temperature, int max_tokens,
                         char **response_json, char **used_provider,
                         char **used_model, int *cache_hit);
int zeno_router_complete_stream(ZenoRouter *router, const char *preferred_model,
                                const char *messages_json, const char *tools_json,
                                double temperature, int max_tokens,
                                ZenoChunkCallback chunk, void *chunk_context,
                                char **response_json, char **used_provider,
                                char **used_model);
char *zeno_router_stats_json(const ZenoRouter *router);
int zeno_http_request(const char *url, const char *method, const char *headers_json,
                      const char *body, int timeout_ms, size_t max_chars,
                      char **response);
int zeno_scrape_url(const char *url, int timeout_ms, size_t max_chars,
                    char **response);

typedef struct ZenoAgentOptions {
    ZenoRegistry *registry;
    ZenoRouter *router;
    ZenoMemory *memory;
    ZenoSandbox *sandbox;
    ZenoCache *cache;
    ZenoTrace *trace;
    ZenoApproval *approval;
    const char *model;
    const char *runs_dir;
    int require_approval;
    int absolute_mode;
    int max_turns;
    size_t max_history_chars;
    size_t max_tool_output_chars;
    int mode;
    int caveman_off;
} ZenoAgentOptions;

typedef int (*ZenoApprovalCallback)(void *context, const char *request_id,
                                    const char *tool_name, const char *args_json,
                                    const char *reason);
typedef void (*ZenoEventCallback)(void *context, const char *event,
                                  const char *run_id, const char *session_id);

typedef struct ZenoRunOptions {

    const char *model;
    int max_turns;
    int max_tokens;
    double temperature;
    int use_cache;
    int require_approval;
    int stream;
    const char *resume_run_id;
    ZenoApprovalCallback approve;
    void *approve_context;
    ZenoEventCallback event;
    ZenoChunkCallback chunk;
    void *callback_context;
    /* Cooperative cancellation, checked between turns. Either mechanism
     * stops the run at the next safe boundary; nothing mid-tool. */
    int (*should_cancel)(void *cancel_context);
    void *cancel_context;
    /* Wall-clock budget for the whole run in milliseconds (0 = unlimited).
     * Checked at the same safe turn boundaries as cancellation. */
    int wall_clock_budget_ms;
    /* v1.3.0 hooks (optional): pre_tool returning 0 denies the call before
     * approval and execution; post_tool observes the outcome. */
    int (*pre_tool_hook)(void *context, const char *tool_name, const char *arguments_json);
    void (*post_tool_hook)(void *context, const char *tool_name, const char *arguments_json, const char *result_text, int ok);
    void *hook_context;
} ZenoRunOptions;

typedef struct ZenoAgentResult {
    char *response;
    char *run_id;
    char *status;
    char *approval_context_json;
    char *tool_logs_json;
    int approval_required;
    int turns;
    int tool_calls;
    int successful_tools;
    int failed_tools;
    int cache_hit;
    long long duration_ms;
    /* Token accounting aggregated over the run (provider-reported when available). */
    long long tokens_in;
    long long tokens_out;
    long long tokens_cached;
} ZenoAgentResult;

ZenoAgent *zeno_agent_create(const ZenoAgentOptions *options);
void zeno_agent_destroy(ZenoAgent *agent);
ZenoAgentResult zeno_agent_run(ZenoAgent *agent, const char *session_id,
                               const char *user_message,
                               const ZenoRunOptions *options);
/* Cooperative cancellation: flags the agent so active and subsequent runs
 * stop at the next turn boundary with status "cancelled". */
void zeno_agent_request_cancel(ZenoAgent *agent);
void zeno_agent_result_free(ZenoAgentResult *result);
int zeno_agent_can_parallelize(const ZenoRegistry *registry,
                               const char *tool_calls_json);
char *zeno_agent_health_json(const ZenoAgent *agent);
/* Runs independent tasks concurrently on real threads; results keep task
 * order. Callbacks passed through options must be thread-safe. */
char *zeno_agent_run_parallel(ZenoAgent *agent, const char *session_id,
                              const char *tasks_json, const ZenoRunOptions *options);
/* Runs a real squad: each role gets its own agent session (in parallel),
 * then a synthesis pass integrates the role outputs. Default roles:
 * coder,reviewer,tester. */
char *zeno_agent_run_squad(ZenoAgent *agent, const char *session_id,
                           const char *task, const char *roles_csv,
                           const ZenoRunOptions *options);
char *zeno_agent_run_with_plan(ZenoAgent *agent, const char *session_id,
                               const char *task, const ZenoRunOptions *options,
                               char **plan_json);

typedef struct ZenoPlan {
    char *objective;
    char *steps_json;
    char *risks_json;
} ZenoPlan;
ZenoPlan *zeno_plan_parse(const char *model_json, const char *task);
void zeno_plan_free(ZenoPlan *plan);
char *zeno_plan_to_json(const ZenoPlan *plan);
char *zeno_reflective_prompt(const char *task, const char *context,
                             const char *reflection, int step);

typedef struct ZenoSubAgentStats {
    char *id;
    char *agent_type;
    char *strain;
    char *status;
    int executions;
    int successes;
    int failures;
    double total_latency;
} ZenoSubAgentStats;
ZenoSubAgentStats *zeno_subagent_create(const char *agent_type,
                                        const char *parent_session);
void zeno_subagent_free(ZenoSubAgentStats *stats);
int zeno_subagent_record(ZenoSubAgentStats *stats, int success,
                         double latency_seconds, const char *tools_csv);
char *zeno_subagent_suggestions(const ZenoSubAgentStats *stats);

ZenoMarketplace *zeno_marketplace_create(void);
void zeno_marketplace_destroy(ZenoMarketplace *marketplace);
int zeno_marketplace_add_skill(ZenoMarketplace *marketplace, const char *id,
                               const char *name, const char *description,
                               const char *category);
int zeno_marketplace_activate(ZenoMarketplace *marketplace, const char *id);
int zeno_marketplace_deactivate(ZenoMarketplace *marketplace, const char *id);
char *zeno_marketplace_search(const ZenoMarketplace *marketplace,
                              const char *query);
char *zeno_marketplace_prompt(const ZenoMarketplace *marketplace);

char *zeno_git_status(const char *workspace_root);
char *zeno_git_diff(const char *workspace_root);
char *zeno_git_log(const char *workspace_root, int count);
char *zeno_git_checkpoint(const char *workspace_root, const char *message);
int zeno_git_is_repo(const char *workspace_root);
char *zeno_git_init(const char *workspace_root);
char *zeno_git_create_branch(const char *workspace_root, const char *name);
char *zeno_git_rollback(const char *workspace_root, int steps);
char *zeno_git_list_branches(const char *workspace_root);
char *zeno_codebase_structure(const char *workspace_root);
char *zeno_find_symbol(const char *workspace_root, const char *name);
char *zeno_detect_test_framework(const char *workspace_root);
char *zeno_run_tests_execute(const char *workspace_root, int timeout_ms);
char *zeno_assert_true(const char *description, const char *value,
                      const char *expected);
char *zeno_assert_contains(const char *description, const char *value,
                           const char *substring);
char *zeno_transcript_append(const char *workspace_root, const char *session_id,
                             const char *user_message, const char *response,
                             const char *tools_json);
char *zeno_transcript_summary(const char *workspace_root, size_t max_entries);
char *zeno_transcript_read(const char *workspace_root);
size_t zeno_transcript_count(const char *workspace_root);
char *zeno_transcript_tool_stats(const char *workspace_root);
char *zeno_generate_agents_summary(const char *workspace_root);
const char *zeno_detect_language(const char *file_name);
int zeno_learning_record(const char *path, const char *session_id,
                         const char *user_message, const char *tools_csv,
                         int success, char **skill_id);
char *zeno_learning_list(const char *path, size_t limit);
int zeno_evolution_record(const char *path, const char *agent_type,
                          const char *strain, int success, double latency,
                          const char *tools_csv);
char *zeno_evolution_best(const char *path, const char *agent_type);
size_t zeno_embed_text(const char *text, double *output, size_t dimensions);
double zeno_embedding_cosine(const double *left, const double *right,
                             size_t dimensions);
char *zeno_semantic_rank(const char *query, const char *candidates_json,
                        size_t limit);

int zeno_mcp_adapter_available(void);
int zeno_composio_adapter_available(const char *api_key);
int zeno_browser_adapter_available(void);
int zeno_vision_is_multimodal_json(const char *content_json);
char *zeno_browser_navigate(const char *url, int timeout_ms);
char *zeno_browser_info(void);
char *zeno_vision_analyze(const char *image_path, const char *prompt);
char *zeno_mcp_call(const char *server_url, const char *tool_name,
                    const char *args_json);
char *zeno_mcp_list_tools(const char *server_url);
char *zeno_composio_status(const char *api_key);
char *zeno_squad_run(const char *task, const char *roles_csv);
char *zeno_coding_agent_plan(const char *task);
char *zeno_install_skill_from_github(const char *repository,
                                     const char *destination);

#ifdef __cplusplus
}
#endif


/* --- v1.3.0: MCP stdio transport, skills, agent definitions, squad_ex, hooks --- */
char *zeno_mcp_call_stdio(const char *command, const char *tool_name, const char *args_json);
char *zeno_mcp_list_tools_stdio(const char *command);
char *zeno_skills_index_markdown(const char *skills_dir);
int zeno_register_skill_tool(ZenoRegistry *registry, const char *skills_dir);
int zeno_agent_attach_skills(ZenoAgent *agent, ZenoRegistry *registry, const char *skills_dir);
char *zeno_agent_run_squad_ex(ZenoAgent *agent, const char *session_id,
                                       const char *task, const char *roles_csv,
                                       const char *agents_dir, const ZenoRunOptions *options);

/* Agent definitions: one Markdown file per agent with YAML frontmatter
 * (name, description, tools, model) and the body as the role system prompt —
 * the same schema the AI Suite uses in its agents/ directory. */
typedef struct ZenoAgentDef {
    char *name;
    char *description;
    char *tools;
    char *model;
    char *system_prompt;
} ZenoAgentDef;
int zeno_load_agent_defs(const char *dir, ZenoAgentDef **out_defs, size_t *out_count);
void zeno_agent_defs_free(ZenoAgentDef *defs, size_t count);

/* --- Zeno Studio backend (pure C11): voice, settings, projects, remotes, plugins ---
 * File-backed stores using the same Markdown+JSON envelope as memory/cache.
 * No network is performed here: STT/TTS helpers build public API URLs and
 * parse provider JSON so the browser/native shell does the audio fetch with
 * the user key, while `zenoc` CLI validates and prints curl equivalents.
 * Voice mode contract: entering voice must NOT create a textual chat session;
 * the agent greeting is spoken automatically via TTS (auto_speak=1). */
typedef struct ZenoVoiceConfig {
    char provider[16];
    char voice[64];
    char language[16];
    char stt_model[64];
    char tts_model[64];
    int auto_speak;
} ZenoVoiceConfig;

void zeno_voice_config_default(ZenoVoiceConfig *config);
int zeno_voice_config_load_env(ZenoVoiceConfig *config);
char *zeno_voice_config_json(const ZenoVoiceConfig *config);
int zeno_voice_config_save(const char *store_path, const ZenoVoiceConfig *config);
int zeno_voice_config_load(const char *store_path, ZenoVoiceConfig *config);
char *zeno_voice_greeting(const ZenoVoiceConfig *config);
char *zeno_voice_stt_url(const ZenoVoiceConfig *config, const char *lang_override);
char *zeno_voice_tts_url(const ZenoVoiceConfig *config, const char *voice_override);
char *zeno_voice_transcript_extract(const char *provider_json);
char *zeno_voice_describe(const ZenoVoiceConfig *config);

char *zeno_settings_defaults_json(void);
char *zeno_settings_list(const char *store_path);
char *zeno_settings_get(const char *store_path, const char *section, const char *key);
int zeno_settings_set(const char *store_path, const char *section, const char *key,
                      const char *value_text);

char *zeno_projects_list(const char *store_path);
char *zeno_project_create(const char *store_path, const char *name,
                          const char *folders_csv);
int zeno_project_rename(const char *store_path, const char *id, const char *new_name);
int zeno_project_delete(const char *store_path, const char *id);
int zeno_project_set_folders(const char *store_path, const char *id,
                             const char *folders_csv);

char *zeno_remotes_list(const char *store_path);
char *zeno_remote_add(const char *store_path, const char *type, const char *name,
                      const char *target);
int zeno_remote_remove(const char *store_path, const char *id);
int zeno_remote_rename(const char *store_path, const char *id, const char *new_name);
char *zeno_remote_connect_cmd(const char *store_path, const char *id);

int zeno_plugin_validate(const char *plugin_json, char *error, size_t error_size);
char *zeno_plugin_create(const char *user_request, const char *name_hint);
int zeno_plugin_save(const char *plugins_dir, const char *plugin_json);
char *zeno_plugin_list(const char *plugins_dir);
int zeno_plugin_set_enabled(const char *plugins_dir, const char *id, int enabled);
int zeno_plugin_rename(const char *plugins_dir, const char *id, const char *new_name);
char *zeno_plugin_apply(const char *plugins_dir);
const char *zeno_plugin_guide(void);
int zeno_register_studio_tools(ZenoRegistry *registry, const char *plugins_dir);
#endif
