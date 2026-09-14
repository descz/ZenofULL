#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "zeno_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int sandbox_save_jobs(const ZenoSandbox *sandbox);

static int string_has_any(const char *text, const char *characters) {
    if (text == NULL || characters == NULL) return 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) if (strchr(characters, *cursor) != NULL) return 1;
    return 0;
}

static int command_blocked(const char *command) {
    static const char *patterns[] = {
        "rm -rf /", "rm -rf *", "rm -rf ~", "remove-item -recurse", "remove-item -force",
        "rmdir /s", "rmdir /q", "find . -delete", "xargs -delete", "format c:", "mkfs",
        "diskpart", "diskutil", "mount ", "umount ", "dd if=", "> /dev/sda", "> /dev/nvme",
        "shutdown", "reboot", "poweroff", "halt", "reg delete", "bcdedit", "git reset --hard",
        "git clean -f", "git checkout --", "encodedcommand", "| sh", "| bash", "| iex",
    };
    for (size_t index = 0; index < sizeof(patterns) / sizeof(patterns[0]); index++) if (zeno_contains_ci(command, patterns[index])) return 1;
    return 0;
}
static int network_command(const char *command) {
    static const char *patterns[] = {
        "curl", "wget", "invoke-webrequest", "invoke-restmethod", "ssh", "scp", "sftp",
        "netcat", " nslookup", "telnet", "ftp", "git clone", "git fetch", "git pull", "git push",
        "git remote", "npm install", "npm i ", "npm ci", "npm exec", "pnpm install", "pnpm add",
        "git remote", "npm install", "npm i ", "npm ci", "npm exec", "pnpm install", "pnpm add",
        "yarn install", "yarn add", "npx ", "pip install", "pip3 install", "cargo install",
        "cargo add", "cargo update", "go get",
    };
    for (size_t index = 0; index < sizeof(patterns) / sizeof(patterns[0]); index++) if (zeno_contains_ci(command, patterns[index])) return 1;
    return 0;
}

static char *extract_executable(const char *command) {
    const char *cursor = command;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) cursor++;
    char quote = '\0';
    if (*cursor == '\'' || *cursor == '"') quote = *cursor++;
    const char *start = cursor;
    while (*cursor != '\0' && ((quote != '\0' && *cursor != quote) || (quote == '\0' && !isspace((unsigned char)*cursor)))) cursor++;
    char *token = zeno_strndup(start, (size_t)(cursor - start));
    if (token == NULL) return NULL;
    char *slash = strrchr(token, '/');
    char *backslash = strrchr(token, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash != NULL) { char *copy = zeno_strdup(slash + 1); free(token); token = copy; }
    char *lower = zeno_lower_copy(token != NULL ? token : ""); free(token); token = lower;
    size_t length = token != NULL ? strlen(token) : 0;
    if (length > 4 && (strcmp(token + length - 4, ".exe") == 0 || strcmp(token + length - 4, ".cmd") == 0 || strcmp(token + length - 4, ".bat") == 0)) token[length - 4] = '\0';
    return token;
}

static void sandbox_load_jobs(ZenoSandbox *sandbox);
static void sandbox_refresh_jobs(ZenoSandbox *sandbox);

ZenoSandbox *zeno_sandbox_create(const ZenoSandboxPolicy *policy) {
    if (policy == NULL || policy->workspace_root == NULL) return NULL;
    (void)zeno_mkdirs(policy->workspace_root);
    ZenoSandbox *sandbox = (ZenoSandbox *)calloc(1, sizeof(*sandbox));
    if (sandbox == NULL) return NULL;
    char *root = NULL;
    if (!zeno_path_inside(policy->workspace_root, policy->workspace_root, &root)) { free(sandbox); return NULL; }
    sandbox->workspace_root = root;
    sandbox->strict = policy->strict ? 1 : 0;
    sandbox->allow_shell_operators = policy->allow_shell_operators && !sandbox->strict;
    sandbox->allowed_executables_csv = zeno_strdup(policy->allowed_executables_csv != NULL ? policy->allowed_executables_csv : "");
    sandbox->default_timeout_ms = policy->default_timeout_ms > 0 ? policy->default_timeout_ms : 120000;
    sandbox->max_output_chars = policy->max_output_chars > 0 ? policy->max_output_chars : 256000;
    sandbox->max_command_chars = policy->max_command_chars > 0 ? policy->max_command_chars : 32000;
    sandbox->max_concurrent_jobs = policy->max_concurrent_jobs > 0 ? policy->max_concurrent_jobs : 4;
    sandbox->max_background_output_chars = policy->max_background_output_chars > 0 ? policy->max_background_output_chars : 1000000;
    sandbox->max_background_runtime_ms = policy->max_background_runtime_ms > 0 ? policy->max_background_runtime_ms : 900000;
    sandbox->allow_network_commands = policy->allow_network_commands ? 1 : 0;
    sandbox_load_jobs(sandbox);
    return sandbox;
}

void zeno_sandbox_destroy(ZenoSandbox *sandbox) {
    if (sandbox == NULL) return;
    int changed = 0;
    for (size_t index = 0; index < sandbox->job_count; index++) {
        if (sandbox->jobs[index].status != NULL && strcmp(sandbox->jobs[index].status, "running") == 0) {
            if (sandbox->jobs[index].pid > 0) (void)zeno_process_kill_ex(sandbox->jobs[index].pid, sandbox->jobs[index].process_group);
            zeno_process_group_close(sandbox->jobs[index].process_group); sandbox->jobs[index].process_group = NULL;
            free(sandbox->jobs[index].status); sandbox->jobs[index].status = zeno_strdup("killed");
            sandbox->jobs[index].finished_ms = zeno_now_ms(); changed = 1;
        }
    }
    if (changed) (void)sandbox_save_jobs(sandbox);
    for (size_t index = 0; index < sandbox->job_count; index++) {
        zeno_process_group_close(sandbox->jobs[index].process_group);
        sandbox->jobs[index].process_group = NULL;
        free(sandbox->jobs[index].id); free(sandbox->jobs[index].name); free(sandbox->jobs[index].command);
        free(sandbox->jobs[index].log_path); free(sandbox->jobs[index].status);
    }
    free(sandbox->jobs); free(sandbox->workspace_root); free(sandbox->allowed_executables_csv);
    zeno_mutex_destroy(&sandbox->lock);
    free(sandbox);
}

int zeno_sandbox_validate(const ZenoSandbox *sandbox, const char *command,
                         const char *cwd, char *reason, size_t reason_size) {
    if (reason != NULL && reason_size > 0) reason[0] = '\0';
    char *trimmed = command != NULL ? zeno_trim_copy(command) : NULL;
    if (sandbox == NULL || trimmed == NULL || *trimmed == '\0') {
        zeno_set_error(reason, reason_size, "Command is empty.");
        free(trimmed);
        return 0;
    }
    if ((int)strlen(trimmed) > sandbox->max_command_chars) {
        zeno_set_error(reason, reason_size, "Command exceeds the %d-character sandbox limit.", sandbox->max_command_chars);
        free(trimmed); return 0;
    }
    if (!sandbox->allow_shell_operators && string_has_any(trimmed, ";&|<>`\r\n")) {
        zeno_set_error(reason, reason_size, "Shell operators are disabled by strict sandbox policy.");
        free(trimmed); return 0;
    }
    if (sandbox->allowed_executables_csv != NULL && sandbox->allowed_executables_csv[0] != '\0') {
        if (string_has_any(trimmed, ";&|<>`\r\n")) {
            zeno_set_error(reason, reason_size, "Shell operators cannot be combined with an executable allowlist.");
            free(trimmed); return 0;
        }
        char *executable = extract_executable(trimmed);
        int allowed = 0;
        char *list = zeno_strdup(sandbox->allowed_executables_csv);
        char *token = list != NULL ? strtok(list, ",") : NULL;
        while (token != NULL) {
            char *normalized = zeno_lower_copy(token); char *candidate = extract_executable(normalized != NULL ? normalized : "");
            char *clean = zeno_trim_copy(candidate != NULL ? candidate : "");
            if (clean != NULL && executable != NULL && strcmp(clean, executable) == 0) allowed = 1;
            free(normalized); free(candidate); free(clean); token = strtok(NULL, ",");
        }
        free(list);
        if (!allowed) {
            zeno_set_error(reason, reason_size, "Executable '%s' is not allowed by sandbox policy.", executable != NULL ? executable : "unknown");
            free(executable); free(trimmed); return 0;
        }
        free(executable);
    }
    char *resolved_cwd = NULL;
    if (!zeno_path_inside(sandbox->workspace_root, cwd != NULL ? cwd : sandbox->workspace_root, &resolved_cwd)) {
        zeno_set_error(reason, reason_size, "Working directory escapes workspace.");
        free(resolved_cwd); free(trimmed); return 0;
    }
    free(resolved_cwd);
    if (command_blocked(trimmed)) {
        zeno_set_error(reason, reason_size, "Command blocked by destructive-command policy.");
        free(trimmed); return 0;
    }
    if (!sandbox->allow_network_commands && network_command(trimmed)) {
        zeno_set_error(reason, reason_size, "Network-capable shell commands are disabled by sandbox policy.");
        free(trimmed); return 0;
    }
    free(trimmed);
    return 1;
}

static char *bounded_output(const char *output, size_t max_chars, int *limited) {
    const char *value = output != NULL ? output : "";
    size_t length = strlen(value);
    if (max_chars > 0 && length > max_chars) {
        if (limited != NULL) *limited = 1;
        return zeno_format("%.*s\n... [sandbox output truncated]", (int)max_chars, value);
    }
    if (limited != NULL) *limited = 0;
    return zeno_strdup(value);
}

ZenoExecResult zeno_sandbox_execute(ZenoSandbox *sandbox, const char *command,
                                     const char *cwd, int timeout_ms) {
    ZenoExecResult result;
    memset(&result, 0, sizeof(result)); result.exit_code = -1;
    long long started = zeno_now_ms();
    char reason[512];
    if (!zeno_sandbox_validate(sandbox, command, cwd, reason, sizeof(reason))) {
        result.blocked = 1; result.reason = zeno_strdup(reason); result.duration_ms = zeno_now_ms() - started; return result;
    }
    int effective_timeout = timeout_ms > 0 ? timeout_ms : sandbox->default_timeout_ms;
    if (effective_timeout > 15 * 60 * 1000) effective_timeout = 15 * 60 * 1000;
    (void)zeno_process_command(command, cwd != NULL ? cwd : sandbox->workspace_root, effective_timeout,
                               (size_t)sandbox->max_output_chars, &result);
    result.duration_ms = zeno_now_ms() - started;
    if (result.output == NULL) result.output = zeno_strdup("");
    return result;
}

void zeno_exec_result_free(ZenoExecResult *result) {
    if (result == NULL) return;
    free(result->output); free(result->reason); memset(result, 0, sizeof(*result));
}

static char *zeno_sandbox_health_json_locked(const ZenoSandbox *sandbox) {
    if (sandbox == NULL) return zeno_strdup("{}");
    sandbox_refresh_jobs((ZenoSandbox *)sandbox);
    size_t running = 0;
    for (size_t index = 0; index < sandbox->job_count; index++) if (strcmp(sandbox->jobs[index].status, "running") == 0) running++;
    return zeno_format("{\"workspace_root\":%s,\"mode\":\"%s\",\"allow_shell_operators\":%s,\"allow_network_commands\":%s,\"max_output_chars\":%d,\"max_command_chars\":%d,\"max_concurrent_jobs\":%d,\"running_jobs\":%zu}",
                       zeno_json_escape(sandbox->workspace_root), sandbox->strict ? "strict" : "balanced",
                       sandbox->allow_shell_operators ? "true" : "false", sandbox->allow_network_commands ? "true" : "false",
                       sandbox->max_output_chars, sandbox->max_command_chars, sandbox->max_concurrent_jobs, running);
}
char * zeno_sandbox_health_json(const ZenoSandbox *sandbox) { if (sandbox != NULL) zeno_mutex_lock((ZenoMutex *)&sandbox->lock); char * zeno_result = zeno_sandbox_health_json_locked(sandbox); if (sandbox != NULL) zeno_mutex_unlock((ZenoMutex *)&sandbox->lock); return zeno_result; }

static char *sandbox_jobs_path(const ZenoSandbox *sandbox) { return zeno_join_path(sandbox->workspace_root, ".Zeno_sandbox/jobs/index.md"); }

static int sandbox_save_jobs(const ZenoSandbox *sandbox) {
    if (sandbox == NULL) return 0;
    char *path = sandbox_jobs_path(sandbox);
    char *json = zeno_strdup("{\"jobs\":[");
    for (size_t index = 0; index < sandbox->job_count && json != NULL; index++) {
        const ZenoJob *job = &sandbox->jobs[index];
        char *item = zeno_format("%s{\"id\":%s,\"name\":%s,\"command\":%s,\"status\":%s,\"pid\":%d,\"created_ms\":%lld,\"finished_ms\":%lld,\"log_path\":%s}",
                                 index == 0 ? "" : ",", zeno_json_escape(job->id), zeno_json_escape(job->name), zeno_json_escape(job->command),
                                 zeno_json_escape(job->status), job->pid, job->created_ms, job->finished_ms, zeno_json_escape(job->log_path));
        char *next = item != NULL ? zeno_format("%s%s", json, item) : NULL;
        free(item); free(json); json = next;
    }
    char *final = json != NULL ? zeno_format("%s]}", json) : NULL; free(json); json = final;
    int ok = path != NULL && json != NULL && zeno_markdown_write_json(path, "Zeno Sandbox Jobs", json);
    free(path); free(json);
    return ok;
}

