# Zeno Agent — UI + servidor em C

Clone estático da interface do Zeno Agent. O painel Browser abre uma **guia dedicada "ZENO AGENT"** no seu próprio navegador — sem headless, sem proxy, sem iframe.

## Como usar

```
zeno-server.exe          (porta padrão 8080)
```

Abra http://localhost:8080

Para recompilar:
```
gcc -O2 -o zeno-server.exe server.c -lws2_32
```

Sem dependências: C puro com WinSock. Não precisa de Node, Python, libcurl nem navegador adicional.

## Como o Browser funciona

1. Abra o painel **Browser** e clique em **"Abrir guia ZENO AGENT"** (ou digite uma URL e clique em **Go**)
2. O navegador cria uma guia/janela com o nome **ZENO AGENT**
3. Cada **Go** seguinte reutiliza a mesma guia (o navegador agrupa pelo `name` da janela)

Controles do painel atuam na guia:
- **Go** → navega a guia para a URL
- **Back/Forward** → histórico mantido pelo painel (cross-origin não expõe o history da guia, então controlamos o nosso)
- **Reload** → re-navega para a URL atual
- **Home** → volta ao DuckDuckGo Lite

Se o navegador bloquear o pop-up, permita pop-ups para o site.

## Barra de endereço

- `site.com`, `foo.net/pagina`, `sub.dominio.site:8080` → vai direto para a URL
- `localhost:3000/app`, `192.168.0.10:8080` → vai direto (http)
- Qualquer outra coisa → busca no DuckDuckGo Lite

## O que mais funciona

- Layout completo: sidebar, topbar, command palette, workspaces
- 42 temas (claro/escuro) com persistência
- Chat simulado com markdown, syntax highlight, thinking blocks, tool calls
- Painéis: Context, Stack, Terminal, Project notes, Chat, Browser (guia dedicada)
- Memory map com grafo de notas
- Settings funcionais (Appearance, Chat, Notifications, Voice, Shortcuts, Projects)
- Configurações persistidas em `zeno-config.json` no servidor + localStorage

## O que não funciona

- Sem IA real: respostas do chat são mock
- Sem voz: STT/TTS precisam de chave de serviço externa
- Terminal do painel é simulado

## Estrutura

- `server.c` — servidor HTTP em C: estáticos + API de config (`/api/config`)
- `zeno-server.exe` — binário compilado
- `index.html` / `app.js` / CSS — frontend vanilla
- `black-hole-threejs.html` — background animado (iframe local)
