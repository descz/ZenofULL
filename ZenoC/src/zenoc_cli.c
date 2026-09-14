#include "zeno.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *stream) {
    fprintf(stream,
        "ZenoC %s\n"
        "Usage:\n"
        "  zenoc [--minimal] --check\n"
        "  zenoc [--minimal] --tools\n"
        "  zenoc [--minimal] --run <session> <task>\n"
        "  zenoc [--minimal] --squad <session> <task> [roles_csv]\n"
        "  zenoc [--minimal] --squad-ex <agents_dir> <session> <task> [roles_csv]\n"
        "  zenoc --voice-config|--voice-stt-url [lang]|--voice-tts-url [voice]|--voice-greet\n"
        "  zenoc --voice-extract <provider_json>\n"
        "  zenoc --settings-list [store]|--settings-get <section.key> [store]\n"
        "  zenoc --settings-set <section.key> <value> [store]\n"
        "  zenoc --projects [store]|--project-create <name> [folders_csv] [store]\n"
        "  zenoc --project-rename <id> <new_name> [store]|--project-delete <id> [store]\n"
        "  zenoc --remotes [store]|--remote-add <type> <name> <target> [store]\n"
        "  zenoc --remote-remove <id> [store]|--remote-rename <id> <new> [store]\n"
        "  zenoc --remote-connect <id> [store]\n"
        "  zenoc --plugins [dir]|--plugin-apply [dir]|--plugin-guide\n"
        "  zenoc --plugin-create <request> [name_hint] [dir]\n"
        "  zenoc --plugin-enable <id> [dir]|--plugin-disable <id> [dir]\n"
        "  zenoc --plugin-rename <id> <new_name> [dir]|--plugin-validate <json>\n"
        "Options:\n"
        "  --minimal      use the four-tool precision coding profile\n"
        "  --skills-dir <dir>  attach skills (dirs with SKILL.md) + load_skill tool\n"
        "  --help         show this help\n"
        "  --version      show the version\n",
        ZENO_VERSION);
}

static int is_studio_command(const char *arg) {
    static const char *cmds[] = {
        "--voice-config", "--voice-stt-url", "--voice-tts-url", "--voice-greet",
        "--voice-extract", "--settings-list", "--settings-get", "--settings-set",
        "--projects", "--project-create", "--project-rename", "--project-delete",
        "--remotes", "--remote-add", "--remote-remove", "--remote-rename",
        "--remote-connect", "--plugins", "--plugin-create", "--plugin-enable",
        "--plugin-disable", "--plugin-rename", "--plugin-apply", "--plugin-guide",
        "--plugin-validate",
    };
    size_t i;
    if (arg == NULL) return 0;
    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        if (strcmp(arg, cmds[i]) == 0) return 1;
    return 0;
}

static const char *studio_arg(int argc, char **argv, int index, const char *fallback) {
    if (index < argc && argv[index] != NULL && argv[index][0] != '\0') return argv[index];
    return fallback;
}

