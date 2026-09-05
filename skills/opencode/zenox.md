# Zenox Router

**Nome do modelo**: zenox
**Modos**: `ultramode` | `base`

## Arquitetura

```
┌─────────────────────────────────────────────────┐
│                  ZENOX ROUTER                    │
│                                                  │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐  │
│  │  MiMo    │───▶│ DeepSeek │───▶│  MiMo    │  │
│  │  V2.5    │◀───│ V4 Flash │◀───│ Review   │  │
│  │          │    │          │    │          │  │
│  │ Entende  │    │ Executa  │    │ Verifica │  │
│  │ Planeja  │    │ Passo a  │    │ Se OK →  │  │
│  │ Responde │    │ passo    │    │ responde │  │
│  └──────────┘    └──────────┘    └──────────┘  │
│       │                              │          │
│       └──────── LOOP até OK ────────┘          │
└─────────────────────────────────────────────────┘
```

## Modos

### `base` (padrão)
- Planejamento rápido com poucos passos
- Execução direta
- 1 rodada de review
- Tarefas simples a médias

### `ultramode`
- Planejamento profundo com análise de edge cases
- Execução detalhada com verificação intermediária
- Múltiplas rodadas de review até aprovação
- Tarefas complexas, multi-arquivo, arquitetura

## Fluxo de Execução

### Fase 1: Entendimento + Planejamento (MiMo V2.5)

Ao receber uma tarefa, o agente MiMo deve:

1. **Entender completamente** a intenção do usuário
2. **Explorar o codebase** relevante (ler arquivos, grep, glob)
3. **Gerar plano estruturado**:

```
## PLANO ZENOX
**Modo**: base | ultramode
**Tarefa**: [descrição clara]

### Passos:
1. [ação específica] - arquivos: [lista] - complexidade: baixa/media/alta
2. [ação específica] - arquivos: [lista] - complexidade: baixa/media/alta
...

### Dependências:
- Passo 2 depende do passo 1
- Passos 3 e 4 são independentes (podem paralelizar)

### Validação:
- Critério de sucesso para cada passo
- Critério de sucesso global

### Dados de Contexto:
- [informações que o executor precisa saber]
- [limitações, preferências, constraints]
```

4. **Passar o plano** para o DeepSeek Flash via subagent

### Fase 2: Execução (DeepSeek V4 Flash)

O DeepSeek Flash recebe o plano e:

1. **Executa cada passo** sequencialmente (ou em paralelo se independentes)
2. **Após cada passo**, registra o resultado exato:

```
## RELATÓRIO DE EXECUÇÃO ZENOX
**Passo 1**: [descrição]
- Status: SUCESSO | FALHA
- Arquivos modificados: [lista com diffs resumidos]
- Arquivos criados: [lista]
- Arquivos deletados: [lista]
- Commands executados: [lista]
- Output relevante: [trechos]
- Erros encontrados: [se houver]

**Passo 2**: [descrição]
- Status: SUCESSO | FALHA
...

**Resumo**:
- Total de passos: N
- Sucesso: X
- Falha: Y
- Dados exatos de cada modificação: [JSON/detalhes]
```

3. **Envia o relatório completo** de volta ao MiMo

### Fase 3: Review (MiMo V2.5)

MiMo recebe o relatório e:

1. **Verifica cada passo** contra o plano original
2. **Verifica os dados** - se as modificações estão corretas
3. **Decisão**:

```
## REVIEW ZENOX
**Passo 1**: APROVADO ✓
**Passo 2**: REPROVADO ✗ - [motivo específico]
**Passo 3**: APROVADO ✓

**Ação necessária**:
- Repetir passo 2 com correção: [instrução específica]

**OU**

**TODOS APROVADOS** → Finalizar e responder ao usuário
```

4. **Se REPROVADO**: Envia instrução de correção ao DeepSeek Flash → volta à Fase 2
5. **Se APROVADO**: Monta resposta final para o usuário

### Fase 4: Resposta Final (MiMo V2.5)

MiMo responde ao usuário com sinceridade:

```
## Tarefa Concluída

### O que foi feito:
- [resumo claro de cada alteração]

### Arquivos modificados:
- path/to/file1 - [o que mudou]
- path/to/file2 - [o que mudou]

### Como funciona:
- [explicação técnica se necessário]

### Possíveis melhorias futuras:
- [sugestões honestas]
```

## Regras do Loop

1. **Máximo de 5 iterações** no modo base
2. **Máximo de 10 iterações** no modo ultramode
3. Se atingir o máximo sem conclusão, MiMo responde honestamente o que conseguiu e o que falta
4. Cada iteração deve ser mais específica que a anterior
5. MiMo nunca diz "está pronto" se os dados não conferem

## Como Usar

No OpenCode TUI:
```
/zenox base [tarefa]       # modo rápido
/zenox ultramode [tarefa]  # modo profundo
```

Ou invoke a skill diretamente:
```
/skill zenox
```

## Compatibilidade

Este skill funciona com o formato OpenAI-compatible. O endpoint ZENOX pode ser acessado via:
- `@ai-sdk/openai-compatible` com `baseURL` do provedor
- Modelo identificado como `zenox-base` ou `zenox-ultramode`

## Exemplo de Uso

```
Usuário: /zenox base "criar uma API REST com CRUD de usuários"

MiMo (planeja):
1. Criar estrutura de pastas - baixa
2. Criar modelo de dados - media
3. Criar rotas CRUD - media
4. Criar middleware de validação - baixa
5. Criar testes - media

DeepSeek (executa):
- Passo 1: OK, criou src/routes/, src/models/, src/middleware/
- Passo 2: OK, criou User model com validação
- Passo 3: OK, criou GET/POST/PUT/DELETE
- Passo 4: OK, criou middleware de auth
- Passo 5: OK, criou testes básicos

MiMo (review):
- Passos 1-5: APROVADOS
→ Responde ao usuário com resumo
```
