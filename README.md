# ZenofULL — Zeno Agent (GUI + backend em C)

Agente de engenharia de software autônomo (ZenoC, runtime em C11) integrado a uma GUI completa: chat com streaming, memória em rede neural, sistema de plugins e MCPs persistentes.

## Build

```
# 1. Build do runtime ZenoC (libzenoc.a)
cd ZenoC
cmake -S . -B build/preset-offline-debug
cmake --build build/preset-offline-debug

# 2. Build do servidor (server.c + ponte + runtime)
cd ..
build-server.cmd
```

## Run

```
zeno-server.exe          (porta padrão 8080)
```

Abra `http://localhost:8080` — o `index.html` é servido automaticamente.

## Arquitetura

| Camada | O que faz |
|---|---|
| `server.c` | HTTP local (WS2), SSE, rotas `/api/zenoc/*` |
| `zeno-bridge.c` | Ponte GUI ↔ runtime: chat, memória, skills, modelos, **plugins** (`.zeno/plugins.json`) |
| `ZenoC/src` | Runtime do agente: LLM router (OpenAI-compat), tools com sandbox, memória durável, approval |

## Funcionalidades

- **Chat** com timeline real: pensamento → ferramentas → texto, renderizado na ordem de execução
- **Memória**: grafo estilo rede neural; notas, skills e MCPs; MCP verde-claro/Skill azul configuráveis, ligações brancas; agente consolida memórias (atualiza/vincula em vez de criar por chat)
- **Plugins** (backend em C): ferramentas de código (templates shell), botões no chat, abas na sidebar, campos nas configurações — persistidos em `.zeno/plugins.json`, registrados dinamicamente no registry do agente
- **MCPs persistentes**: salvos como memória `kind=mcp` e reutilizáveis via `mcp_call`
- **Configuração de LLM** na aba Models (provider OpenAI-compatível, busca de modelos da API)

## Config de provider

Na primeira execução o agente cria `zeno-agent.json` (fora do repo). Configure em **Settings → Models**: provider, Base URL, API key e modelos.

> Segurança: `zeno-agent.json`, `zeno-config.json` e memórias são runtime local e não são versionados.