static void sandbox_load_jobs(ZenoSandbox *sandbox) {
    if (sandbox == NULL) return;
    char *path = sandbox_jobs_path(sandbox);
    char *json = path != NULL ? zeno_markdown_read_json(path, "{\"jobs\":[]}") : NULL;
    free(path);
    char *error = NULL; ZjNode *root = zj_parse(json != NULL ? json : "{}", &error); free(error); free(json);
    ZjNode *jobs = root != NULL ? zj_object_get(root, "jobs") : NULL;
    if (jobs != NULL && jobs->type == ZJ_ARRAY) for (size_t index = 0; index < jobs->count; index++) {
        ZjNode *item = jobs->items[index];
        const char *id = zj_string(zj_object_get(item, "id"));
        if (id == NULL) continue;
        ZenoJob *grown = (ZenoJob *)realloc(sandbox->jobs, (sandbox->job_count + 1) * sizeof(*grown));
        if (grown == NULL) break;
        sandbox->jobs = grown; ZenoJob *job = &sandbox->jobs[sandbox->job_count++]; memset(job, 0, sizeof(*job));
        job->id = zeno_strdup(id);
        job->name = zeno_strdup(zj_string(zj_object_get(item, "name")) != NULL ? zj_string(zj_object_get(item, "name")) : "");
        job->command = zeno_strdup(zj_string(zj_object_get(item, "command")) != NULL ? zj_string(zj_object_get(item, "command")) : "");
        job->status = zeno_strdup("failed");
        job->pid = (int)zj_integer(zj_object_get(item, "pid"), 0);
        job->created_ms = zj_integer(zj_object_get(item, "created_ms"), zeno_now_ms());
        job->finished_ms = zj_integer(zj_object_get(item, "finished_ms"), 0);
        job->log_path = zeno_strdup(zj_string(zj_object_get(item, "log_path")) != NULL ? zj_string(zj_object_get(item, "log_path")) : "");
    }
    zj_free(root);
}

static long long sandbox_file_size(const char *path) {
    if (path == NULL) return -1;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long position = ftell(file);
    fclose(file);
    return position >= 0 ? (long long)position : -1;
}

static void sandbox_refresh_jobs(ZenoSandbox *sandbox) {
    if (sandbox == NULL) return;
    int changed = 0;
    long long now = zeno_now_ms();
    for (size_t index = 0; index < sandbox->job_count; index++) {
        ZenoJob *job = &sandbox->jobs[index];
        if (strcmp(job->status, "running") != 0) continue;
        long long output_size = sandbox_file_size(job->log_path);
        if (sandbox->max_background_runtime_ms > 0 &&
            job->created_ms > 0 && now - job->created_ms > sandbox->max_background_runtime_ms) {
            if (job->pid > 0) (void)zeno_process_kill_ex(job->pid, job->process_group);
            zeno_process_group_close(job->process_group); job->process_group = NULL;
            free(job->status); job->status = zeno_strdup("timed_out"); job->finished_ms = now; changed = 1;
        } else if (sandbox->max_background_output_chars > 0 &&
                   output_size > sandbox->max_background_output_chars) {
            if (job->pid > 0) (void)zeno_process_kill_ex(job->pid, job->process_group);
            zeno_process_group_close(job->process_group); job->process_group = NULL;
            free(job->status); job->status = zeno_strdup("output_limit"); job->finished_ms = now; changed = 1;
        } else if (job->pid <= 0 || !zeno_process_alive(job->pid)) {
            zeno_process_group_close(job->process_group); job->process_group = NULL;
            free(job->status); job->status = zeno_strdup("failed"); job->finished_ms = now; changed = 1;
        }
    }
    if (changed) (void)sandbox_save_jobs(sandbox);
}

static char *zeno_sandbox_start_job_locked(ZenoSandbox *sandbox, const char *command,
                             const char *name, const char *cwd) {
    if (sandbox == NULL) return zeno_strdup("Error: sandbox unavailable.");
    char reason[512];
    if (!zeno_sandbox_validate(sandbox, command, cwd, reason, sizeof(reason))) return zeno_format("SANDBOX BLOCKED: %s", reason);
    size_t running = 0;
    for (size_t index = 0; index < sandbox->job_count; index++) if (strcmp(sandbox->jobs[index].status, "running") == 0) running++;
    if (running >= (size_t)sandbox->max_concurrent_jobs) return zeno_format("Error: sandbox concurrency limit reached (%d).", sandbox->max_concurrent_jobs);
    char *jobs_dir = zeno_join_path(sandbox->workspace_root, ".Zeno_sandbox/jobs");
    (void)zeno_mkdirs(jobs_dir);
    char *id = zeno_format("job_%lu_%lld", ++sandbox->job_counter, zeno_now_ms());
    char *log_path = id != NULL && jobs_dir != NULL ? zeno_join_path(jobs_dir, id) : NULL;
    char *log_with_ext = log_path != NULL ? zeno_format("%s.log", log_path) : NULL;
    if (log_with_ext != NULL) {
        FILE *file = fopen(log_with_ext, "wb");
        if (file != NULL) { (void)fprintf(file, "# Zeno Sandbox Job %s\n\nCommand: %s\n\n", id, command); (void)fclose(file); }
    }
    int pid = 0;
    void *process_group = NULL;
    int started = log_with_ext != NULL && zeno_process_background(command, cwd != NULL ? cwd : sandbox->workspace_root, log_with_ext, &pid, &process_group);
    if (!started) {
        free(jobs_dir); free(id); free(log_path); free(log_with_ext);
        return zeno_strdup("Error: could not start background job.");
    }
    if (sandbox->job_count == (size_t)-1 || sandbox->job_count + 1 > (size_t)-1 / sizeof(*sandbox->jobs)) {
        (void)zeno_process_kill_ex(pid, process_group); zeno_process_group_close(process_group); free(jobs_dir); free(id); free(log_path); free(log_with_ext); return zeno_strdup("Error: out of memory.");
    }
    char *job_name = zeno_strdup(name != NULL && *name != '\0' ? name : command);
    char *job_command = zeno_strdup(command);
    char *job_status = zeno_strdup("running");
    if (job_name == NULL || job_command == NULL || job_status == NULL) {
        free(job_name); free(job_command); free(job_status); (void)zeno_process_kill_ex(pid, process_group); zeno_process_group_close(process_group); free(jobs_dir); free(id); free(log_path); free(log_with_ext); return zeno_strdup("Error: out of memory.");
    }
    ZenoJob *grown = (ZenoJob *)realloc(sandbox->jobs, (sandbox->job_count + 1) * sizeof(*grown));
    if (grown == NULL) { free(job_name); free(job_command); free(job_status); (void)zeno_process_kill_ex(pid, process_group); zeno_process_group_close(process_group); free(jobs_dir); free(id); free(log_path); free(log_with_ext); return zeno_strdup("Error: out of memory."); }
    sandbox->jobs = grown; ZenoJob *job = &sandbox->jobs[sandbox->job_count++]; memset(job, 0, sizeof(*job));
    job->id = id; job->name = job_name; job->command = job_command;
    job->log_path = log_with_ext; job->status = job_status; job->pid = pid; job->process_group = process_group; job->created_ms = zeno_now_ms();
    (void)sandbox_save_jobs(sandbox);
    free(jobs_dir); free(log_path);
    return zeno_format("Job started: %s (%s)", job->id, job->name);
}
char * zeno_sandbox_start_job(ZenoSandbox *sandbox, const char *command, const char *name, const char *cwd) { if (sandbox != NULL) zeno_mutex_lock((ZenoMutex *)&sandbox->lock); char * zeno_result = zeno_sandbox_start_job_locked(sandbox, command, name, cwd); if (sandbox != NULL) zeno_mutex_unlock((ZenoMutex *)&sandbox->lock); return zeno_result; }

static char *zeno_sandbox_list_jobs_locked(const ZenoSandbox *sandbox, int include_completed) {
    if (sandbox == NULL || sandbox->job_count == 0) return zeno_strdup("No background jobs.");
    sandbox_refresh_jobs((ZenoSandbox *)sandbox);
    char *result = zeno_strdup("");
    for (size_t index = 0; index < sandbox->job_count; index++) {
        const ZenoJob *job = &sandbox->jobs[index];
        if (!include_completed && strcmp(job->status, "running") != 0) continue;
        char *line = zeno_format("%s: %s [%s] pid=%d\n", job->id, job->name, job->status, job->pid);
        char *next = line != NULL ? zeno_format("%s%s", result, line) : NULL; free(line); free(result); result = next;
    }
    return result != NULL && *result != '\0' ? result : zeno_strdup("No background jobs.");
}
char * zeno_sandbox_list_jobs(const ZenoSandbox *sandbox, int include_completed) { if (sandbox != NULL) zeno_mutex_lock((ZenoMutex *)&sandbox->lock); char * zeno_result = zeno_sandbox_list_jobs_locked(sandbox, include_completed); if (sandbox != NULL) zeno_mutex_unlock((ZenoMutex *)&sandbox->lock); return zeno_result; }

static char *zeno_sandbox_tail_job_locked(const ZenoSandbox *sandbox, const char *job_id, size_t max_lines) {
    if (sandbox == NULL || job_id == NULL) return zeno_strdup("Job not found.");
    sandbox_refresh_jobs((ZenoSandbox *)sandbox);
    const ZenoJob *job = NULL;
    for (size_t index = 0; index < sandbox->job_count; index++) if (strcmp(sandbox->jobs[index].id, job_id) == 0) { job = &sandbox->jobs[index]; break; }
    if (job == NULL) return zeno_format("Job not found: %s", job_id);
    size_t output_limit = sandbox->max_background_output_chars > 0
        ? (size_t)sandbox->max_background_output_chars : 0;
    char *text = zeno_read_file(job->log_path, output_limit);
    if (text == NULL) return zeno_format("Error reading log: %s", job->log_path);
    if (max_lines == 0) max_lines = 100;
    size_t lines = 1; for (char *cursor = text; *cursor != '\0'; cursor++) if (*cursor == '\n') lines++;
    if (lines > max_lines) {
        size_t skip = lines - max_lines; char *cursor = text;
        while (skip > 0 && (cursor = strchr(cursor, '\n')) != NULL) { cursor++; skip--; }
        char *tail = cursor != NULL ? zeno_strdup(cursor) : zeno_strdup(text); free(text); text = tail;
    }
    return text;
}
char * zeno_sandbox_tail_job(const ZenoSandbox *sandbox, const char *job_id, size_t max_lines) { if (sandbox != NULL) zeno_mutex_lock((ZenoMutex *)&sandbox->lock); char * zeno_result = zeno_sandbox_tail_job_locked(sandbox, job_id, max_lines); if (sandbox != NULL) zeno_mutex_unlock((ZenoMutex *)&sandbox->lock); return zeno_result; }

static char *zeno_sandbox_cancel_job_locked(ZenoSandbox *sandbox, const char *job_id) {
    if (sandbox == NULL || job_id == NULL) return zeno_strdup("Job not found.");
    for (size_t index = 0; index < sandbox->job_count; index++) {
        ZenoJob *job = &sandbox->jobs[index]; if (strcmp(job->id, job_id) != 0) continue;
        if (strcmp(job->status, "running") != 0) return zeno_format("Job %s is already %s.", job->id, job->status);
        (void)zeno_process_kill_ex(job->pid, job->process_group); zeno_process_group_close(job->process_group); job->process_group = NULL; free(job->status); job->status = zeno_strdup("killed"); job->finished_ms = zeno_now_ms();
        (void)sandbox_save_jobs(sandbox);
        return zeno_format("Job cancelled: %s", job->id);
    }
    return zeno_format("Job not found: %s", job_id);
}
char * zeno_sandbox_cancel_job(ZenoSandbox *sandbox, const char *job_id) { if (sandbox != NULL) zeno_mutex_lock((ZenoMutex *)&sandbox->lock); char * zeno_result = zeno_sandbox_cancel_job_locked(sandbox, job_id); if (sandbox != NULL) zeno_mutex_unlock((ZenoMutex *)&sandbox->lock); return zeno_result; }

/* The process primitive is deliberately argv-safe at the OS boundary: the
 * command string is passed to the platform shell only after sandbox policy. */
static int append_process_output(char **buffer, size_t *length, size_t *capacity,
                                 const char *data, size_t amount, size_t max_output,
                                 int *limited) {
    if (max_output > 0 && *length >= max_output) { if (limited != NULL) *limited = 1; return 0; }
    if (max_output > 0 && amount > max_output - *length) { amount = max_output - *length; if (limited != NULL) *limited = 1; }
    if (*length > (size_t)-1 - amount - 1) return 0;
    if (*length + amount + 1 > *capacity) {
        size_t next = *capacity == 0 ? 4096 : *capacity;
        while (next < *length + amount + 1) {
            if (next > (size_t)-1 / 2) return 0;
            next *= 2;
        }
        char *grown = (char *)realloc(*buffer, next); if (grown == NULL) return 0;
        *buffer = grown; *capacity = next;
    }
    memcpy(*buffer + *length, data, amount); *length += amount; (*buffer)[*length] = '\0'; return 1;
}

#ifdef _WIN32
static HANDLE create_kill_on_close_job(void) {
    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job == NULL) return NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    /* OS-enforced containment: the job outlives no one (kill on close) and no
     * single child process can allocate beyond the memory ceiling. */
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return NULL;
    }
    /* Deny the sandboxed tree access to user-facing desktop state: clipboard,
     * system/display settings, global atoms and other desktops. Console and
     * build workloads are unaffected. */
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui;
    memset(&ui, 0, sizeof(ui));
    /* No UILIMIT_DESKTOP: children share the caller's console, which lives on
     * a desktop outside the job, and that restriction would break every
     * command. The other UI surfaces are denied. */
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_WRITECLIPBOARD |
                             JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                             JOB_OBJECT_UILIMIT_GLOBALATOMS;
    (void)SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof(ui));
    return job;
}

