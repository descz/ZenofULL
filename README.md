# Zeno Agent — Wails

Aplicativo desktop do Zeno Agent empacotado com Wails. A interface continua em HTML/CSS/JavaScript e a transcrição Deepgram roda no backend Go do próprio processo, sem servidor HTTP, porta local ou CORS.

## Como usar
Execute `wails-app/build/bin/ZenoAgent.exe`.

Para gerar novamente o executável, execute `build-wails.cmd`. Configure o `.env` ao lado do executável ou use variáveis de ambiente; a chave não é copiada para o bundle.

## Transcrição Deepgram
Defina `DEEPGRAM_API_KEY` no ambiente ou em um `.env` ao lado de `ZenoAgent.exe`. O frontend grava o áudio e chama diretamente o método Go `App.TranscribeAudio`; a chave nunca entra no JavaScript.

## O que funciona

### Shell
- Layout idêntico ao original (CSS compilado real do projeto + DOM capturado do app vivo)
- Sidebar: nova sessão, seleção, hover actions, resize por drag, colapsar
- Splash de loading com cubo isométrico, animações de entrada/hover/clique
- Command palette (Ctrl+K): busca sessões + atalhos (toggle theme, open settings)
- Settings (Ctrl+, ou botão da sidebar): 20 seções navegáveis

### Navbar (9 painéis laterais funcionais)
- **Context / Git / Pull Request / Changes / Walkthrough / Files / Terminal / Project notes / Browser**
- Cada botão abre/fecha o painel com animação de slide (200ms), largura real por painel (446/502/670px), resize por drag, botões expand/close
- Files: árvore de arquivos; Terminal: terminal simulado com input; demais: empty states fiéis ao original

### Settings
- Dialog com overlay escuro + animação de entrada (scale 0.98 → 1), sidebar de navegação com busca
- **Appearance funcional**: Color mode (System/Light/Dark) + seletor de tema claro/escuro com **42 temas reais** extraídos do build (Zeno Mono, Flexoki, Dracula, Nord, Catppuccin, Solarized, Tokyonight…) — mudam as cores na hora e persistem

### Chat
- Mensagens user/assistant com markdown, code blocks com syntax highlight + copy/wrap
- Design de resposta selecionável em diálogo amplo: Default, None, Low, Medium, High, Extra high e Max, com descrições persistentes
- Ditado por microfone com gravação, estado de transcrição e integração Deepgram no backend Go
- **Tools**: tool calls renderizados como no original (Shell Command com comando digitando, tempo, hover arrow) — clique expande o output com linha-guia e syntax highlight
- **Thinking**: spinner durante o raciocínio + bloco "Thought process" colapsável
- Recap, sugestão de follow-up, typing indicator, respostas simuladas locais

## Estrutura
- `index.html` — shell + sprite SVG (236 ícones reais) + splash
- `app.js` — app vanilla JS (~96KB, zero dependências)
- `style.css` — temas base + animações (clique, dialogs, menus, spinner, tool output)
- `themes.css` — 42 paletas de tema extraídas do build real
- `themes.js` — metadados dos temas (nome/variante)
- `wails-app/app.go` — backend Go da transcrição Deepgram
- `wails-app/main.go` — janela e runtime Wails
- `wails-app/frontend/build.mjs` — empacota a UI atual no executável
- `build-wails.cmd` — recompila o aplicativo Windows
- `.env.example` — variáveis esperadas pelo backend de transcrição
- `assets/` — CSS compilado real do build (index.css 390KB + main.css)

## Fidelidade verificada (pixel a pixel, 1440x900, vs UI real servida)
- Estado vazio: 95.6% idêntico | Chat: 94.1% | Painel Files: 93.0% | Settings: 90.6% | Tools: 86.0%
- Nav rail e topbar: 99.4% / 100%
- Bounding boxes de sidebar, nav, msgCol e composer: 1:1

## Limitações (por design)
- Sem backend: respostas são simuladas localmente (mock), não há IA real
- Seções de settings fora de Appearance/General mostram placeholder
- Menus de modelo/variante/agente no composer são visuais (sem dropdown)
