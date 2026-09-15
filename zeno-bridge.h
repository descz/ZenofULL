/*
 * zeno-bridge.h — Ponte entre o servidor da UI (server.c) e o runtime do
 * agente ZenoC (libzenoc). O servidor expõe estas funções como HTTP:
 * chat com streaming de eventos, memória (notas + links), skills, modelos
 * e configuração de provider.
 *
 * Tudo é offline-first: quando não há provider configurado as funções
 * respondem JSON de erro e a UI cai de volta no comportamento simulado.
 */
#ifndef ZENO_BRIDGE_H
#define ZENO_BRIDGE_H

#include <stddef.h>

/* Callback de eventos do agente. Recebe um objeto JSON por chamada, com um
 * campo "type" entre: run_started, thinking, tool_started, tool_completed,
 * text, run_completed, error. */
typedef void (*ZenoBridgeEmit)(void *context, const char *event_json);

/* Carrega zeno-agent.json + variáveis de ambiente. Idempotente. */
void zeno_bridge_init(const char *base_dir);

/* Status do runtime: versão, provider, modelo, workspace, nota de memória,
 * contagem de skills/ferramentas e se um run está ativo. */
char *zeno_bridge_status_json(void);

/* Lista curada de ferramentas registradas no agente. */
char *zeno_bridge_tools_json(void);

/* Modelos disponíveis. refresh=1 consulta GET {base_url}/models no provider
 * e atualiza o cache em zeno-agent.json; refresh=0 usa cache/config. */
char *zeno_bridge_models_json(int refresh);

/* Salva a configuração do provider (merge no zeno-agent.json).
 * JSON: {provider,base_url,api_key,model,fallback_models,workspace,
 *        agent_mode,require_approval,skills_dir,ollama_url,ollama_model}. */
int zeno_bridge_config_save(const char *json, char **error);

/* Executa o agente de forma síncrona (o servidor roda em thread própria) e
 * emite eventos via callback. Retorna JSON de resultado:
 * {status,response,run_id,turns,tool_calls,tokens_in,tokens_out,duration_ms}
 * ou NULL em erro (*error recebe a mensagem). */
char *zeno_bridge_run(const char *session_id, const char *message,
                      const char *model, ZenoBridgeEmit emit, void *emit_context,
                      char **error);

/* Igual a zeno_bridge_run, mas recebe o body HTTP cru:
 * {"message":"...","model":"...","session":"..."}. */
char *zeno_bridge_chat(const char *request_json, ZenoBridgeEmit emit,
                       void *emit_context, char **error);

/* Cancela o run ativo (cooperativo, no limite de turno). */
void zeno_bridge_cancel(void);
int zeno_bridge_busy(void);

/* Memória (mesmo store que o agente usa: ZenoC_memory.md no workspace). */
char *zeno_bridge_memory_notes_json(void);
char *zeno_bridge_memory_note_add(const char *json, char **error);
char *zeno_bridge_memory_note_update(const char *json, char **error);
char *zeno_bridge_memory_note_delete(const char *id, char **error);
char *zeno_bridge_memory_links_json(void);
char *zeno_bridge_memory_link_add(const char *json, char **error);
char *zeno_bridge_memory_link_delete(const char *json, char **error);

/* Skills indexadas da pasta skills/ (SKILL.md): [{id,name,description,path}]. */
char *zeno_bridge_skills_json(void);

/* Plugins (modificador do Zeno): ferramentas de código, botões, abas e
 * campos de configuração, persistidos em <workspace>/.zeno/plugins.json. */
char *zeno_bridge_plugins_json(void);
char *zeno_bridge_plugins_save(const char *json, char **error);
char *zeno_bridge_plugins_delete(const char *id, char **error);
char *zeno_bridge_plugins_toggle(const char *id, int enabled, char **error);

#endif /* ZENO_BRIDGE_H */