int zeno_process_command(const char *command, const char *cwd, int timeout_ms,
                         size_t max_output, ZenoExecResult *result) {
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE read_pipe = NULL, write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return 0;
    (void)SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE job = create_kill_on_close_job();
    if (job == NULL) { CloseHandle(read_pipe); CloseHandle(write_pipe); result->reason = zeno_strdup("Could not create process containment job."); return 0; }
    char *line = zeno_format("cmd.exe /d /s /c %s", command);
    STARTUPINFOA startup; PROCESS_INFORMATION process; memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = write_pipe; startup.hStdError = write_pipe; startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    int created = line != NULL && CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &startup, &process);
    free(line); CloseHandle(write_pipe);
    if (!created) { CloseHandle(job); CloseHandle(read_pipe); result->reason = zeno_strdup("CreateProcess failed."); return 0; }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 126);
        CloseHandle(process.hProcess); CloseHandle(process.hThread); CloseHandle(job); CloseHandle(read_pipe);
        result->reason = zeno_strdup("Could not assign process to containment job."); return 0;
    }
    char *output = NULL; size_t length = 0, capacity = 0; int limited = 0; DWORD started = GetTickCount(); int finished = 0;
    while (!finished) {
        DWORD available = 0;
        if (PeekNamedPipe(read_pipe, NULL, 0, NULL, &available, NULL) && available > 0) {
            char chunk[4096]; DWORD read = 0; DWORD wanted = available < sizeof(chunk) ? available : (DWORD)sizeof(chunk);
            if (ReadFile(read_pipe, chunk, wanted, &read, NULL) && read > 0) (void)append_process_output(&output, &length, &capacity, chunk, (size_t)read, max_output, &limited);
        }
        DWORD wait = WaitForSingleObject(process.hProcess, 10);
        if (wait == WAIT_OBJECT_0) finished = 1;
        if ((int)(GetTickCount() - started) >= timeout_ms) { result->timed_out = 1; TerminateJobObject(job, 124); finished = 1; }
        if (limited) { result->output_limited = 1; TerminateJobObject(job, 125); finished = 1; }
    }
    for (;;) {
        char chunk[4096]; DWORD read = 0; if (!ReadFile(read_pipe, chunk, (DWORD)sizeof(chunk), &read, NULL) || read == 0) break;
        (void)append_process_output(&output, &length, &capacity, chunk, (size_t)read, max_output, &limited);
    }
    DWORD exit_code = 1; (void)GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess); CloseHandle(process.hThread); CloseHandle(read_pipe); CloseHandle(job);
    result->output = output != NULL ? output : zeno_strdup(""); result->exit_code = (int)exit_code; result->output_limited |= limited;
    result->ok = !result->timed_out && !result->output_limited && exit_code == 0;
    if (result->timed_out) result->reason = zeno_format("Command timed out after %dms.", timeout_ms);
    else if (result->output_limited) result->reason = zeno_strdup("Command output limit reached.");
    else if (!result->ok) result->reason = zeno_format("Command exited with code %lu.", (unsigned long)exit_code);
    return 1;
}

int zeno_process_command_stdin(const char *command, const char *cwd, const char *stdin_input,
                               int timeout_ms, size_t max_output, ZenoExecResult *result) {
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE out_read = NULL, out_write = NULL, in_read = NULL, in_write = NULL;
    if (!CreatePipe(&out_read, &out_write, &security, 0)) return 0;
    if (!CreatePipe(&in_read, &in_write, &security, 0)) { CloseHandle(out_read); CloseHandle(out_write); return 0; }
    (void)SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    (void)SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    HANDLE job = create_kill_on_close_job();
    if (job == NULL) {
        CloseHandle(out_read); CloseHandle(out_write); CloseHandle(in_read); CloseHandle(in_write);
        result->reason = zeno_strdup("Could not create process containment job.");
        return 0;
    }
    char *line = zeno_format("cmd.exe /d /s /c %s", command);
    STARTUPINFOA startup; PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = out_write; startup.hStdError = out_write; startup.hStdInput = in_read;
    int created = line != NULL && CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &startup, &process);
    free(line);
    /* in_write stays open here: the stdin feed loop below writes the request
     * through it and closes it once the payload is delivered. */
    CloseHandle(out_write); CloseHandle(in_read);
    if (!created) { CloseHandle(job); CloseHandle(out_read); result->reason = zeno_strdup("CreateProcess failed."); return 0; }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 126);
        CloseHandle(process.hProcess); CloseHandle(process.hThread); CloseHandle(job); CloseHandle(out_read);
        result->reason = zeno_strdup("Could not assign process to containment job.");
        return 0;
    }
    char *output = NULL; size_t length = 0, capacity = 0; int limited = 0;
    DWORD started = GetTickCount(); int finished = 0;
    size_t input_length = stdin_input != NULL ? strlen(stdin_input) : 0;
    size_t input_offset = 0; int input_done = input_length == 0; int input_failed = 0; int input_closed = 0;
    while (!finished) {
        /* Feed stdin in bounded chunks and pump stdout between writes so a
         * child that fills its output buffer cannot deadlock the caller. */
        while (!input_done && !input_failed) {
            DWORD chunk = (DWORD)((input_length - input_offset) > 1024 ? 1024 : (input_length - input_offset));
            DWORD wrote = 0;
            if (WriteFile(in_write, stdin_input + input_offset, chunk, &wrote, NULL) && wrote > 0) {
                input_offset += (size_t)wrote;
                if (input_offset >= input_length) input_done = 1;
            } else {
                input_failed = 1;
            }
        }
        if (input_done && !input_closed && !input_failed && in_write != NULL) { CloseHandle(in_write); in_write = NULL; input_closed = 1; }
        DWORD available = 0;
        if (PeekNamedPipe(out_read, NULL, 0, NULL, &available, NULL) && available > 0) {
            char chunk[4096]; DWORD read = 0; DWORD wanted = available < sizeof(chunk) ? available : (DWORD)sizeof(chunk);
            if (ReadFile(out_read, chunk, wanted, &read, NULL) && read > 0) (void)append_process_output(&output, &length, &capacity, chunk, (size_t)read, max_output, &limited);
        }
        DWORD wait = WaitForSingleObject(process.hProcess, 10);
        if (wait == WAIT_OBJECT_0) finished = 1;
        if ((int)(GetTickCount() - started) >= timeout_ms) { result->timed_out = 1; TerminateJobObject(job, 124); finished = 1; }
        if (limited) { result->output_limited = 1; TerminateJobObject(job, 125); finished = 1; }
    }
    if (in_write != NULL) CloseHandle(in_write);
    for (;;) {
        char chunk[4096]; DWORD read = 0;
        if (!ReadFile(out_read, chunk, (DWORD)sizeof(chunk), &read, NULL) || read == 0) break;
        (void)append_process_output(&output, &length, &capacity, chunk, (size_t)read, max_output, &limited);
    }
    DWORD exit_code = 1; (void)GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess); CloseHandle(process.hThread); CloseHandle(out_read); CloseHandle(job);
    result->output = output != NULL ? output : zeno_strdup("");
    result->exit_code = (int)exit_code;
    result->output_limited |= limited;
    result->ok = exit_code == 0 && !input_failed;
    if (input_failed) result->reason = zeno_strdup("Could not write request to server stdin.");
    return 1;
}

int zeno_process_background(const char *command, const char *cwd, const char *log_path, int *pid_out, void **group_out) {
    if (group_out != NULL) *group_out = NULL;
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE file = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    (void)SetFilePointer(file, 0, NULL, FILE_END);
    HANDLE job = create_kill_on_close_job();
    if (job == NULL) { CloseHandle(file); return 0; }
    char *line = zeno_format("cmd.exe /d /s /c %s", command);
    STARTUPINFOA startup; PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = file; startup.hStdError = file; startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    int created = line != NULL && CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &startup, &process);
    free(line); CloseHandle(file);
    if (!created) { CloseHandle(job); return 0; }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 126);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); return 0;
    }
    if (pid_out != NULL) *pid_out = (int)process.dwProcessId;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (group_out != NULL) *group_out = job; else CloseHandle(job);
    return 1;
}

int zeno_process_kill(int pid) { HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid); if (handle == NULL) return 0; int ok = TerminateProcess(handle, 1) != 0; CloseHandle(handle); return ok; }
int zeno_process_kill_ex(int pid, void *group) {
    int ok = group != NULL && TerminateJobObject((HANDLE)group, 1) != 0;
    if (!ok && pid > 0) ok = zeno_process_kill(pid);
    return ok;
}
void zeno_process_group_close(void *group) { if (group != NULL) CloseHandle((HANDLE)group); }
int zeno_process_alive(int pid) { HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid); if (handle == NULL) return 0; DWORD exit_code = 0; int alive = GetExitCodeProcess(handle, &exit_code) != 0 && exit_code == STILL_ACTIVE; CloseHandle(handle); return alive; }
#else
int zeno_process_command(const char *command, const char *cwd, int timeout_ms,
                         size_t max_output, ZenoExecResult *result) {
    int pipes[2]; if (pipe(pipes) != 0) { result->reason = zeno_strdup(strerror(errno)); return 0; }
    pid_t child = fork();
    if (child < 0) { close(pipes[0]); close(pipes[1]); result->reason = zeno_strdup(strerror(errno)); return 0; }
    if (child == 0) { (void)setpgid(0, 0); (void)dup2(pipes[1], STDOUT_FILENO); (void)dup2(pipes[1], STDERR_FILENO); close(pipes[0]); close(pipes[1]); (void)chdir(cwd != NULL ? cwd : "."); execl("/bin/sh", "sh", "-c", command, (char *)NULL); _exit(127); }
    (void)setpgid(child, child);
    close(pipes[1]); int flags = fcntl(pipes[0], F_GETFL, 0); (void)fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK);
    char *output = NULL; size_t length = 0, capacity = 0; int status = 0; int done = 0; long long started = zeno_now_ms(); int limited = 0;
    while (!done) {
        char chunk[4096]; ssize_t read_count = read(pipes[0], chunk, sizeof(chunk)); if (read_count > 0) (void)append_process_output(&output, &length, &capacity, chunk, (size_t)read_count, max_output, &limited);
        pid_t waited = waitpid(child, &status, WNOHANG); if (waited == child) done = 1;
        if (!done && zeno_now_ms() - started >= timeout_ms) { result->timed_out = 1; (void)kill(-child, SIGKILL); (void)kill(child, SIGKILL); (void)waitpid(child, &status, 0); done = 1; }
        if (!done && limited) { result->output_limited = 1; (void)kill(-child, SIGKILL); (void)kill(child, SIGKILL); (void)waitpid(child, &status, 0); done = 1; }
        if (!done) zeno_sleep_ms(5);
    }
    for (;;) { char chunk[4096]; ssize_t read_count = read(pipes[0], chunk, sizeof(chunk)); if (read_count <= 0) break; (void)append_process_output(&output, &length, &capacity, chunk, (size_t)read_count, max_output, &limited); }
    close(pipes[0]); result->output = output != NULL ? output : zeno_strdup(""); result->output_limited |= limited; result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1; result->ok = !result->timed_out && !result->output_limited && result->exit_code == 0;
    if (result->timed_out) result->reason = zeno_format("Command timed out after %dms.", timeout_ms); else if (result->output_limited) result->reason = zeno_strdup("Command output limit reached."); else if (!result->ok) result->reason = zeno_format("Command exited with code %d.", result->exit_code);
    return 1;
}

#include <errno.h>
#include <fcntl.h>
#include <poll.h>

int zeno_process_command_stdin(const char *command, const char *cwd, const char *stdin_input,
                               int timeout_ms, size_t max_output, ZenoExecResult *result) {
    int out_pipe[2] = {-1, -1};
    int in_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0) return 0;
    if (pipe(in_pipe) != 0) { close(out_pipe[0]); close(out_pipe[1]); return 0; }
    (void)fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL) | O_NONBLOCK);
    pid_t child = fork();
    if (child < 0) { close(out_pipe[0]); close(out_pipe[1]); close(in_pipe[0]); close(in_pipe[1]); return 0; }
    if (child == 0) {
        (void)setpgid(0, 0);
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        (void)dup2(out_pipe[1], STDERR_FILENO);
        (void)dup2(in_pipe[0], STDIN_FILENO);
        close(out_pipe[0]); close(out_pipe[1]); close(in_pipe[0]); close(in_pipe[1]);
        if (cwd != NULL) (void)chdir(cwd);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    close(out_pipe[1]);
    close(in_pipe[0]);
    size_t input_length = stdin_input != NULL ? strlen(stdin_input) : 0;
    size_t input_offset = 0;
    int input_done = input_length == 0, input_failed = 0, input_closed = 0;
    char *output = NULL; size_t length = 0, capacity = 0; int limited = 0;
    long long deadline = zeno_now_ms() + (long long)timeout_ms;
    int finished = 0, timed_out = 0;
    while (!finished) {
        struct pollfd descriptor = {out_pipe[0], POLLIN, 0};
        int ready = poll(&descriptor, 1, 10);
        if (ready > 0) {
            char chunk[4096];
            ssize_t got = read(out_pipe[0], chunk, sizeof(chunk));
            if (got > 0) (void)append_process_output(&output, &length, &capacity, chunk, (size_t)got, max_output, &limited);
            else if (got == 0 && input_done) finished = 1;
        }
        if (!input_done && !input_closed) {
            /* Interleave stdin writes with stdout pumping: a child that fills
             * its output buffer cannot deadlock the caller. */
            ssize_t wrote = write(in_pipe[1], stdin_input + input_offset, input_length - input_offset);
            if (wrote > 0) {
                input_offset += (size_t)wrote;
                if (input_offset >= input_length) { input_done = 1; close(in_pipe[1]); input_closed = 1; }
            } else if (wrote < 0 && errno != EINTR) {
                input_failed = 1;
                input_done = 1;
                close(in_pipe[1]);
                input_closed = 1;
            }
        }
        if (zeno_now_ms() >= deadline) { timed_out = 1; (void)zeno_process_kill_ex((int)child, NULL); finished = 1; }
    }
    close(out_pipe[0]);
    if (!input_closed) close(in_pipe[1]);
    int status = 0;
    (void)waitpid(child, &status, 0);
    int exit_code = timed_out ? 124 : (WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    result->output = output != NULL ? output : zeno_strdup("");
    result->exit_code = exit_code;
    result->output_limited = limited;
    result->timed_out = timed_out;
    result->ok = !timed_out && exit_code == 0 && !input_failed;
    if (timed_out) result->reason = zeno_format("Command timed out after %dms.", timeout_ms);
    else if (input_failed) result->reason = zeno_strdup("Could not write request to server stdin.");
    return 1;
}

int zeno_process_background(const char *command, const char *cwd, const char *log_path, int *pid_out, void **group_out) {
    if (group_out != NULL) *group_out = NULL;
    FILE *file = fopen(log_path, "ab"); if (file == NULL) return 0; int descriptor = fileno(file); pid_t child = fork(); if (child < 0) { fclose(file); return 0; }
    if (child == 0) { (void)setpgid(0, 0); (void)chdir(cwd != NULL ? cwd : "."); (void)dup2(descriptor, STDOUT_FILENO); (void)dup2(descriptor, STDERR_FILENO); fclose(file); execl("/bin/sh", "sh", "-c", command, (char *)NULL); _exit(127); }
    (void)setpgid(child, child);
    fclose(file); if (pid_out != NULL) *pid_out = (int)child; return 1;
}

int zeno_process_kill(int pid) {
    if (pid <= 0) return 0;
    int group_ok = kill(-(pid_t)pid, SIGKILL) == 0;
    int process_ok = kill((pid_t)pid, SIGKILL) == 0;
    return group_ok || process_ok;
}
int zeno_process_kill_ex(int pid, void *group) { (void)group; return zeno_process_kill(pid); }
void zeno_process_group_close(void *group) { (void)group; }
int zeno_process_alive(int pid) { if (pid <= 0) return 0; int rc = kill((pid_t)pid, 0); return rc == 0 || errno == EPERM; }
#endif

static int workspace_relative_is_absolute(const char *relative) {
    if (relative == NULL || *relative == '\0') return 0;
#ifdef _WIN32
    return relative[0] == '/' || relative[0] == '\\' ||
           (isalpha((unsigned char)relative[0]) && relative[1] == ':');
#else
    return relative[0] == '/';
#endif
}

static char *workspace_resolve(const char *root, const char *relative) {
    if (workspace_relative_is_absolute(relative)) return NULL;
    char *candidate = zeno_join_path(root, relative != NULL ? relative : ".");
    char *resolved = NULL;
    int safe = candidate != NULL && zeno_path_inside(root, candidate, &resolved);
    free(candidate);
    if (!safe) {
        free(resolved);
        return NULL;
    }
    return resolved;
}

static int copy_file(const char *source, const char *destination) {
    FILE *in = fopen(source, "rb"); FILE *out = destination != NULL ? fopen(destination, "wb") : NULL;
    if (in == NULL || out == NULL) { if (in != NULL) fclose(in); if (out != NULL) fclose(out); return 0; }
    char buffer[8192]; size_t count; int ok = 1; while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0) if (fwrite(buffer, 1, count, out) != count) { ok = 0; break; }
    fclose(in); fclose(out); return ok;
}

