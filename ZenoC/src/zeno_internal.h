#ifndef ZENO_INTERNAL_H
#define ZENO_INTERNAL_H

#include "zeno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum ZjType { ZJ_NULL, ZJ_BOOL, ZJ_NUMBER, ZJ_STRING, ZJ_OBJECT, ZJ_ARRAY } ZjType;
typedef struct ZjNode ZjNode;
typedef struct ZjPair {
    char *key;
    ZjNode *value;
    struct ZjPair *next;
} ZjPair;
struct ZjNode {
    ZjType type;
    char *string;
    double number;
    int boolean;
    ZjPair *object;
    ZjNode **items;
    size_t count;
};

ZjNode *zj_parse(const char *text, char **error);
void zj_free(ZjNode *node);
ZjNode *zj_object_get(const ZjNode *node, const char *key);
ZjNode *zj_array_get(const ZjNode *node, size_t index);
const char *zj_string(const ZjNode *node);
int zj_bool(const ZjNode *node, int fallback);
long long zj_integer(const ZjNode *node, long long fallback);
char *zj_stringify(const ZjNode *node);
char *zj_stringify_compact(const ZjNode *node);
int zj_object_has(const ZjNode *node, const char *key);
int zeno_json_get_string(const char *json, const char *key, char *out, size_t out_size);
long zeno_json_get_int(const char *json, const char *key, long fallback);
int zeno_json_get_bool(const char *json, const char *key, int fallback);
char *zeno_json_get_path_string(const char *json, const char *path);
char *zeno_json_escape(const char *text);

char *zeno_strdup(const char *text);
char *zeno_strndup(const char *text, size_t length);
char *zeno_format(const char *format, ...);
long long zeno_now_ms(void);
void zeno_sleep_ms(int milliseconds);
int zeno_copy_string(char *dst, size_t dst_size, const char *src);
void zeno_set_error(char *dst, size_t size, const char *format, ...);
char *zeno_trim_copy(const char *text);
char *zeno_lower_copy(const char *text);
int zeno_contains_ci(const char *text, const char *needle);
int zeno_path_inside(const char *root, const char *candidate, char **resolved);
int zeno_mkdirs(const char *path);
char *zeno_join_path(const char *left, const char *right);
char *zeno_json_array_append(char *array_json, const char *item_json);
char *zeno_json_object_string(const char *key, const char *value);
char *zeno_json_object_merge(const char *base_json, const char *extra_json);
char *zeno_json_array_empty(void);

int zeno_read_all(FILE *file, size_t max_chars, char **output, int *limited);
int zeno_write_text(FILE *file, const char *text);
char *zeno_timestamp_iso(void);
char *zeno_timestamp_id(void);
char *zeno_shell_quote(const char *text);

/* Portable concurrency primitives. Mutexes are recursive and lazily created,
 * so a zero-initialized ZenoMutex embedded in a calloc'd struct just works. */
typedef struct ZenoMutex { void *handle; } ZenoMutex;
void zeno_mutex_lock(ZenoMutex *mutex);
void zeno_mutex_unlock(ZenoMutex *mutex);
void zeno_mutex_destroy(ZenoMutex *mutex);
typedef void (*ZenoParallelFn)(void *context, size_t index);
int zeno_parallel_for(size_t count, size_t max_threads, ZenoParallelFn fn, void *context);

/* Platform process primitive used by sandbox and fixed Git helpers. */
int zeno_process_command(const char *command, const char *cwd, int timeout_ms,
                         size_t max_output, ZenoExecResult *result);
int zeno_process_background(const char *command, const char *cwd,
                            const char *log_path, int *pid_out, void **group_out);
int zeno_process_kill_ex(int pid, void *group);
void zeno_process_group_close(void *group);
int zeno_process_kill(int pid);
int zeno_process_alive(int pid);

typedef struct ZenoMemoryTurn {
    char *id;
    char *session_id;
    char *role;
    char *content;
    long long created_ms;
} ZenoMemoryTurn;
typedef struct ZenoMemoryNote {
    char *id;
    char *title;
    char *content;
    char *kind;
    char *scope;
    char *tags_json;
    long long created_ms;
} ZenoMemoryNote;
typedef struct ZenoMemoryPref {
    char *key;
    char *value;
    char *scope;
} ZenoMemoryPref;
typedef struct ZenoUserProfile {
    char *user_id;
    long long created_ms;
    long interactions;
    char preferred_language[16];
    char preferred_style[16];
    int auto_test;
    long direct_count;
    long detailed_count;
    long pt_count;
    long en_count;
    long test_yes_count;
    long test_no_count;
    char *frequent_tools;
    long long total_response_chars;
} ZenoUserProfile;

struct ZenoMemory {
    ZenoMutex lock;
    char *path;
    ZenoMemoryTurn *turns;
    size_t turn_count;
    ZenoMemoryNote *notes;
    size_t note_count;
    ZenoMemoryPref *prefs;
    size_t pref_count;
    ZenoUserProfile *profiles;
    size_t profile_count;
    unsigned long sequence;
};

