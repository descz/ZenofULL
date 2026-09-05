---
name: criacao-de-skills
description: >-
  Metodologia completa para criar skills de elite para agentes de código.
  Use quando o usuário quiser criar uma skill, melhorar uma skill existente,
  escrever um SKILL.md, estruturar workflows de agentes, montar evals para
  skills, ou perguntar como skills devem ser escritas. Cobre evaluation-driven
  development (ciclo de 6 passos), qualidade de descrição/triggers, progressive
  disclosure e anti-padrões. Triggers: "criar skill", "nova skill", "SKILL.md",
  "melhorar skill", "como escrever uma skill", "eval de skill",
  "benchmark de skill".
user-invocable: true
---

# Criacao de Skills Bem-Elaboradas

> **Uma linha:** skill é um pacote de conhecimento que muda o **comportamento**
> do agente quando ativada — e só existe de verdade se a descrição acionar
> na hora certa.

## Sumário

1. [O que é uma skill](#1-o-que-é-uma-skill-e-o-que-não-é)
2. [Metodologia: evaluation-driven development](#2-metodologia-evaluation-driven-development)
3. [Qualidade de descrição (trigger accuracy)](#3-qualidade-de-descrição-trigger-accuracy)
4. [Progressive disclosure](#4-progressive-disclosure)
5. [Anti-padrões](#5-anti-padrões)
6. [25 exemplos curados](#6-25-exemplos-curados)
7. [Checklist de 60 segundos](#7-checklist-de-60-segundos)
8. [Fontes](#8-fontes)

---

## 1. O que é uma skill (e o que não é)

Uma skill **não é** um documento de referência que o agente lê "às vezes".
É um pacote carregado no contexto quando a descrição casa com o pedido.
**Skill de elite = instruções mínimas + acionamento confiável + evidência de que funciona.**

### As 3 camadas de uma skill

| Camada | O que é | Pergunta que responde |
|---|---|---|
| **Descrição** (frontmatter) | O radar | *Quando* a skill é carregada? Descrição ruim = skill nunca carregada = skill inexistente |
| **Corpo** (`SKILL.md`) | O manual | *Como* executar? Instruções mínimas, com progressive disclosure |
| **Evidência** (evals) | A prova | *Funciona?* Casos que medem acionamento e execução |

**Regra de ouro do tamanho:** `SKILL.md < 500 linhas`. Se passar, mova
detalhe para `references/`. Instrução que o agente não lê é **ruído que atrapalha**.

user-invocable: true
---

## 2. Metodologia: evaluation-driven development

> Vinda de quem constrói skills profissionalmente (ecossistemas
> mattpocock/skills e anthropics/skills). A skill se escreve **para passar
> nos evals** — não para ficar bonita.

```text
┌────────────────┐     ┌────────────────┐     ┌────────────────┐
│ 1. Capture     │ ──▶ │ 2. Create      │ ──▶ │ 3. Establish   │
│    Intent      │     │    Evaluations │     │    Baseline    │
└────────────────┘     └────────────────┘     └────────────────┘
        ▲                                            │
        │                                            ▼
┌────────────────┐     ┌────────────────┐     ┌────────────────┐
│ 6. Iterate &   │ ◀── │ 5. Execute     │ ◀── │ 4. Write       │
│    Optimize    │     │    Evals       │     │    Minimal     │
│    Description │     │                │     │    Instructions│
└────────────────┘     └────────────────┘     └────────────────┘
```

### Passo 1 — Capture Intent

Escreva em **uma frase** o comportamento esperado:

> "Quando o usuário pedir X, o agente deve fazer **Y** em vez de **Z**."

Ex.: *"Quando pedirem criação de docx, o agente deve gerar `.docx` válidos
com python-docx, estilos corretos, e **nunca** sugerir 'não sei fazer'."*

### Passo 2 — Create Evaluations (escreva os testes ANTES da skill)

Crie **5–20 evals** cobrindo três dimensões:

| Tipo de eval | Testa | Exemplo |
|---|---|---|
| **Acionamento** | A descrição | "usuário pede X → skill deve ser carregada" |
| **Execução** | O corpo | "skill carregada → saída atende o rubric" |
| **Anti-caso** | Falso positivo | "pedido parecido mas diferente → skill NÃO carrega" |

```text
evals/
  case_001__acionamento/
    input.txt          # pedido do usuário
    rubric.md          # critérios 0-5 por dimensão, nota mínima
    expected.md        # opcional: resposta de referência
    meta.json          # estado (draft/ready), owner, data
```

### Passo 3 — Establish Baseline

Rode os evals **SEM a skill** e depois **COM ela**. A diferença de nota é o
**valor real** da skill. Sem baseline não há iteração — só achismo.

### Passo 4 — Write Minimal Instructions

Escreva o `SKILL.md` com o **menor conteúdo que passa nos evals**. Estrutura canônica:

```markdown
---
name: minha-skill
description: >-
  O que faz + quando usar + triggers verbatim + quando NÃO usar.
user-invocable: true
---

# Minha Skill

## Quando usar (e quando não usar)
## Workflow (passos numerados, cada um verificável)
## Princípios / constraints (o que nunca fazer)
## Checklist de verificação final
```

Nada de história, elogios ou tutorial longo. O agente precisa de **ordem de
operação**, não de palestra.

### Passo 5 — Execute Evals

Rode todos os evals, pontue por dimensão:

- **Trigger accuracy** — descrição aciona no momento certo?
- **Qualidade** — saída atende o rubric?
- **Schema compliance** — contratos respeitados?

Rode **múltiplas vezes** (modelos são não-determinísticos). A nota da skill
é a **mediana**, não o melhor caso.

### Passo 6 — Iterate & Optimize Description

Mudança de evidência exige **uma variável por vez**:

| Falha | Mexa em |
|---|---|
| Acionamento falha | **Description** (triggers, exemplos, anti-triggers) |
| Execução falha | **Corpo** (instruções, references) |
| Instável entre runs | **Contrato de saída** (formato/checklist obrigatório) |

A descrição é a parte mais otimizada: é o único texto que decide se a skill
**existe** para o usuário. Escreva variantes e compare trigger accuracy.

---

## 3. Qualidade de descrição (trigger accuracy)

### ✅ Boa descrição

- Verbo de ação + quando usar: *"Use quando o usuário quiser…"*
- Triggers verbatim: `"triggers: 'pentest', 'security audit'"` — incluindo
  **sinônimos e frases naturais** do usuário, não só jargão técnico
- Anti-triggers explícitos (quando **não** usar) → reduz falso positivo

### ❌ Descrição ruim

| Problema | Consequência |
|---|---|
| Só o nome da pasta reescrito ("skill de pentest") | Nunca aciona |
| Linguagem acadêmica que o usuário não usa | Nunca aciona |
| Genérica demais (compete com todas as skills) | Aciona em contexto errado |
| Sem anti-triggers | Aciona errado, desperdiça contexto |

user-invocable: true
---

## 4. Progressive disclosure

| Caminho | Conteúdo |
|---|---|
| `SKILL.md` (raiz) | Instruções mínimas, workflow, checklist |
| `references/` | Schemas, guias, documentação de apoio |
| `scripts/` | Código executável (engines, validadores) |
| `agents/` | Subagentes usados pela skill (ex.: graders) |
| `assets/`, `templates/` | Exemplos, modelos de saída |
| `evals/` | Casos de teste + rubric + baselines |

**Regra:** o `SKILL.md` nunca repete o conteúdo das subpastas; ele **aponta** e diz quando carregar.

---

## 5. Anti-padrões

| ❌ Anti-padrão | Sintoma | Correção |
|---|---|---|
| Skill-monolito | 3000 linhas; instrução importante se perde | Progressive disclosure |
| Descrição sem triggers | Skill perfeita que nunca aciona | Passo 6: iterar description |
| Sem evals | Sem baseline, não dá para saber se ajuda | Passos 2-3 |
| Explica em vez de executar | Tutorial em vez de workflow | Passo 4: ordens de operação |
| Duplica ferramentas nativas | "Escrever código limpo" — o agente já faz | Não criar |
| Conflita com comportamento padrão | Comportamento esquizofrênico | Alinhar com o padrão do agente |
| Scripts não testados | Engine quebrada = skill queimada | Testar scripts antes |
| Iterar sem evidência | "Acho que ficou melhor" | Rode os evals |

user-invocable: true
---

## 6. 25 exemplos curados

### Do pacote oficial (anthropics/skills)

| # | Skill | Destaque | Lição de design |
|---|---|---|---|
| 1 | **docx** | Gera `.docx` válidos (python-docx) com TOC, headers, paginação | Ensina o agente a usar a ferramenta de geração, não o conteúdo |
| 2 | **pdf** | Ler/extrair/criar, OCR, merge, split | Escopo amplo bem roteado; cada operação tem seu caminho |
| 3 | **pptx** | Cria/edita decks com layouts e notas | Trigger por sinônimo do usuário ("deck", "slides") > nome técnico |
| 4 | **xlsx** | Planilhas: ler, escrever, fórmulas, charting, limpeza | "Quando NÃO usar" escrito explicitamente economiza contexto |
| 5 | **mcp-builder** | Cria servidores MCP (FastMCP ou SDK TS) | Ensinar o design (o *porquê*) é tão importante quanto a implementação |
| 6 | **webapp-testing** | Testa apps web com Playwright: screenshot, logs, fluxos | Workflow numerado + evidência por screenshot + subagente próprio |
| 7 | **skills-creator** | Cria skills via metodologia eval-driven | Meta-skill: ensina o próprio processo de criação de skills |

### Do pacote de engenheiro (mattpocock/skills)

| # | Skill | Destaque | Lição de design |
|---|---|---|---|
| 8 | **writing-great-skills** | Vocabulário e princípios de skills previsíveis | Pacote de skills precisa de um padrão compartilhado |
| 9 | **tdd** | Red-green-refactor test-first | Workflows de *processo* também são skills e mudam comportamento |
| 10 | **code-review** | Revisa em 2 eixos (standards × spec) com subagentes | Skill composta (skill orquestra subagentes) escala melhor |
| 11 | **debug-systematically** | Debug com evidência, hipóteses, fix mínimo | O maior valor é impedir o comportamento ruim (chutar fix) |
| 12 | **domain-modeling** | Glossário + ADRs + terminologia de domínio | Skill estreita com contrato de saída vence skill larga sem contrato |
| 13 | **grill-me** | Entrevista implacável para afiar planos | Skills de *interação* mudam o workflow do usuário tanto quanto skills de código |
| 14 | **research** | Fontes primárias confiáveis → markdown no repo | Definir *qualidade da fonte* evita alucinação coletiva |
| 15 | **to-spec / to-tickets** | Conversa → spec/issues/tickets com blocking edges | Skill que estrutura a saída para outro sistema (tracker) multiplica valor |
| 16 | **doc-coauthoring** | Coautoria de documentação em fases | Human-in-the-loop bem desenhado = mais confiável |

### Do pacote composable (obra/superpowers)

| # | Skill | Destaque | Lição de design |
|---|---|---|---|
| 17 | **brainstorming + writing-plans** | Divergir → convergir; done criteria antes de executar | Separar pensar de planejar vence em qualquer escala |
| 18 | **executing-plans** | Passos pequenos e verificáveis, um por vez | Execução exige ground truth a cada passo |
| 19 | **poetry** | Oficina criativa em etapas | Metodologia é universal — não só para código |
| 20 | **skills-authoring** | Criar/melhorar skills com scripts de validação | Automatize o automatizável dentro da skill |

### Nossas e do ecossistema

| # | Skill | Destaque | Lição de design |
|---|---|---|---|
| 21 | **zenox-pentest** (nossa) | Pentest Playwright + Python, 5 fases, princípios de autorização/evidência no topo | Skill viva em uso real vence skill decorativa |
| 22 | **zenox-scraper** (nossa) | Scraping enterprise: stealth, CAPTCHA, saída CSV/JSON/XLSX | Par de skills que compartilham infra (Playwright) com contratos diferentes |
| 23 | **skill-creator** (opencode) | O pacote eval-driven mais completo: grader, comparator, analyzer como subagentes + scripts de benchmark | Evals não são opcionais; são o motor da iteração |
| 24 | **/deep-research** (Claude Code) | Pesquisa fan-out com verificação adversária | Skill pode ser o entry point de um sistema multi-agente completo |
| 25 | **Skills pessoais** (ex.: resumo de artigos) | Adapta o agente ao workflow individual do usuário | A skill mais poderosa é a que só o seu usuário aciona |

---

## 7. Checklist de 60 segundos

- [ ] Intenção em 1 frase (comportamento que muda)?
- [ ] Evals criados ANTES do corpo (acionamento + execução + anti-casos)?
- [ ] Baseline medido SEM a skill?
- [ ] `SKILL.md` < 500 linhas com progressive disclosure?
- [ ] Descrição com triggers verbatim + quando NÃO usar?
- [ ] Workflow numerado com passos verificáveis?
- [ ] Contrato de saída explícito (formato, checklist)?
- [ ] Evals rodados e nota registrada?
- [ ] Iteração por evidência (1 variável por vez)?

user-invocable: true
---

## 8. Fontes

- [anthropics/skills](https://github.com/anthropics/skills) — skills oficiais com evals
- [mattpocock/skills](https://github.com/mattpocock/skills) — skills de engenheiro real
- [obra/superpowers](https://github.com/obra/superpowers) — metodologia composable
- Ciclo evaluation-driven: Capture Intent → Create Evals → Baseline → Minimal Instructions → Execute Evals → Iterate & Optimize Description
- [Building Effective Agents — Anthropic](https://www.anthropic.com/engineering/building-effective-agents) — ACI, poka-yoke
