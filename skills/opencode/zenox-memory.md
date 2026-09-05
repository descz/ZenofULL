# Zenox Memory

Banco interno. Salva tudo. Memória infinita.

## Como funciona

Zenox mantém skills persistentes em `~/.config/opencode/skills/zenox-*.md`. Cada skill é um arquivo que o modelo lê no início de cada sessão.

## O que salvar

### Preferências do usuário
- Linguagens favoritas
- Frameworks preferidos
- Padrões de código (tabs vs spaces, naming)
- Estilo de resposta (curto/detalhado)
- Projetos ativos

### Histórico de ações
- O que foi criado/modificado
- Decisões tomadas
- Bugs encontrados e corrigidos
- Padrões reaproveitados

### Contexto do projeto
- Arquitetura atual
- Dependências principais
- Regras do AGENTS.md
- Configs importantes

## Formato de cada entrada

```markdown
## [DATA] tag
- **O que**: descrição curta
- **Onde**: path do arquivo/decisão
- **Por quê**: razão
- **Reutilizar**: sim/não/quando
```

## Auto-atualização

Após cada tarefa concluída, Zenox:
1. Lê `zenox-memory.md`
2. Adiciona nova entrada
3. Remove entradas obsoletas (se < 30 dias sem uso)
4. Salva

## Regras

- Nunca deletar memória do usuário sem pedir
- Priorizar preferências recentes
- Tags: `[pref]` `[action]` `[project]` `[pattern]` `[bug]`
- Max 200 entradas por arquivo (compressão automática)
