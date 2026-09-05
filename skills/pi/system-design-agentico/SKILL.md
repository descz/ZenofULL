---
name: system-design-agentico
description: >-
  Framework de system design adaptado para sistemas agenticos (LLM agents).
  Use quando o usuário quiser arquitetar um agente ou sistema multi-agente,
  escolher um padrão de arquitetura (chain, routing, orchestrator-workers,
  evaluator-optimizer, ReAct, state machine), dimensionar tokens/latência/
  custo, analisar falhas de agentes, desenhar tools (ACI) ou avaliar
  performance de agentes. Triggers: "system design", "arquitetura do agente",
  "qual padrao usar", "orchestrator", "multi-agente", "design do sistema",
  "escalar o agente", "desenhar tools".
user-invocable: true
---

# System Design Agentico

> **Uma linha:** arquitetar sistemas agenticos é decidir **quanto deixar o
> LLM decidir** — e cada grau de autonomia custa latência, tokens e
> confiabilidade. Comece simples; escale por evidência.

## Sumário

1. [Quando usar](#1-quando-usar)
2. [Framework: os 5 passos clássicos adaptados](#2-framework-os-5-passos-clássicos-adaptados)
3. [Os 7 padrões de arquitetura](#3-os-7-padrões-de-arquitetura)
4. [Árvore de decisão](#4-árvore-de-decisão)
5. [Decisões-chave de design](#5-decisões-chave-de-design)
6. [Falhas e resiliência](#6-falhas-e-resiliência)
7. [Avaliação](#7-avaliação)
8. [15 exemplos curados](#8-15-exemplos-curados)
9. [Checklist de 60 segundos](#9-checklist-de-60-segundos)
10. [Fontes](#10-fontes)

---

## 1. Quando usar

- Vai construir um agente ou sistema multi-agente do zero
- Um agente existente falha muito (planejamento, ferramentas, loops)
- Precisa decidir entre **workflow determinístico** vs **agente autônomo**
- Precisa dimensionar (tokens, latência, custo) um sistema agentico

**Princípio central (Anthropic):**

> *"Comece com prompts simples, otimize com avaliação abrangente, e adicione
> sistemas agenticos multi-step apenas quando soluções mais simples não
> bastarem."* O objetivo não é o sistema mais sofisticado — é o sistema
> **certo** para a necessidade.

user-invocable: true
---

## 2. Framework: os 5 passos clássicos adaptados

### Passo 1 — Requirements Clarification

| Dimensão | Pergunta-chave | Exemplo |
|---|---|---|
| **Autonomia** | Workflow (caminhos pre-definidos em código) ou **agente** (LLM dirige dinamicamente)? | *"Workflows são sistemas onde LLMs e tools são orquestrados por caminhos de código pre-definidos. Agentes são sistemas onde LLMs dirigem dinamicamente seus próprios processos e uso de tools."* |
| **Latência budget** | 1 call LLM ~1–3s; loop de 10 turnos = 10–30s; multi-agente paralelo = mais rápido, porém mais tokens | O que o usuário tolera? |
| **Confiabilidade** | Custo do erro? Refund de $500 por engano vs bug verificável por teste | O ambiente define a taxa de erro aceitável |
| **Human-in-the-loop** | Grau de aprovação humana para operações de escrita | Modos: default / acceptEdits / plan / bypassPermissions |

### Passo 2 — Capacity Estimation (back-of-the-envelope)

| Recurso | Estimativa | Mitigação |
|---|---|---|
| **Tokens por tarefa** | Coding agent = 10–50 tool calls; cada call ~1K–8K input + 500–2K output | Subagentes, compactação |
| **Pressão de contexto** | Janela de contexto = recurso mais limitado; acumula entre turnos | Compactação automática, subagentes (contexto fresco), regras persistentes (CLAUDE.md) |
| **Custo** | ~$1/M (barato) a ~$15/M (médio) a ~$75/M (forte); 100K tokens no médio ≈ $1.50 | Routing de modelo, effort levels |
| **Paralelismo** | Fan-out exige rate-limit (API enterprise ~4K RPM) | Batching, backoff |

> **Erros compostos:** `accuracy_total = per_step_accuracy^N`.
> 95% por passo → 10 passos = 60%, 100 passos = 0,6%.

### Passo 3 — API / Interface Design (o ACI)

> *"Invista tanto esforço no Agent-Computer Interface (ACI) quanto em HCI."* — Anthropic

- **Tool definitions são a API**: nome, descrição, schema de parâmetros. O LLM seleciona tools automaticamente
- **Poka-yoke**: mude argumentos para que o uso incorreto seja mais difícil (ex.: path absoluto elimina classe de erros de path relativo)
- **Formato próximo do visto em treinamento**; evite overhead de formatação (diffs vs arquivos completos)
- **O agent loop é o transporte**: prompt → LLM avalia → tools executam → resultados voltam → repete até resposta final sem tool calls

### Passo 4 — Data Model

| Camada | O que é | Implementação |
|---|---|---|
| **Histórico de conversa** | Datastore primário (user_messages, assistant_messages, tool_calls, tool_results) | Cresce sem limite — é a fonte da pressão de contexto |
| **Memória (Lilian Weng)** | Sensory (embeddings, curta duração) → Short-term (in-context) → Long-term (vector store) | Retrieval ANN/MIPS: FAISS, HNSW, ScaNN; score = `w1·relevance + w2·recency + w3·importance` |
| **Sessões persistentes** | Resume entre restarts | Session store em banco próprio |
| **Saídas estruturadas** | JSON/function calling para sistemas; linguagem natural para humanos | Contrato por consumidor |

### Passo 5 — Failure Modes

Detalhado na [seção 6](#6-falhas-e-resiliência). Resumo: planejamento
(tool inválida, parâmetros errados, falsa conclusão), tools (saída errada,
falha silenciosa), goal drift, loops infinitos, não-determinismo, erros compostos.

---

## 3. Os 7 padrões de arquitetura

### 1. Prompt Chaining (sequencial)

```text
[Input] → [LLM 1] → [Gate] → [LLM 2] → [Gate] → [Output]
```

**Uso:** tarefa decomponível em subtarefas fixas e ordenadas; cada passo depende do anterior; precisão > latência.
**Ex.:** draft → tradução; extração → normalização → formatação.

### 2. Routing (classificador)

```text
[Input] → [Classifier LLM] → [Especialista A]  (tools A)
                             [Especialista B]  (tools B)
                             [Especialista C]  (tools C)
```

**Uso:** categorias distintas de entrada com tratamento especializado.
**Ex.:** suporte (billing/tech); routing de modelo (fácil → barato, difícil → forte).

### 3. Parallelization (sectioning + voting)

```text
Sectioning:  [Tarefa] → parte A ─┐
                         parte B ─┼→ [merge programático]
                         parte C ─┘

Voting:      [Tarefa] → tentativa 1 ─┐
                         tentativa 2 ─┼→ [agregação: maioria / best-of-N]
                         tentativa 3 ─┘
```

**Uso:** subtarefas independentes; ou múltiplas perspectivas aumentam confiança.
**Trade-off:** custo maior por latência menor / confiabilidade maior.

### 4. Orchestrator-Workers (decomposição dinâmica)

```text
[Input] → [Orchestrator: planeja + designa]
              ├→ [Worker 1]
              ├→ [Worker 2]
              └→ [Worker 3]
                        ↓
          [Orchestrator: sintetiza] → [Output]
```

**Uso:** tarefa complexa cujas subtarefas **não** são previsíveis (código: nº de arquivos depende da tarefa; pesquisa: nº de fontes).
**Ex.:** sistema de subagentes do Claude Code.

### 5. Evaluator-Optimizer (loop gerador-crítico)

```text
[Input] → [Generator] → [Evaluator: nota + feedback]
              ↑                    ↓ (se ruim)
              └────── [itera] ──────┘ (se bom) → [Output]
```

**Uso:** existem critérios claros e o LLM dá feedback útil.
> *"Reflexion é relativamente fácil de implementar e traz ganho surpreendente. O custo é latência e tokens."* — Chip Huyen

**Ex.:** tradução literária com crítico; geração de código com test runner.

### 6. Hierarchical / Multi-Agent (equipes)

Sub-padrões: **fan-out-and-synthesize, adversarial verification, tournament, generate-and-filter, classify-and-act, loop-until-done, quarantine.**

**Uso:** tarefas longas, massivamente paralelas, estruturadas ou adversariais; combate agentic laziness, self-preferential bias e goal drift.
**Não use:** *"a maioria das tarefas tradicionais de código não precisa de um painel de 5 revisores."* Paralelismo precisa merecer o custo.

### 7. Autonomous Agent (ReAct loop)

```text
[Prompt] → [LLM avalia] → [chama tools?] ──sim──→ [executa tools]
              ↑                                        ↓
              └──────── [resultados voltam] ──────────┘
                                  ↓ não
                          [Resposta final]
```

**Uso:** problemas abertos com nº imprevisível de passos; existe **ground truth** no ambiente.
**Implementação:** `max_turns` (~30), `max_budget_usd`, permission mode, effort levels, compactação automática.

### 7b. State Machine (LangGraph-style)

```text
[Start] → [Node A: LLM decide] ──condição──→ [Node B]
              │                                  │
              └──────condição──→ [Node C] ←──────┘
```

**Uso:** workflows multi-step com side-effects (aprovação humana, APIs), checkpointing, pause-and-resume. Previsibilidade vs flexibilidade.

user-invocable: true
---

## 4. Árvore de decisão

| Situação | Padrão |
|---|---|
| Passos fixos e simples | **Chain** (1) |
| Categorias distintas | **Route** (2) |
| Subtarefas independentes | **Parallelize** (3) |
| Decomposição imprevisível | **Orchestrator-Workers** (4) |
| Melhoria iterativa | **Evaluator-Optimizer** (5) |
| Escala massiva / adversarial | **Hierarchical Multi-Agent** (6) |
| Aberto com tools | **Autonomous Agent** (7) |
| Multi-step com aprovações | **State Machine** (7b) |

---

## 5. Decisões-chave de design

| Decisão | Regra |
|---|---|
| **Modelo** | O menor que resolve; cadeias longas exigem per-step accuracy maior; effort: `low` (lookups) → `max` (raciocínio complexo) |
| **Contexto** | Regras persistentes em CLAUDE.md (sobrevivem à compactação); subagentes com contexto fresco; exposição seletiva de tools; deferred loading (MCP) |
| **Tools** | Inventário menor é melhor; ablation studies; separar read-only (auto-run, paralelo) de write (gated) |
| **Memória** | Taxonomia sensory/short/long; Reflexion: reflections no long-term, até 3 no working memory |
| **Human-in-the-loop** | Níveis: gerar plano → validar plano → pausar em checkpoint → aprovar write. *"Defina claramente o nível de automação para cada ação."* (Chip Huyen) |

user-invocable: true
---

## 6. Falhas e resiliência

### Planejamento (a mais comum)

| Falha | Exemplo | Resiliência |
|---|---|---|
| Tool inválida | Alucinou nome de tool | Valide planos antes de executar (sem ações inválidas ou > X passos) |
| Parâmetros errados | Nº/tipo de args errado | Decouple planejamento de execução |
| Valores errados | `lbs_to_kg(lbs=100)` quando era 120 | AI-judge valida o plano |
| Falsa conclusão | Declara sucesso sem atingir o objetivo | Múltiplos planos, escolha o melhor |

### Tools

| Falha | Resiliência |
|---|---|
| Saída errada | Valide saídas antes de usar downstream |
| Falha silenciosa | Retry com backoff |
| Não-determinismo | Ground truth: releia arquivo editado, valide API |

### Agentic (Claude Code workflows)

| Falha | Definição | Resiliência |
|---|---|---|
| **Agentic laziness** | Para no meio e declara pronto (35/50 itens) | Verificação adversária contra rubric |
| **Self-preferential bias** | Prefere os próprios resultados | Separar gerador de verificador |
| **Goal drift** | Perde fidelidade após compactação (resumo é lossy) | Stop conditions; regras persistentes; subagentes isolados |

### Infra

| Falha | Mitigação |
|---|---|
| Alucinação | RAG + verificação de ground truth |
| Loops infinitos | `max_turns`, detecção de ações repetidas |
| Exaustão de tokens | Compactação, subagentes, orçamento de contexto |
| Permissão | Regras, pre-tool hooks |
| Rate limit | Batching, backoff |
| Custo | `max_budget_usd`, effort, routing |

---

## 7. Avaliação

### Planejamento

- % de planos válidos (de K gerados)
- Plans necessários para 1 válido
- % de tool calls válidas (tool certa, params certos, valores certos)
- Padrão de falha por tipo de tarefa / tool

### Fim-a-fim

- **Task completion rate** — atingiu o objetivo?
- **Constraint satisfaction** — seguiu orçamento/formato?
- Distribuição de tempo por tarefa
- **Per-step accuracy** — detecta fonte de erros compostos

> *"A chave do sucesso, como em qualquer feature LLM, é medir performance e iterar."* — Anthropic

user-invocable: true
---

## 8. 15 exemplos curados

| # | Sistema | Arquitetura | Licao de design |
|---|---|---|---|
| 1 | **Claude Code** | ReAct loop + orchestrator-workers + dynamic workflows | Começa simples, adiciona complexidade por evidência; subagentes combatem contexto, laziness e bias |
| 2 | **Customer support** (Anthropic) | Routing + loop com write gated + critério de resolução explícito | Sucesso = resoluções, não conversas; pricing por resolução; aprovação para writes |
| 3 | **Deep research** (skill) | Fan-out + adversarial verification + barreira de síntese | Fan-out dá cobertura, verificação dá confiabilidade, barreira impede relatório parcial |
| 4 | **Triage em escala** | Classify → dedupe → fix OU escalar | Limite de confiança é política do sistema, não do agente |
| 5 | **Root-cause investigation** | Agentes paralelos (logs/arquivos/dados) → hipóteses → painel adversário | Hipótese vira conclusão só após verificação cruzada |
| 6 | **SWE-bench coding agent** | Generator + test runner como evaluator | O avaliador mais forte é o ambiente, não um segundo LLM |
| 7 | **Generative Agents** (Park et al.) | Memory stream + retrieval (relevance+recency+importance) + reflection | Memória longa com scoring composto e reflexão agendada |
| 8 | **LangGraph state machine** | Grafo de nós, LLM decide arestas, checkpointable | Para fluxos com side-effects: explicitude > flexibilidade |
| 9 | **Model routing** | Classificador: fácil → barato, difícil → forte | A otimização de custo mais simples e segura |
| 10 | **Tradução literária** | Evaluator-optimizer contra rubric | Reflexion com custo controlado (max iterações) |
| 11 | **Code review panel** | Voting (N prompts, agregação) | Voting aumenta recall em tarefas de detecção |
| 12 | **Refund agent** | Classify → dados → proposta → aprovação humana → executa | Hierarquia de aprovação no sistema, não no prompt |
| 13 | **MCP connector** | Servidor local exposto; deferred loading; read/write separados | Tool boundaries via protocolo padrão gastam menos contexto |
| 14 | **Monitoring agent autônomo** | ReAct + stop conditions + detecção de loop + escalate humano | Loops autônomos precisam de stop conditions e escalation |
| 15 | **Relatório diário** | Prompt chaining com gates programáticos | Gates entre steps convertem 60% de precisão em pipeline confiável |

---

## 9. Checklist de 60 segundos

- [ ] Workflow ou agente? (autonomia justificada?)
- [ ] Latência e custo do erro definidos?
- [ ] Padrão escolhido pela árvore de decisão?
- [ ] ACI: tool definitions com poka-yoke, read/write separados?
- [ ] Estimativa de tokens/tarefa e pressão de contexto?
- [ ] Regras persistentes fora do contexto compactável?
- [ ] Stop conditions (`max_turns`, `max_budget_usd`)?
- [ ] Ground truth verificável em cada passo?
- [ ] Failure modes mapeados com resiliência correspondente?
- [ ] Evals definidos (per-step accuracy, task completion, constraints)?

user-invocable: true
---

## 10. Fontes

- [Building Effective Agents — Anthropic](https://www.anthropic.com/engineering/building-effective-agents)
- [Agents — Chip Huyen](https://huyenchip.com/2025/01/07/agents.html)
- [LLM Powered Autonomous Agents — Lilian Weng](https://lilianweng.github.io/posts/2023-06-23-agent/)
- [Agent loop & contexto — Claude Agent SDK](https://code.claude.com/docs/en/agent-sdk/agent-loop)
- [A Harness for Every Task: Dynamic Workflows](https://claude.com/blog/a-harness-for-every-task-dynamic-workflows-in-claude-code)
- [LangGraph](https://langchain-ai.github.io/langgraph/)
- [ReAct (arXiv:2210.03629)](https://arxiv.org/abs/2210.03629) · [Reflexion (arXiv:2303.11366)](https://arxiv.org/abs/2303.11366)
- Relatório completo de pesquisa: `agentic-system-design-report.md` (mesma pasta)