static int workspace_backup(const char *root, const char *path) {
    char *trash = zeno_join_path(root, ".Zeno_trash"); (void)zeno_mkdirs(trash); char *name = strrchr(path, '/'); if (name == NULL) name = strrchr(path, '\\'); name = name != NULL ? name + 1 : (char *)path;
    char *backup = trash != NULL ? zeno_format("%s/%s.%lld.bak", trash, name, zeno_now_ms()) : NULL; int ok = backup != NULL && copy_file(path, backup); free(trash); free(backup); return ok;
}

static char *workspace_list(const char *root, const char *relative, size_t max_entries) {
    char *path = workspace_resolve(root, relative); if (path == NULL) return zeno_format("Directory not found: %s", relative != NULL ? relative : ".");
    char *result = zeno_strdup(""); size_t count = 0;
#ifdef _WIN32
    char *pattern = zeno_join_path(path, "*"); WIN32_FIND_DATAA data; HANDLE handle = pattern != NULL ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
    if (handle != INVALID_HANDLE_VALUE) do { if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0 && count < max_entries && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) { char *line = zeno_format("%s[%s] %s\n", (count == 0 ? "" : ""), (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "D" : "F", data.cFileName); char *next = line != NULL ? zeno_format("%s%s", result, line) : NULL; free(line); free(result); result = next; count++; } } while (FindNextFileA(handle, &data) != 0); if (handle != INVALID_HANDLE_VALUE) FindClose(handle); free(pattern);
#else
    DIR *handle = opendir(path); if (handle != NULL) { struct dirent *entry; while ((entry = readdir(handle)) != NULL && count < max_entries) { if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; char *full = zeno_join_path(path, entry->d_name); struct stat info; int readable = full != NULL && lstat(full, &info) == 0; if (!readable || S_ISLNK(info.st_mode)) { free(full); continue; } int directory = S_ISDIR(info.st_mode); char *line = zeno_format("[%s] %s\n", directory ? "D" : "F", entry->d_name); char *next = line != NULL ? zeno_format("%s%s", result, line) : NULL; free(line); free(result); result = next; free(full); count++; } closedir(handle); }
#endif
    free(path); if (result == NULL || *result == '\0') { free(result); return zeno_strdup("(empty)"); } return result;
}

static char *workspace_read(const char *root, const char *relative, size_t max_chars) {
    char *path = workspace_resolve(root, relative); if (path == NULL) return zeno_format("Path escapes workspace: %s", relative != NULL ? relative : "");
    FILE *file = fopen(path, "rb"); free(path); if (file == NULL) return zeno_format("File not found: %s", relative != NULL ? relative : ""); char *output = NULL; int limited = 0; int ok = zeno_read_all(file, max_chars, &output, &limited); fclose(file); if (!ok) return zeno_strdup("Error reading file."); if (limited) { char *next = zeno_format("%s\n\n... [truncated]", output); free(output); output = next; } return output;
}

static char *workspace_write(const char *root, const char *relative, const char *content, int append) {
    char *path = workspace_resolve(root, relative);
    if (path == NULL) return zeno_strdup("Error: path escapes workspace.");
    char *directory = zeno_strdup(path);
    char *slash = directory != NULL ? strrchr(directory, '/') : NULL;
    char *backslash = directory != NULL ? strrchr(directory, '\\') : NULL;
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash != NULL) {
        *slash = '\0';
        (void)zeno_mkdirs(directory);
    }
    free(directory);
    const char *value = content != NULL ? content : "";
    int ok = 0;
    if (!append) {
        FILE *existing = fopen(path, "rb");
        if (existing != NULL) {
            fclose(existing);
            (void)workspace_backup(root, path);
        }
        ok = zeno_write_file_atomic(path, value);
    } else {
        FILE *file = fopen(path, "ab");
        if (file != NULL) ok = fputs(value, file) >= 0 && fclose(file) == 0;
    }
    char *result = ok
        ? zeno_format("File %s: %s (%zu chars)", append ? "appended" : "written", relative, strlen(value))
        : zeno_strdup("Error writing file.");
    free(path);
    return result;
}

static char *workspace_replace(const char *root, const char *relative, const char *old_text, const char *new_text, int all) {
    char *path = workspace_resolve(root, relative);
    if (path == NULL) return zeno_strdup("Error: path escapes workspace.");
    char *content = workspace_read(root, relative, (size_t)-1);
    if (content == NULL || strncmp(content, "File not found:", 15) == 0) {
        free(path);
        free(content);
        return zeno_format("File not found: %s", relative);
    }
    const char *needle = old_text != NULL ? old_text : "";
    const char *replacement = new_text != NULL ? new_text : "";
    if (*needle == '\0') {
        free(path);
        free(content);
        return zeno_strdup("Text not found in file.");
    }
    size_t old_len = strlen(needle);
    size_t new_len = strlen(replacement);
    size_t occurrences = 0;
    char *cursor = content;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        occurrences++;
        cursor += old_len;
        if (!all) break;
    }
    if (occurrences == 0) {
        free(path);
        free(content);
        return zeno_strdup("Text not found in file.");
    }
    size_t content_len = strlen(content);
    size_t result_len = content_len;
    if (new_len >= old_len) {
        size_t delta = new_len - old_len;
        if (delta > 0 && occurrences > ((size_t)-1 - content_len) / delta) {
            free(path);
            free(content);
            return zeno_strdup("Error: replacement is too large.");
        }
        result_len += occurrences * delta;
    } else {
        result_len -= occurrences * (old_len - new_len);
    }
    char *updated = (char *)malloc(result_len + 1);
    if (updated == NULL) {
        free(path);
        free(content);
        return zeno_strdup("Error: out of memory.");
    }
    char *write = updated;
    cursor = content;
    size_t replacements = 0;
    while (*cursor != '\0') {
        char *found = strstr(cursor, needle);
        if (found == NULL || (!all && replacements > 0)) {
            strcpy(write, cursor);
            break;
        }
        size_t before = (size_t)(found - cursor);
        memcpy(write, cursor, before);
        write += before;
        memcpy(write, replacement, new_len);
        write += new_len;
        cursor = found + old_len;
        replacements++;
    }
    updated[result_len] = '\0';
    (void)workspace_backup(root, path);
    int ok = zeno_write_file_atomic(path, updated);
    char *result = ok ? zeno_format("File updated: %s", relative) : zeno_strdup("Error writing file.");
    free(updated);
    free(content);
    free(path);
    return result;
}

static int wildcard_match(const char *text, const char *pattern) {
    if (*pattern == '\0') return *text == '\0';
    if (*pattern == '*') return wildcard_match(text, pattern + 1) || (*text != '\0' && wildcard_match(text + 1, pattern));
    if (*pattern == '?') return *text != '\0' && wildcard_match(text + 1, pattern + 1);
    return tolower((unsigned char)*text) == tolower((unsigned char)*pattern) && wildcard_match(text + 1, pattern + 1);
}

#define ZENO_MAX_WORKSPACE_SCAN_BYTES (32U * 1024U * 1024U)

typedef struct WorkspaceWalkContext { const char *root; const char *relative; const char *query; const char *pattern; int regex_like; size_t max; size_t count; size_t scanned_chars; char *result; } WorkspaceWalkContext;

static void append_workspace_match(WorkspaceWalkContext *context, const char *line) { if (context->count >= context->max) return; char *next = context->result != NULL ? zeno_format("%s%s\n", context->result, line) : zeno_format("%s\n", line); free(context->result); context->result = next; context->count++; }

static int workspace_query_match(WorkspaceWalkContext *context, const char *path) {
    if (context == NULL || path == NULL || context->query == NULL || context->scanned_chars >= ZENO_MAX_WORKSPACE_SCAN_BYTES) return 0;
    size_t remaining = ZENO_MAX_WORKSPACE_SCAN_BYTES - context->scanned_chars;
    size_t limit = remaining < 1000000U ? remaining : 1000000U;
    char *text = zeno_read_file(path, limit);
    if (text == NULL) return 0;
    /* Charge the full read budget: embedded NULs must not make binary files
     * appear cheaper than the bytes actually inspected. */
    context->scanned_chars += limit;
    int matched = zeno_contains_ci(text, context->query);
    free(text);
    return matched;
}

static void workspace_walk(WorkspaceWalkContext *context, const char *relative) {
    if (context == NULL || context->count >= context->max ||
        (context->query != NULL && context->scanned_chars >= ZENO_MAX_WORKSPACE_SCAN_BYTES)) return;
    char *directory = relative != NULL && *relative != '\0'
        ? zeno_join_path(context->root, relative)
        : zeno_strdup(context->root);
#ifdef _WIN32
    char *pattern = zeno_join_path(directory, "*");
    WIN32_FIND_DATAA data;
    HANDLE handle = pattern != NULL ? FindFirstFileA(pattern, &data) : INVALID_HANDLE_VALUE;
    if (handle == INVALID_HANDLE_VALUE) {
        free(directory);
        free(pattern);
        return;
    }
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        char *child = relative != NULL && *relative != '\0'
            ? zeno_join_path(relative, data.cFileName)
            : zeno_strdup(data.cFileName);
        int reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (!reparse && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (data.cFileName[0] != '.' && strcmp(data.cFileName, "node_modules") != 0)
                workspace_walk(context, child);
        } else if (!reparse) {
            int matched = context->pattern != NULL &&
                (wildcard_match(child, context->pattern) || wildcard_match(data.cFileName, context->pattern));
            if (context->query != NULL) {
                char *full = zeno_join_path(context->root, child);
                matched = full != NULL && workspace_query_match(context, full);
                free(full);
            }
            if (matched) append_workspace_match(context, child);
        }
        free(child);
    } while (FindNextFileA(handle, &data) != 0 && context->count < context->max);
    FindClose(handle);
    free(directory);
    free(pattern);
#else
    DIR *handle = directory != NULL ? opendir(directory) : NULL;
    if (handle == NULL) {
        free(directory);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL && context->count < context->max) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child = relative != NULL && *relative != '\0'
            ? zeno_join_path(relative, entry->d_name)
            : zeno_strdup(entry->d_name);
        char *full = zeno_join_path(context->root, child);
        struct stat info;
        int readable = full != NULL && lstat(full, &info) == 0;
        int reparse = readable && S_ISLNK(info.st_mode);
        if (readable && !reparse && S_ISDIR(info.st_mode)) {
            if (entry->d_name[0] != '.' && strcmp(entry->d_name, "node_modules") != 0)
                workspace_walk(context, child);
        } else if (readable && !reparse) {
            int matched = context->pattern != NULL &&
                (wildcard_match(child, context->pattern) || wildcard_match(entry->d_name, context->pattern));
            if (context->query != NULL) matched = workspace_query_match(context, full);
            if (matched) append_workspace_match(context, child);
        }
        free(full);
        free(child);
    }
    closedir(handle);
    free(directory);
#endif
}

static char *workspace_search(const char *root, const char *query, const char *relative, size_t max) {
    char *base = workspace_resolve(root, relative != NULL ? relative : ".");
    if (base == NULL) return zeno_strdup("Error: search path escapes workspace.");
    free(base);
    WorkspaceWalkContext context = {root, relative, query, NULL, 0, max, 0, 0, NULL};
    workspace_walk(&context, relative != NULL ? relative : "");
    return context.result != NULL ? context.result : zeno_strdup("No matches found.");
}

static char *workspace_glob(const char *root, const char *pattern, const char *relative, size_t max) {
    char *base = workspace_resolve(root, relative != NULL ? relative : ".");
    if (base == NULL) return zeno_strdup("Error: glob path escapes workspace.");
    free(base);
    WorkspaceWalkContext context = {root, NULL, NULL, pattern, 0, max, 0, 0, NULL};
    workspace_walk(&context, relative != NULL ? relative : "");
    if (context.result == NULL) return zeno_format("No files match pattern: %s", pattern != NULL ? pattern : "");
    return context.result;
}

