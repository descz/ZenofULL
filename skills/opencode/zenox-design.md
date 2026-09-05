# Zenox Design

Padrões visuais e UX. Use sempre que criar UI.

## Regras

### Layout
- Grid 12 colunas ou flexbox
- Max-width: 1200px centralizado
- Spacing: 4px base (4, 8, 12, 16, 24, 32, 48, 64)
- Border-radius: 8px default, 12px cards, 9999px pills

### Cores
- Primária: #6366f1 (indigo)
- Sucesso: #22c55e
- Erro: #ef4444
- Aviso: #f59e0b
- Texto: #1e293b
- Fundo: #ffffff / #0f172a (dark)
- Muted: #64748b

### Tipografia
- Font: system-ui, -apple-system, sans-serif
- H1: 2rem bold
- H2: 1.5rem semibold
- Body: 1rem regular
- Small: 0.875rem
- Code: 0.875rem monospace

### Componentes
- Botão primário: bg indigo, text white, hover darken 10%
- Input: border gray-300, focus ring indigo
- Card: bg white, shadow sm, border gray-100
- Modal: backdrop blur, centered, max-w-md

### Agentic UI
- Status indicators: dot verde (ativo), amarelo (pensando), vermelho (erro)
- Progress: barra linear ou steps
- Output: monospace, scroll, auto-scroll bottom
- Thinking: collapsible section com reasoning_content

### Animações
- Transitions: 150ms ease
- Hover: scale 1.02 ou opacity 0.8
- Loading: pulse ou spinner
- Page: fade-in 200ms

## Dark mode

```css
:root {
  --bg: #0f172a;
  --text: #e2e8f0;
  --border: #1e293b;
  --card: #1e293b;
}
```

## Padrão Zenox

Cor primária do Zenox: #6366f1 → #8b5cf6 (gradient).
Ícone: retângulo arredondado com "Z" branco.
