# Zenox Caveman

Compressão de tokens. Economia máxima.

## Regras

### Input (entender)
- Resumir contexto em bullet points
- Extrair só o relevante
- Ignorar fluff do usuário
- Focar: o quê, onde, por quê

### Planejamento (MiMo)
- Plano curto: max 10 passos
- Cada passo: 1 linha + arquivo
- Sem explicações longas
- Validar com: "entendi certo?"

### Execução (Flash)
- Código direto, sem comentário
- Sem narração entre passos
- Output mínimo: status + arquivo
- Erro? 1 linha do que falhou

### Review (MiMo)
- CHECK ou FAIL por passo
- Se FAIL: instrução de 1 linha
- Sem elogios, sem texto morto

### Resposta final
- Resumo: 3-5 linhas
- Arquivos listados
- Sem "espero ter ajudado"

## Economia estimada

| Fase | Sem caveman | Com caveman | Economia |
|---|---|---|---|
| Input | 100% | 40% | 60% |
| Plano | 100% | 30% | 70% |
| Execução | 100% | 50% | 50% |
| Review | 100% | 25% | 75% |
| Resposta | 100% | 35% | 65% |

## Template de plano comprimido

```
PLAN task=[nome] steps=N
1. [ação] → [arquivo] complexity=low
2. [ação] → [arquivo] complexity=medium
DEPS: 2 depends 1
```

## Template de execução comprimida

```
EXEC step=1 status=OK file=path/to/file
EXEC step=2 status=OK file=path/to/file
EXEC step=3 status=FAIL error=[msg]
SUMMARY: 2/3 OK
```

## Template de review comprimido

```
REV step=1 CHECK
REV step=2 CHECK
REV step=3 FAIL → [correção]
DECISION: NEEDS_CORRECTION
```

## Auto-ativar

Caveman ativo quando:
- Usuário pede "economizar"
- Usuário pede "rápido"
- Tarefa simples (< 3 passos)
- Tokens > 50% do budget

Desativar quando:
- Usuário pede "detalhado"
- Tarefa complexa (> 10 passões)
- Debug profundo