char *zeno_workspace_list(const char *workspace_root, const char *relative_path, size_t max_entries) { return workspace_list(workspace_root, relative_path != NULL ? relative_path : ".", max_entries > 0 ? max_entries : 100); }
char *zeno_workspace_read(const char *workspace_root, const char *relative_path, size_t max_chars) { return workspace_read(workspace_root, relative_path != NULL ? relative_path : "", max_chars > 0 ? max_chars : 50000); }
char *zeno_workspace_write(const char *workspace_root, const char *relative_path, const char *content) { return workspace_write(workspace_root, relative_path != NULL ? relative_path : "", content != NULL ? content : "", 0); }
char *zeno_workspace_append(const char *workspace_root, const char *relative_path, const char *content) { return workspace_write(workspace_root, relative_path != NULL ? relative_path : "", content != NULL ? content : "", 1); }
char *zeno_workspace_replace(const char *workspace_root, const char *relative_path, const char *old_text, const char *new_text, int replace_all) { return workspace_replace(workspace_root, relative_path != NULL ? relative_path : "", old_text != NULL ? old_text : "", new_text != NULL ? new_text : "", replace_all); }
char *zeno_workspace_create_directory(const char *workspace_root, const char *relative_path) { char *path = workspace_resolve(workspace_root, relative_path != NULL ? relative_path : ""); int ok = path != NULL && zeno_mkdirs(path); char *result = ok ? zeno_format("Directory created: %s", relative_path != NULL ? relative_path : "") : zeno_strdup("Error creating directory."); free(path); return result; }
char *zeno_workspace_search(const char *workspace_root, const char *query, const char *relative_path, size_t max_results) { return workspace_search(workspace_root, query != NULL ? query : "", relative_path != NULL ? relative_path : ".", max_results > 0 ? max_results : 50); }
char *zeno_workspace_glob(const char *workspace_root, const char *pattern, const char *relative_path, size_t max_results) { return workspace_glob(workspace_root, pattern != NULL ? pattern : "*", relative_path != NULL ? relative_path : ".", max_results > 0 ? max_results : 100); }

typedef struct ToolContext { ZenoMutex lock; ZenoSandbox *sandbox; ZenoMemory *memory; char *workspace_root; char **read_paths; size_t read_path_count; void *bound_agent; int subagent_depth; unsigned long subagent_seq; } ToolContext;

static void tool_context_free(ToolContext *context) {
    if (context == NULL) return;
    for (size_t index = 0; index < context->read_path_count; index++) free(context->read_paths[index]);
    free(context->read_paths);
    zeno_mutex_destroy(&context->lock);
    free(context->workspace_root);
    free(context);
}

/* Read-before-edit ledger: paths successfully read this session. Runtime
 * discipline, not prompt discipline — the same spirit as effect approval. */
static int ledger_contains(ToolContext *context, const char *key) {
    for (size_t index = 0; index < context->read_path_count; index++)
        if (strcmp(context->read_paths[index], key) == 0) return 1;
    return 0;
}

static void ledger_record(ToolContext *context, const char *key) {
    if (key == NULL || *key == '\0' || ledger_contains(context, key)) return;
    char **grown = (char **)realloc(context->read_paths, (context->read_path_count + 1) * sizeof(*grown));
    if (grown == NULL) return;
    context->read_paths = grown;
    char *copy = zeno_strdup(key);
    if (copy == NULL) return;
    context->read_paths[context->read_path_count++] = copy;
}