/* Studio backend is pure C and offline-first: no LLM provider required. */
static int run_studio(int argc, char **argv) {
    const char *cmd = argv[1];
    if (strcmp(cmd, "--voice-config") == 0) {
        ZenoVoiceConfig voice;
        char *json;
        zeno_voice_config_load_env(&voice);
        json = zeno_voice_describe(&voice);
        if (json == NULL) return 1;
        puts(json);
        zeno_free(json);
        return 0;
    }
    if (strcmp(cmd, "--voice-stt-url") == 0) {
        ZenoVoiceConfig voice;
        char *url;
        zeno_voice_config_load_env(&voice);
        url = zeno_voice_stt_url(&voice, studio_arg(argc, argv, 2, NULL));
        if (url == NULL) return 1;
        puts(url);
        zeno_free(url);
        return 0;
    }
    if (strcmp(cmd, "--voice-tts-url") == 0) {
        ZenoVoiceConfig voice;
        char *url;
        zeno_voice_config_load_env(&voice);
        url = zeno_voice_tts_url(&voice, studio_arg(argc, argv, 2, NULL));
        if (url == NULL) return 1;
        puts(url);
        zeno_free(url);
        return 0;
    }
    if (strcmp(cmd, "--voice-greet") == 0) {
        ZenoVoiceConfig voice;
        char *greeting;
        zeno_voice_config_load_env(&voice);
        greeting = zeno_voice_greeting(&voice);
        if (greeting == NULL) return 1;
        puts(greeting);
        zeno_free(greeting);
        return 0;
    }
    if (strcmp(cmd, "--voice-extract") == 0) {
        char *text;
        if (argc < 3) {
            fputs("ZenoC: --voice-extract takes <provider_json>.\n", stderr);
            return 2;
        }
        text = zeno_voice_transcript_extract(argv[2]);
        if (text == NULL) {
            fputs("ZenoC: no transcript found.\n", stderr);
            return 1;
        }
        puts(text);
        zeno_free(text);
        return 0;
    }
    if (strcmp(cmd, "--settings-list") == 0) {
        char *json = zeno_settings_list(studio_arg(argc, argv, 2, "zeno_settings.md"));
        if (json == NULL) return 1;
        puts(json);
        zeno_free(json);
        return 0;
    }
    if (strcmp(cmd, "--settings-get") == 0) {
        const char *dot;
        char section[65];
        const char *key;
        char *value;
        size_t len;
        if (argc < 3) {
            fputs("ZenoC: --settings-get takes <section.key> [store].\n", stderr);
            return 2;
        }
        dot = strchr(argv[2], '.');
        if (dot == NULL || dot == argv[2] || dot[1] == '\0' || (size_t)(dot - argv[2]) > 64) {
            fputs("ZenoC: key must look like <section.key>.\n", stderr);
            return 2;
        }
        len = (size_t)(dot - argv[2]);
        memcpy(section, argv[2], len);
        section[len] = '\0';
        key = dot + 1;
        value = zeno_settings_get(studio_arg(argc, argv, 3, "zeno_settings.md"), section, key);
        if (value == NULL) {
            fputs("ZenoC: setting not found.\n", stderr);
            return 1;
        }
        puts(value);
        zeno_free(value);
        return 0;
    }
    if (strcmp(cmd, "--settings-set") == 0) {
        const char *dot;
        char section[65];
        const char *key;
        size_t len;
        if (argc < 4) {
            fputs("ZenoC: --settings-set takes <section.key> <value> [store].\n", stderr);
            return 2;
        }
        dot = strchr(argv[2], '.');
        if (dot == NULL || dot == argv[2] || dot[1] == '\0' || (size_t)(dot - argv[2]) > 64) {
            fputs("ZenoC: key must look like <section.key>.\n", stderr);
            return 2;
        }
        len = (size_t)(dot - argv[2]);
        memcpy(section, argv[2], len);
        section[len] = '\0';
        key = dot + 1;
        if (!zeno_settings_set(studio_arg(argc, argv, 4, "zeno_settings.md"), section, key, argv[3])) {
            fputs("ZenoC: could not save setting.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--projects") == 0) {
        char *json = zeno_projects_list(studio_arg(argc, argv, 2, "zeno_projects.md"));
        if (json == NULL) return 1;
        puts(json);
        zeno_free(json);
        return 0;
    }
    if (strcmp(cmd, "--project-create") == 0) {
        char *item;
        if (argc < 3) {
            fputs("ZenoC: --project-create takes <name> [folders_csv] [store].\n", stderr);
            return 2;
        }
        item = zeno_project_create(studio_arg(argc, argv, 4, "zeno_projects.md"), argv[2],
                                   studio_arg(argc, argv, 3, ""));
        if (item == NULL) {
            fputs("ZenoC: could not create project.\n", stderr);
            return 1;
        }
        puts(item);
        zeno_free(item);
        return 0;
    }
    if (strcmp(cmd, "--project-rename") == 0) {
        if (argc < 4) {
            fputs("ZenoC: --project-rename takes <id> <new_name> [store].\n", stderr);
            return 2;
        }
        if (!zeno_project_rename(studio_arg(argc, argv, 4, "zeno_projects.md"), argv[2], argv[3])) {
            fputs("ZenoC: could not rename project.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--project-delete") == 0) {
        if (argc < 3) {
            fputs("ZenoC: --project-delete takes <id> [store].\n", stderr);
            return 2;
        }
        if (!zeno_project_delete(studio_arg(argc, argv, 3, "zeno_projects.md"), argv[2])) {
            fputs("ZenoC: could not delete project.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--remotes") == 0) {
        char *json = zeno_remotes_list(studio_arg(argc, argv, 2, "zeno_remotes.md"));
        if (json == NULL) return 1;
        puts(json);
        zeno_free(json);
        return 0;
    }
    if (strcmp(cmd, "--remote-add") == 0) {
        char *item;
        if (argc < 5) {
            fputs("ZenoC: --remote-add takes <vps|vm|colab> <name> <target> [store].\n", stderr);
            return 2;
        }
        item = zeno_remote_add(studio_arg(argc, argv, 5, "zeno_remotes.md"), argv[2], argv[3], argv[4]);
        if (item == NULL) {
            fputs("ZenoC: could not add remote (check type vps|vm|colab and target).\n", stderr);
            return 1;
        }
        puts(item);
        zeno_free(item);
        return 0;
    }
    if (strcmp(cmd, "--remote-remove") == 0) {
        if (argc < 3) {
            fputs("ZenoC: --remote-remove takes <id> [store].\n", stderr);
            return 2;
        }
        if (!zeno_remote_remove(studio_arg(argc, argv, 3, "zeno_remotes.md"), argv[2])) {
            fputs("ZenoC: could not remove remote.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--remote-rename") == 0) {
        if (argc < 4) {
            fputs("ZenoC: --remote-rename takes <id> <new_name> [store].\n", stderr);
            return 2;
        }
        if (!zeno_remote_rename(studio_arg(argc, argv, 4, "zeno_remotes.md"), argv[2], argv[3])) {
            fputs("ZenoC: could not rename remote.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--remote-connect") == 0) {
        char *info;
        if (argc < 3) {
            fputs("ZenoC: --remote-connect takes <id> [store].\n", stderr);
            return 2;
        }
        info = zeno_remote_connect_cmd(studio_arg(argc, argv, 3, "zeno_remotes.md"), argv[2]);
        if (info == NULL) {
            fputs("ZenoC: remote not found.\n", stderr);
            return 1;
        }
        puts(info);
        zeno_free(info);
        return 0;
    }
    if (strcmp(cmd, "--plugins") == 0) {
        char *json = zeno_plugin_list(studio_arg(argc, argv, 2, "zeno_plugins"));
        if (json == NULL) return 1;
        puts(json);
        zeno_free(json);
        return 0;
    }
    if (strcmp(cmd, "--plugin-apply") == 0) {
        char *json = zeno_plugin_apply(studio_arg(argc, argv, 2, "zeno_plugins"));
        if (json == NULL) return 1;
        puts(json);
        zeno_free(json);
        return 0;
    }
    if (strcmp(cmd, "--plugin-guide") == 0) {
        fputs(zeno_plugin_guide(), stdout);
        return 0;
    }
    if (strcmp(cmd, "--plugin-validate") == 0) {
        char err[256];
        if (argc < 3) {
            fputs("ZenoC: --plugin-validate takes <json>.\n", stderr);
            return 2;
        }
        if (!zeno_plugin_validate(argv[2], err, sizeof(err))) {
            fprintf(stderr, "ZenoC: invalid plugin: %s\n", err);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--plugin-create") == 0) {
        char *plugin;
        if (argc < 3) {
            fputs("ZenoC: --plugin-create takes <request> [name_hint] [dir].\n", stderr);
            return 2;
        }
        plugin = zeno_plugin_create(argv[2], studio_arg(argc, argv, 3, NULL));
        if (plugin == NULL) {
            fputs("ZenoC: could not generate plugin.\n", stderr);
            return 1;
        }
        if (argc >= 5) {
            if (!zeno_plugin_save(argv[4], plugin)) {
                fprintf(stderr, "ZenoC: generated but could not save to %s.\n", argv[4]);
                zeno_free(plugin);
                return 1;
            }
            fprintf(stderr, "ZenoC: plugin saved to %s.\n", argv[4]);
        }
        puts(plugin);
        zeno_free(plugin);
        return 0;
    }
    if (strcmp(cmd, "--plugin-enable") == 0 || strcmp(cmd, "--plugin-disable") == 0) {
        int enabled;
        if (argc < 3) {
            fputs("ZenoC: --plugin-enable|--plugin-disable takes <id> [dir].\n", stderr);
            return 2;
        }
        enabled = strcmp(cmd, "--plugin-enable") == 0 ? 1 : 0;
        if (!zeno_plugin_set_enabled(studio_arg(argc, argv, 3, "zeno_plugins"), argv[2], enabled)) {
            fputs("ZenoC: could not update plugin.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    if (strcmp(cmd, "--plugin-rename") == 0) {
        if (argc < 4) {
            fputs("ZenoC: --plugin-rename takes <id> <new_name> [dir].\n", stderr);
            return 2;
        }
        if (!zeno_plugin_rename(studio_arg(argc, argv, 4, "zeno_plugins"), argv[2], argv[3])) {
            fputs("ZenoC: could not rename plugin.\n", stderr);
            return 1;
        }
        puts("{\"ok\":true}");
        return 0;
    }
    fputs("ZenoC: unknown studio command.\n", stderr);
    return 2;
}

static ZenoRouter *build_router(const ZenoConfig *config) {
    ZenoRouter *router = zeno_router_create();
    if (router == NULL || config == NULL) return router;
    if (config->openai_api_key[0] != '\0') {
        ZenoProviderConfig provider = {"openai", config->openai_base_url, config->openai_api_key,
                                       config->model_id, 1, 3, config->llm_timeout_ms};
        (void)zeno_router_add_provider(router, provider);
    }
    if (config->fireworks_api_key[0] != '\0') {
        ZenoProviderConfig provider = {"fireworks", config->fireworks_base_url,
                                       config->fireworks_api_key, config->model_id, 2, 3,
                                       config->llm_timeout_ms};
        (void)zeno_router_add_provider(router, provider);
    }
    if (config->use_ollama && config->ollama_url[0] != '\0') {
        ZenoProviderConfig provider = {"ollama", config->ollama_url, "", config->ollama_model,
                                       3, 3, config->llm_timeout_ms};
        (void)zeno_router_add_provider(router, provider);
    }
    return router;
}

static void destroy_runtime(ZenoRouter *router, ZenoApproval *approval,
                            ZenoRegistry *registry, ZenoMemory *memory,
                            ZenoSandbox *sandbox) {
    zeno_router_destroy(router);
    zeno_approval_destroy(approval);
    zeno_registry_destroy(registry);
    zeno_memory_destroy(memory);
    zeno_sandbox_destroy(sandbox);
}

/* Eval/benchmark mode: every approval request is granted automatically so
 * runs never block an unattended harness. ZENO_AUTO_APPROVE=1 enables it. */
static int auto_approve_callback(void *context, const char *request_id, const char *tool_name, const char *args_json, const char *reason) {
    (void)context; (void)request_id; (void)reason;
    fprintf(stderr, "[auto-approve] %s %s\n", tool_name != NULL ? tool_name : "?", args_json != NULL ? args_json : "");
    return 1;
}

static int result_exit_code(const char *status) {
    if (status != NULL && strcmp(status, "completed") == 0) return 0;
    if (status != NULL && strcmp(status, "waiting_approval") == 0) return 3;
    return 1;
}

int main(int argc, char **argv) {
    ZenoConfig config;
    if (!zeno_config_load_env(&config)) {
        fputs("ZenoC: could not load configuration.\n", stderr);
        return 1;
    }
    int minimal_only = 0;
    int compact = 1;
    const char *skills_dir = NULL;
    int is_squad_ex = 0;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--minimal") == 0) {
            minimal_only = 1;
        } else if (strcmp(argv[index], "--skills-dir") == 0 && index + 1 < argc) {
            skills_dir = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[index], "--version") == 0 || strcmp(argv[index], "-V") == 0) {
            if (argc != 2) {
                fputs("ZenoC: --version cannot be combined with other arguments.\n", stderr);
                return 2;
            }
            puts(ZENO_VERSION);
            return 0;
        } else if (strcmp(argv[index], "--check") == 0 || strcmp(argv[index], "--tools") == 0 ||
                   strcmp(argv[index], "--run") == 0 || strcmp(argv[index], "--squad") == 0 ||
                   strcmp(argv[index], "--squad-ex") == 0 || is_studio_command(argv[index])) {
            if (strcmp(argv[index], "--squad-ex") == 0) is_squad_ex = 1;
            argv[compact++] = argv[index];
        } else if (argv[index][0] == '-') {
            fprintf(stderr, "ZenoC: unknown option: %s\n", argv[index]);
            usage(stderr);
            return 2;
        } else {
            argv[compact++] = argv[index];
        }
    }
    argc = compact;
    if (minimal_only) config.agent_mode = ZENO_AGENT_MODE_MINIMAL;
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (is_studio_command(argv[1])) return run_studio(argc, argv);
    if (strcmp(argv[1], "--check") == 0) {
        if (argc != 2) {
            fputs("ZenoC: --check takes no positional arguments.\n", stderr);
            return 2;
        }
        char *json = zeno_config_public_json(&config);
        if (json == NULL) {
            fputs("ZenoC: failed to serialize configuration.\n", stderr);
            return 1;
        }
        puts(json);
        zeno_free(json);
        return 0;
    }
    int is_tools = strcmp(argv[1], "--tools") == 0;
    int is_run = strcmp(argv[1], "--run") == 0;
    int is_squad = strcmp(argv[1], "--squad") == 0;
    if (is_squad_ex && argc != 5 && argc != 6) {
        fputs("ZenoC: --squad-ex takes <agents_dir> <session> <task> [roles_csv].\n", stderr);
        return 2;
    }
    if ((!is_tools && !is_run && !is_squad && !is_squad_ex) || (is_tools && argc != 2) ||
        (is_run && argc != 4) || (is_squad && (argc < 4 || argc > 5))) {
        fputs("ZenoC: invalid command or argument count.\n", stderr);
        usage(stderr);
        return 2;
    }

    ZenoSandboxPolicy policy = {
        config.workspace_root, config.sandbox_strict, "", config.sandbox_allow_shell_operators,
        config.sandbox_timeout_ms, config.sandbox_max_output_chars, config.sandbox_max_command_chars,
        config.sandbox_max_jobs, config.sandbox_max_job_output_chars, config.sandbox_max_job_runtime_ms,
        config.sandbox_allow_network
    };
    ZenoSandbox *sandbox = zeno_sandbox_create(&policy);
    ZenoMemory *memory = zeno_memory_create("ZenoC_memory.md");
    ZenoRegistry *registry = zeno_registry_create();
    ZenoApproval *approval = zeno_approval_create();
    ZenoRouter *router = build_router(&config);
    int registered = config.agent_mode == ZENO_AGENT_MODE_MINIMAL
        ? zeno_registry_register_minimal(registry, sandbox, memory, config.workspace_root)
        : zeno_registry_register_builtins(registry, sandbox, memory, config.workspace_root);
    if (config.agent_mode != ZENO_AGENT_MODE_MINIMAL)
        (void)zeno_register_studio_tools(registry, "zeno_plugins");
    if (sandbox == NULL || memory == NULL || registry == NULL || approval == NULL || router == NULL || !registered) {
        fputs("ZenoC: initialization failed.\n", stderr);
        destroy_runtime(router, approval, registry, memory, sandbox);
        return 1;
    }
    if (is_tools) {
        char *tools = zeno_registry_list_json(registry);
        if (tools == NULL) {
            fputs("ZenoC: failed to list tools.\n", stderr);
            destroy_runtime(router, approval, registry, memory, sandbox);
            return 1;
        }
        puts(tools);
        zeno_free(tools);
        destroy_runtime(router, approval, registry, memory, sandbox);
        return 0;
    }
    if (!zeno_router_has_providers(router)) {
        fputs("ZenoC: no LLM provider configured. Set OPENAI_API_KEY, FIREWORKS_API_KEY, or USE_OLLAMA=1.\n", stderr);
        destroy_runtime(router, approval, registry, memory, sandbox);
        return 3;
    }
    char validation_error[256];
    if (!zeno_config_validate(&config, 0, validation_error, sizeof(validation_error))) {
        fprintf(stderr, "ZenoC: invalid configuration: %s\n", validation_error);
        destroy_runtime(router, approval, registry, memory, sandbox);
        return 2;
    }
    ZenoAgentOptions agent_options = {
        registry, router, memory, sandbox, NULL, NULL, approval, config.model_id,
        config.runs_dir, config.require_approval, config.absolute_mode, config.max_agent_turns,
        120000, 20000, config.agent_mode, config.caveman_mode ? 0 : 1
    };
    ZenoAgent *agent = zeno_agent_create(&agent_options);
    if (agent == NULL) {
        fputs("ZenoC: agent initialization failed.\n", stderr);
        destroy_runtime(router, approval, registry, memory, sandbox);
        return 1;
    }
    if (skills_dir != NULL) {
        if (!zeno_agent_attach_skills(agent, registry, skills_dir))
            fprintf(stderr, "ZenoC: warning: no skills found in %s\n", skills_dir);
    }
    int exit_code = 1;
    if (is_run) {
        ZenoRunOptions run_options = {
            config.model_id, config.max_agent_turns, 4096, 0.3, 0, config.require_approval,
            0, NULL, auto_approve_callback, NULL, NULL, NULL, NULL, NULL, NULL
        };
        ZenoAgentResult result = zeno_agent_run(agent, argv[2], argv[3], &run_options);
        fprintf(stderr, "status=%s run_id=%s turns=%d tools=%d tokens_in=%lld tokens_out=%lld cached=%lld\n",
                result.status != NULL ? result.status : "failed",
                result.run_id != NULL ? result.run_id : "", result.turns, result.tool_calls,
                result.tokens_in, result.tokens_out, result.tokens_cached);
        if (result.response != NULL) puts(result.response);
        exit_code = result_exit_code(result.status);
        zeno_agent_result_free(&result);
    } else if (is_squad_ex) {
        ZenoRunOptions run_options = {
            config.model_id, config.max_agent_turns, 4096, 0.3, 0, config.require_approval,
            0, NULL, auto_approve_callback, NULL, NULL, NULL, NULL, NULL, NULL
        };
        char *result = zeno_agent_run_squad_ex(agent, argv[3], argv[4], argc == 6 ? argv[5] : NULL,
                                               argv[2], &run_options);
        if (result == NULL) {
            fputs("ZenoC: squad-ex execution failed.\n", stderr);
            exit_code = 1;
        } else {
            puts(result);
            exit_code = strstr(result, "\"success\":true") != NULL ? 0 : 1;
            zeno_free(result);
        }
    } else {
        ZenoRunOptions run_options = {
            config.model_id, config.max_agent_turns, 4096, 0.3, 0, config.require_approval,
            0, NULL, auto_approve_callback, NULL, NULL, NULL, NULL, NULL, NULL
        };
        char *result = zeno_agent_run_squad(agent, argv[2], argv[3], argc == 5 ? argv[4] : NULL,
                                            &run_options);
        if (result == NULL) {
            fputs("ZenoC: squad execution failed.\n", stderr);
            exit_code = 1;
        } else {
            puts(result);
            exit_code = strstr(result, "\"success\":true") != NULL ? 0 : 1;
            zeno_free(result);
        }
    }
    zeno_agent_destroy(agent);
    destroy_runtime(router, approval, registry, memory, sandbox);
    return exit_code;
}