typedef struct ZenoTraceSpan {
    char *trace_id;
    char *span_id;
    char *parent_id;
    char *name;
    char *kind;
    long long started_ms;
    long long ended_ms;
    char *status;
    char *attributes_json;
} ZenoTraceSpan;
struct ZenoTrace {
    ZenoMutex lock;
    char *file_path;
    ZenoTraceSpan *spans;
    size_t count;
    size_t capacity;
};

typedef struct ZenoCacheEntry {
    char *key;
    char *value;
    long long timestamp_ms;
} ZenoCacheEntry;
struct ZenoCache {
    ZenoMutex lock;
    char *path;
    int ttl_seconds;
    size_t max_entries;
    ZenoCacheEntry *entries;
    size_t count;
    size_t capacity;
    size_t hits;
    size_t misses;
};

struct ZenoApproval {
    ZenoMutex lock;
    char **ids;
    char **contexts;
    size_t count;
};

typedef struct ZenoJob {
    char *id;
    char *name;
    char *command;
    char *log_path;
    char *status;
    int pid;
    long long created_ms;
    long long finished_ms;
    void *process_group;
} ZenoJob;
struct ZenoSandbox {
    ZenoMutex lock;
    char *workspace_root;
    char *allowed_executables_csv;
    int strict;
    int allow_shell_operators;
    int default_timeout_ms;
    int max_output_chars;
    int max_command_chars;
    int max_concurrent_jobs;
    int max_background_output_chars;
    int max_background_runtime_ms;
    int allow_network_commands;
    ZenoJob *jobs;
    size_t job_count;
    unsigned long job_counter;
};

typedef struct ZenoRegisteredTool {
    char *name;
    char *description;
    char *parameters_json;
    ZenoEffect effect;
    int requires_approval;
    int timeout_ms;
    int max_retries;
    ZenoToolHandler handler;
    void *context;
} ZenoRegisteredTool;
struct ZenoRegistry {
    char *skills_dir;
    ZenoMutex lock;
    ZenoRegisteredTool *tools;
    size_t count;
    size_t capacity;
    void *owned_context;
};

typedef struct ZenoProvider {
    char *id;
    char *base_url;
    char *api_key;
    char *models_csv;
    int priority;
    int max_errors;
    int timeout_ms;
    ZenoProviderStats stats;
} ZenoProvider;
struct ZenoRouter {
    ZenoMutex lock;
    ZenoProvider *providers;
    size_t count;
    size_t capacity;
    char *fallback_models;
    ZenoTransport transport;
    void *transport_context;
};

struct ZenoAgent {
    ZenoMutex lock;
    ZenoAgentOptions options;
    char *model;
    char *runs_dir;
    ZenoRegistry *registry;
    ZenoRouter *router;
    ZenoMemory *memory;
    ZenoSandbox *sandbox;
    ZenoCache *cache;
    ZenoTrace *trace;
    ZenoApproval *approval;
    unsigned long message_counter;
    volatile int cancel_requested;
    char *skills_index;
    char *skills_dir;
};

typedef struct ZenoSkill {
    char *id;
    char *name;
    char *description;
    char *category;
    int active;
} ZenoSkill;
struct ZenoMarketplace {
    ZenoSkill *skills;
    size_t count;
    size_t capacity;
};

int zeno_registry_validate_args(const ZenoRegisteredTool *tool,
                                const char *args_json, char **error);
int zeno_registry_execute_handler(ZenoRegisteredTool *tool, const char *args_json,
                                  ZenoToolResult *result);
int zeno_tool_index(const ZenoRegistry *registry, const char *name);
int zeno_registry_register_minimal(ZenoRegistry *registry, ZenoSandbox *sandbox,
                                   ZenoMemory *memory, const char *workspace_root);
/* Binds a runtime instance to the builtins' shared tool context so model-
 * callable orchestration tools (spawn_subagent) can reach it. */
void zeno_tools_bind_agent(void *owned_context, void *agent);
/* Returns the persisted live plan (malloc'd) for prompt injection, or NULL. */
char *zeno_tools_current_plan(const void *owned_context);
char *zeno_transcript_append(const char *workspace_root, const char *session_id,
                             const char *user_message, const char *response,
                             const char *tools_json);
char *zeno_transcript_summary(const char *workspace_root, size_t max_entries);

int zeno_parse_completion(const char *response_json, char **content,
                          char **tool_calls_json);
char *zeno_build_messages_json(const char *system_prompt, const char *user_message,
                               const char *context_json);
char *zeno_build_system_prompt(const ZenoAgent *agent, const char *session_id,
                               const char *user_message);
char *zeno_tool_calls_fingerprint(const char *tool_name, const char *args_json);
int zeno_execute_tool_calls(ZenoAgent *agent, const char *session_id,
                            const char *run_id, const char *tool_calls_json,
                            const ZenoRunOptions *options, char **logs_json,
                            int *successful, int *failed, int *waiting);


int zeno_process_command_stdin(const char *command, const char *cwd, const char *stdin_input,
                               int timeout_ms, size_t max_output, ZenoExecResult *result);
#endif