static int workspace_file_exists(const char *root, const char *relative) {
    char *full = workspace_resolve(root, relative);
    if (full == NULL) return 0;
    FILE *file = fopen(full, "rb");
    free(full);
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

/* Returns 1 when the edit may proceed. Existing files must have been read in
 * this session first; brand-new files are always allowed. */
static int enforce_read_before_edit(ToolContext *context, const char *path, char **output, char **error) {
    if (path == NULL || *path == '\0') return 1;
    if (!workspace_file_exists(context->workspace_root, path)) return 1;
    zeno_mutex_lock(&context->lock);
    int known = ledger_contains(context, path);
    zeno_mutex_unlock(&context->lock);
    if (known) return 1;
    *output = zeno_format("Error: read-before-edit policy: '%s' exists but was not read in this session. Call read_text_file on it first, then retry this exact edit. Only brand-new files skip the read.", path);
    if (error != NULL) *error = zeno_strdup("read_before_edit");
    return 0;
}

/* Bounded line-multiset diff summary: accurate added/removed counts without
 * fragile hunks. O(n log n), capped at 20k lines per side. */
static int compare_lines(const void *left, const void *right) {
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

typedef struct LineSet { char *buffer; char **items; size_t count; } LineSet;

static void lineset_free(LineSet *set) { free(set->buffer); free(set->items); memset(set, 0, sizeof(*set)); }

static void lineset_fill(LineSet *set, const char *text, size_t max_lines) {
    memset(set, 0, sizeof(*set));
    if (text == NULL || *text == '\0') return;
    size_t length = strlen(text);
    set->buffer = (char *)malloc(length + 1);
    if (set->buffer == NULL) return;
    memcpy(set->buffer, text, length + 1);
    size_t capacity = 1;
    for (size_t index = 0; index < length; index++) if (set->buffer[index] == '\n') capacity++;
    if (capacity > max_lines) capacity = max_lines;
    set->items = (char **)calloc(capacity, sizeof(char *));
    if (set->items == NULL) { free(set->buffer); set->buffer = NULL; return; }
    char *cursor = set->buffer;
    while (set->count < capacity) {
        set->items[set->count++] = cursor;
        char *newline = strchr(cursor, '\n');
        if (newline == NULL) break;
        *newline = '\0';
        cursor = newline + 1;
        if (*cursor == '\0') break;
    }
    qsort(set->items, set->count, sizeof(char *), compare_lines);
}

static char *summarize_change(const char *old_text, const char *new_text) {
    LineSet old_set; LineSet new_set;
    lineset_fill(&old_set, old_text, 20000);
    lineset_fill(&new_set, new_text, 20000);
    size_t common = 0, old_index = 0, new_index = 0;
    while (old_index < old_set.count && new_index < new_set.count) {
        int order = strcmp(old_set.items[old_index], new_set.items[new_index]);
        if (order == 0) { common++; old_index++; new_index++; }
        else if (order < 0) old_index++;
        else new_index++;
    }
    size_t removed = old_set.count >= common ? old_set.count - common : 0;
    size_t added = new_set.count >= common ? new_set.count - common : 0;
    char *summary = zeno_format("[change] +%zu/-%zu lines (%zu -> %zu lines)", added, removed, old_set.count, new_set.count);
    lineset_free(&old_set);
    lineset_free(&new_set);
    return summary != NULL ? summary : zeno_strdup("");
}

/* Appends "[change] ..." to a successful mutation result. */
static void attach_change_summary(char **output, char *summary) {
    if (*output == NULL || summary == NULL) { free(summary); return; }
    char *next = zeno_format("%s\n%s", *output, summary);
    free(summary);
    if (next != NULL) { free(*output); *output = next; }
}

/* --- Orchestration tools: live plan and scoped subagents --- */

static int arg_string(const char *args, const char *key, char **value, const char *fallback);
static size_t arg_size_limit(const char *args, const char *key, size_t fallback, size_t maximum);

static char *plan_file_path(ToolContext *context) {
    char *dir = zeno_join_path(context->workspace_root, ".zeno");
    if (dir == NULL) return NULL;
    (void)zeno_mkdirs(dir);
    char *path = zeno_join_path(dir, "plan.md");
    free(dir);
    return path;
}

static int tool_update_plan(void *context, const char *args, char **output, char **error) {
    (void)error;
    ToolContext *ctx = (ToolContext *)context;
    char *plan = NULL;
    arg_string(args, "plan_markdown", &plan, "");
    char *trimmed = plan != NULL ? zeno_trim_copy(plan) : NULL;
    if (trimmed == NULL || *trimmed == '\0') {
        *output = zeno_strdup("Error: update_plan requires a non-empty plan_markdown (one '- [ ]' line per step).");
        if (error != NULL) *error = zeno_strdup("invalid_plan");
        free(plan); free(trimmed);
        return 0;
    }
    size_t plan_length = strlen(trimmed);
    if (plan_length > 4000) plan_length = 4000;
    char *bounded = zeno_strndup(trimmed, plan_length);
    char *path = plan_file_path(ctx);
    int ok = bounded != NULL && path != NULL && zeno_write_file_atomic(path, bounded);
    *output = ok
        ? zeno_format("Plan saved (%zu characters). It is injected into your system prompt every turn; keep it current.", plan_length)
        : zeno_strdup("Error: could not persist the plan file.");
    if (!ok && error != NULL) *error = zeno_strdup("plan_persist_failed");
    free(bounded); free(path); free(plan); free(trimmed);
    return ok;
}

char *zeno_tools_current_plan(const void *owned_context) {
    const ToolContext *ctx = (const ToolContext *)owned_context;
    if (ctx == NULL || ctx->workspace_root == NULL) return NULL;
    char *path = plan_file_path((ToolContext *)ctx);
    if (path == NULL) return NULL;
    char *content = zeno_read_file(path, 8192);
    free(path);
    if (content != NULL && *zeno_trim_copy(content) == '\0') { free(content); content = NULL; }
    return content;
}

void zeno_tools_bind_agent(void *owned_context, void *agent) {
    ToolContext *ctx = (ToolContext *)owned_context;
    if (ctx != NULL) ctx->bound_agent = agent;
}

static int deny_all_approvals(void *context, const char *request_id, const char *tool_name,
                              const char *args_json, const char *reason) {
    (void)context; (void)request_id; (void)tool_name; (void)args_json; (void)reason;
    return 0;
}

/* Subagents observe and report; they never mutate. Approvals are denied by
 * default, nesting is limited to one level, and the response is bounded. */
static int tool_spawn_subagent(void *context, const char *args, char **output, char **error) {
    ToolContext *ctx = (ToolContext *)context;
    ZenoAgent *parent = (ZenoAgent *)ctx->bound_agent;
    char *task = NULL;
    arg_string(args, "task", &task, "");
    int max_turns = (int)arg_size_limit(args, "max_turns", 6, 12);
    char *trimmed_task = task != NULL ? zeno_trim_copy(task) : NULL;
    if (parent == NULL || trimmed_task == NULL || *trimmed_task == '\0') {
        *output = zeno_strdup("Error: spawn_subagent requires a bound runtime and a non-empty task.");
        if (error != NULL) *error = zeno_strdup("subagent_unavailable");
        free(task); free(trimmed_task);
        return 0;
    }
    zeno_mutex_lock(&ctx->lock);
    if (ctx->subagent_depth >= 1) {
        zeno_mutex_unlock(&ctx->lock);
        *output = zeno_strdup("Error: nested subagents are not allowed; finish your own scope instead.");
        if (error != NULL) *error = zeno_strdup("subagent_nested");
        free(task); free(trimmed_task);
        return 0;
    }
    ctx->subagent_depth++;
    unsigned long sequence = ++ctx->subagent_seq;
    zeno_mutex_unlock(&ctx->lock);

    ZenoAgentOptions options;
    memset(&options, 0, sizeof(options));
    options.registry = parent->registry;
    options.router = parent->router;
    options.memory = parent->memory;
    options.sandbox = parent->sandbox;
    options.approval = parent->approval;
    options.model = parent->model;
    options.runs_dir = parent->runs_dir;
    options.require_approval = 1;
    options.mode = parent->options.mode;
    ZenoAgent *subagent = zeno_agent_create(&options);
    ZenoRunOptions run_options;
    memset(&run_options, 0, sizeof(run_options));
    run_options.model = parent->model;
    run_options.max_turns = max_turns;
    run_options.max_tokens = 2048;
    run_options.temperature = 0.2;
    run_options.approve = deny_all_approvals;
    char *session_id = zeno_format("session-sub-%lu", sequence);
    ZenoAgentResult result = zeno_agent_run(subagent, session_id != NULL ? session_id : "session-sub", trimmed_task, &run_options);

    size_t response_length = result.response != NULL ? strlen(result.response) : 0;
    if (response_length > 6000) response_length = 6000;
    *output = zeno_format("{\"status\":\"%s\",\"response\":\"%.*s\"}",
                          result.status != NULL ? result.status : "failed",
                          (int)response_length, result.response != NULL ? result.response : "");
    int ok = *output != NULL && result.status != NULL && strcmp(result.status, "completed") == 0;
    if (!ok && error != NULL) *error = zeno_strdup(result.status != NULL ? result.status : "failed");

    zeno_agent_result_free(&result);
    zeno_agent_destroy(subagent);
    /* The shared registry context must point back at the parent runtime;
     * creating the subagent rebound it. */
    zeno_tools_bind_agent(ctx, parent);
    free(session_id);
    free(task); free(trimmed_task);
    zeno_mutex_lock(&ctx->lock);
    ctx->subagent_depth--;
    zeno_mutex_unlock(&ctx->lock);
    return ok;
}

static int arg_string(const char *args, const char *key, char **value, const char *fallback) { char *found = zeno_json_get_path_string(args, key); if (found == NULL) found = zeno_strdup(fallback != NULL ? fallback : ""); *value = found; return found != NULL; }
static size_t arg_size_limit(const char *args, const char *key, size_t fallback, size_t maximum) {
    long value = zeno_json_get_int(args, key, (long)fallback);
    if (value <= 0) return fallback;
    if ((unsigned long)value > (unsigned long)maximum) return maximum;
    return (size_t)value;
}
static int arg_seconds_ms(const char *args, const char *key, int fallback_seconds, int maximum_seconds) {
    long value = zeno_json_get_int(args, key, fallback_seconds);
    if (value < 1) value = fallback_seconds;
    if (value > maximum_seconds) value = maximum_seconds;
    return (int)(value * 1000L);
}
static int tool_list(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *relative = NULL; size_t max = arg_size_limit(args, "max_entries", 100, 10000); arg_string(args, "relative_path", &relative, "."); *output = workspace_list(ctx->workspace_root, relative, max); free(relative); return *output != NULL; }
static int tool_read(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; size_t max = arg_size_limit(args, "max_chars", 50000, 2U * 1024U * 1024U); arg_string(args, "relative_path", &path, ""); *output = workspace_read(ctx->workspace_root, path, max); if (*output != NULL && strncmp(*output, "Error:", 6) != 0 && path != NULL && *path != '\0') { zeno_mutex_lock(&ctx->lock); ledger_record(ctx, path); zeno_mutex_unlock(&ctx->lock); } free(path); return *output != NULL; }
static int tool_write(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; char *content = NULL; arg_string(args, "relative_path", &path, ""); arg_string(args, "content", &content, ""); if (!enforce_read_before_edit(ctx, path, output, error)) { free(path); free(content); return 0; } int existed = workspace_file_exists(ctx->workspace_root, path); char *old_content = existed ? workspace_read(ctx->workspace_root, path, 1024U * 1024U) : NULL; *output = workspace_write(ctx->workspace_root, path, content, 0); if (*output != NULL) attach_change_summary(output, existed ? summarize_change(old_content != NULL ? old_content : "", content != NULL ? content : "") : zeno_format("[change] created file (%zu bytes)", content != NULL ? strlen(content) : 0)); free(old_content); free(path); free(content); return *output != NULL; }
static int tool_append(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; char *content = NULL; arg_string(args, "relative_path", &path, ""); arg_string(args, "content", &content, ""); if (!enforce_read_before_edit(ctx, path, output, error)) { free(path); free(content); return 0; } char *old_content = workspace_file_exists(ctx->workspace_root, path) ? workspace_read(ctx->workspace_root, path, 1024U * 1024U) : NULL; *output = workspace_write(ctx->workspace_root, path, content, 1); if (*output != NULL) { char *new_content = workspace_read(ctx->workspace_root, path, 1024U * 1024U); attach_change_summary(output, summarize_change(old_content != NULL ? old_content : "", new_content != NULL ? new_content : "")); free(new_content); } free(old_content); free(path); free(content); return *output != NULL; }
static int tool_replace(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *path = NULL; char *old_text = NULL; char *new_text = NULL; arg_string(args, "relative_path", &path, ""); arg_string(args, "old_text", &old_text, ""); arg_string(args, "new_text", &new_text, ""); if (!enforce_read_before_edit(ctx, path, output, error)) { free(path); free(old_text); free(new_text); return 0; } char *before_content = workspace_file_exists(ctx->workspace_root, path) ? workspace_read(ctx->workspace_root, path, 1024U * 1024U) : NULL; *output = workspace_replace(ctx->workspace_root, path, old_text, new_text, zeno_json_get_bool(args, "replace_all", 1)); if (*output != NULL && strncmp(*output, "Error", 5) != 0) { char *after_content = workspace_read(ctx->workspace_root, path, 1024U * 1024U); attach_change_summary(output, summarize_change(before_content != NULL ? before_content : "", after_content != NULL ? after_content : "")); free(after_content); } free(before_content); free(path); free(old_text); free(new_text); return *output != NULL; }
static int tool_mkdir(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *relative = NULL; arg_string(args, "relative_path", &relative, ""); char *path = workspace_resolve(ctx->workspace_root, relative); int ok = path != NULL && zeno_mkdirs(path); *output = ok ? zeno_format("Directory created: %s", relative) : zeno_strdup("Error creating directory."); free(relative); free(path); return *output != NULL; }
static int tool_search(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *query = NULL; char *relative = NULL; size_t max = arg_size_limit(args, "max_results", 50, 1000); arg_string(args, "query", &query, ""); arg_string(args, "relative_path", &relative, "."); *output = workspace_search(ctx->workspace_root, query, relative, max); free(query); free(relative); return *output != NULL; }
static int tool_glob(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *pattern = NULL; char *relative = NULL; size_t max = arg_size_limit(args, "max_results", 100, 1000); arg_string(args, "pattern", &pattern, "*"); arg_string(args, "relative_path", &relative, "."); *output = workspace_glob(ctx->workspace_root, pattern, relative, max); free(pattern); free(relative); return *output != NULL; }
static int tool_run_command(void *context, const char *args, char **output, char **error) { ToolContext *ctx = (ToolContext *)context; char *command = NULL; arg_string(args, "command", &command, ""); int timeout_ms = arg_seconds_ms(args, "timeout_seconds", 120, 900); ZenoExecResult result = zeno_sandbox_execute(ctx->sandbox, command, ctx->workspace_root, timeout_ms); int ok = result.ok; *output = ok ? zeno_strdup(result.output) : zeno_format("%s%s%s", result.blocked ? "SANDBOX BLOCKED: " : "Error: ", result.reason != NULL ? result.reason : "command failed", result.output != NULL && *result.output != '\0' ? "\n" : ""); if (*output != NULL && result.output != NULL && !ok && *result.output != '\0') { char *next = zeno_format("%s%s", *output, result.output); free(*output); *output = next; } if (!ok && error != NULL) *error = zeno_strdup(result.reason != NULL ? result.reason : "command failed"); zeno_exec_result_free(&result); free(command); return *output != NULL && ok; }
static int tool_start_job(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *command = NULL; char *name = NULL; arg_string(args, "command", &command, ""); arg_string(args, "name", &name, ""); *output = zeno_sandbox_start_job(ctx->sandbox, command, name, ctx->workspace_root); free(command); free(name); return *output != NULL; }
static int tool_list_jobs(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_sandbox_list_jobs(ctx->sandbox, zeno_json_get_bool(args, "include_completed", 1)); return *output != NULL; }
static int tool_tail_job(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *id = NULL; arg_string(args, "job_id", &id, ""); *output = zeno_sandbox_tail_job(ctx->sandbox, id, arg_size_limit(args, "max_lines", 100, 10000)); free(id); return *output != NULL; }
static int tool_cancel_job(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *id = NULL; arg_string(args, "job_id", &id, ""); *output = zeno_sandbox_cancel_job(ctx->sandbox, id); free(id); return *output != NULL; }
static int tool_memory_remember(void *context, const char *args, char **output, char **error) { ToolContext *ctx = (ToolContext *)context; char *title = NULL; char *content = NULL; char *kind = NULL; char *scope = NULL; char *tags = NULL; arg_string(args, "title", &title, ""); arg_string(args, "content", &content, ""); arg_string(args, "kind", &kind, "note"); arg_string(args, "scope", &scope, "global"); arg_string(args, "tags_json", &tags, "[]"); char *id = NULL; int ok = zeno_memory_add_note(ctx->memory, title, content, kind, scope, tags, &id); *output = ok ? zeno_format("Memory stored: %s", id != NULL ? id : "unknown") : zeno_strdup("Error: tags_json must be a JSON string array."); if (!ok && error != NULL) *error = zeno_strdup("invalid memory note"); free(title); free(content); free(kind); free(scope); free(tags); free(id); return *output != NULL && ok; }
static int tool_memory_search(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *query = NULL; arg_string(args, "query", &query, ""); *output = zeno_memory_search_notes(ctx->memory, query, arg_size_limit(args, "limit", 20, 1000)); free(query); return *output != NULL; }
static int tool_memory_list(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *scope = NULL; arg_string(args, "scope", &scope, ""); *output = zeno_memory_list_notes(ctx->memory, scope, arg_size_limit(args, "limit", 50, 1000)); free(scope); return *output != NULL; }
static int tool_assert_true(void *context, const char *args, char **output, char **error) { (void)context; (void)error; char *description = NULL; char *value = NULL; char *expected = NULL; arg_string(args, "condition_description", &description, "assertion"); arg_string(args, "value", &value, ""); arg_string(args, "expected", &expected, ""); *output = zeno_assert_true(description, value, expected); free(description); free(value); free(expected); return *output != NULL; }
static int tool_assert_contains(void *context, const char *args, char **output, char **error) { (void)context; (void)error; char *description = NULL; char *value = NULL; char *substring = NULL; arg_string(args, "condition_description", &description, "assertion"); arg_string(args, "value", &value, ""); arg_string(args, "substring", &substring, ""); *output = zeno_assert_contains(description, value, substring); free(description); free(value); free(substring); return *output != NULL; }
static int tool_memory_query(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *query = NULL; arg_string(args, "sql", &query, ""); if (zeno_contains_ci(query, "insert") || zeno_contains_ci(query, "update") || zeno_contains_ci(query, "delete") || zeno_contains_ci(query, "drop") || zeno_contains_ci(query, "alter") || zeno_contains_ci(query, "create")) *output = zeno_strdup("Error: Only SELECT queries allowed."); else *output = zeno_memory_search_notes(ctx->memory, "", 50); free(query); return *output != NULL; }
static int tool_memory_insert(void *context, const char *args, char **output, char **error) { (void)context; char *table = NULL; char *data = NULL; arg_string(args, "table", &table, ""); arg_string(args, "data_json", &data, ""); char *parse_error = NULL; ZjNode *node = zj_parse(data, &parse_error); int valid = node != NULL && (strcmp(table, "memory_records") == 0 || strcmp(table, "facts") == 0); zj_free(node); free(parse_error); *output = valid ? zeno_format("Inserted into %s.", table) : zeno_strdup("Error: Invalid JSON or table not allowed."); if (!valid && error != NULL) *error = zeno_strdup("invalid database insert"); free(table); free(data); return *output != NULL && valid; }

static int tool_http(void *context, const char *args, char **output, char **error) {
    (void)context;
    char *url = NULL; char *method = NULL; char *text_body = NULL;
    arg_string(args, "url", &url, ""); arg_string(args, "method", &method, "GET"); arg_string(args, "text_body", &text_body, "");
    char *parse_error = NULL; ZjNode *root = zj_parse(args != NULL ? args : "{}", &parse_error); free(parse_error);
    char *headers = root != NULL ? zj_stringify_compact(zj_object_get(root, "headers")) : NULL;
    char *json_body = root != NULL && zj_object_has(root, "json_body") ? zj_stringify_compact(zj_object_get(root, "json_body")) : NULL;
    int ok = json_body == NULL || text_body == NULL || *text_body == '\0';
    if (!ok) { if (error != NULL) *error = zeno_strdup("json_body and text_body are mutually exclusive"); *output = zeno_strdup("Error: provide json_body OR text_body, not both."); }
    else ok = zeno_http_request(url, method, headers, json_body != NULL ? json_body : (*text_body != '\0' ? text_body : NULL), arg_seconds_ms(args, "timeout_seconds", 60, 900), arg_size_limit(args, "max_response_chars", 8000, 16U * 1024U * 1024U), output);
    if (!ok && *output == NULL) *output = zeno_strdup("Request error.");
    zj_free(root); free(url); free(method); free(text_body); free(headers); free(json_body); return ok;
}

static int tool_scrape(void *context, const char *args, char **output, char **error) {
    (void)context; (void)error; char *url = NULL; arg_string(args, "url", &url, ""); int ok = zeno_scrape_url(url, arg_seconds_ms(args, "timeout_seconds", 30, 900), arg_size_limit(args, "max_chars", 8000, 8U * 1024U * 1024U), output); free(url); return ok;
}

static int tool_browser_navigate(void *context, const char *args, char **output, char **error) {
    return tool_scrape(context, args, output, error);
}

static int tool_mcp(void *context, const char *args, char **output, char **error) {
    (void)context; char *server = NULL; char *command = NULL; char *tool_name = NULL; char *tool_args = NULL;
    arg_string(args, "server_url", &server, ""); arg_string(args, "command", &command, ""); arg_string(args, "tool_name", &tool_name, ""); arg_string(args, "args_json", &tool_args, "{}");
    if (command != NULL && *command != '\0') *output = zeno_mcp_call_stdio(command, tool_name, tool_args);
    else *output = zeno_mcp_call(server, tool_name, tool_args);
    int ok = *output != NULL && strncmp(*output, "MCP error", 9) != 0;
    if (!ok && error != NULL) *error = zeno_strdup("mcp call failed");
    free(server); free(command); free(tool_name); free(tool_args); return ok;
}

static int register_tool(ZenoRegistry *registry, const char *name, const char *description, const char *params, ZenoEffect effect, int approval, ZenoToolHandler handler, void *context);

static int tool_load_skill(void *context, const char *args, char **output, char **error) {
    ZenoRegistry *registry = (ZenoRegistry *)context;
    if (registry == NULL || registry->skills_dir == NULL) { if (error != NULL) *error = zeno_strdup("no skills directory attached"); return 0; }
    char *name = NULL;
    arg_string(args, "name", &name, "");
    int valid = name != NULL && *name != '\0' && strchr(name, '/') == NULL && strchr(name, '\\') == NULL && strstr(name, "..") == NULL;
    if (!valid) { free(name); if (error != NULL) *error = zeno_strdup("skill name required (no path separators)"); return 0; }
    char *path = zeno_format("%s/%s/SKILL.md", registry->skills_dir, name);
    int ok = 0;
    if (path != NULL) {
        FILE *file = fopen(path, "rb");
        if (file != NULL) {
            size_t capacity = 9000; size_t length = 0;
            char *buffer = (char *)malloc(capacity);
            if (buffer != NULL) {
                size_t got;
                while (length + 4096 < capacity && (got = fread(buffer + length, 1, 4096, file)) > 0) length += got;
                buffer[length] = '\0';
                *output = buffer;
                ok = 1;
            }
            (void)fclose(file);
        }
        free(path);
    }
    if (!ok) { if (error != NULL) *error = zeno_strdup("skill not found in skills directory"); }
    free(name);
    return ok;
}

int zeno_register_skill_tool(ZenoRegistry *registry, const char *skills_dir) {
    if (registry == NULL || skills_dir == NULL || *skills_dir == '\0') return 0;
    free(registry->skills_dir);
    registry->skills_dir = zeno_strdup(skills_dir);
    if (registry->skills_dir == NULL) return 0;
    return register_tool(registry, "load_skill", "Load the full instructions of one available skill by name (skills are listed in the system prompt).",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":\"Skill directory name\"}},\"required\":[\"name\"]}",
                         ZENO_EFFECT_READ_LOCAL, 0, tool_load_skill, registry);
}


static int tool_git_status(void *context, const char *args, char **output, char **error) { (void)args; (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_git_status(ctx->workspace_root); return *output != NULL; }
static int tool_git_diff(void *context, const char *args, char **output, char **error) { (void)args; (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_git_diff(ctx->workspace_root); return *output != NULL; }
static int tool_git_log(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_git_log(ctx->workspace_root, (int)arg_size_limit(args, "n", 10, 1000)); return *output != NULL; }
static int tool_git_checkpoint(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *message = NULL; arg_string(args, "message", &message, "Zeno checkpoint"); *output = zeno_git_checkpoint(ctx->workspace_root, message); free(message); return *output != NULL; }
static int tool_codebase(void *context, const char *args, char **output, char **error) { (void)args; (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_codebase_structure(ctx->workspace_root); return *output != NULL; }
static int tool_symbol(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; char *name = NULL; arg_string(args, "name", &name, ""); *output = zeno_find_symbol(ctx->workspace_root, name); free(name); return *output != NULL; }
static int tool_tests(void *context, const char *args, char **output, char **error) { (void)error; ToolContext *ctx = (ToolContext *)context; *output = zeno_run_tests_execute(ctx->workspace_root, arg_seconds_ms(args, "timeout_seconds", 120, 900)); return *output != NULL; }

static int register_tool(ZenoRegistry *registry, const char *name, const char *description, const char *params, ZenoEffect effect, int approval, ZenoToolHandler handler, void *context) { ZenoToolDefinition definition = {name, description, params, effect, approval, 120000, 0}; return zeno_registry_register(registry, definition, handler, context); }

int zeno_registry_register_builtins(ZenoRegistry *registry, ZenoSandbox *sandbox, ZenoMemory *memory, const char *workspace_root) {
    if (registry == NULL || workspace_root == NULL || registry->owned_context != NULL) return 0;
    ToolContext *context = (ToolContext *)calloc(1, sizeof(*context)); if (context == NULL) return 0; context->sandbox = sandbox; context->memory = memory; context->workspace_root = zeno_strdup(workspace_root); if (context->workspace_root == NULL) { tool_context_free(context); return 0; }
    const char *path = "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"}}}";
    int ok = 1;
    ok &= register_tool(registry, "list_workspace", "List files and folders inside the workspace.", path, ZENO_EFFECT_READ_LOCAL, 0, tool_list, context);
    ok &= register_tool(registry, "read_text_file", "Read a UTF-8 text file from the workspace.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"max_chars\":{\"type\":\"integer\"}},\"required\":[\"relative_path\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_read, context);
    ok &= register_tool(registry, "write_text_file", "Create or overwrite a text file in the workspace.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"relative_path\",\"content\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_write, context);
    ok &= register_tool(registry, "append_text_file", "Append text to a file in the workspace.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"relative_path\",\"content\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_append, context);
    ok &= register_tool(registry, "replace_in_file", "Replace exact text inside a workspace file.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"old_text\":{\"type\":\"string\"},\"new_text\":{\"type\":\"string\"},\"replace_all\":{\"type\":\"boolean\"}},\"required\":[\"relative_path\",\"old_text\",\"new_text\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_replace, context);
    ok &= register_tool(registry, "create_directory", "Create a folder inside the workspace.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"}},\"required\":[\"relative_path\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_mkdir, context);
    ok &= register_tool(registry, "search_workspace", "Search text across workspace files.", "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"relative_path\":{\"type\":\"string\"},\"max_results\":{\"type\":\"integer\"}},\"required\":[\"query\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_search, context);
    ok &= register_tool(registry, "glob_workspace", "Return workspace paths matching a glob pattern.", "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\"},\"relative_path\":{\"type\":\"string\"},\"max_results\":{\"type\":\"integer\"}},\"required\":[\"pattern\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_glob, context);
    ok &= register_tool(registry, "run_command", "Execute a shell command in the workspace sandbox.", "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"},\"timeout_seconds\":{\"type\":\"integer\"}},\"required\":[\"command\"]}", ZENO_EFFECT_PROCESS, 1, tool_run_command, context);
    ok &= register_tool(registry, "start_background_job", "Start a tracked shell command.", "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}},\"required\":[\"command\"]}", ZENO_EFFECT_PROCESS, 1, tool_start_job, context);
    ok &= register_tool(registry, "list_background_jobs", "List tracked background jobs.", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, tool_list_jobs, context);
    ok &= register_tool(registry, "tail_background_job", "Read background job output.", "{\"type\":\"object\",\"properties\":{\"job_id\":{\"type\":\"string\"},\"max_lines\":{\"type\":\"integer\"}},\"required\":[\"job_id\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_tail_job, context);
    ok &= register_tool(registry, "cancel_background_job", "Cancel a background job.", "{\"type\":\"object\",\"properties\":{\"job_id\":{\"type\":\"string\"}},\"required\":[\"job_id\"]}", ZENO_EFFECT_PROCESS, 1, tool_cancel_job, context);
    ok &= register_tool(registry, "memory_remember", "Persist a durable memory note.", "{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"tags_json\":{\"type\":\"string\"}},\"required\":[\"title\",\"content\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_memory_remember, context);
    ok &= register_tool(registry, "memory_search", "Search durable memories.", "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"query\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_memory_search, context);
    ok &= register_tool(registry, "memory_list", "List durable memories.", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, tool_memory_list, context);
    ok &= register_tool(registry, "assert_true", "Assert exact equality.", "{\"type\":\"object\",\"properties\":{\"condition_description\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"},\"expected\":{\"type\":\"string\"}},\"required\":[\"condition_description\",\"value\",\"expected\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_assert_true, context);
    ok &= register_tool(registry, "assert_contains", "Assert substring containment.", "{\"type\":\"object\",\"properties\":{\"condition_description\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"},\"substring\":{\"type\":\"string\"}},\"required\":[\"condition_description\",\"value\",\"substring\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_assert_contains, context);
    ok &= register_tool(registry, "db_query", "Run a read-only memory query.", "{\"type\":\"object\",\"properties\":{\"sql\":{\"type\":\"string\"}},\"required\":[\"sql\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_memory_query, context);
    ok &= register_tool(registry, "db_insert", "Insert a record into an allowed table.", "{\"type\":\"object\",\"properties\":{\"table\":{\"type\":\"string\"},\"data_json\":{\"type\":\"string\"}},\"required\":[\"table\",\"data_json\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_memory_insert, context);
    ok &= register_tool(registry, "http_request", "Perform an HTTP request.", "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"},\"method\":{\"type\":\"string\"},\"headers\":{\"type\":\"object\"},\"json_body\":{},\"text_body\":{\"type\":\"string\"},\"timeout_seconds\":{\"type\":\"integer\"},\"max_response_chars\":{\"type\":\"integer\"}},\"required\":[\"url\"]}", ZENO_EFFECT_WRITE_EXTERNAL, 1, tool_http, context);
    ok &= register_tool(registry, "scrape_url", "Fetch a URL and extract readable text (approval required).", "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"},\"max_chars\":{\"type\":\"integer\"}},\"required\":[\"url\"]}", ZENO_EFFECT_READ_EXTERNAL, 1, tool_scrape, context);
    ok &= register_tool(registry, "browser_navigate", "Navigate a lightweight external reader.", "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}", ZENO_EFFECT_BROWSER, 1, tool_browser_navigate, context);
    ok &= register_tool(registry, "git_status", "Show Git status.", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, tool_git_status, context);
    ok &= register_tool(registry, "git_diff", "Show the current Git diff.", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, tool_git_diff, context);
    ok &= register_tool(registry, "git_log", "Show recent Git commits.", "{\"type\":\"object\",\"properties\":{\"n\":{\"type\":\"integer\"}}}", ZENO_EFFECT_READ_LOCAL, 0, tool_git_log, context);
    ok &= register_tool(registry, "git_checkpoint", "Create a Git checkpoint.", "{\"type\":\"object\",\"properties\":{\"message\":{\"type\":\"string\"}}}", ZENO_EFFECT_PROCESS, 1, tool_git_checkpoint, context);
    ok &= register_tool(registry, "run_tests", "Detect the workspace test framework.", "{\"type\":\"object\"}", ZENO_EFFECT_PROCESS, 1, tool_tests, context);
    ok &= register_tool(registry, "codebase_structure", "Scan code structure.", "{\"type\":\"object\"}", ZENO_EFFECT_READ_LOCAL, 0, tool_codebase, context);
    ok &= register_tool(registry, "find_symbol", "Find code symbols by name.", "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_symbol, context);
    ok &= register_tool(registry, "mcp_call", "Call a tool on an external MCP server: pass server_url for HTTP JSON-RPC servers or command for stdio servers (e.g. \"node server.js\"); args_json carries the tool arguments.", "{\"type\":\"object\",\"properties\":{\"server_url\":{\"type\":\"string\"},\"command\":{\"type\":\"string\",\"description\":\"stdio server command line\"},\"tool_name\":{\"type\":\"string\"},\"args_json\":{\"type\":\"string\"}},\"required\":[\"tool_name\"]}", ZENO_EFFECT_WRITE_EXTERNAL, 1, tool_mcp, context);
    ok &= register_tool(registry, "update_plan", "Persist the live task plan as Markdown checklist lines ('- [ ] pending', '- [x] done'). Re-injected into your system prompt every turn.", "{\"type\":\"object\",\"properties\":{\"plan_markdown\":{\"type\":\"string\"}},\"required\":[\"plan_markdown\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_update_plan, context);
    ok &= register_tool(registry, "spawn_subagent", "Run an isolated read-only subagent on a self-contained research/analysis task and return its final report. Subagents cannot write files or execute commands.", "{\"type\":\"object\",\"properties\":{\"task\":{\"type\":\"string\"},\"max_turns\":{\"type\":\"integer\"}},\"required\":[\"task\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_spawn_subagent, context);
    registry->owned_context = context;
    return ok;
}

int zeno_registry_register_minimal(ZenoRegistry *registry, ZenoSandbox *sandbox, ZenoMemory *memory, const char *workspace_root) {
    if (registry == NULL || workspace_root == NULL || registry->owned_context != NULL) return 0;
    ToolContext *context = (ToolContext *)calloc(1, sizeof(*context)); if (context == NULL) return 0;
    context->sandbox = sandbox; context->memory = memory; context->workspace_root = zeno_strdup(workspace_root); if (context->workspace_root == NULL) { tool_context_free(context); return 0; }
    int ok = 1;
    ok &= register_tool(registry, "read_text_file", "Read a UTF-8 text file from the workspace.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"max_chars\":{\"type\":\"integer\"}},\"required\":[\"relative_path\"]}", ZENO_EFFECT_READ_LOCAL, 0, tool_read, context);
    ok &= register_tool(registry, "write_text_file", "Create or overwrite a text file in the workspace.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"relative_path\",\"content\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_write, context);
    ok &= register_tool(registry, "replace_in_file", "Replace exact text inside a workspace file.", "{\"type\":\"object\",\"properties\":{\"relative_path\":{\"type\":\"string\"},\"old_text\":{\"type\":\"string\"},\"new_text\":{\"type\":\"string\"},\"replace_all\":{\"type\":\"boolean\"}},\"required\":[\"relative_path\",\"old_text\",\"new_text\"]}", ZENO_EFFECT_WRITE_LOCAL, 0, tool_replace, context);
    ok &= register_tool(registry, "run_command", "Execute a shell command in the workspace sandbox.", "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"},\"timeout_seconds\":{\"type\":\"integer\"}},\"required\":[\"command\"]}", ZENO_EFFECT_PROCESS, 1, tool_run_command, context);
    registry->owned_context = context;
    return ok;
}

#define ZENO_MAX_TOOL_ARGS_JSON (16U * 1024U * 1024U)

static int schema_type_known(const char *type) {
    return type != NULL && (strcmp(type, "string") == 0 || strcmp(type, "integer") == 0 || strcmp(type, "number") == 0 || strcmp(type, "boolean") == 0 || strcmp(type, "object") == 0 || strcmp(type, "array") == 0 || strcmp(type, "null") == 0);
}

static int schema_definition_valid(const ZjNode *schema) {
    if (schema == NULL || schema->type != ZJ_OBJECT) return 0;
    ZjNode *root_type = zj_object_get(schema, "type");
    if (root_type != NULL && (root_type->type != ZJ_STRING || strcmp(root_type->string, "object") != 0)) return 0;
    ZjNode *required = zj_object_get(schema, "required");
    if (required != NULL) {
        if (required->type != ZJ_ARRAY) return 0;
        for (size_t index = 0; index < required->count; index++) if (required->items[index] == NULL || required->items[index]->type != ZJ_STRING || required->items[index]->string == NULL || *required->items[index]->string == '\0') return 0;
    }
    ZjNode *properties = zj_object_get(schema, "properties");
    if (properties != NULL) {
        if (properties->type != ZJ_OBJECT) return 0;
        for (ZjPair *pair = properties->object; pair != NULL; pair = pair->next) {
            if (pair->value == NULL || pair->value->type != ZJ_OBJECT) return 0;
            ZjNode *type_node = zj_object_get(pair->value, "type");
            if (type_node != NULL && (type_node->type != ZJ_STRING || !schema_type_known(type_node->string))) return 0;
            ZjNode *enumeration = zj_object_get(pair->value, "enum");
            if (enumeration != NULL && enumeration->type != ZJ_ARRAY) return 0;
        }
    }
    return 1;
}

static int schema_value_matches(const ZjNode *value, const char *type) {
    if (type == NULL || value == NULL) return 0;
    if (strcmp(type, "string") == 0) return value->type == ZJ_STRING;
    if (strcmp(type, "integer") == 0) {
        if (value->type != ZJ_NUMBER || value->number < -9223372036854774784.0 || value->number > 9223372036854774784.0) return 0;
        return value->number == (double)(long long)value->number;
    }
    if (strcmp(type, "number") == 0) return value->type == ZJ_NUMBER;
    if (strcmp(type, "boolean") == 0) return value->type == ZJ_BOOL;
    if (strcmp(type, "object") == 0) return value->type == ZJ_OBJECT;
    if (strcmp(type, "array") == 0) return value->type == ZJ_ARRAY;
    if (strcmp(type, "null") == 0) return value->type == ZJ_NULL;
    return 0;
}

static int schema_enum_matches(const ZjNode *value, const ZjNode *enumeration) {
    if (value == NULL || enumeration == NULL || enumeration->type != ZJ_ARRAY) return 1;
    for (size_t index = 0; index < enumeration->count; index++) {
        const ZjNode *candidate = enumeration->items[index];
        if (candidate->type != value->type) continue;
        if (value->type == ZJ_STRING && strcmp(value->string, candidate->string) == 0) return 1;
        if (value->type == ZJ_NUMBER && value->number == candidate->number) return 1;
        if (value->type == ZJ_BOOL && value->boolean == candidate->boolean) return 1;
        if (value->type == ZJ_NULL) return 1;
    }
    return 0;
}

int zeno_registry_validate_args(const ZenoRegisteredTool *tool, const char *args_json, char **error) {
    if (error != NULL) *error = NULL;
    if (tool == NULL) {
        if (error != NULL) *error = zeno_strdup("Unknown tool.");
        return 0;
    }
    if (args_json != NULL && strlen(args_json) > ZENO_MAX_TOOL_ARGS_JSON) {
        if (error != NULL) *error = zeno_strdup("Error: tool arguments exceed the 16 MiB limit.");
        return 0;
    }
    char *parse_error = NULL;
    ZjNode *args = zj_parse(args_json != NULL && *args_json != '\0' ? args_json : "{}", &parse_error);
    if (args == NULL || args->type != ZJ_OBJECT) {
        if (error != NULL) *error = zeno_strdup("Arguments must be a JSON object.");
        free(parse_error); zj_free(args); return 0;
    }
    char *schema_error = NULL;
    ZjNode *schema = zj_parse(tool->parameters_json != NULL ? tool->parameters_json : "{\"type\":\"object\"}", &schema_error);
    if (schema == NULL || schema->type != ZJ_OBJECT) {
        if (error != NULL) *error = zeno_format("Invalid schema for tool %s.", tool->name);
        free(parse_error); free(schema_error); zj_free(args); zj_free(schema); return 0;
    }
    ZjNode *required = zj_object_get(schema, "required");
    if (required != NULL && required->type == ZJ_ARRAY) for (size_t index = 0; index < required->count; index++) {
        const char *name = zj_string(required->items[index]);
        if (name != NULL && !zj_object_has(args, name)) {
            if (error != NULL) *error = zeno_format("Error: missing required argument for %s: %s", tool->name, name);
            zj_free(args); zj_free(schema); free(parse_error); free(schema_error); return 0;
        }
    }
    ZjNode *properties = zj_object_get(schema, "properties");
    if (properties != NULL && properties->type == ZJ_OBJECT) {
        for (ZjPair *pair = properties->object; pair != NULL; pair = pair->next) {
            ZjNode *value = zj_object_get(args, pair->key);
            if (value == NULL) continue;
            ZjNode *type_node = zj_object_get(pair->value, "type");
            const char *type = zj_string(type_node);
            if (type != NULL && !schema_value_matches(value, type)) {
                if (error != NULL) *error = zeno_format("Error: argument '%s' for %s must be a %s.", pair->key, tool->name, type);
                zj_free(args); zj_free(schema); free(parse_error); free(schema_error); return 0;
            }
            if (!schema_enum_matches(value, zj_object_get(pair->value, "enum"))) {
                if (error != NULL) *error = zeno_format("Error: argument '%s' for %s has an invalid value.", pair->key, tool->name);
                zj_free(args); zj_free(schema); free(parse_error); free(schema_error); return 0;
            }
        }
    }
    zj_free(args); zj_free(schema); free(parse_error); free(schema_error); return 1;
}

ZenoRegistry *zeno_registry_create(void) { return (ZenoRegistry *)calloc(1, sizeof(ZenoRegistry)); }

void zeno_registry_destroy(ZenoRegistry *registry) { free(registry->skills_dir);
    if (registry == NULL) return;
    for (size_t index = 0; index < registry->count; index++) { free(registry->tools[index].name); free(registry->tools[index].description); free(registry->tools[index].parameters_json); }
    free(registry->tools);
    tool_context_free((ToolContext *)registry->owned_context);
    zeno_mutex_destroy(&registry->lock);
    free(registry);
}

int zeno_tool_index(const ZenoRegistry *registry, const char *name) { if (name == NULL) return -1; for (size_t index = 0; registry != NULL && index < registry->count; index++) if (strcmp(registry->tools[index].name, name) == 0) return (int)index; return -1; }

static int zeno_registry_register_locked(ZenoRegistry *registry, ZenoToolDefinition definition, ZenoToolHandler handler, void *context) {
    if (registry == NULL || definition.name == NULL || *definition.name == '\0' || handler == NULL) return 0;
    if (zeno_tool_index(registry, definition.name) >= 0) return 0;
    const char *schema_text = definition.parameters_json != NULL ? definition.parameters_json : "{\"type\":\"object\"}";
    char *schema_error = NULL;
    ZjNode *schema = zj_parse(schema_text, &schema_error);
    int schema_valid = schema_definition_valid(schema);
    zj_free(schema); free(schema_error);
    if (!schema_valid) return 0;
    if (registry->count == registry->capacity) {
        if (registry->capacity > (size_t)-1 / 2) return 0;
        size_t next = registry->capacity == 0 ? 16 : registry->capacity * 2;
        ZenoRegisteredTool *grown = (ZenoRegisteredTool *)realloc(registry->tools, next * sizeof(*grown));
        if (grown == NULL) return 0;
        registry->tools = grown; registry->capacity = next;
    }
    ZenoRegisteredTool candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.name = zeno_strdup(definition.name);
    candidate.description = zeno_strdup(definition.description != NULL ? definition.description : "");
    candidate.parameters_json = zeno_strdup(schema_text);
    if (candidate.name == NULL || candidate.description == NULL || candidate.parameters_json == NULL) {
        free(candidate.name); free(candidate.description); free(candidate.parameters_json);
        return 0;
    }
    candidate.effect = definition.effect;
    candidate.requires_approval = definition.requires_approval ? 1 : 0;
    candidate.timeout_ms = definition.timeout_ms > 0 ? definition.timeout_ms : 120000;
    candidate.max_retries = definition.max_retries >= 0 ? definition.max_retries : 0;
    candidate.handler = handler;
    candidate.context = context;
    registry->tools[registry->count++] = candidate;
    return 1;
}
int zeno_registry_register(ZenoRegistry *registry, ZenoToolDefinition definition, ZenoToolHandler handler, void *context) { if (registry != NULL) zeno_mutex_lock((ZenoMutex *)&registry->lock); int zeno_result = zeno_registry_register_locked(registry, definition, handler, context); if (registry != NULL) zeno_mutex_unlock((ZenoMutex *)&registry->lock); return zeno_result; }

static int zeno_registry_unregister_locked(ZenoRegistry *registry, const char *name) { int index = zeno_tool_index(registry, name); if (index < 0) return 0; ZenoRegisteredTool *tool = &registry->tools[index]; free(tool->name); free(tool->description); free(tool->parameters_json); memmove(tool, tool + 1, (registry->count - (size_t)index - 1) * sizeof(*tool)); registry->count--; return 1; }
int zeno_registry_unregister(ZenoRegistry *registry, const char *name) { if (registry != NULL) zeno_mutex_lock((ZenoMutex *)&registry->lock); int zeno_result = zeno_registry_unregister_locked(registry, name); if (registry != NULL) zeno_mutex_unlock((ZenoMutex *)&registry->lock); return zeno_result; }
int zeno_registry_has(const ZenoRegistry *registry, const char *name) {
    if (registry == NULL) return 0;
    zeno_mutex_lock((ZenoMutex *)&registry->lock);
    int found = zeno_tool_index(registry, name) >= 0;
    zeno_mutex_unlock((ZenoMutex *)&registry->lock);
    return found;
}
int zeno_registry_requires_approval(const ZenoRegistry *registry, const char *name) {
    if (registry == NULL) return 0;
    zeno_mutex_lock((ZenoMutex *)&registry->lock);
    int index = zeno_tool_index(registry, name);
    int required = index >= 0 && registry->tools[index].requires_approval;
    zeno_mutex_unlock((ZenoMutex *)&registry->lock);
    return required;
}
int zeno_registry_is_read_only(const ZenoRegistry *registry, const char *name) {
    if (registry == NULL) return 0;
    zeno_mutex_lock((ZenoMutex *)&registry->lock);
    int index = zeno_tool_index(registry, name);
    int read_only = index >= 0 && (registry->tools[index].effect == ZENO_EFFECT_READ_LOCAL || registry->tools[index].effect == ZENO_EFFECT_READ_EXTERNAL);
    zeno_mutex_unlock((ZenoMutex *)&registry->lock);
    return read_only;
}

char *zeno_registry_list_json(const ZenoRegistry *registry) {
    char *result = zeno_strdup("[");
    if (registry != NULL) zeno_mutex_lock((ZenoMutex *)&registry->lock);
    for (size_t index = 0; registry != NULL && index < registry->count; index++) {
        const ZenoRegisteredTool *tool = &registry->tools[index];
        const char *effect = tool->effect == ZENO_EFFECT_PROCESS ? "process" : tool->effect == ZENO_EFFECT_WRITE_EXTERNAL ? "write_external" : tool->effect == ZENO_EFFECT_BROWSER ? "browser" : tool->effect == ZENO_EFFECT_WRITE_LOCAL ? "write_local" : tool->effect == ZENO_EFFECT_READ_EXTERNAL ? "read_external" : "read_local";
        char *item = zeno_format("{\"name\":%s,\"description\":%s,\"effect\":%s,\"requires_approval\":%s}", zeno_json_escape(tool->name), zeno_json_escape(tool->description), zeno_json_escape(effect), tool->requires_approval ? "true" : "false");
        char *next = item != NULL ? zeno_json_array_append(result, item) : NULL; free(item); free(result); result = next;
    }
    if (registry != NULL) zeno_mutex_unlock((ZenoMutex *)&registry->lock);
    return result != NULL ? result : zeno_strdup("[]");
}
char *zeno_registry_openai_schema_json(const ZenoRegistry *registry) {
    char *result = zeno_strdup("[");
    if (registry != NULL) zeno_mutex_lock((ZenoMutex *)&registry->lock);
    for (size_t index = 0; registry != NULL && index < registry->count; index++) {
        const ZenoRegisteredTool *tool = &registry->tools[index];
        char *item = zeno_format("{\"type\":\"function\",\"function\":{\"name\":%s,\"description\":%s,\"parameters\":%s}}", zeno_json_escape(tool->name), zeno_json_escape(tool->description), tool->parameters_json);
        char *next = item != NULL ? zeno_json_array_append(result, item) : NULL; free(item); free(result); result = next;
    }
    if (registry != NULL) zeno_mutex_unlock((ZenoMutex *)&registry->lock);
    return result != NULL ? result : zeno_strdup("[]");
}

static void registry_tool_snapshot_free(ZenoRegisteredTool *tool) {
    if (tool == NULL) return;
    free(tool->name); free(tool->description); free(tool->parameters_json);
    memset(tool, 0, sizeof(*tool));
}

ZenoToolResult zeno_registry_execute(ZenoRegistry *registry, const char *name, const char *args_json) {
    ZenoToolResult result;
    memset(&result, 0, sizeof(result));
    result.name = zeno_strdup(name != NULL ? name : "");
    long long started = zeno_now_ms();
    ZenoRegisteredTool tool;
    memset(&tool, 0, sizeof(tool));
    int index = -1;
    if (registry != NULL) zeno_mutex_lock(&registry->lock);
    index = zeno_tool_index(registry, name);
    if (index >= 0) {
        const ZenoRegisteredTool *source = &registry->tools[index];
        tool.name = zeno_strdup(source->name);
        tool.description = zeno_strdup(source->description);
        tool.parameters_json = zeno_strdup(source->parameters_json);
        tool.effect = source->effect;
        tool.requires_approval = source->requires_approval;
        tool.timeout_ms = source->timeout_ms;
        tool.max_retries = source->max_retries;
        tool.handler = source->handler;
        tool.context = source->context;
    }
    if (registry != NULL) zeno_mutex_unlock(&registry->lock);
    if (index < 0) {
        result.output = zeno_format("Error: tool '%s' not found.", name != NULL ? name : "");
        result.error = zeno_strdup("not_found");
        result.error_code = zeno_tool_error_code(result.error);
        result.latency_ms = zeno_now_ms() - started;
        return result;
    }
    if (tool.name == NULL || tool.parameters_json == NULL) {
        registry_tool_snapshot_free(&tool);
        result.output = zeno_strdup("Error: tool metadata allocation failed.");
        result.error = zeno_strdup("out_of_memory");
        result.error_code = zeno_tool_error_code(result.error);
        result.latency_ms = zeno_now_ms() - started;
        return result;
    }
    char *validation = NULL;
    if (!zeno_registry_validate_args(&tool, args_json, &validation)) {
        result.output = validation != NULL ? validation : zeno_strdup("Error: invalid arguments.");
        result.error = zeno_strdup("invalid_arguments");
        result.error_code = zeno_tool_error_code(result.error);
        registry_tool_snapshot_free(&tool);
        result.latency_ms = zeno_now_ms() - started;
        return result;
    }
    for (int attempt = 1; attempt <= tool.max_retries + 1; attempt++) {
        char *output = NULL;
        char *error = NULL;
        long long attempt_started = zeno_now_ms();
        int ok = tool.handler(tool.context, args_json != NULL ? args_json : "{}", &output, &error);
        result.attempts = attempt;
        if (tool.timeout_ms > 0 && zeno_now_ms() - attempt_started > tool.timeout_ms) {
            free(output);
            free(error);
            output = zeno_format("Error executing %s: tool timed out after %dms.", name, tool.timeout_ms);
            error = zeno_strdup("tool_timeout");
            ok = 0;
        }
        if (ok && error == NULL) {
            result.ok = 1;
            result.output = output != NULL ? output : zeno_strdup("(empty)");
            break;
        }
        free(result.output);
        result.output = output != NULL ? output : zeno_format("Error executing %s: %s", name, error != NULL ? error : "tool failed");
        free(result.error);
        result.error = error != NULL ? error : zeno_strdup("tool_failed");
        if (attempt == tool.max_retries + 1) break;
        /* Linear backoff between handler retries. Validation errors never
         * reach this path: they return immediately to the caller above. */
        zeno_sleep_ms(attempt <= 4 ? attempt * 100 : 500);
    }
    registry_tool_snapshot_free(&tool);
    result.error_code = zeno_tool_error_code(result.error);
    result.latency_ms = zeno_now_ms() - started;
    return result;
}

void zeno_tool_result_free(ZenoToolResult *result) { if (result == NULL) return; free(result->name); free(result->output); free(result->error); memset(result, 0, sizeof(*result)); }

int zeno_tool_error_code(const char *error_string) {
    if (error_string == NULL || *error_string == '\0') return ZENO_TOOL_OK;
    if (strcmp(error_string, "not_found") == 0) return ZENO_TOOL_ERROR_NOT_FOUND;
    if (strcmp(error_string, "invalid_arguments") == 0 || strcmp(error_string, "invalid_plan") == 0) return ZENO_TOOL_ERROR_INVALID_ARGUMENTS;
    if (strcmp(error_string, "tool_timeout") == 0) return ZENO_TOOL_ERROR_TIMEOUT;
    if (strcmp(error_string, "approval_required") == 0) return ZENO_TOOL_ERROR_APPROVAL_REQUIRED;
    if (strcmp(error_string, "read_before_edit") == 0) return ZENO_TOOL_ERROR_READ_BEFORE_EDIT;
    if (strcmp(error_string, "sandbox_blocked") == 0) return ZENO_TOOL_ERROR_SANDBOX_BLOCKED;
    if (strcmp(error_string, "subagent_nested") == 0 || strcmp(error_string, "subagent_unavailable") == 0) return ZENO_TOOL_ERROR_SUBAGENT_LIMIT;
    if (strcmp(error_string, "out_of_memory") == 0) return ZENO_TOOL_ERROR_OUT_OF_MEMORY;
    if (strcmp(error_string, "adapter_disabled") == 0 || strcmp(error_string, "not_enabled") == 0) return ZENO_TOOL_ERROR_UNAVAILABLE;
    return ZENO_TOOL_ERROR_GENERIC;
}
