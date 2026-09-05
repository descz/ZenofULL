---
name: criacao-de-subagentes
description: >-
  Metodologia completa para criar subagentes de elite para agentes de código.
  Use quando o usuário quiser criar um subagente, delegar trabalho a agentes
  especializados, estruturar sistemas multi-agente, usar a Task tool, ou
  perguntar como dividir uma tarefa entre agentes. Cobre contrato de saída,
  prompts com 5 blocos, right-sizing de ferramentas/modelo, padrões de
  orquestração (fan-out, adversarial verification, tournament, quarantine) e
  anti-padrões. Triggers: "subagente", "sub-agent", "delegar para um agente",
  "criar um agente", "task tool", "orchestrator", "worker", "multi-agente",
  "como dividir essa tarefa entre agentes".
user-invocable: true
---

# Criacao de Subagentes de Elite

> **Uma linha:** subagente é **isolamento de contexto + contrato de saída**.
> Tudo que o agente principal recebe é a **mensagem final** — o resto é
> engenharia de prompt, ferramentas e orquestração.

## Sumário

1. [Quando usar (e quando não usar)](#1-quando-usar-e-quando-não-usar)
2. [Metodologia: 6 passos](#2-metodologia-6-passos-para-um-subagente-de-elite)
3. [Padrões de orquestração](#3-padrões-de-orquestração)
4. [Contrato de saída](#4-contrato-de-saída)
5. [Anti-padrões](#5-anti-padrões)
6. [25 exemplos curados](#6-25-exemplos-curados)
7. [Checklist de 60 segundos](#7-checklist-de-60-segundos)
8. [Fontes](#8-fontes)

---

## 1. Quando usar (e quando não usar)

### ✅ Crie um subagente quando

| Motivo | Regra prática | Resultado |
|---|---|---|
| **Isolamento de contexto** | A subtarefa consome > ~25% da janela de contexto e o resultado cabe em poucas linhas | O principal cresce só com o resumo final, não com o transcripto completo |
| **Paralelismo** | Subtarefas independentes (fan-out) | Menos latência, mais cobertura — mais custo |
| **Especialização** | Prompt, ferramentas, modelo ou permissões diferentes do principal | Cada agente com o toolset mínimo que precisa |
| **Combate a falhas** | Agentic laziness, self-preferential bias, goal drift | Contexto isolado impede contaminação cruzada |

### ❌ NÃO crie subagente quando

- A tarefa é curta e o resultado entra direto no fluxo principal
- O subagente precisaria do **histórico acumulado** do principal (o isolamento quebra a continuidade)
- *"A maioria das tarefas tradicionais de código não precisa de um painel de 5 revisores. Paralelismo e especialização precisam merecer o custo de coordenação."* — Anthropic

user-invocable: true
---

## 2. Metodologia: 6 passos para um subagente de elite

### Passo 1 — Decida o padrão (a forma segue a função)

Escolha o padrão de orquestração antes de escrever qualquer prompt (ver [seção 3](#3-padrões-de-orquestração)). O padrão define quantos agentes, quem fala com quem, e onde fica a barreira de síntese.

### Passo 2 — Defina o contrato de saída (a parte mais importante)

O principal **só vê a mensagem final**. O contrato precisa ser:

- **Estruturado e parseável** — JSON, checklist, tabela, `arquivo:linha`
- **Sem ruído** — defina o que **não** deve ser reportado
- **Com fracasso explícito** — nunca inventar resultado

```json
{
  "status": "done | partial | failed",
  "summary": "2 linhas no máximo",
  "findings": [
    { "file": "src/app.ts", "line": 42, "severity": "high", "detail": "..." }
  ],
  "constraints_violated": [],
  "next_step": "o que falta, se status != done"
}
```

**Regra:** o contrato deve ser preenchível por um humano em 30 segundos.

### Passo 3 — Escreva o prompt (estrutura canônica de 5 blocos)

```text
(a) ROLE        — 1 linha. Quem ele é. ("Você é um auditor de segurança.")
(b) CONTEXT     — o mínimo necessário. NADA além.
(c) STEPS       — 3-6 passos numerados, cada um verificável.
(d) CONSTRAINTS — limites: tools, o que NÃO fazer, max iterações, effort.
(e) OUTPUT      — o contrato do Passo 2, com exemplo concreto.
```

```markdown
Você é um auditor de segurança web.

CONTEXTO: O app em src/ é um dashboard com login. Analise apenas os arquivos
da pasta src/auth/.

STEPS:
1. Liste todos os pontos de autenticação (arquivo:linha)
2. Verifique cada um contra a checklist OWASP ASVS v4 (fator 1-4)
3. Marque vulnerabilidades com severidade crítica/alta/média/baixa

CONSTRAINTS:
- NÃO modifique nenhum arquivo (read-only)
- NÃO reporte issues de estilo ou performance
- Máximo de 3 buscas por ponto antes de declarar "não encontrado"

OUTPUT:
Retorne JSON: {"findings": [{"file", "line", "severity", "issue", "fix"}],
"summary": "1-2 linhas"}. Se nada for encontrado: {"findings": []}.
```

**Regras de ouro do prompt:**

1. **Ground truth sempre que possível** — se editou um arquivo, releia; se usou API, valide o retorno
2. **Condição de parada anti-loop** — `max_turns`, `max_budget_usd`
3. **Tokens para pensar** — nunca force resposta imediata
4. **Proibição explícita evita goal drift** — *"NÃO declare sucesso antes de conferir os 3 critérios abaixo"*

### Passo 4 — Faça o right-size (ferramentas, esforço, modelo)

| Dimensão | Regra |
|---|---|
| **Tools** | Conjunto mínimo necessário. Mais tools = mais contexto e pior seleção (ablation studies mostram tools nunca usadas) |
| **Permissões** | read-only para exploração; write gated para ações |
| **Modelo** | O menor que resolve. Erros compostos: `95%^10 = 60%` — cadeias longas pedem modelo forte |
| **Effort** | `low` para lookups, `high`/`max` para raciocínio complexo |

### Passo 5 — Valide com um exemplo real

Rode o subagente em uma tarefa representativa **antes** de usar em produção. Verifique:

- **Task completion** — completou a tarefa?
- **Constraint satisfaction** — seguiu os limites?
- **Contrato** — saída parseável, sem ruído?
- **Custo** — tempo/contexto proporcionais?

### Passo 6 — Itere por evidência

Falhou? Mude **uma variável por vez** (prompt, tools, modelo, effort) e re-teste **no mesmo exemplo**. Nunca reescreva tudo a cada falha.

---

## 3. Padrões de orquestração

| Padrão | Estrutura | Uso ideal | Exemplo real |
|---|---|---|---|
| **Fan-out-and-synthesize** | Dividir → N agentes → **barreira** de síntese | Cobertura ampla (pesquisa, busca) | Deep research |
| **Adversarial verification** | Para cada saída, um agente **diferente** verifica contra rubric | Confiabilidade alta | Code review, fact-checking |
| **Tournament** | N competem → juízes par-a-par → vencedor | Escolher melhor solução | Model selection |
| **Generate-and-filter** | N ideias → filtro por rubric → dedupe | Criatividade com curadoria | Feature ideas |
| **Classify-and-act** | Classificador → roteia para equipes | Volume com categorias | Triage de issues |
| **Loop-until-done** | Spawn até condição de parada | Mineração exaustiva | Minerar sessões → regras |
| **Quarantine** | Leitura não confiável separada de write | Entrada não confiável | PDFs, scrapes, HTML |

```text
Fan-out-and-synthesize          Adversarial verification
┌─────────┐                     ┌─────────┐   ┌──────────────┐
│ Tarefa  │                     │ Gerador │──▶│ Verificador  │
└────┬────┘                     └─────────┘   │ contra rubric│
     ▼                                        └──────┬───────┘
┌─────────┐  ┌─────────┐  ┌─────────┐                ▼
│Worker 1 │  │Worker 2 │  │Worker 3 │           [achados OK]
└────┬────┘  └────┬────┘  └────┬────┘
     └────────────┼────────────┘
                  ▼
        [Barreira: espera TODOS]
                  ▼
           [Síntese final]
```

**A barreira de síntese é o que impede o relatório parcial.** Se N-1 agentes terminaram, ela **não** libera o resultado.

user-invocable: true
---

## 4. Contrato de saída

| Elemento | Regra | Exemplo |
|---|---|---|
| **Formato** | Estruturado e parseável | JSON, checklist, `arquivo:linha` |
| **Ruído** | O que NÃO reportar, explícito | "não reporte estilo/performance" |
| **Fracasso** | Condição explícita de não-encontrado | `{"found": false, "reason": "..."}` |
| **Ground truth** | Verificação no ambiente | "releia o arquivo editado" |
| **Fonte** | Proibido inventar | "cite arquivo:linha ou diga não encontrado" |

---

## 5. Anti-padrões

| ❌ Anti-padrão | Sintoma | Correção |
|---|---|---|
| Subagente sem contrato | Resumo vago em prosa; principal re-trabalha | Contrato estruturado no Passo 2 |
| Tools demais | Escolhe tool errada; contexto estourado | Toolset mínimo (Passo 4) |
| Contexto vazado | Copiou histórico inteiro; isolamento anulado | Passar só o contexto do Passo 3b |
| Fan-out sem barreira | Relatório parcial vira "pronto" | Barreira de síntese |
| Gerador = verificador | Self-preferential bias | Separar papeis sempre |
| Sem condição de parada | Loops infinitos, custo imprevisível | `max_turns` + `max_budget_usd` |
| Saída não verificada | Edição não aplicada, dado inventado | Ground truth no contrato |

user-invocable: true
---

## 6. 25 exemplos curados

> Formato: **Nome** — origem · padrão · a lição de design.

### Nativos (Claude Code)

| # | Subagente | Padrão | Lição de design |
|---|---|---|---|
| 1 | **Explore** | Lookup read-only | Ferramentas mínimas (glob/grep/read), effort `low`, retorna só o fato com `arquivo:linha`, sem opinião |
| 2 | **Plan** | Read-only garantido | `permission_mode: plan` (modo, não instrução); saída = plano com passos, riscos e **out-of-scope** |
| 3 | **General-purpose** | Contexto zero | Delegação genérica via Task tool; contexto fresco, modelo configurável, um único resultado de volta |

### Por padrão de orquestração

| # | Subagente | Padrão | Lição de design |
|---|---|---|---|
| 4 | **Debugger** | Evidência → hipótese → fix | Contrato: causa raiz + evidência + fix mínimo. "Proponha fix apenas com evidência; não adivinhe" |
| 5 | **Code-reviewer adversarial** | Adversarial verification | Dois revisores em paralelo (standards × spec), rubric explícito por eixo, relatório lado a lado |
| 6 | **Docs-writer** | Escopo restrito | "Se o código divergir da doc, REPORTE — não corrija o código". Nunca altera código |
| 7 | **Security auditor** | Quarantine | Lê tudo, escreve nada; saída = vulnerabilidades com severidade, localização, recomendação |
| 8 | **Deep-research fan-out** | Fan-out + adversarial + barreira | Combina 4 padrões; N buscas com ângulos diferentes, verificação de cada claim contra fonte |
| 9 | **Claim verifier** | Adversarial puro | Recebe claims + fontes, responde `{claim, verdict, evidence}`; nunca passa pelo gerador |
| 10 | **Tournament judge** | Tournament | Juízes comparam par-a-par com critérios **escritos antes** de ver respostas; ranking + justificativa |
| 11 | **Idea generator + filter** | Generate-and-filter | Gerador sem restrições; filtro com rubric; dedupe por similaridade. Dois agentes, papeis opostos |
| 12 | **Classifier-router** | Classify-and-act | Retorna `{intent, confidence}` com modelo barato; a rota (com tools próprias) faz o resto |
| 13 | **Session miner** | Loop-until-done | Condição de parada explícita ("2 rodadas sem achado novo = para"); dedupe por iteração |
| 14 | **Test-runner (SWE-bench)** | Evaluator = ambiente | O avaliador é o teste, não um LLM: "sucesso = teste verde". Ground truth puro |
| 15 | **Stakeholder analyzer** | Parallelization (sectioning) | N perspectivas independentes (engenharia, produto, legal); merge programático |
| 16 | **Triage agent** | Classify → fix → escalar | Política de escalonamento é do sistema, não do agente: "confiança < X → escala" |
| 17 | **Root-cause panel** | Paralelo + verificação | 3 agentes (logs/files/data) → hipóteses → painel adversário. Hipótese só vira conclusão após verificação cruzada |
| 18 | **Quarantine reader** | Quarantine | Processa conteúdo não confiável **sem acesso a write tools**; saída sanitizada e estruturada |
| 19 | **Webapp tester** | Playwright + evidência | Contrato: screenshot + log + passo reproduzível. Evidência em arquivo, não em texto |
| 20 | **Doc extractor** | Schema obrigatório | Campo ausente = `null`, nunca inventado. "Nunca preencha valor para campo não encontrado" |
| 21 | **MCP scoped agent** | Tool boundary | Recebe só as MCP tools necessárias (deferred loading); schemas não poluem o contexto principal |
| 22 | **Prototype builder** | POC descartável | Contrato: "entregue X em Y arquivos + como rodar + o que isso decide". Proibido embelezar |
| 23 | **Translator + critic** | Evaluator-optimizer | Par fixo: gera → critica contra rubric (fidelidade/fluência) → itera. "Reflexion é fácil e surpreendentemente eficaz; o custo é latência" |
| 24 | **Eval grader** | Rubric rígida | Nota sem justificativa = resposta inválida; escala de pontos obrigatória |
| 25 | **Comparator** | Blind comparison | Compara duas versões sem saber qual é qual (baseline × iteração) — mata viés de novidade |

---

## 7. Checklist de 60 segundos

- [ ] O isolamento de contexto justifica o subagente?
- [ ] Qual padrão? (fan-out / adversarial / tournament / generate-filter / classify-act / loop-until-done / quarantine)
- [ ] Contrato de saída escrito (estruturado, sem ruído, com fracasso)?
- [ ] Prompt com 5 blocos (role, context, steps, constraints, output)?
- [ ] Tools mínimas? Permission mode correto?
- [ ] Condição de parada (max_turns/budget)?
- [ ] Ground truth no contrato (reler arquivo, validar API)?
- [ ] Separação de papeis (gerador ≠ verificador)?
- [ ] Validado em exemplo real antes de produção?

user-invocable: true
---

## 8. Fontes

- [Subagents — Claude Agent SDK](https://code.claude.com/docs/en/agent-sdk/subagents)
- [Agent loop & contexto — Claude Agent SDK](https://code.claude.com/docs/en/agent-sdk/agent-loop)
- [A Harness for Every Task: Dynamic Workflows](https://claude.com/blog/a-harness-for-every-task-dynamic-workflows-in-claude-code)
- [Building Effective Agents — Anthropic](https://www.anthropic.com/engineering/building-effective-agents)
- [Agents — Chip Huyen](https://huyenchip.com/2025/01/07/agents.html)
- [mattpocock/skills](https://github.com/mattpocock/skills) e [anthropics/skills](https://github.com/anthropics/skills) — exemplos reais
