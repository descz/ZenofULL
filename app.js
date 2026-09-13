/* OpenChamber UI — static clone (no server, no build). Vanilla JS. */
(() => {
  'use strict';

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  const now = () => new Date();
  const fmtTime = (d) => d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  const uid = () => Math.random().toString(36).slice(2, 10);
  const relTime = (s) => {
    const ts = s.updatedAt || s.createdAt;
    if (!ts) return s.time || '';
    const diff = Date.now() - new Date(ts).getTime();
    if (!(diff >= 0)) return '';
    const min = Math.floor(diff / 60000);
    if (min < 1) return 'agora';
    if (min < 60) return `${min}min`;
    const h = Math.floor(min / 60);
    if (h < 24) return `${h}h`;
    return `${Math.floor(h / 24)}d`;
  };

  const LS = {
    get(k, fb) { try { const v = localStorage.getItem(k); return v === null ? fb : JSON.parse(v); } catch { return fb; } },
    set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch {} },
  };

  /* ---------- mock data ---------- */
  const SEED_SESSIONS = [
    {
      id: 'ses_clone_1', title: 'Teste Clone', active: true,
      createdAt: new Date(Date.now() - 19 * 60000).toISOString(),
      messages: [
        { role: 'user', text: 'Explique o que é um for loop em JavaScript com um exemplo curto de código. Uma linha só.', time: '4:33 PM' },
        {
          role: 'assistant', time: '4:33 PM', model: 'DeepSeek V4 Pro', agent: 'build', duration: '10.9s',
          text: '**For loop** em JavaScript repete um bloco de código enquanto uma condição for verdadeira, com inicialização, condição e incremento.',
          code: { lang: 'js', lines: [['for', 'keyword'], [' (', null], ['let', 'keyword'], [' ', null], ['i', 'variable'], [' =', 'operator'], [' 0', 'number'], [';', 'operator'], [' ', null], ['i', 'variable'], [' <', 'operator'], [' 5', 'number'], [';', 'operator'], [' ', null], ['i', 'variable'], ['++', 'operator'], [') ', null], ['console.log', 'function'], ['(', null], ['i', 'variable'], [')', null], [';', 'operator'], [' // imprime 0 1 2 3 4', 'comment']] },
          recap: 'For loop repete código enquanto condição verdadeira; inicialização, condição, incremento. Exemplo: `for (let i=0; i<5; i++) console.log(i)` imprime 0 a 4.',
          suggestion: 'Mostre um exemplo de como percorrer um array usando `for` em JavaScript.'
        },
      ],
    },
    { id: 'ses_clone_2', title: 'Responder apenas OK', createdAt: new Date(Date.now() - 60 * 60000).toISOString(), messages: [] },
    { id: 'ses_clone_3', title: 'Mensagem de saudação', createdAt: new Date(Date.now() - 5 * 3600000).toISOString(), messages: [] },
  ];

  const CHIPS = [
    ['oc-compass-3', 'Explore the codebase'],
    ['oc-history', 'Catch me up'],
    ['oc-survey', 'Start feature planning'],
    ['oc-target', 'Craft a Goal'],
    ['oc-calendar-schedule', 'Schedule a Task'],
    ['oc-bug', 'Debug an issue'],
  ];
  const PLACEHOLDER_PHRASES = [
    'Defina um objetivo para esta sessão',
    'Me ajude a criar uma solução',
    'Crie um código novo',
    'Explique este trecho de código',
    'Planeje uma nova funcionalidade',
    'Investigue um erro no projeto',
    'Organize o contexto desta tarefa',
    'Transforme uma ideia em execução',
  ];

  const MODELS = ['DeepSeek V4 Pro', 'gpt-4o-mini', 'claude-sonnet-4-5', 'gemini-2.5-flash'];
  const AGENTS = ['Build', 'Plan', 'Ask'];
  const DESIGN_OPTIONS = [
    ['Default', 'Equilíbrio entre velocidade, contexto e profundidade.'],
    ['None', 'Respostas diretas, sem esforço adicional de raciocínio.'],
    ['Low', 'Raciocínio leve para tarefas simples e rápidas.'],
    ['Medium', 'Mais contexto para decisões e implementações comuns.'],
    ['High', 'Análise aprofundada para problemas com várias partes.'],
    ['Extra high', 'Investiga alternativas, riscos e detalhes antes de responder.'],
    ['Max', 'Máxima profundidade e verificação para tarefas críticas.'],
  ];

  const FILES_TREE = [
    ['oc-folder', 'packages', true, [
      ['oc-folder', 'ui', true, [
        ['oc-folder', 'src', true, [
          ['oc-file', 'index.ts'],
          ['oc-file', 'App.tsx'],
          ['oc-file', 'components.tsx'],
        ]],
        ['oc-file', 'package.json'],
      ]],
      ['oc-folder', 'web', true, [
        ['oc-folder', 'server', true, [
          ['oc-file', 'index.js'],
        ]],
        ['oc-file', 'index.html'],
        ['oc-file', 'package.json'],
      ]],
    ]],
    ['oc-file', 'AGENTS.md'],
    ['oc-file', 'README.md'],
    ['oc-file', 'package.json'],
    ['oc-file', 'bun.lock'],
  ];

  const MEMORY_NOTES = [
    {
      id: 'memory-orbit',
      title: 'Zeno workspace',
      tag: 'Core memory',
      excerpt: 'O centro de gravidade das decisões, padrões e contexto que o agente reutiliza.',
      content: '# Zeno workspace\n\nEste é o mapa vivo do contexto que o Zeno reuniu durante as sessões. As notas próximas representam ideias relacionadas; novas descobertas podem ser capturadas a qualquer momento.\n\n## Como ler\n\n- O núcleo reúne o contexto mais importante.\n- Cada ramo organiza um tipo de memória.\n- Clique em qualquer cartão para abrir a nota completa em Markdown.',
      accent: '#8b7cff',
      x: 50,
      y: 47,
      root: true,
      updated: 'Atualizado agora',
    },
    {
      id: 'memory-project-context',
      title: 'Contexto do projeto',
      tag: 'Context',
      excerpt: 'Arquivos, limites e objetivos que definem a superfície atual de trabalho.',
      content: '# Contexto do projeto\n\nO agente mantém aqui os sinais que ajudam a retomar uma tarefa sem recomeçar do zero.\n\n## Inclui\n\n- Pastas vinculadas ao projeto\n- Objetivos ativos\n- Convenções descobertas nos arquivos\n- Dependências que não podem ser esquecidas',
      accent: '#58b6ff',
      x: 23,
      y: 24,
      updated: 'há 4 min',
    },
    {
      id: 'memory-sessions',
      title: 'Sessões recentes',
      tag: 'Conversations',
      excerpt: 'Conversas que deixaram decisões úteis para as próximas interações.',
      content: '# Sessões recentes\n\nAs conversas do Zeno alimentam o mapa quando uma decisão, uma descoberta ou um padrão pode ser reutilizado.\n\n> Uma boa memória reduz o caminho entre a pergunta e a próxima ação.\n\nUse **Capture note** na tela de Memory para guardar o último resultado da sessão atual.',
      accent: '#65d7c1',
      x: 78,
      y: 23,
      updated: 'há 8 min',
    },
    {
      id: 'memory-decisions',
      title: 'Decisões de produto',
      tag: 'Decisions',
      excerpt: 'Escolhas registradas para manter a interface coerente ao longo do tempo.',
      content: '# Decisões de produto\n\nA interface deve ser silenciosa, densa e legível. O mapa usa relações visuais para mostrar contexto sem transformar a memória em uma lista infinita.\n\n### Princípios\n\n1. Mostrar o que é relevante agora.\n2. Manter o detalhe a um clique de distância.\n3. Preservar a paleta escura do Zeno.',
      accent: '#f0ad67',
      x: 20,
      y: 68,
      updated: 'há 18 min',
    },
    {
      id: 'memory-patterns',
      title: 'Padrões descobertos',
      tag: 'Patterns',
      excerpt: 'Soluções e estruturas que apareceram mais de uma vez no trabalho.',
      content: '# Padrões descobertos\n\nO agente pode agrupar repetições em notas menores e conectá-las ao contexto que as explica.\n\n```js\nconst memory = new Map();\nmemory.set("pattern", "reusable context");\n```\n\nEsse tipo de nota funciona como uma pequena biblioteca de decisões práticas.',
      accent: '#e785b9',
      x: 80,
      y: 76,
      updated: 'há 22 min',
    },
    {
      id: 'memory-projects',
      title: 'Projetos e escopos',
      tag: 'Workspace',
      excerpt: 'Relações entre pastas, projetos e os chats que dão continuidade ao trabalho.',
      content: '# Projetos e escopos\n\nCada projeto pode reunir mais de uma pasta e manter seus próprios chats. Isso separa o contexto de uma iniciativa do restante das sessões.\n\nAbra **Projects** na lateral para criar um novo escopo ou iniciar um chat dentro dele.',
      accent: '#b48cff',
      x: 49,
      y: 13,
      updated: 'há 31 min',
    },
    {
      id: 'memory-tools',
      title: 'Ferramentas do agente',
      tag: 'Capabilities',
      excerpt: 'Comandos, plugins e superfícies que o Zeno pode usar para transformar contexto em ação.',
      content: '# Ferramentas do agente\n\nShell, navegador e integrações aparecem como capacidades conectadas ao contexto. Marketplace será o lugar para descobrir novos plugins e skills.\n\nPor enquanto, esta nota representa o conjunto local de ferramentas disponível nesta réplica estática.',
      accent: '#92d36e',
      x: 50,
      y: 87,
      updated: 'há 36 min',
    },
    { id: 'memory-skills', title: 'Skills', tag: 'Knowledge base', excerpt: 'Capacidades locais organizadas por área.', content: '# Skills\n\nMapa das capacidades disponíveis na pasta `skills` do projeto.', accent: '#fff', x: 72, y: 48, updated: 'agora' },
    { id: 'skill-engineering', title: 'Engineering', tag: 'Skills', excerpt: 'Implementação, revisão e arquitetura.', content: '# Engineering\n\nSkills para implementar, revisar, testar e estruturar software.', accent: '#fff', x: 84, y: 30, updated: 'agora' },
    { id: 'skill-implement', title: 'Implement', tag: 'Engineering', excerpt: 'Implementação orientada por especificações.', content: '# Implement\n\nExecuta trabalhos definidos por especificações ou tickets.', accent: '#fff', x: 94, y: 17, updated: 'agora' },
    { id: 'skill-code-review', title: 'Code review', tag: 'Engineering', excerpt: 'Revisão de padrões e requisitos.', content: '# Code review\n\nRevisa alterações quanto a bugs, padrões e aderência à especificação.', accent: '#fff', x: 96, y: 29, updated: 'agora' },
    { id: 'skill-tdd', title: 'TDD', tag: 'Engineering', excerpt: 'Desenvolvimento guiado por testes.', content: '# TDD\n\nFluxo red, green e refactor para mudanças seguras.', accent: '#fff', x: 96, y: 41, updated: 'agora' },
    { id: 'skill-design', title: 'Design & UI', tag: 'Skills', excerpt: 'Interfaces intencionais e testes visuais.', content: '# Design & UI\n\nDireção visual e validação funcional da interface.', accent: '#fff', x: 84, y: 55, updated: 'agora' },
    { id: 'skill-frontend', title: 'Frontend design', tag: 'Design', excerpt: 'Identidade visual e composição.', content: '# Frontend design\n\nCria interfaces distintas, deliberadas e coerentes com o produto.', accent: '#fff', x: 96, y: 52, updated: 'agora' },
    { id: 'skill-webtest', title: 'Webapp testing', tag: 'Design', excerpt: 'Testes de interface com Playwright.', content: '# Webapp testing\n\nValida comportamento, responsividade e erros no navegador.', accent: '#fff', x: 96, y: 64, updated: 'agora' },
    { id: 'skill-automation', title: 'Automation', tag: 'Skills', excerpt: 'Scraping, MCP e fluxos operacionais.', content: '# Automation\n\nFerramentas para integrar serviços e automatizar tarefas.', accent: '#fff', x: 68, y: 76, updated: 'agora' },
    { id: 'skill-scraper', title: 'Zenox scraper', tag: 'Automation', excerpt: 'Extração estruturada da web.', content: '# Zenox scraper\n\nFramework Playwright para coleta e estruturação de dados web.', accent: '#fff', x: 77, y: 91, updated: 'agora' },
    { id: 'skill-mcp', title: 'MCP builder', tag: 'Automation', excerpt: 'Integrações por ferramentas MCP.', content: '# MCP builder\n\nConstrói servidores MCP com interfaces de ferramentas bem definidas.', accent: '#fff', x: 63, y: 94, updated: 'agora' },
    { id: 'skill-knowledge', title: 'Knowledge work', tag: 'Skills', excerpt: 'Pesquisa, documentação e escrita.', content: '# Knowledge work\n\nSkills para transformar fontes em conhecimento reutilizável.', accent: '#fff', x: 38, y: 78, updated: 'agora' },
    { id: 'skill-research', title: 'Research', tag: 'Knowledge', excerpt: 'Pesquisa em fontes primárias.', content: '# Research\n\nInvestiga questões e registra resultados verificáveis.', accent: '#fff', x: 27, y: 92, updated: 'agora' },
    { id: 'skill-docs', title: 'Doc coauthoring', tag: 'Knowledge', excerpt: 'Criação colaborativa de documentos.', content: '# Doc coauthoring\n\nEstrutura documentação, propostas e especificações.', accent: '#fff', x: 42, y: 96, updated: 'agora' },
    { id: 'skill-security', title: 'Security', tag: 'Skills', excerpt: 'Auditoria e reconhecimento autorizado.', content: '# Security\n\nCapacidades de avaliação de segurança e pesquisa pública.', accent: '#fff', x: 17, y: 59, updated: 'agora' },
    { id: 'skill-pentest', title: 'Zenox pentest', tag: 'Security', excerpt: 'Testes autorizados de aplicações web.', content: '# Zenox pentest\n\nFramework de avaliação de segurança para aplicações autorizadas.', accent: '#fff', x: 5, y: 53, updated: 'agora' },
    { id: 'skill-dorks', title: 'Google dorks', tag: 'Security', excerpt: 'Pesquisa pública avançada.', content: '# Google dorks\n\nOperadores avançados para investigação de informações públicas.', accent: '#fff', x: 7, y: 68, updated: 'agora' },
  ];

  const MEMORY_EDGES = [
    ['memory-orbit', 'memory-project-context'],
    ['memory-orbit', 'memory-sessions'],
    ['memory-orbit', 'memory-decisions'],
    ['memory-orbit', 'memory-patterns'],
    ['memory-orbit', 'memory-projects'],
    ['memory-orbit', 'memory-tools'],
    ['memory-projects', 'memory-project-context'],
    ['memory-projects', 'memory-sessions'],
    ['memory-decisions', 'memory-patterns'],
    ['memory-orbit', 'memory-skills'],
    ['memory-skills', 'skill-engineering'],
    ['skill-engineering', 'skill-implement'], ['skill-engineering', 'skill-code-review'], ['skill-engineering', 'skill-tdd'],
    ['memory-skills', 'skill-design'], ['skill-design', 'skill-frontend'], ['skill-design', 'skill-webtest'],
    ['memory-skills', 'skill-automation'], ['skill-automation', 'skill-scraper'], ['skill-automation', 'skill-mcp'],
    ['memory-orbit', 'skill-knowledge'], ['skill-knowledge', 'skill-research'], ['skill-knowledge', 'skill-docs'],
    ['memory-orbit', 'skill-security'], ['skill-security', 'skill-pentest'], ['skill-security', 'skill-dorks'],
  ];

  const state = {
    theme: 'dark', // 'light' | 'dark'
    colorMode: 'dark',
    design: LS.get('oc-clone-design', 'Default'),
    model: LS.get('oc-clone-model', 'DeepSeek V4 Pro'),
    voiceMode: false,
    voiceLang: LS.get('oc-clone-voice-lang', (navigator.language || 'pt-BR').startsWith('en') ? 'en-US' : (navigator.language || 'pt-BR').startsWith('es') ? 'es-ES' : 'pt-BR'),
    lightTheme: LS.get('oc-clone-light-theme', 'openchamber-light'),
    darkTheme: LS.get('oc-clone-dark-theme', 'openchamber-dark'),
    sidebarW: LS.get('oc-clone-sbw', 280),
    sidebarOpen: true,
    sessions: LS.get('oc-clone-sessions', SEED_SESSIONS),
    activeId: LS.get('oc-clone-active', 'ses_clone_1'),
    typing: false,
    showPalette: false,
    panel: LS.get('oc-clone-panel', null),
    panelW: LS.get('oc-clone-panel-w', 502),
    settings: false,
    settingsSection: 'Appearance',
    expandedTools: {},
    traceExpanded: {},
    traceFilter: 'all',
    liveTrace: null,
    quickActions: LS.get('oc-clone-quick-actions', CHIPS),
    browserUrl: 'about:blank',
    browserHistory: ['about:blank'],
    browserHistoryIndex: 0,
    modal: null,
    archivedSessions: LS.get('oc-clone-archived-sessions', []),
    projects: LS.get('oc-clone-projects', []),
    modalAnchor: null,
    workspace: LS.get('oc-clone-workspace', 'chat'),
    activeProjectId: LS.get('oc-clone-active-project', null),
    memoryNotes: LS.get('oc-clone-memory-notes', MEMORY_NOTES),
    memoryEdges: LS.get('oc-clone-memory-edges', MEMORY_EDGES),
    noteId: null,
    memoryEditor: null,
    chatsCollapsed: LS.get('oc-clone-chats-collapsed', false) === true,
    editProjectId: null,
  };

  if (!['chat', 'memory'].includes(state.workspace)) state.workspace = 'chat';
  {
    let sessionsTouched = false;
    state.sessions.forEach((session) => {
      if (!session.createdAt) { session.createdAt = new Date().toISOString(); sessionsTouched = true; }
    });
    if (sessionsTouched) LS.set('oc-clone-sessions', state.sessions);
  }
  if (!Array.isArray(state.memoryNotes) || !state.memoryNotes.length) state.memoryNotes = MEMORY_NOTES;
  else {
    const storedMemoryIds = new Set(state.memoryNotes.map((note) => note.id));
    state.memoryNotes.push(...MEMORY_NOTES.filter((note) => !storedMemoryIds.has(note.id)));
  }
  if (!Array.isArray(state.memoryEdges)) state.memoryEdges = MEMORY_EDGES;
  else {
    const storedEdges = new Set(state.memoryEdges.map((edge) => edge.join('|')));
    state.memoryEdges.push(...MEMORY_EDGES.filter((edge) => !storedEdges.has(edge.join('|'))));
  }

  const PANELS = {
    Context: ['oc-donut-chart-fill', 502],
    Stack: ['oc-stack', 520],
    Terminal: ['oc-terminal-box', 670],
    'Project notes': ['oc-sticky-note', 670],
    Browser: ['oc-global', 670],
    Chat: ['oc-chat-history', 560],
  };

  // Migrate stale localStorage values from the removed/renamed surfaces.
  if (state.panel === 'Walkthrough') state.panel = 'Stack';
  if (state.panel && !PANELS[state.panel]) state.panel = null;
  LS.set('oc-clone-panel', state.panel);

  const icon = (name, cls = 'remixicon h-[18px] w-[18px]') =>
    `<svg class="${cls}" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><use href="#${name}"></use></svg>`;

  const activeSession = () => state.draftNew ? null : (state.sessions.find((s) => s.id === state.activeId) || state.sessions[0]);
  const activeProject = () => state.projects.find((project) => project.id === state.activeProjectId) || null;
  const projectSessions = (projectId) => state.sessions.filter((session) => session.projectId === projectId);
  const persistWorkspace = () => {
    LS.set('oc-clone-workspace', state.workspace);
    LS.set('oc-clone-active-project', state.activeProjectId);
  };
  const createSession = (projectId = null) => {
    const project = projectId ? state.projects.find((item) => item.id === projectId) : null;
    const session = {
      id: 'ses_' + uid(),
      title: project ? `New chat · ${project.name}` : `New chat ${state.sessions.length + 1}`,
      messages: [],
      createdAt: new Date().toISOString(),
      ...(project ? { projectId: project.id } : {}),
    };
    state.sessions.unshift(session);
    state.activeId = session.id;
    state.draftNew = false;
    LS.set('oc-clone-sessions', state.sessions);
    return session;
  };
  const BROWSER_HOME = 'about:blank';
  const resolveBrowserUrl = (value) => {
    const raw = String(value || '').trim();
    if (!raw || raw === 'about:blank') return BROWSER_HOME;
    if (/^https?:\/\//i.test(raw)) return raw;
    if (/^[\w.-]+\.[a-z]{2,}(\/.*)?$/i.test(raw)) return `https://${raw}`;
    return `https://www.startpage.com/sp/search?query=${encodeURIComponent(raw)}`;
  };

  const navigateBrowser = (value) => {
    const url = resolveBrowserUrl(value);
    state.browserUrl = url;
    if (window.ZenoNativeBrowser?.available()) window.ZenoNativeBrowser.navigate(url);
    render();
  };

  const tplBrowserPanel = () => {
    if (!window.ZenoNativeBrowser?.available()) return `<div class="native-browser-required"><div>${icon('oc-global', 'remixicon h-6 w-6')}</div><h3>Runtime nativo necessário</h3><p>HTML e iframe não são navegador. Execute <code>ZenoBrowser.exe</code> para usar WebCore real.</p></div>`;
    return `<div class="micro-browser native-real-browser flex h-full min-h-0 flex-col"><form data-browser-form class="micro-browser-toolbar flex items-center gap-1.5 border-b border-border/70 p-2"><button type="button" data-browser-action="back" class="micro-browser-control" title="Back">${icon('oc-arrow-left', 'remixicon h-3.5 w-3.5')}</button><button type="button" data-browser-action="forward" class="micro-browser-control" title="Forward">${icon('oc-arrow-right', 'remixicon h-3.5 w-3.5')}</button><button type="button" data-browser-action="reload" class="micro-browser-control" title="Reload">${icon('oc-refresh', 'remixicon h-3.5 w-3.5')}</button><button type="button" data-browser-action="home" class="micro-browser-control" title="Home">${icon('oc-home', 'remixicon h-3.5 w-3.5')}</button><div class="micro-browser-address flex min-w-0 flex-1 items-center gap-1.5 rounded-md border px-2">${icon('oc-global', 'remixicon h-3 w-3')}<input data-browser-url value="${esc(state.browserUrl || BROWSER_HOME)}" class="min-w-0 flex-1 bg-transparent py-1.5 text-xs outline-none" placeholder="URL ou pesquisa"></div><button type="submit" class="micro-browser-go rounded-md px-2.5 py-1.5 text-xs font-medium">Go</button></form><div data-native-browser-host class="min-h-0 flex-1 bg-[#090909]"></div></div>`;
  };

  /* ---------- theme ---------- */
  const THEME_VARIANTS = {
    'openchamber-light': ['OpenChamber Mono', 'light'], 'openchamber-dark': ['OpenChamber Mono', 'dark'],
    'flexoki-light': ['Flexoki', 'light'], 'flexoki-dark': ['Flexoki', 'dark'],
    'aura-light': ['Aura', 'light'], 'aura-dark': ['Aura', 'dark'],
    'ayu-light': ['Ayu', 'light'], 'ayu-dark': ['Ayu', 'dark'],
    'carbonfox-light': ['Carbonfox', 'light'], 'carbonfox-dark': ['Carbonfox', 'dark'],
    'catppuccin-light': ['Catppuccin', 'light'], 'catppuccin-dark': ['Catppuccin', 'dark'],
    'dracula-light': ['Dracula', 'light'], 'dracula-dark': ['Dracula', 'dark'],
    'gruvbox-light': ['Gruvbox', 'light'], 'gruvbox-dark': ['Gruvbox', 'dark'],
    'jetbrains-light': ['JetBrains', 'light'], 'jetbrains-dark': ['JetBrains', 'dark'],
    'kanagawa-light': ['Kanagawa', 'light'], 'kanagawa-dark': ['Kanagawa', 'dark'],
    'monokai-light': ['Monokai', 'light'], 'monokai-dark': ['Monokai', 'dark'],
    'nightowl-light': ['Night Owl', 'light'], 'nightowl-dark': ['Night Owl', 'dark'],
    'nord-light': ['Nord', 'light'], 'nord-dark': ['Nord', 'dark'],
    'fields-of-the-shire-light': ['Fields of the Shire', 'light'], 'fields-of-the-shire-dark': ['Fields of the Shire', 'dark'],
    'onedarkpro-light': ['One Dark Pro', 'light'], 'onedarkpro-dark': ['One Dark Pro', 'dark'],
    'solarized-light': ['Solarized', 'light'], 'solarized-dark': ['Solarized', 'dark'],
    'tokyonight-light': ['Tokyonight', 'light'], 'tokyonight-dark': ['Tokyonight', 'dark'],
  };
  const themeList = (variant) => Object.entries(THEME_VARIANTS).filter(([, v]) => v[1] === variant).map(([id, v]) => ({ id, name: v[0] }));

  const applyTheme = () => {
    if (!localStorage.getItem('oc-clone-color-mode')) { state.colorMode = 'dark'; LS.set('oc-clone-color-mode', 'dark'); }
    const root = document.documentElement;
    const isDark = state.colorMode === 'dark' || (state.colorMode === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);
    const active = isDark ? state.darkTheme : state.lightTheme;
    root.classList.toggle('dark', isDark);
    root.classList.toggle('light', !isDark);
    root.style.colorScheme = isDark ? 'dark' : 'light';
    // remove previous theme classes
    root.classList.forEach((c) => { if (c.startsWith('theme-')) root.classList.remove(c); });
    if (OC_THEMES[active]) root.classList.add('theme-' + active);
    const bg = isDark ? '#0b0b0b' : '#ffffff';
    root.style.setProperty('--background', bg);
    root.style.backgroundColor = bg;
    document.body.style.backgroundColor = bg;
  };

  /* ---------- markdown ---------- */
  const CODE_KEYWORDS = new Set(['for', 'let', 'const', 'var', 'if', 'else', 'while', 'return', 'function', 'class', 'import', 'from', 'export', 'new', 'await', 'async', 'try', 'catch', 'throw', 'switch', 'case', 'break', 'continue', 'typeof', 'in', 'of', 'do', 'null', 'true', 'false', 'undefined', 'this', 'default']);
  const mdHighlight = (line, lang) => {
    if (lang !== 'js' && lang !== 'javascript' && lang !== 'bash' && lang !== 'shell' && lang !== 'text' && lang !== '') return esc(line);
    const re = /(\/\/.*$)|("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')|(\b\d+(?:\.\d+)?\b)|(\b[A-Za-z_$][\w$]*\b)|([^\w\s])/gm;
    let out = '', m;
    const clsOf = (tok) => {
      if (!tok) return null;
      if (tok.startsWith('//')) return 'comment';
      if (/^["']/.test(tok)) return 'string';
      if (/^\d/.test(tok)) return 'number';
      if (CODE_KEYWORDS.has(tok)) return 'keyword';
      if (/[+\-*/%=<>!&|?;,:{}()[\]`]/.test(tok)) return 'operator';
      return 'variable';
    };
    let last = 0;
    while ((m = re.exec(line))) {
      const tok = m[0], idx = m.index;
      if (idx > last) out += esc(line.slice(last, idx));
      const cls = clsOf(tok);
      out += cls ? `<span style="color:var(--md-syntax-${cls})">${esc(tok)}</span>` : esc(tok);
      last = idx + tok.length;
    }
    out += esc(line.slice(last));
    return out;
  };

  const renderInline = (text) => {
    let t = esc(text);
    t = t.replace(/`([^`]+)`/g, '<code data-component="markdown-code-inline" class="rounded border border-border/60 bg-[var(--surface-elevated)] px-1 py-0.5 font-mono text-[0.875em]">$1</code>');
    t = t.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    t = t.replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>');
    t = t.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noreferrer" class="text-primary underline underline-offset-2 hover:opacity-80">$1</a>');
    return t;
  };

  const renderMarkdown = (text) => {
    const blocks = text.split(/\n```(\w*)\n?([\s\S]*?)```/g);
    let out = '', html = '';
    for (let i = 0; i < blocks.length; i += 3) {
      const body = blocks[i];
      if (body) {
        for (const line of body.trim().split('\n')) {
          const t = line.trim();
          if (!t) continue;
          if (/^#{1,6}\s/.test(t)) {
            const lvl = t.match(/^#{1,6}/)[0].length;
            html += `<h${Math.min(lvl, 4)}>${renderInline(t.replace(/^#{1,6}\s*/, ''))}</h${Math.min(lvl, 4)}>`;
          } else if (/^[-*]\s/.test(t)) {
            html += `<ul><li>${renderInline(t.replace(/^[-*]\s*/, ''))}</li></ul>`;
          } else if (/^\d+\.\s/.test(t)) {
            html += `<ol><li>${renderInline(t.replace(/^\d+\.\s*/, ''))}</li></ol>`;
          } else {
            html += `<p>${renderInline(line)}</p>`;
          }
        }
      }
      if (i + 2 < blocks.length) {
        const lang = blocks[i + 1], code = blocks[i + 2];
        const lines = code.replace(/\n$/, '').split('\n');
        const lineHtml = lines.map((l, li) => {
          const nums = `<span data-md-code-line-number="" aria-hidden="true">${li + 1}</span>`;
          const content = `<span data-md-code-line-content="" style="white-space: pre-wrap; overflow-wrap: anywhere;">${mdHighlight(l, lang)}</span>`;
          return `<span data-md-code-line="">${nums}${content}</span>`;
        }).join('<span data-md-code-line-break="">\n</span>');
        html += `<div data-component="markdown-code" class="my-4 group overflow-hidden rounded-2xl border border-border/80 bg-[var(--surface-elevated)]" data-code-wrap="true">
          <div class="flex items-center justify-between border-b border-border/70 px-3 py-1.5">
            <span class="font-mono text-[13px] text-muted-foreground">${esc(lang || 'text')}</span>
            <div class="flex items-center gap-1" data-md-code-actions="">
              <button type="button" class="p-1 rounded hover:text-foreground transition-colors text-foreground opacity-100" data-md-action="toggle-code-wrap" title="Disable line wrap" aria-label="Disable line wrap" aria-pressed="true">${icon('oc-text-wrap', 'remixicon size-3.5')}</button>
              <button type="button" class="p-1 rounded text-muted-foreground hover:text-foreground transition-colors" data-md-action="copy-code" title="Copy code" aria-label="Copy code">${icon('oc-file-copy', 'remixicon size-3.5')}</button>
            </div>
          </div>
          <div data-md-code-body="" class="px-3 py-2.5 overflow-x-hidden">
            <pre class="shiki openchamber-md min-w-0 w-full flex-1 whitespace-pre-wrap break-words" style="background: transparent; color: var(--md-syntax-foreground); margin: 0px; white-space: pre-wrap; overflow-wrap: anywhere;" data-md-lang="${esc(lang)}"><code class="whitespace-pre-wrap break-words" data-md-code-lines="" data-md-code-trailing-newline="" style="white-space: pre-wrap; overflow-wrap: anywhere;">${lineHtml}</code></pre>
          </div>
        </div>`;
      }
    }
    return `<div data-md-block="" style="display: contents;" data-md-id="${uid()}:full:1">${html}</div>`;
  };

  /* ---------- tools ---------- */
  const tplToolRow = (tool, toolKey, phase) => {
    const expanded = !!state.expandedTools[toolKey];
    const typing = phase === 'typing-cmd';
    const shownCmd = typing ? tool.cmd.slice(0, Math.max(6, Math.round(tool.cmd.length * (tool.progress || 0)))) : tool.cmd;
    const iconNow = expanded ? 'oc-arrow-down-s' : (typing ? 'oc-terminal-box' : 'oc-terminal-box');
    return `
    <div class="group/tool flex gap-1.5 pr-2 pl-px py-1.5 rounded-xl items-center cursor-pointer" role="button" tabindex="0" data-tool-key="${toolKey}">
      <div class="flex gap-1.5 items-center flex-shrink-0">
        <div class="relative h-5 w-3.5 flex-shrink-0 cursor-pointer">
          <div class="absolute inset-0 flex items-center justify-center transition-opacity ${expanded || !typing ? 'opacity-0' : ''}" style="color: var(--tools-icon);">${icon('oc-terminal-box', 'remixicon h-3.5 w-3.5 flex-shrink-0')}</div>
          <div class="absolute inset-0 transition-opacity flex items-center justify-center ${expanded ? 'opacity-100' : 'opacity-0 group-hover/tool:opacity-100'}">${icon(iconNow, 'remixicon h-3.5 w-3.5')}</div>
        </div>
        <div class="flex items-center gap-2 min-w-0 flex-1"><span class="transition-opacity duration-200 typography-meta font-medium !text-[length:var(--text-meta)] !leading-5 sm:!leading-6 tracking-normal flex-shrink-0" title="${esc(tool.title)}" style="color: var(--tools-title);">${esc(tool.title)}</span></div>
        ${tool.duration ? `<span class="flex-shrink-0 tabular-nums text-muted-foreground/80 typography-meta !text-[length:var(--text-meta)] !leading-5 sm:!leading-6 tracking-normal">${esc(tool.duration)}</span>` : ''}
      </div>
      <div class="flex items-center gap-1 flex-1 min-w-0 typography-meta !text-[length:var(--text-meta)] !leading-5 sm:!leading-6 tracking-normal" style="color: var(--tools-description);">
        <div class="flex items-center gap-1 flex-1 min-w-0"><span title="${esc(tool.cmd)}" class="inline-block whitespace-pre align-baseline min-w-0 truncate" style="color: var(--tools-description);">${esc(shownCmd)}${typing ? '<span class="tool-caret"></span>' : ''}</span></div>
      </div>
    </div>
    ${expanded ? `
    <div aria-hidden="false" style="height: auto; overflow: visible; overflow-anchor: none;">
      <div class="relative ml-2 pl-3">
        <span aria-hidden="true" class="pointer-events-none absolute left-0 top-px bottom-0 w-px" style="background-color: var(--tools-border);"></span>
        <div class="relative pr-2 pb-2 pt-2 space-y-2 pl-4">
          <div class="my-1">
            <div class="w-full min-w-0 flex-none overflow-hidden">
              <div data-scrollable="true" class="tool-output-surface w-full min-w-0 max-h-60 overflow-auto tool-input-surface p-0 rounded-none" data-orientation="vertical" data-scroll-shadow="true" style="--scroll-shadow-size: 48px;">
                <div class="w-full min-w-0"><pre class="tool-input-text whitespace-pre-wrap break-words">${tool.output.map((l) => mdHighlight(l, 'text')).join('\n')}</pre></div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>` : ''}`;
  };

  const tplThinkingBlock = (m) => {
    const key = 'think_' + m.id;
    const expanded = !!state.expandedTools[key];
    return `
    <div class="group/tool flex gap-1.5 pr-2 pl-px py-1.5 rounded-xl items-center cursor-pointer" role="button" tabindex="0" data-tool-key="${key}">
      <div class="flex gap-1.5 items-center flex-shrink-0">
        <div class="relative h-5 w-3.5 flex-shrink-0 cursor-pointer">
          <div class="absolute inset-0 flex items-center justify-center transition-opacity ${expanded ? 'opacity-0' : ''}" style="color: var(--tools-icon);">${icon('oc-brain-ai-3', 'remixicon h-3.5 w-3.5 flex-shrink-0')}</div>
          <div class="absolute inset-0 transition-opacity flex items-center justify-center ${expanded ? 'opacity-100' : 'opacity-0 group-hover/tool:opacity-100'}">${icon('oc-arrow-down-s', 'remixicon h-3.5 w-3.5')}</div>
        </div>
        <div class="flex items-center gap-2 min-w-0 flex-1"><span class="transition-opacity duration-200 typography-meta font-medium !text-[length:var(--text-meta)] !leading-5 sm:!leading-6 tracking-normal flex-shrink-0" style="color: var(--tools-title);">Thought process</span></div>
      </div>
      <div class="flex items-center gap-1 flex-1 min-w-0 typography-meta !text-[length:var(--text-meta)] !leading-5 sm:!leading-6 tracking-normal" style="color: var(--tools-description);">
        <div class="flex items-center gap-1 flex-1 min-w-0"><span class="inline-block whitespace-pre align-baseline min-w-0 truncate">${esc(m.thinking)}</span></div>
      </div>
    </div>
    ${expanded ? `
    <div aria-hidden="false" style="height: auto; overflow: visible; overflow-anchor: none;">
      <div class="relative ml-2 pl-3">
        <span aria-hidden="true" class="pointer-events-none absolute left-0 top-px bottom-0 w-px" style="background-color: var(--tools-border);"></span>
        <div class="relative pr-2 pb-2 pt-2 space-y-2 pl-4">
          <div class="relative flex flex-col gap-3 rounded-lg border border-border/60 bg-[var(--surface-elevated)] p-3">
            <div class="text-sm leading-relaxed text-foreground/85" style="white-space: pre-wrap;">${esc(m.thinking)}</div>
          </div>
        </div>
      </div>
    </div>` : ''}`;
  };

  /* ---------- templates: shell ---------- */
  const tplTopbar = () => `
    <div class="app-region-no-drag absolute left-0 top-0 z-30 flex select-none items-center pr-2" style="height: var(--oc-header-height, 3rem); padding-left: var(--oc-titlebar-left-inset, 0.75rem);">
      <div class="flex items-center gap-2">
        <button type="button" aria-label="Open sessions" data-action="toggle-sidebar" data-topbar-toggle class="app-region-no-drag inline-flex h-8 w-8 items-center justify-center gap-2 rounded-md typography-ui-label font-medium text-foreground transition-colors shrink-0">${icon('oc-layout-left')}</button>
      </div>
    </div>`;

  const SIDEBAR_DESTINATIONS = [
    ['memory', 'Memory', 'oc-brain-ai-3', 'Agent knowledge graph'],
  ];

  const tplSidebarDestination = ([key, label, iconName, description]) => `
    <button type="button" data-workspace="${key}" class="zeno-sidebar-nav-item ${state.workspace === key ? 'is-active' : ''}" aria-current="${state.workspace === key ? 'page' : 'false'}">
      <span class="zeno-sidebar-nav-icon">${icon(iconName, 'remixicon h-4 w-4')}</span>
      <span class="min-w-0 flex-1 truncate">${label}</span>
      <span class="zeno-sidebar-nav-description">${description}</span>
    </button>`;

  const tplSidebarHeader = () => `
    <div class="zeno-sidebar-header select-none flex-shrink-0">
      <button type="button" data-action="new-session" class="zeno-new-chat-button ${state.workspace === 'chat' && state.draftNew ? 'is-active' : ''}">
        <span class="zeno-sidebar-nav-icon">${icon('oc-chat-new', 'remixicon h-4 w-4')}</span>
        <span class="truncate">New chat</span>
        <kbd>Ctrl N</kbd>
      </button>
      <nav class="zeno-sidebar-nav" aria-label="Workspace navigation">
        ${SIDEBAR_DESTINATIONS.map(tplSidebarDestination).join('')}
      </nav>
      <div class="zeno-project-tools">
        <span>My Projects</span>
        <div class="flex items-center gap-0.5">
          <button type="button" data-action="archive-popup" class="zeno-sidebar-tool" aria-label="Archived sessions" title="Archived sessions">${icon('oc-archive', 'remixicon h-3.5 w-3.5')}</button>
          <button type="button" data-action="search-sessions" class="zeno-sidebar-tool" aria-label="Search sessions" title="Search sessions">${icon('oc-search', 'remixicon h-3.5 w-3.5')}</button>
          <button type="button" data-action="add-project" class="zeno-sidebar-tool zeno-sidebar-tool-primary" aria-label="Create project" title="Create project">${icon('oc-add', 'remixicon h-3.5 w-3.5')}</button>
        </div>
      </div>
    </div>`;

  const tplSessionRow = (s, indent = 29) => {
    const active = s.id === state.activeId;
    return `
      <div role="button" tabindex="0" data-session-id="${s.id}">
        <div data-session-row="${s.id}" class="zeno-session-row group relative my-0.5 flex cursor-pointer items-center rounded-md py-1 pr-1.5 ${active ? 'bg-primary/10' : ''}" style="padding-left: ${indent}px;">
          <div class="flex min-w-0 flex-1 items-center">
            <button type="button" data-action="open-session" class="flex min-w-0 flex-1 cursor-pointer flex-col gap-0 overflow-hidden text-left focus-visible:outline-none text-foreground select-none">
              <div class="flex w-full items-center min-w-0 flex-1 gap-1 overflow-hidden">
                <div class="block min-w-0 flex-1 truncate typography-ui-label font-normal ${active ? 'text-primary' : 'text-foreground/80'}">${esc(s.title)}</div>
              </div>
            </button>
          </div>
          <span class="zeno-chat-time">${esc(relTime(s))}</span>
          <button type="button" data-session-menu="${s.id}" class="zeno-chat-menu" aria-label="Chat options" title="Options">${icon('oc-more', 'remixicon h-4 w-4')}</button>
        </div>
      </div>`;
  };

  const tplProjectSidebarRow = (project) => {
    const chats = projectSessions(project.id);
    const active = state.activeProjectId === project.id;
    return `<div class="zeno-project-group ${active ? 'is-active' : ''}">
      <div class="zeno-project-row">
        <button type="button" data-project-open="${project.id}" class="zeno-project-open" aria-label="Edit project ${esc(project.name)}" title="Edit project">
          <span class="zeno-sidebar-nav-icon">${icon('oc-folder', 'remixicon h-4 w-4')}</span>
          <span class="min-w-0 flex-1 truncate">${esc(project.name)}</span>
        </button>
        <button type="button" data-project-menu="${project.id}" class="zeno-sidebar-tool zeno-project-add" aria-label="Project options" title="Options">${icon('oc-more', 'remixicon h-3.5 w-3.5')}</button>
      </div>
      ${chats.length ? `<div class="zeno-project-chats">${chats.map((chat) => tplSessionRow(chat, 34)).join('')}</div>` : '<div class="zeno-project-empty">No chats yet</div>'}
    </div>`;
  };

  const tplSidebar = () => `
    <aside class="relative flex h-full overflow-hidden border-r will-change-[width] motion-reduce:transition-none bg-sidebar oc-vibrancy-surface shadow-[inset_-2px_0_10px_-2px_rgb(0_0_0_/_0.06)] border-border" aria-hidden="${!state.sidebarOpen}" style="width: ${state.sidebarOpen ? state.sidebarW : 0}px; min-width: ${state.sidebarOpen ? state.sidebarW : 0}px; max-width: ${state.sidebarOpen ? state.sidebarW : 0}px; --oc-left-sidebar-width: ${state.sidebarOpen ? state.sidebarW : 0}px; overflow-x: clip; transition-property: width, min-width, max-width; transition-duration: 200ms; transition-timing-function: cubic-bezier(0.22, 1, 0.36, 1);">
      <div class="absolute right-0 top-0 z-20 h-full w-[3px] cursor-col-resize hover:bg-[var(--interactive-border)]/80 transition-colors" role="separator" aria-orientation="vertical" aria-label="Resize left panel" data-action="resize-sidebar"></div>
      <div data-sidebar-content class="relative z-10 flex h-full shrink-0 flex-col motion-reduce:transition-none" aria-hidden="false" style="width: ${state.sidebarOpen ? state.sidebarW : 0}px; overflow-x: hidden; transition-property: width, min-width, max-width, opacity; transition-duration: 200ms; transition-timing-function: cubic-bezier(0.22, 1, 0.36, 1);">
        <div aria-hidden="true" class="flex shrink-0" style="height: var(--oc-header-height, 3rem);"></div>
        <div class="min-h-0 flex-1 overflow-y-auto">
          <div class="relative flex h-full flex-col text-foreground overflow-x-hidden bg-transparent">
            ${tplSidebarHeader()}
            <div class="oc-sticky-fade-root relative flex min-h-0 flex-1">
              <div class="relative flex flex-col w-full overflow-hidden flex-1 min-h-0">
                <div class="overlay-scrollbar-target overlay-scrollbar-container flex-1 min-h-0 w-full overflow-auto oc-sidebar-scroller space-y-1.5 pb-1 pl-2.5 pr-2 [overflow-anchor:none]" data-orientation="vertical" data-scroll-shadow="true" style="--scroll-shadow-size: 96px;">
                  <div class="space-y-2 pb-2">
                    ${state.projects.length ? `<div class="zeno-sidebar-section"><div class="space-y-1">${state.projects.map(tplProjectSidebarRow).join('')}</div></div>` : `<div class="zeno-sidebar-empty-projects"><span class="zeno-sidebar-empty-icon">${icon('oc-folder', 'remixicon h-4 w-4')}</span><span><strong>No projects yet</strong><small>Create one to group chats and folders.</small><button type="button" data-action="add-project" class="zeno-empty-project-cta">Create project ${icon('oc-arrow-right', 'remixicon h-3 w-3')}</button></span></div>`}
                    <div class="relative space-y-1">
                      <div class="-ml-2.5 -mr-2 sticky top-0 z-20 bg-sidebar" data-sidebar-sticky-header="true">
                        <button type="button" data-chats-toggle class="group flex w-full items-center gap-1.5 py-1 pl-4 pr-3.5 text-left focus-visible:outline-none" aria-expanded="${!state.chatsCollapsed}">
                          <span class="inline-flex h-3.5 w-3.5 items-center justify-center transition-transform duration-150 ${state.chatsCollapsed ? '-rotate-90' : ''}">${icon('oc-arrow-down-s', 'remixicon h-3.5 w-3.5')}</span>
                          <span class="text-[14px] font-semibold text-foreground">Chats</span>
                        </button>
                      </div>
                      <div class="space-y-0.5" data-chats-list${state.chatsCollapsed ? ' hidden' : ''}>
                        ${state.sessions.map((s) => tplSessionRow(s)).join('')}
                        <button type="button" class="mt-0.5 flex items-center justify-start rounded-md pl-[26px] pr-1.5 py-0.5 text-left text-xs text-muted-foreground/70 leading-tight hover:text-foreground hover:underline">Show more sessions</button>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            <div class="zeno-sidebar-footer">
              <button type="button" data-action="open-settings" class="zeno-sidebar-footer-button" aria-label="Settings" title="Settings">${icon('oc-settings-3', 'remixicon h-4 w-4')}<span>Settings</span></button>
            </div>
          </div>
        </div>
      </div>
    </aside>`;

  const tplNav = () => {
    const s = activeSession();
    if (state.workspace === 'chat' && (!s || !s.messages.length)) return '';
    return `
    <nav aria-label="Panel surfaces" class="flex h-full w-11 flex-shrink-0 flex-col items-center gap-1 bg-background py-2">
      ${Object.entries(PANELS).map(([label, [ic]]) => {
        const on = state.panel === label;
        const extra = ' panel-' + label.toLowerCase().replace(/[^a-z0-9]+/g, '-');
        return `
      <div class="relative"><button type="button" role="button" tabindex="0" data-panel-btn="${label}" aria-label="${label}" title="${label}" aria-pressed="${on}" class="flex h-9 w-9 touch-none select-none items-center justify-center rounded-md transition-colors ${on ? 'text-foreground bg-interactive-hover' : 'text-muted-foreground hover:text-foreground'}${extra}">${icon(ic, 'remixicon h-[18px] w-[18px]')}</button></div>`;
      }).join('')}
    </nav>`;
  };

  /* ---------- templates: composer ---------- */
  const getQuickActions = () => {
    const actions = state.quickActions;
    return Array.isArray(actions) && actions.every((item) => Array.isArray(item) && item.length >= 2)
      ? actions
      : CHIPS;
  };

  const tplComposer = (big, session) => {
    const suggestion = session && session.messages.length ? session.messages[session.messages.length - 1].suggestion : null;
    const project = activeProject();
    const chatInput = () => `
      <div data-composer-shell="true" class="flex flex-col relative overflow-visible border border-border/80 shadow-[0_4px_16px_-4px_rgb(0_0_0_/_0.12)] focus-within:ring-1 focus-within:ring-primary/50" style="border-radius: var(--radius-xl); background-color: var(--surface-elevated);">
        <div class="relative flex flex-col">
          <div class="overflow-hidden">
            <div class="flex items-center gap-1 px-3 pt-1 flex-wrap relative z-10"></div>
            <div class="relative overflow-hidden">
              <div data-testid="chat-input" data-chat-input="true" class="composer-editor w-full [&_.cm-editor]:h-full min-h-[52px] px-3 relative z-10 pt-4 pb-2 typography-markdown md:typography-ui-label">
                <div class="cm-editor ͼ1 ͼ2 ͼ4 ͼp ͼo">
                  <div class="cm-scroller" style="max-height: 180px;">
                    <div spellcheck="false" autocorrect="off" autocapitalize="none" writingsuggestions="false" translate="no" contenteditable="true" style="tab-size: 4;" class="cm-content cm-lineWrapping" role="textbox" aria-multiline="true" aria-placeholder="Digite uma instrução para o Zeno Agent" data-composer-input><div class="cm-line"><img class="cm-widgetBuffer" aria-hidden="true"><span class="cm-placeholder" data-animated-placeholder aria-hidden="true" contenteditable="false" style="pointer-events: none;"></span><br></div></div>
                  </div>
                </div>
              </div>
            </div>
          </div>
          <div class="bg-transparent flex-shrink-0 px-2.5 py-1.5 flex items-center justify-between gap-1" data-chat-input-footer="true" style="border-bottom-left-radius: var(--radius-xl); border-bottom-right-radius: var(--radius-xl);">
            <div class="flex items-center gap-1">
              <button type="button" data-action="attach" class="zeno-icon-btn" title="Adicionar anexo" aria-label="Adicionar anexo">${icon('oc-add-circle', 'remixicon h-[18px] w-[18px]')}</button>
            </div>
            <div class="flex items-center gap-1">
              <span data-dictation-label aria-live="polite"></span>
              <button type="button" data-action="voice-mode" class="zeno-icon-btn" title="Conversar por voz" aria-label="Conversar por voz">${icon('oc-pulse', 'remixicon h-[18px] w-[18px]')}</button>
              <button type="button" data-action="dictation" data-dictation-phase="idle" class="zeno-icon-btn" title="Ditar mensagem" aria-label="Ditar mensagem">${icon('oc-mic', 'remixicon h-[18px] w-[18px]')}</button>
              <button type="submit" data-action="send" class="zeno-icon-btn" aria-label="Send message">${icon('oc-send-plane-2', 'remixicon h-[18px] w-[18px]')}</button>
            </div>
          </div>
        </div>
      </div>`;

    if (big) {
      return `
        <form class="relative w-full pt-0 pb-4" data-composer-form>
          <div class="chat-input-column mb-7 text-center">
            <h1 class="text-balance text-2xl font-normal tracking-tight text-foreground md:text-3xl">What are we working on in <span class="font-medium">${project ? esc(project.name) : 'Zeno Agent'}</span>?</h1>
          </div>
          <div class="chat-input-column relative overflow-visible">
            <div class="contents">${chatInput()}
              <div class="flex flex-wrap items-center justify-center gap-1.5 chat-input-column mt-6" data-quick-actions-list>
                ${getQuickActions().map(([i, t], index) => `
                <div class="group/chip relative" draggable="true" data-quick-action data-quick-index="${index}" title="Arraste para mover · botão direito para editar">
                  <button type="button" role="button" tabindex="0" class="group inline-flex touch-none select-none items-center gap-1 rounded-full border px-2 py-1 text-[10px] leading-4 text-muted-foreground transition-colors hover:text-foreground" data-chip="${esc(t)}">${icon(i, 'remixicon h-3 w-3')}<span class="whitespace-nowrap">${esc(t)}</span></button>
                </div>`).join('')}
              </div>
            </div>
          </div>
        </form>`;
    }
    return `
      <form class="relative w-full pt-0 pb-4" data-composer-form>
        <div class="chat-input-column relative overflow-visible">
          ${suggestion ? `
          <div class="flex w-full min-w-0 justify-center mb-1.5">
            <div class="relative w-full min-w-0 max-w-full">
              <button type="button" aria-label="Use suggested message" data-action="use-suggestion" class="group flex w-full min-w-0 select-none items-center gap-1.5 rounded-full border py-1.5 pl-3 pr-8 text-sm text-muted-foreground transition-colors hover:text-foreground" style="background-color: var(--surface-elevated); border-color: var(--interactive-border);">${icon('oc-search-eye', 'remixicon h-4 w-4')}<span class="truncate">${esc(suggestion)}</span></button>
              <button type="button" aria-label="Dismiss suggestion" title="Dismiss suggestion" data-action="dismiss-suggestion" class="absolute right-1.5 top-1/2 flex h-5 w-5 -translate-y-1/2 items-center justify-center rounded-full text-muted-foreground/60 transition-colors hover:text-foreground">${icon('oc-close', 'remixicon h-3 w-3')}</button>
            </div>
          </div>` : ''}
          <div class="contents">${chatInput()}</div>
        </div>
      </form>`;
  };

  const tplUserMsg = (m) => `
    <div class="group w-full pt-4 pb-0" data-message-id="${m.id || uid()}" data-message-role="user">
      <div class="chat-message-column relative">
        <div class="relative flex justify-end group/user-shell">
          <div class="max-w-[85%]">
            <div class="px-5 py-3 shadow-none border border-primary/5" style="background-color: var(--chat-user-message-bg); border-bottom-right-radius: var(--radius-sm);">
              <div class="relative w-full group/message" style="contain: layout; transform: translateZ(0px);">
                <div class="text-foreground/90 text-base overflow-x-hidden overflow-y-hidden">
                  <div class="relative">
                    <div class="break-words font-sans typography-markdown-body">
                      <div class="break-words w-full min-w-0 [&_.markdown-content>*:first-child]:mt-0 [&_.markdown-content>*:last-child]:mb-0 [&_.markdown-content>*]:my-0">
                        <div class="markdown-content leading-relaxed" data-markdown-content="true">
                          <div data-md-block="" style="display: contents;"><p>${renderInline(m.text)}</p></div>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            <div class="group/user-actions flex h-8 items-start justify-end pt-2">
              <div class="flex items-center justify-end gap-1 translate-x-0 pointer-events-none opacity-0 transition-opacity duration-150 group-hover/message:pointer-events-auto group-hover/message:opacity-100 group-hover/user-actions:pointer-events-auto group-hover/user-actions:opacity-100 group-hover/user-shell:pointer-events-auto group-hover/user-shell:opacity-100">
                <span class="mr-1 flex items-center gap-1 text-sm tabular-nums text-muted-foreground/60" aria-label="Message time: ${m.time || fmtTime(now())}">${icon('oc-time', 'remixicon h-3.5 w-3.5')}<span class="message-footer__label">${m.time || fmtTime(now())}</span></span>
                ${[['oc-arrow-go-back', 'Revert to this message'], ['oc-git-branch', 'Fork from this message'], ['oc-pushpin-2', 'Pin into context (survives compaction)'], ['oc-file-copy', 'Copy message text']].map(([i, l]) => `
                <button data-slot="tooltip-trigger" class="group relative inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-[10px] [corner-shape:squircle] supports-[corner-shape:squircle]:rounded-[50px] typography-ui-label font-medium lowercase tracking-[0.01em] shrink-0 select-none transition-[background-color,border-color,color,opacity] duration-150 ease-out outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 size-9 h-6 w-6 text-muted-foreground bg-transparent hover:text-foreground" type="button" aria-label="${l}">${icon(i, 'remixicon h-3 w-3')}</button>`).join('')}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>`;

  const tplAssistantMsg = (m) => {
    let body = '';
    if (m.thinking) body += tplThinkingBlock(m);
    if (m.tools) body += m.tools.map((t, i) => tplToolRow(t, m.id + '_t' + i, 'done')).join('');
    if (m.text) body += renderMarkdown(m.text);
    if (m.code) {
      const lines = m.code.lines.map(([t]) => t).join('');
      body += `<div data-component="markdown-code" class="my-4 group overflow-hidden rounded-2xl border border-border/80 bg-[var(--surface-elevated)]" data-code-wrap="true">
        <div class="flex items-center justify-between border-b border-border/70 px-3 py-1.5">
          <span class="font-mono text-[13px] text-muted-foreground">${esc(m.code.lang)}</span>
          <div class="flex items-center gap-1" data-md-code-actions="">
            <button type="button" class="p-1 rounded hover:text-foreground transition-colors text-foreground opacity-100" data-md-action="toggle-code-wrap" title="Disable line wrap" aria-label="Disable line wrap" aria-pressed="true">${icon('oc-text-wrap', 'remixicon size-3.5')}</button>
            <button type="button" class="p-1 rounded text-muted-foreground hover:text-foreground transition-colors" data-md-action="copy-code" title="Copy code" aria-label="Copy code">${icon('oc-file-copy', 'remixicon size-3.5')}</button>
          </div>
        </div>
        <div data-md-code-body="" class="px-3 py-2.5 overflow-x-hidden">
          <pre class="shiki openchamber-md min-w-0 w-full flex-1 whitespace-pre-wrap break-words" style="background: transparent; color: var(--md-syntax-foreground); margin: 0px; white-space: pre-wrap; overflow-wrap: anywhere;" data-md-lang="${esc(m.code.lang)}"><code class="whitespace-pre-wrap break-words" data-md-code-lines="" data-md-code-trailing-newline="" style="white-space: pre-wrap; overflow-wrap: anywhere;"><span data-md-code-line=""><span data-md-code-line-number="" aria-hidden="true">1</span><span data-md-code-line-content="" style="white-space: pre-wrap; overflow-wrap: anywhere;">${mdHighlight(lines, m.code.lang)}</span></span><span data-md-code-line-break="">\n</span></code></pre>
        </div>
      </div>`;
    }
    return `
      <div class="group w-full pt-0 pb-2" data-message-id="${m.id || uid()}" data-message-role="assistant">
        <div class="chat-message-column relative">
          <div class="relative">
            <div data-message-text-export-root="true" class="relative w-full group/message" style="contain: layout; transform: translateZ(0px);">
              <div>
                <div class="message-content-text leading-relaxed overflow-hidden text-foreground/90 [&_p:last-child]:mb-0 [&_ul:last-child]:mb-0 [&_ol:last-child]:mb-0">
                  <div data-message-text-export-source="true">
                    <div class="group/assistant-text relative break-words my-1">
                      <div class="break-words w-full min-w-0">
                        <div class="markdown-content leading-relaxed" data-markdown-content="true">
                          ${body}
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            ${m.recap ? `<div class="chat-message-column" style="padding-inline:0"><div aria-label="Session recap"><span class="typography-meta text-muted-foreground/70 line-clamp-2"><span class="italic text-muted-foreground/50">Recap: </span>${esc(m.recap)}</span></div></div>` : ''}
            <div class="mt-2 mb-1 flex flex-wrap items-center justify-start gap-x-3 gap-y-1.5" style="container: message-footer / inline-size;">
              <div class="flex min-w-0 flex-wrap items-center gap-x-2.5 gap-y-1 text-sm text-muted-foreground/60">
                <span class="flex min-w-0 items-center gap-1.5"><img alt="" class="h-3.5 w-3.5 flex-shrink-0" src="https://models.dev/logos/opencode-go.svg" style="filter: brightness(0.9) contrast(1.1);"><span class="truncate">${esc(m.model)}</span></span>
                <span class="flex items-center gap-1">${icon('oc-ai-agent', 'remixicon h-3.5 w-3.5 flex-shrink-0')}<span class="message-footer__label">${esc(m.agent)}</span></span>
                <span class="text-sm text-muted-foreground/60 tabular-nums flex items-center gap-1">${icon('oc-hourglass', 'remixicon h-3.5 w-3.5')}<span class="message-footer__label">${esc(m.duration)}</span></span>
                <span class="text-sm text-muted-foreground/60 tabular-nums flex items-center gap-1" aria-label="Message time: ${m.time}">${icon('oc-time', 'remixicon h-3.5 w-3.5')}<span class="message-footer__label">${m.time}</span></span>
              </div>
              <div class="flex items-center gap-1.5 pointer-events-none opacity-0 transition-opacity duration-150 focus-within:pointer-events-auto focus-within:opacity-100 group-hover/message:pointer-events-auto group-hover/message:opacity-100" data-message-action-group="true">
                ${[['oc-file-copy', 'Copy message text'], ['oc-image-download', 'Download as image'], ['oc-booklet', 'Export markdown'], ['oc-pushpin-2', 'Pin into context'], ['oc-chat-new', 'Continue in new session']].map(([i, l]) => `
                <button data-slot="tooltip-trigger" class="group relative inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-[10px] [corner-shape:squircle] supports-[corner-shape:squircle]:rounded-[50px] typography-ui-label font-medium lowercase tracking-[0.01em] shrink-0 select-none transition-[background-color,border-color,color,opacity] duration-150 ease-out outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 size-9 h-8 w-8 text-muted-foreground bg-transparent hover:text-foreground" type="button" aria-label="${l}">${icon(i, 'remixicon h-3.5 w-3.5')}</button>`).join('')}
              </div>
            </div>
          </div>
        </div>
      </div>`;
  };

  const tplTyping = () => `
    <div class="group w-full pt-0 pb-2" data-message-role="typing">
      <div class="chat-message-column relative">
        <div class="relative">
          <div class="relative w-full group/message" style="contain: layout; transform: translateZ(0px);">
            <div class="flex items-center gap-2 py-1.5">
              <span class="oc-spinner" aria-hidden="true"></span>
              <img alt="" class="h-3.5 w-3.5 flex-shrink-0" src="https://models.dev/logos/opencode-go.svg" style="filter: brightness(0.9) contrast(1.1);">
              <span class="truncate text-sm text-muted-foreground">DeepSeek V4 Pro</span>
              <span class="flex items-center gap-1 text-sm text-muted-foreground/60">${icon('oc-ai-agent', 'remixicon h-3.5 w-3.5')}<span class="message-footer__label">build</span></span>
              <span class="text-sm text-muted-foreground/70 italic">thinking…</span>
            </div>
          </div>
        </div>
      </div>
    </div>`;

  const tplEmpty = () => `
    <div class="absolute inset-0 empty-chat-stage" data-composer-bound="true">
      <div class="empty-chat-surface">
        <div class="empty-chat-copy">${tplComposer(true)}</div>
      </div>
    </div>`;

  const memoryNoteById = (id) => state.memoryNotes.find((note) => note.id === id);

  const tplMemoryNode = (note) => {
    const x = note.x * 16, y = note.y * 9, size = note.root ? 26 : 18;
    return `<g data-memory-note="${esc(note.id)}" class="memory-node ${note.root ? 'memory-node-root' : ''}" transform="translate(${x} ${y})" tabindex="0" role="button" aria-label="Open ${esc(note.title)}">
      <circle cx="0" cy="0" r="${size / 2}" class="memory-node-shape"></circle>
      <text y="${size / 2 + 20}" text-anchor="middle" class="memory-node-label">${esc(note.title)}</text>
      <g class="memory-node-actions" transform="translate(-54 ${-size / 2 - 32})">
        <rect width="108" height="32" rx="7" class="memory-node-actions-bg"></rect>
        <g data-memory-action="edit" transform="translate(18 16)"><rect x="-12" y="-12" width="24" height="24" fill="transparent"></rect><use href="#oc-edit" x="-7" y="-7" width="14" height="14"></use><title>Edit</title></g>
        <g data-memory-action="view" transform="translate(54 16)"><rect x="-12" y="-12" width="24" height="24" fill="transparent"></rect><use href="#oc-eye" x="-7" y="-7" width="14" height="14"></use><title>View</title></g>
        <g data-memory-action="delete" transform="translate(90 16)"><rect x="-12" y="-12" width="24" height="24" fill="transparent"></rect><use href="#oc-delete-bin" x="-7" y="-7" width="14" height="14"></use><title>Delete</title></g>
      </g>
    </g>`;
  };

  const tplMemoryNoteModal = () => {
    const note = memoryNoteById(state.noteId);
    if (!note) return '';
    const editing = state.memoryEditor?.id === note.id;
    return `<div class="memory-note-layer" data-note-layer>
      <div class="memory-note-backdrop" data-note-backdrop></div>
      <article role="dialog" aria-modal="true" aria-label="${esc(note.title)}" class="memory-note-dialog oc-dialog">
        <header class="memory-note-header">
          <div class="min-w-0">
            <div class="memory-note-kicker"><span class="memory-node-dot" style="--memory-node-accent: ${note.accent}"></span>${esc(note.tag)} <span>/</span> Markdown note</div>
            ${editing ? `<input data-memory-title class="memory-editor-title" value="${esc(state.memoryEditor.title)}" aria-label="Note title">` : `<h2>${esc(note.title)}</h2>`}
          </div>
          <div class="memory-note-actions">
            ${editing ? `<button type="button" data-action="save-memory-note" class="memory-editor-save">Save</button><button type="button" data-action="discard-memory-note" class="memory-editor-discard">Discard</button>` : `<button type="button" data-action="edit-memory-note" class="memory-icon-button" title="Edit note" aria-label="Edit note">${icon('oc-edit', 'remixicon h-4 w-4')}</button>`}
            <button type="button" data-action="close-note" class="memory-icon-button" title="Close note" aria-label="Close note">${icon('oc-close', 'remixicon h-4 w-4')}</button>
          </div>
        </header>
        <div class="memory-note-scroll">
          <div class="memory-note-content">${editing ? `<textarea data-memory-content class="memory-editor-content" aria-label="Note content">${esc(state.memoryEditor.content)}</textarea>` : renderMarkdown(note.content)}</div>
          <footer class="memory-note-footer"><span>Captured by ${esc(note.by || 'Zeno Agent')}</span><span>${esc(note.updated)}</span></footer>
        </div>
      </article>
    </div>`;
  };

  const tplMemoryDeleteModal = () => {
    const note = memoryNoteById(state.memoryDeleteId);
    if (!note) return '';
    return `<div class="memory-confirm-layer">
      <div class="memory-confirm-backdrop" data-memory-confirm-cancel></div>
      <div role="dialog" aria-modal="true" class="memory-confirm-dialog">
        <h3>Delete note</h3>
        <p>“${esc(note.title)}” will be removed together with its links.</p>
        <div class="memory-confirm-actions">
          <button type="button" data-memory-confirm-cancel class="memory-confirm-btn memory-confirm-secondary">Cancel</button>
          <button type="button" data-memory-confirm-accept class="memory-confirm-btn memory-confirm-primary">Delete</button>
        </div>
      </div>
    </div>`;
  };

  const tplMemoryWorkspace = () => {
    const notes = Array.isArray(state.memoryNotes) ? state.memoryNotes : MEMORY_NOTES;
    const positions = new Map(notes.map((note) => [note.id, note]));
    const edges = state.memoryEdges.map(([from, to], index) => {
      const a = positions.get(from), b = positions.get(to);
      if (!a || !b) return '';
      const ax = a.x * 16, ay = a.y * 9, bx = b.x * 16, by = b.y * 9;
      return `<line data-memory-edge data-from="${esc(from)}" data-to="${esc(to)}" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}" class="memory-edge" style="--memory-edge-delay: ${index * 35}ms"></line>`;
    }).join('');
    return `<div class="memory-view" data-memory-view>
      <header class="memory-overlay-heading"><h1>Zeno Agent Memory</h1><p>Your knowledge, notes and skills connected in one living graph.</p></header>
      <div class="memory-map-viewport" data-memory-viewport>
        <svg class="memory-map-canvas" data-memory-canvas viewBox="0 0 1600 900" aria-label="Memory knowledge graph">
          <defs><pattern id="memory-grid" width="30" height="30" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="1" class="memory-grid-dot"></circle></pattern></defs>
          <rect width="1600" height="900" fill="url(#memory-grid)" class="memory-grid-surface" aria-hidden="true"></rect>
          <g data-memory-edges>${edges}</g>
          <g data-memory-nodes>${notes.map(tplMemoryNode).join('')}</g>
        </svg>
        <div class="memory-map-help">Drag to move · Scroll to zoom · Double-click a node to open</div>
      </div>
      ${tplMemoryNoteModal()}
      ${tplMemoryDeleteModal()}
    </div>`;
  };

  const tplWorkspace = () => {
    if (state.workspace === 'memory') return tplMemoryWorkspace();
    return '';
  };

  const tplChat = () => {
    if (state.workspace !== 'chat') return tplWorkspace();
    const s = activeSession();
    if (!s || !s.messages.length) return tplEmpty();
    return `
      <div class="absolute inset-0">
        <div class="relative flex flex-col h-full bg-background" data-composer-bound="true">
          <div class="relative min-h-0 flex-1" aria-hidden="false">
            <div class="absolute inset-0">
              <div tabindex="0" data-scrollbar="chat" class="absolute inset-0 overflow-y-auto overflow-x-hidden z-0 chat-scroll overlay-scrollbar-target" data-orientation="vertical" data-scroll-shadow="true" style="overflow-anchor: none; overscroll-behavior: contain; --scroll-shadow-size: 48px;" data-top-scroll="false" data-bottom-scroll="false">
                <div class="relative z-0 min-h-full">
                  <div>
                    <div class="relative w-full">
                      <div class="relative w-full"></div>
                      <div class="pt-4">
                        ${s.messages.map((m) => m.role === 'user' ? tplUserMsg(m) : tplAssistantMsg(m)).join('')}
                        ${state.typing ? tplTyping() : ''}
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
          <div class="relative z-10 flex-shrink-0 bg-background">
            ${tplComposer(false, s)}
          </div>
        </div>
      </div>`;
  };

  const PROJECT_FOLDERS = ['GUI Zeno', 'Downloads', 'Pictures', 'Documents', 'Projects', 'Desktop'];

  const tplMenuPopover = (items) => {
    const anchor = state.modalAnchor || { x: 12, y: 64, w: 24, h: 24 };
    const width = 190;
    const height = items.length * 38 + 14;
    let left = anchor.x + anchor.w + 8;
    let top = anchor.y - 6;
    if (left + width > window.innerWidth - 8) left = Math.max(8, anchor.x - width - 8);
    if (top + height > window.innerHeight - 8) top = Math.max(8, window.innerHeight - height - 8);
    if (top < 8) top = 8;
    return `<div class="fixed inset-0 z-[80]" data-action-modal><div role="menu" class="oc-menu" style="position:fixed;left:${left}px;top:${top}px;min-width:${width}px;">${items.map(([action, ic, label, danger]) => `<button type="button" role="menuitem" data-menu-action="${action}" class="oc-menu-item${danger ? ' oc-menu-item-danger' : ''}">${icon(ic, 'remixicon h-4 w-4')}<span>${label}</span></button>`).join('')}</div></div>`;
  };

  const tplActionModal = () => {
    if (!state.modal) return '';
    let title = '', body = '';
    if (state.modal === 'archive') {
      title = 'Archived sessions';
      body = state.archivedSessions.length
        ? `<div class="max-h-72 overflow-y-auto space-y-1">${state.archivedSessions.map((s) => `<div class="flex items-center gap-2 rounded-md px-2.5 py-2"><span class="min-w-0 flex-1 truncate text-sm text-foreground">${esc(s.title)}</span><button type="button" data-action="restore-session" data-session-id="${s.id}" class="rounded-md px-2 py-1 text-xs text-muted-foreground hover:text-foreground">Restore</button><button type="button" data-action="delete-archived" data-session-id="${s.id}" class="rounded-md p-1 text-muted-foreground hover:text-destructive" aria-label="Delete archived session">${icon('oc-delete-bin', 'remixicon h-3.5 w-3.5')}</button></div>`).join('')}</div>`
        : '<div class="py-8 text-center text-sm text-muted-foreground">No archived sessions.</div>';
    }
    if (state.modal === 'project' || state.modal === 'project-edit') {
      const editing = state.modal === 'project-edit' ? state.projects.find((item) => item.id === state.editProjectId) : null;
      if (!Array.isArray(state.draftFolders)) state.draftFolders = editing ? [...editing.folders] : [];
      return `<div class="fixed inset-0 z-[80] flex items-center justify-center bg-black/75 p-3 backdrop-blur-[3px] oc-backdrop" data-action-modal style="background-color: rgb(0 0 0 / 0.8)">
        <div role="dialog" aria-modal="true" aria-label="${editing ? 'Edit project' : 'Create project'}" class="oc-dialog project-dialog">
          <header class="project-dialog-header"><div><div class="workspace-eyebrow">Workspace / Projects</div><h2>${editing ? 'Edit project' : 'Create a project'}</h2><p>Defina um contexto durável para seus chats e pastas.</p></div><button type="button" data-action="close-modal" aria-label="Close dialog" class="memory-icon-button">${icon('oc-close', 'remixicon h-4 w-4')}</button></header>
          <form data-project-form class="project-dialog-form">
            <div class="project-form-main">
              <label class="project-field"><span>Project title</span><input required data-project-name class="project-title-input" value="${editing ? esc(editing.name) : ''}" placeholder="Ex.: Website da oficina" autocomplete="off"></label>
              <div class="project-folder-heading"><div><span>Attach folders</span><small>Escolha uma ou mais pastas do seu computador para dar ao projeto seu contexto.</small></div><span class="project-folder-count" data-project-folder-count>${state.draftFolders.length} selected</span></div>
              <div class="project-folder-list" data-project-folder-list>
                ${state.draftFolders.length ? state.draftFolders.map((folder, index) => `<span class="project-folder-chip"><span class="project-folder-icon">${icon('oc-folder', 'remixicon h-4 w-4')}</span><span class="project-folder-name">${esc(folder)}</span><button type="button" data-remove-folder="${index}" aria-label="Remove folder">${icon('oc-close', 'remixicon h-3 w-3')}</button></span>`).join('') : '<span class="project-folder-empty">Nenhuma pasta selecionada ainda.</span>'}
              </div>
              <button type="button" data-action="pick-folder" class="project-folder-add">${icon('oc-folder-add', 'remixicon h-4 w-4')}<span>Adicionar pasta</span></button>
              <p class="project-form-error" data-project-error role="alert"></p>
            </div>
            <aside class="project-dialog-aside"><div class="project-aside-icon">${icon('oc-folder-add', 'remixicon h-6 w-6')}</div><strong>One context, many chats</strong><p>Os chats criados dentro deste projeto vão compartilhar as pastas selecionadas sem misturar o restante do seu workspace.</p><div class="project-aside-rule"></div><span>${icon('oc-information', 'remixicon h-3.5 w-3.5')} Stored locally in this browser</span></aside>
            <footer class="project-dialog-footer"><button type="button" data-action="close-modal" class="project-secondary-button">Cancel</button><button type="submit" class="project-primary-button">${editing ? 'Save changes' : 'Create project'} ${icon('oc-arrow-right', 'remixicon h-3.5 w-3.5')}</button></footer>
          </form>
        </div>
      </div>`;
    }
    if (state.modal === 'chat-menu') {
      return tplMenuPopover([
        ['rename', 'oc-edit', 'Rename'],
        ['archive', 'oc-archive', 'Archive'],
        ['delete', 'oc-delete-bin', 'Delete', true],
      ]);
    }
    if (state.modal === 'project-menu') {
      return tplMenuPopover([
        ['new-chat', 'oc-chat-new', 'New chat'],
        ['rename', 'oc-edit', 'Rename'],
        ['delete', 'oc-delete-bin', 'Delete project', true],
      ]);
    }
    if (state.modal === 'rename-session') {
      const session = state.sessions.find((item) => item.id === state.menuId);
      return `<div class="fixed inset-0 z-[80] flex items-center justify-center bg-black/60 p-3 backdrop-blur-[3px] oc-backdrop" data-action-modal>
        <div role="dialog" aria-modal="true" aria-label="Rename chat" class="oc-dialog rename-dialog">
          <header class="project-dialog-header"><div><h2>Rename chat</h2></div><button type="button" data-action="close-modal" aria-label="Close dialog" class="memory-icon-button">${icon('oc-close', 'remixicon h-4 w-4')}</button></header>
          <form data-rename-form class="project-dialog-form">
            <div class="project-form-main">
              <label class="project-field"><span>Title</span><input required data-rename-name class="project-title-input" value="${esc(session ? session.title : '')}" autocomplete="off"></label>
              <p class="project-form-error" data-rename-error role="alert"></p>
            </div>
            <footer class="project-dialog-footer"><button type="button" data-action="close-modal" class="project-secondary-button">Cancel</button><button type="submit" class="project-primary-button">Save</button></footer>
          </form>
        </div>
      </div>`;
    }
    if (state.modal === 'search') {
      title = 'Search sessions';
      body = `<div class="space-y-3"><input autofocus data-session-search class="h-9 w-full rounded-md border border-border bg-transparent px-3 text-sm text-foreground outline-none focus:border-primary" placeholder="Search sessions"><div data-search-results class="max-h-64 overflow-y-auto space-y-1">${state.sessions.map((s) => `<button type="button" data-search-session data-session-id="${s.id}" class="flex w-full rounded-md px-2.5 py-2 text-left text-sm text-foreground">${esc(s.title)}</button>`).join('')}</div></div>`;
    }
    const widths = { project: 410, archive: 360, search: 360 };
    const heights = { project: 430, archive: 330, search: 350 };
    const width = widths[state.modal] || 360;
    const height = heights[state.modal] || 350;
    const anchor = state.modalAnchor || { x: 12, y: 64, w: 24, h: 24, above: false };
    let left = anchor.x + anchor.w + 8;
    let top = anchor.above ? anchor.y - height - 8 : anchor.y;
    if (left + width > window.innerWidth - 8) left = Math.max(8, anchor.x - width - 8);
    if (top + height > window.innerHeight - 8) top = Math.max(8, window.innerHeight - height - 8);
    if (top < 8) top = 8;
    return `<div class="fixed inset-0 z-[80]" data-action-modal><div role="dialog" aria-label="${title}" class="oc-dialog oc-action-popover w-[min(92vw,520px)] max-h-[86dvh] overflow-hidden rounded-xl border border-border bg-background shadow-2xl" style="position:fixed;left:${left}px;top:${top}px;width:min(${width}px,calc(100vw - 16px));"><div class="flex items-center justify-between border-b border-border/70 px-4 py-3"><h2 class="text-sm font-medium text-foreground">${title}</h2><button type="button" data-action="close-modal" aria-label="Close dialog" class="rounded-md p-1 text-muted-foreground hover:text-foreground">${icon('oc-close', 'remixicon h-4 w-4')}</button></div><div class="overflow-y-auto p-4">${body}</div></div></div>`;
  };

  /* ---------- templates: right panel ---------- */
  const tplPanelEmpty = (iconName, title, desc) => `
    <div class="flex h-full items-center justify-center p-6 text-center">
      <div class="flex flex-col items-center gap-2">
        ${icon(iconName, 'remixicon mb-3 size-6 text-muted-foreground')}
        <p class="typography-ui-label font-semibold text-foreground">${esc(title)}</p>
        <p class="typography-meta mt-1 text-muted-foreground">${esc(desc)}</p>
      </div>
    </div>`;


  const traceEntries = (session) => {
    if (!session) return [];
    const entries = [];
    session.messages.forEach((m, index) => {
      const id = m.id || `message_${index}`;
      if (m.role === 'user') entries.push({ id: `${id}_input`, type: 'input', label: 'Input', icon: 'oc-user', text: m.text, meta: m.time || '' });
      if (m.thinking) entries.push({ id: `${id}_thinking`, type: 'thinking', label: 'Thinking', icon: 'oc-brain-ai-3', text: m.thinking, meta: m.duration || '' });
      (m.tools || []).forEach((tool, toolIndex) => {
        entries.push({ id: `${id}_tool_${toolIndex}`, type: 'tool', label: 'Tool', icon: 'oc-tools', text: tool.cmd, detail: (tool.output || []).join('\n'), meta: tool.duration || '' });
      });
      if (m.text || m.code) entries.push({ id: `${id}_output`, type: 'output', label: 'Output', icon: 'oc-ai-agent', text: m.text || (m.code?.lines || []).map(([line]) => line).join(''), meta: m.time || '' });
    });
    if (state.liveTrace) entries.push(...state.liveTrace);
    return entries;
  };

  const traceTone = (type) => ({ input: 'trace-input', output: 'trace-output', thinking: 'trace-thinking', tool: 'trace-tool' }[type] || 'trace-output');

  const tplContextPanel = () => {
    const s = activeSession();
    const counts = traceEntries(s).reduce((a, e) => { a[e.type] = (a[e.type] || 0) + 1; return a; }, {});
    return `<div class="flex h-full flex-col p-3"><div class="rounded-lg border border-border/70 bg-[var(--surface-elevated)] p-3"><div class="text-sm font-medium text-foreground">Session context</div><div class="mt-2 grid grid-cols-2 gap-2 text-xs text-muted-foreground"><span>Inputs <b class="text-foreground">${counts.input || 0}</b></span><span>Outputs <b class="text-foreground">${counts.output || 0}</b></span><span>Thinkings <b class="text-foreground">${counts.thinking || 0}</b></span><span>Tools <b class="text-foreground">${counts.tool || 0}</b></span></div></div><div class="mt-3 text-xs text-muted-foreground">Abra Chat para ver o log completo, com payloads e detalhes.</div></div>`;
  };

  const tplTracePanel = () => {
    const entries = traceEntries(activeSession()).filter((e) => state.traceFilter === 'all' || e.type === state.traceFilter);
    const filters = [['all', 'All'], ['input', 'Inputs'], ['output', 'Outputs'], ['thinking', 'Thinkings'], ['tool', 'Tools']];
    return `<div class="flex h-full min-h-0 flex-col"><div class="border-b border-border/70 px-3 py-2"><div class="text-xs text-muted-foreground">Conversation trace</div><div class="mt-2 flex gap-1 overflow-x-auto">${filters.map(([key, label]) => `<button type="button" data-trace-filter="${key}" class="trace-filter rounded-md px-2 py-1 text-[11px] ${state.traceFilter === key ? 'bg-interactive-hover text-foreground' : 'text-muted-foreground hover:text-foreground'}">${label}</button>`).join('')}</div></div><div class="min-h-0 flex-1 overflow-y-auto p-2.5 space-y-1.5">${entries.length ? entries.map(tplTraceEntry).join('') : '<div class="p-4 text-center text-xs text-muted-foreground">Nenhum evento nesta conversa.</div>'}</div></div>`;
  };

  const tplTraceEntry = (entry) => {
    const open = !!state.traceExpanded[entry.id];
    return `<div class="trace-entry ${traceTone(entry.type)} ${open ? 'is-open' : ''}" data-trace-id="${esc(entry.id)}"><button type="button" data-trace-toggle="${esc(entry.id)}" class="flex w-full items-center gap-2 rounded-md px-2 py-2 text-left"><span class="trace-entry-icon">${icon(entry.icon, 'remixicon h-3.5 w-3.5')}</span><span class="min-w-0 flex-1"><span class="block text-xs font-medium">${esc(entry.label)}</span><span class="block truncate text-[11px] opacity-70">${esc(entry.text || '')}</span></span><span class="text-[10px] opacity-60">${esc(entry.meta || '')}</span>${icon(open ? 'oc-arrow-up-s' : 'oc-arrow-down-s', 'remixicon h-3 w-3 opacity-60')}</button>${open ? `<div class="trace-entry-details"><div class="trace-detail-label">${entry.type === 'tool' ? 'Command / input' : entry.type === 'thinking' ? 'Reasoning trace' : entry.type === 'input' ? 'User input' : 'Assistant output'}</div><pre>${esc(entry.text || '')}</pre>${entry.detail ? `<div class="trace-detail-label">Output</div><pre>${esc(entry.detail)}</pre>` : ''}</div>` : ''}</div>`;
  };

  const tplPanelBody = (label) => {
    switch (label) {
      case 'Context':
        return tplContextPanel();
      case 'Chat':
        return tplTracePanel();
      case 'Stack':
        return `<div class="flex h-full flex-col p-3"><div class="rounded-lg border border-border/70 bg-[var(--surface-elevated)] p-3"><div class="flex items-center gap-2 text-sm font-medium text-foreground">${icon('oc-stack', 'remixicon h-4 w-4 text-muted-foreground')}Stack</div><p class="mt-2 text-xs leading-relaxed text-muted-foreground">Camada de contexto da sessão: mensagens, ferramentas e artefatos usados nesta conversa.</p></div><div class="mt-3 space-y-1.5 text-xs text-muted-foreground"><div class="rounded-md border border-border/60 px-2.5 py-2">Contexto ativo</div><div class="rounded-md border border-border/60 px-2.5 py-2">Ferramentas disponíveis</div><div class="rounded-md border border-border/60 px-2.5 py-2">Notas do projeto</div></div></div>`;
      case 'Terminal':
        return `
        <div class="flex h-full flex-col font-mono text-[13px]">
          <div class="min-h-0 flex-1 overflow-y-auto p-3 leading-relaxed text-foreground/85">
            <div class="text-muted-foreground/60">Windows PowerShell</div>
            <div class="text-muted-foreground/60">Copyright (C) Microsoft Corporation. All rights reserved.</div>
            <div class="mt-1">PS <span class="text-primary">C:\\Users\\GM METELURGICA\\Pictures\\Projects\\GUI Zeno</span>&gt; <span class="text-foreground">git status</span></div>
            <div class="text-muted-foreground">fatal: not a git repository (or any of the parent directories): .git</div>
            <div class="mt-1">PS <span class="text-primary">C:\\Users\\GM METELURGICA\\Pictures\\Projects\\GUI Zeno</span>&gt; <span class="text-foreground">ls</span></div>
            <div class="text-muted-foreground">AGENTS.md&nbsp;&nbsp;CHANGELOG.md&nbsp;&nbsp;packages&nbsp;&nbsp;README.md&nbsp;&nbsp;bun.lock&nbsp;&nbsp;openchamber-clone</div>
            <div class="mt-1 flex items-center gap-1">PS <span class="text-primary">C:\\Users\\GM METELURGICA\\Pictures\\Projects\\GUI Zeno</span>&gt; <span data-terminal-input class="text-foreground outline-none"></span><span class="tool-caret"></span></div>
          </div>
        </div>`;
      case 'Project notes':
        return tplPanelEmpty('oc-sticky-note', 'No file open', 'Pick a note from the project to start writing.');
      case 'Browser':
        return tplBrowserPanel();
      default:
        return '';
    }
  };

  const tplPanel = () => {
    if (!state.panel) return '';
    const label = state.panel;
    const [ic] = PANELS[label];
    const w = state.panelW;
    return `
    <aside data-context-panel="true" tabindex="-1" class="flex min-h-0 flex-col overflow-hidden bg-background relative h-full flex-shrink-0 will-change-[width] motion-reduce:transition-none transition-[width] duration-200 ease-[cubic-bezier(0.22,1,0.36,1)]" style="--oc-context-panel-width: ${w}px; width: min(var(--oc-context-panel-width), 100%); max-width: 100%; overflow-x: clip;">
      <div aria-hidden="true" class="absolute left-0 top-0 z-40 h-full w-px bg-border"></div>
      <div aria-hidden="true" class="absolute right-0 top-0 z-40 h-full w-px bg-border"></div>
      <div class="absolute left-0 top-0 z-50 h-full w-[3px] cursor-col-resize transition-colors hover:bg-[var(--interactive-border)]/80" role="separator" aria-orientation="vertical" aria-label="Resize context panel" data-action="resize-panel"></div>
      <div class="relative z-10 flex h-full min-h-0 shrink-0 flex-col duration-200 ease-[cubic-bezier(0.22,1,0.36,1)] motion-reduce:transition-none transition-[width,opacity]" aria-hidden="false" style="width: var(--oc-context-panel-width);">
        <header class="flex h-10 items-stretch border-b border-border">
          <div class="flex min-w-0 flex-1 items-center gap-1.5 px-3">${icon(ic, 'remixicon h-3.5 w-3.5')}<span class="truncate typography-ui-label text-foreground">${label}</span></div>
          <div class="flex items-center gap-1 px-1.5">
            <button data-slot="button" class="group relative inline-flex items-center justify-center whitespace-nowrap [corner-shape:squircle] typography-ui-label font-medium lowercase tracking-[0.01em] shrink-0 select-none transition-[background-color,border-color,color,opacity] duration-150 ease-out outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 text-foreground hover:text-foreground gap-1.5 has-[>svg]:px-2 rounded-[9px] supports-[corner-shape:squircle]:rounded-[50px] h-7 w-7 p-0" type="button" title="Expand panel" aria-label="Expand panel" data-action="expand-panel">${icon('oc-fullscreen', 'remixicon h-3.5 w-3.5')}</button>
            <button data-slot="button" class="group relative inline-flex items-center justify-center whitespace-nowrap [corner-shape:squircle] typography-ui-label font-medium lowercase tracking-[0.01em] shrink-0 select-none transition-[background-color,border-color,color,opacity] duration-150 ease-out outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 text-foreground hover:text-foreground gap-1.5 has-[>svg]:px-2 rounded-[9px] supports-[corner-shape:squircle]:rounded-[50px] h-7 w-7 p-0" type="button" title="Close panel" aria-label="Close panel" data-action="close-panel">${icon('oc-close', 'remixicon h-3.5 w-3.5')}</button>
          </div>
        </header>
        <div class="relative min-h-0 flex-1 overflow-hidden">
          ${tplPanelBody(label)}
        </div>
      </div>
    </aside>`;
  };

  /* ---------- templates: settings ---------- */
  const SETTINGS_GROUPS = [
    ['General', [
      ['Appearance', 'oc-palette'], ['Chat', 'oc-chat-ai-3'], ['Notifications', 'oc-notification-3'],
      ['Shortcuts', 'oc-command'], ['Voice', 'oc-mic'], ['Usage', 'oc-bar-chart-2'],
    ]],
    ['Workspace', [
      ['Projects', 'oc-folder'], ['Remote Instances', 'oc-server'], ['Plugins', 'oc-puzzle-2'],
    ]],
  ];

  const tplSettingsRadio = (group, value, options) => `
    <div role="radiogroup" aria-label="${group}" class="space-y-1.5">
      ${options.map((o) => `
      <div class="flex cursor-pointer gap-2 py-0.5 items-center" role="button" tabindex="0" data-radio="${o}">
        <button type="button" role="radio" aria-checked="${value === o}" aria-label="${o}" class="group/radio relative flex h-[14px] w-[14px] min-h-[14px] min-w-[14px] shrink-0 self-center items-center justify-center rounded-full outline-none transition-[background-color,box-shadow] duration-200 ease-out ${value === o ? 'bg-[color-mix(in_srgb,var(--primary-base)_80%,transparent)] shadow-none' : 'bg-[var(--surface-muted)] shadow-[inset_0_0_0_1px_var(--interactive-border)]'}"><span aria-hidden="true" class="block h-[5px] w-[5px] rounded-full ${value === o ? 'bg-white' : 'bg-white opacity-0'}"></span></button>
        <div class="flex min-w-0 flex-col"><span class="typography-settings-field-label font-normal text-foreground">${o}</span></div>
      </div>`).join('')}
    </div>`;

  const tplSettingsSelect = (label, value, options, dataMenu) => `
    <div class="flex min-w-0 max-w-[24rem] items-center gap-2">
      <button type="button" tabindex="0" role="combobox" aria-expanded="false" aria-haspopup="listbox" data-slot="select-trigger" data-size="settings" aria-label="${label}" data-select-menu="${dataMenu}" class="border-input flex items-center justify-between gap-2 rounded-md border bg-transparent typography-ui-label whitespace-nowrap shadow-none outline-none text-left focus-visible:outline-none h-8 min-h-8 px-3 w-full min-w-40 max-w-48">
        <span data-slot="select-value">${esc(value)}</span>
        <span aria-hidden="true">${icon('oc-arrow-down-s', 'remixicon size-4 opacity-50')}</span>
      </button>
    </div>`;

  const tplSettingsSection = () => {
    const sec = state.settingsSection;
    if (sec === 'Appearance') {
      const isDark = state.colorMode === 'dark' || (state.colorMode === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);
      const lightSel = themeList('light').find((t) => t.id === state.lightTheme) || { name: 'OpenChamber Mono' };
      const darkSel = themeList('dark').find((t) => t.id === state.darkTheme) || { name: 'OpenChamber Mono' };
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Appearance</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Customize how OpenChamber looks and feels.</p>
        <div class="mt-6">
          <h3 class="typography-ui-label font-semibold text-foreground">Color mode &amp; Theme</h3>
          <div class="mt-3 grid grid-cols-1 gap-6 @3xl:grid-cols-2 @3xl:gap-10">
            <div class="space-y-4">
              ${tplSettingsRadio('Color Mode', state.colorMode === 'system' ? 'System' : state.colorMode, ['System', 'Light', 'Dark'])}
              <div>
                <div class="mb-1.5 typography-meta font-medium text-muted-foreground">Light Theme</div>
                ${tplSettingsSelect('Select light theme', lightSel.name, themeList('light'), 'light-theme')}
              </div>
              <div>
                <div class="mb-1.5 typography-meta font-medium text-muted-foreground">Dark Theme</div>
                ${tplSettingsSelect('Select dark theme', darkSel.name, themeList('dark'), 'dark-theme')}
              </div>
              <button type="button" class="inline-flex items-center justify-center gap-2 rounded-md border border-border px-3 h-8 text-sm text-foreground transition-colors" data-action="reload-themes">${icon('oc-refresh', 'remixicon h-3.5 w-3.5')}Reload themes</button>
            </div>
            <div class="space-y-4">
              <div>
                <div class="mb-1.5 typography-meta font-medium text-muted-foreground">Current theme</div>
                <div class="flex items-center gap-2 rounded-lg border border-border/70 bg-[var(--surface-elevated)] p-3">
                  <div class="flex h-8 w-8 items-center justify-center rounded-md" style="background: var(--primary); color: var(--primary-foreground);">${icon('oc-palette', 'remixicon h-4 w-4')}</div>
                  <div>
                    <div class="text-sm font-medium text-foreground">${isDark ? darkSel.name : lightSel.name}</div>
                    <div class="text-xs text-muted-foreground">${isDark ? 'Dark' : 'Light'} variant</div>
                  </div>
                </div>
              </div>
              <div>
                <div class="mb-1.5 typography-meta font-medium text-muted-foreground">Localization</div>
                <div class="space-y-3">
                  <div>
                    <div class="mb-1.5 typography-meta text-muted-foreground">Language</div>
                    ${tplSettingsSelect('Select language', 'English', [], 'lang')}
                  </div>
                  <div>
                    <div class="mb-1.5 typography-meta text-muted-foreground">Time Format</div>
                    ${tplSettingsSelect('Select time format', 'Auto', [], 'timefmt')}
                  </div>
                </div>
              </div>
              <div>
                <div class="mb-1.5 typography-meta font-medium text-muted-foreground">App</div>
                <div class="space-y-3">
                  <div>
                    <div class="mb-1.5 typography-meta text-muted-foreground">Install App Name</div>
                    <input value="OpenChamber" class="h-8 w-full max-w-[24rem] rounded-md border border-border bg-transparent px-3 text-sm text-foreground outline-none">
                  </div>
                  <div>
                    <div class="mb-1.5 typography-meta text-muted-foreground">Install Orientation</div>
                    ${tplSettingsSelect('Select orientation', 'Follow system', [], 'orientation')}
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>`;
    }
    if (sec === 'General') {
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">General</h2>
        <p class="typography-meta mt-1 text-muted-foreground">General settings for OpenChamber.</p>
        <div class="mt-6 space-y-4">
          <div>
            <div class="mb-1.5 typography-meta font-medium text-muted-foreground">Application name</div>
            <input value="OpenChamber" class="h-8 w-full max-w-[24rem] rounded-md border border-border bg-transparent px-3 text-sm text-foreground outline-none">
          </div>
          <div>
            <div class="mb-1.5 typography-meta font-medium text-muted-foreground">Startup</div>
            ${tplSettingsSelect('Select startup behavior', 'Continue last session', [], 'startup')}
          </div>
        </div>
      </div>`;
    }
    return `
    <div class="px-6 py-5">
      <h2 class="typography-h3 text-foreground">${esc(sec)}</h2>
      <p class="typography-meta mt-1 text-muted-foreground">Settings for ${esc(sec)} are managed by the OpenChamber server.</p>
      <div class="mt-10 flex flex-col items-center justify-center gap-2 text-center">
        ${icon('oc-settings-3', 'remixicon size-8 text-muted-foreground/50')}
        <p class="typography-meta text-muted-foreground">This section is not available in the static build.</p>
      </div>
    </div>`;
  };

  const tplSettings = () => `
    <div class="fixed inset-0 z-[95] flex items-center justify-center bg-black/50 oc-backdrop" data-settings-backdrop style="background-color: rgb(0 0 0 / 0.8)">
      <div role="dialog" aria-label="OpenChamber settings window." data-open="" class="oc-dialog relative pointer-events-auto w-[90vw] max-w-[1200px] h-[85vh] max-h-[900px] rounded-xl border shadow-none overflow-hidden origin-center bg-background transition-all duration-150 ease-out" style="--nested-dialogs: 0; height: min(85vh, calc(100dvh - 24px)); max-height: calc(100dvh - 24px); max-width: min(805px, calc(100vw - 24px));">
        <div class="absolute right-0.5 z-50 top-0.5">
          <button type="button" aria-label="Close settings" title="Close Settings (Ctrl+,)" data-action="close-settings" class="inline-flex h-7 w-7 items-center justify-center rounded-md p-0.5 text-muted-foreground hover:text-foreground focus-visible:outline-none">${icon('oc-close', 'remixicon h-5 w-5')}</button>
        </div>
        <div class="relative flex h-full min-h-0 flex-col overflow-hidden bg-background">
          <div class="flex flex-1 min-h-0 overflow-hidden">
            <div class="relative flex h-full min-h-0 flex-col overflow-hidden border-r bg-sidebar" style="width: 256px; min-width: 256px; border-color: var(--interactive-border);">
              <div class="flex h-full flex-col overflow-hidden">
                <div class="flex-1 min-h-0 overflow-y-auto overflow-x-hidden">
                  <div class="flex flex-col gap-0.5 px-4 pt-4 pb-2">
                    ${SETTINGS_GROUPS.map(([group, items]) => `
                    <div class="space-y-0.5">
                      ${group ? `<div class="px-3 pb-1 typography-micro font-semibold uppercase tracking-wide text-muted-foreground sm:px-2 sm:pb-0.5 pt-4 sm:pt-3">${group}</div>` : ''}
                      ${items.map(([name, ic]) => `
                      <button type="button" data-settings-section="${name}" class="flex h-11 w-full items-center gap-2.5 rounded-md px-3 overflow-hidden sm:h-8 sm:gap-2 sm:px-2 ${state.settingsSection === name ? 'bg-interactive-hover text-foreground' : 'text-foreground'}">${icon(ic, 'remixicon h-[18px] w-[18px] shrink-0 sm:h-4 sm:w-4')}<span class="flex items-center gap-1.5 whitespace-nowrap overflow-hidden transition-opacity duration-150 opacity-100"><span class="typography-ui-label font-normal truncate">${name}</span></span></button>`).join('')}
                    </div>`).join('')}
                  </div>
                </div>
              </div>
            </div>
            <div class="relative flex-1 min-h-0 overflow-y-auto" data-settings-content>
              ${tplSettingsSection()}
            </div>
          </div>
        </div>
      </div>
    </div>`;

  const tplSelectMenu = (anchor, title, options, current, key) => `
    <div class="fixed z-[110] oc-menu" data-menu-popup="${key}">
      <div class="w-56 overflow-hidden rounded-lg border border-border/80 bg-popover text-popover-foreground shadow-xl">
        <div class="border-b border-border/60 px-3 py-1.5 typography-micro font-semibold uppercase tracking-wide text-muted-foreground">${esc(title)}</div>
        <div class="p-1">
          ${options.map((o) => `
          <button type="button" data-menu-option="${key}" data-value="${esc(o.id)}" class="flex w-full items-center justify-between gap-2 rounded-md px-2.5 py-1.5 text-left text-sm ${o.id === current ? 'bg-interactive-hover text-foreground' : 'text-muted-foreground hover:text-foreground'}">
            <span class="truncate">${esc(o.name)}</span>
            ${o.id === current ? icon('oc-check', 'remixicon h-3.5 w-3.5 text-primary') : ''}
          </button>`).join('')}
        </div>
      </div>
    </div>`;

  const tplVariantDialog = () => `
    <div class="oc-variant-backdrop" data-variant-backdrop>
      <section class="oc-variant-dialog" role="dialog" aria-modal="true" aria-labelledby="oc-variant-title">
        <header class="oc-variant-dialog-header">
          <div>
            <div class="oc-variant-eyebrow">Response design</div>
            <h2 id="oc-variant-title">Choose how deeply Zeno should work</h2>
            <p>Pick a response style for this session. You can change it whenever you need.</p>
          </div>
          <button type="button" class="oc-variant-close" data-variant-close aria-label="Close response design">${icon('oc-close', 'remixicon h-4 w-4')}</button>
        </header>
        <div class="oc-variant-grid">
          ${DESIGN_OPTIONS.map(([name, description]) => `
            <button type="button" class="oc-variant-option" data-design-option="${esc(name)}" aria-checked="${state.design === name}">
              <span class="oc-variant-option-top"><strong>${esc(name)}</strong>${state.design === name ? icon('oc-check', 'remixicon h-4 w-4') : ''}</span>
              <span>${esc(description)}</span>
            </button>`).join('')}
        </div>
      </section>
    </div>`;

  const bindVariantDialog = (rootEl) => {
    const close = () => { rootEl.innerHTML = ''; };
    $('[data-variant-close]', rootEl)?.addEventListener('click', close);
    $('[data-variant-backdrop]', rootEl)?.addEventListener('mousedown', (event) => { if (event.target === event.currentTarget) close(); });
    $$('[data-design-option]', rootEl).forEach((option) => option.addEventListener('click', () => {
      state.design = option.dataset.designOption;
      LS.set('oc-clone-design', state.design);
      $$('.model-controls__variant-label').forEach((label) => { label.textContent = state.design; });
      close();
    }));
  };

  const openVariantDialog = () => {
    const rootEl = $('[data-variant-root]');
    if (!rootEl) return;
    rootEl.innerHTML = tplVariantDialog();
    bindVariantDialog(rootEl);
  };

  /* ---------- render ---------- */
  const root = () => $('#root');

  const render = () => {
    applyTheme();
    document.body.classList.toggle('zeno-voice-active', state.voiceMode);
    renderVoice();
    const chat = $('#chat-root');
    if (chat) {
      const scroller = $('[data-scrollbar="chat"]', chat);
      const atBottom = !scroller || scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 60;
      chat.innerHTML = tplChat();
      bindChat(chat);
      bindComposer(chat);
      if (atBottom && scroller) scroller.scrollTop = scroller.scrollHeight;
    }
    const sb = $('[data-sidebar-root]');
    if (sb) { sb.innerHTML = tplSidebar(); bindSidebar(sb); }
    const nav = $('[data-nav-root]');
    if (nav) { nav.innerHTML = tplNav(); bindNav(nav); }
    const panel = $('[data-panel-root]');
    if (panel) { panel.innerHTML = tplPanel(); bindPanel(panel); }
    const settings = $('[data-settings-root]');
    if (settings) {
      const sig = state.settings ? [state.settingsSection, state.colorMode, state.lightTheme, state.darkTheme].join('|') : '';
      if (settings.dataset.sig !== sig) {
        settings.innerHTML = state.settings ? tplSettings() : '';
        settings.dataset.sig = sig;
        if (state.settings) bindSettings(settings);
      }
    }
    const actionModal = $('[data-action-modal-root]');
    if (actionModal) { actionModal.innerHTML = tplActionModal(); if (state.modal) bindActionModal(actionModal); }
    const chat2 = $('#chat-root');
    if (chat2) { const sc = $('[data-scrollbar="chat"]', chat2); if (sc) sc.scrollTop = sc.scrollHeight; }
  };

  const captureMemoryNote = () => {
    const session = activeSession();
    const message = session?.messages.slice().reverse().find((item) => item.role === 'assistant' && (item.text || item.code));
    const source = message?.text || (message?.code?.lines || []).map(([line]) => line).join('') || 'Ainda não há uma resposta nesta sessão. Esta nota pode receber contexto novo conforme o trabalho avança.';
    const title = message ? `Session note · ${session.title}` : 'New captured note';
    const note = {
      id: 'memory_' + uid(),
      title,
      tag: 'Captured note',
      excerpt: source.replace(/[`*_#\n]/g, ' ').trim().slice(0, 116) + (source.length > 116 ? '…' : ''),
      content: `# ${title}\n\n${source}\n\n> Captured from the active Zeno session.`,
      accent: '#58b6ff',
      x: 31 + ((state.memoryNotes.length * 19) % 42),
      y: 35 + ((state.memoryNotes.length * 23) % 38),
      updated: 'agora',
    };
    state.memoryNotes.push(note);
    LS.set('oc-clone-memory-notes', state.memoryNotes);
    state.workspace = 'memory';
    state.noteId = note.id;
    state.panel = null;
    persistWorkspace();
    render();
  };

  const openMemoryEditor = (note, isNew = false) => {
    state.noteId = note.id;
    state.memoryEditor = { id: note.id, title: note.title, content: note.content, isNew };
    render();
  };

  const createMemoryNoteAt = (x, y) => {
    const note = { id: 'memory_' + uid(), title: 'Untitled note', tag: 'Note', excerpt: '', content: '# Untitled note\n\nStart writing here.', accent: '#fff', x, y, updated: 'agora', by: 'User' };
    state.memoryNotes.push(note);
    openMemoryEditor(note, true);
  };

  const bindMemory = (rootEl) => {
    $$('[data-memory-note]', rootEl).forEach((node) => node.addEventListener('dblclick', (event) => {
      event.stopPropagation();
      state.noteId = node.dataset.memoryNote;
      render();
    }));
    const viewport = $('[data-memory-viewport]', rootEl);
    const canvas = $('[data-memory-canvas]', rootEl);
    if (viewport && canvas) {
      let scale = Math.min(1, viewport.clientWidth / 1700, viewport.clientHeight / 960);
      let x = (viewport.clientWidth - 1600 * scale) / 2;
      let y = (viewport.clientHeight - 900 * scale) / 2;
      let dragging = false, draggedNode = null, startX = 0, startY = 0;
      let connectingFrom = null, connectionLine = null;
      const transform = () => { canvas.style.transform = `translate(${x}px, ${y}px) scale(${scale})`; };
      const nodePoint = (event) => {
        const rect = viewport.getBoundingClientRect();
        return { x: (event.clientX - rect.left - x) / scale, y: (event.clientY - rect.top - y) / scale };
      };
      const updateEdges = (node) => {
        const id = node.dataset.memoryNote;
        const matrix = node.transform.baseVal.getItem(0).matrix;
        $$(`[data-memory-edge][data-from="${id}"]`, canvas).forEach((edge) => { edge.setAttribute('x1', matrix.e); edge.setAttribute('y1', matrix.f); });
        $$(`[data-memory-edge][data-to="${id}"]`, canvas).forEach((edge) => { edge.setAttribute('x2', matrix.e); edge.setAttribute('y2', matrix.f); });
      };
      transform();
      viewport.addEventListener('pointerdown', (event) => {
        const targetNode = event.target.closest('[data-memory-note]');
        if (event.button === 2 && targetNode) {
          event.preventDefault();
          connectingFrom = targetNode;
          state.memoryLinkPending = true;
          const matrix = targetNode.transform.baseVal.getItem(0).matrix;
          connectionLine = document.createElementNS('http://www.w3.org/2000/svg', 'line');
          connectionLine.setAttribute('class', 'memory-edge memory-edge-preview');
          connectionLine.setAttribute('x1', matrix.e); connectionLine.setAttribute('y1', matrix.f);
          connectionLine.setAttribute('x2', matrix.e); connectionLine.setAttribute('y2', matrix.f);
          $('[data-memory-edges]', canvas).appendChild(connectionLine);
          viewport.setPointerCapture(event.pointerId);
          return;
        }
        if (event.button !== 0) return;
        if (event.target.closest('[data-memory-action]')) return;
        draggedNode = targetNode;
        dragging = true;
        if (draggedNode) {
          const point = nodePoint(event);
          const matrix = draggedNode.transform.baseVal.getItem(0).matrix;
          startX = point.x - matrix.e;
          startY = point.y - matrix.f;
          draggedNode.classList.add('is-dragging');
        } else {
          startX = event.clientX - x;
          startY = event.clientY - y;
        }
        (draggedNode || viewport).setPointerCapture(event.pointerId);
        viewport.classList.add('is-dragging');
      });
      viewport.addEventListener('pointermove', (event) => {
        if (connectingFrom && connectionLine) {
          const point = nodePoint(event);
          connectionLine.setAttribute('x2', point.x); connectionLine.setAttribute('y2', point.y);
          return;
        }
        if (!dragging) return;
        if (draggedNode) {
          const point = nodePoint(event);
          draggedNode.setAttribute('transform', `translate(${point.x - startX} ${point.y - startY})`);
          updateEdges(draggedNode);
        } else {
          x = event.clientX - startX;
          y = event.clientY - startY;
          transform();
        }
      });
      viewport.addEventListener('click', (event) => {
        const node = event.target.closest('[data-memory-note]');
        if (!node || event.detail !== 2) return;
        state.noteId = node.dataset.memoryNote;
        render();
      });
      const stopDragging = () => {
        dragging = false;
        draggedNode?.classList.remove('is-dragging');
        draggedNode = null;
        viewport.classList.remove('is-dragging');
      };
      viewport.addEventListener('pointerup', (event) => {
        if (connectingFrom) {
          const target = document.elementFromPoint(event.clientX, event.clientY)?.closest('[data-memory-note]');
          const from = connectingFrom.dataset.memoryNote, to = target?.dataset.memoryNote;
          if (to && to !== from && !state.memoryEdges.some((edge) => edge.includes(from) && edge.includes(to))) {
            state.memoryEdges.push([from, to]);
            LS.set('oc-clone-memory-edges', state.memoryEdges);
            render();
            return;
          }
          connectionLine?.remove(); connectingFrom = null; connectionLine = null;
        }
        stopDragging();
      });
      viewport.addEventListener('pointercancel', stopDragging);
      viewport.addEventListener('wheel', (event) => {
        event.preventDefault();
        const rect = viewport.getBoundingClientRect();
        const px = event.clientX - rect.left;
        const py = event.clientY - rect.top;
        const nextScale = Math.min(2.5, Math.max(.35, scale * Math.exp(-event.deltaY * .001)));
        x = px - (px - x) * (nextScale / scale);
        y = py - (py - y) * (nextScale / scale);
        scale = nextScale;
        transform();
      }, { passive: false });
      viewport.addEventListener('contextmenu', (event) => {
        event.preventDefault();
        if (state.memoryLinkPending) { state.memoryLinkPending = false; return; }
        if (event.target.closest('[data-memory-note]')) return;
        const point = nodePoint(event);
        createMemoryNoteAt(Math.max(2, Math.min(98, point.x / 16)), Math.max(3, Math.min(97, point.y / 9)));
      });
    }
    $$('[data-memory-action]', rootEl).forEach((action) => action.addEventListener('click', (event) => {
      event.stopPropagation();
      const note = memoryNoteById(action.closest('[data-memory-note]').dataset.memoryNote);
      if (!note) return;
      const type = action.dataset.memoryAction;
      if (type === 'view') { state.noteId = note.id; state.memoryEditor = null; render(); }
      if (type === 'edit') openMemoryEditor(note);
      if (type === 'rename') {
        const title = window.prompt('Rename note', note.title)?.trim();
        if (title) { note.title = title; LS.set('oc-clone-memory-notes', state.memoryNotes); render(); }
      }
      if (type === 'delete') { state.memoryDeleteId = note.id; render(); }
    }));
    $$('[data-memory-confirm-cancel]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.memoryDeleteId = null;
      render();
    }));
    $('[data-memory-confirm-accept]', rootEl)?.addEventListener('click', () => {
      const note = memoryNoteById(state.memoryDeleteId);
      if (note) {
        state.memoryNotes = state.memoryNotes.filter((item) => item.id !== note.id);
        state.memoryEdges = state.memoryEdges.filter((edge) => !edge.includes(note.id));
        LS.set('oc-clone-memory-notes', state.memoryNotes);
        LS.set('oc-clone-memory-edges', state.memoryEdges);
      }
      state.memoryDeleteId = null;
      render();
    });
    $('[data-action="edit-memory-note"]', rootEl)?.addEventListener('click', () => openMemoryEditor(memoryNoteById(state.noteId)));
    $('[data-action="save-memory-note"]', rootEl)?.addEventListener('click', () => {
      const note = memoryNoteById(state.noteId);
      if (!note) return;
      note.title = $('[data-memory-title]', rootEl).value.trim() || 'Untitled note';
      note.content = $('[data-memory-content]', rootEl).value;
      note.excerpt = note.content.replace(/[`*_#\n]/g, ' ').trim().slice(0, 116);
      state.memoryEditor = null; LS.set('oc-clone-memory-notes', state.memoryNotes); render();
    });
    $('[data-action="discard-memory-note"]', rootEl)?.addEventListener('click', () => {
      if (state.memoryEditor?.isNew) state.memoryNotes = state.memoryNotes.filter((note) => note.id !== state.noteId);
      state.memoryEditor = null; state.noteId = null; render();
    });
    $$('[data-action="capture-memory"]', rootEl).forEach((button) => button.addEventListener('click', captureMemoryNote));
    $('[data-action="close-note"]', rootEl)?.addEventListener('click', () => { state.noteId = null; state.memoryEditor = null; render(); });
    $('[data-note-backdrop]', rootEl)?.addEventListener('click', () => { state.noteId = null; render(); });
  };

  const bindChat = (rootEl) => {
    $$('[data-md-action="copy-code"]', rootEl).forEach((b) => b.addEventListener('click', () => {
      const pre = b.closest('[data-component="markdown-code"]').querySelector('pre');
      if (pre && navigator.clipboard) navigator.clipboard.writeText(pre.innerText).then(() => {
        b.setAttribute('aria-label', 'Copied');
        setTimeout(() => b.setAttribute('aria-label', 'Copy code'), 1200);
      });
    }));
    $$('[data-md-action="toggle-code-wrap"]', rootEl).forEach((b) => b.addEventListener('click', () => {
      const box = b.closest('[data-component="markdown-code"]');
      const pre = box.querySelector('pre');
      const wrapping = pre.style.whiteSpace !== 'pre';
      pre.style.whiteSpace = wrapping ? 'pre-wrap' : 'pre';
      pre.style.overflowWrap = wrapping ? 'anywhere' : 'normal';
      box.setAttribute('data-code-wrap', String(wrapping));
      b.setAttribute('aria-pressed', String(wrapping));
    }));
    $$('[data-tool-key]', rootEl).forEach((row) => row.addEventListener('click', () => {
      const k = row.dataset.toolKey;
      state.expandedTools[k] = !state.expandedTools[k];
      render();
    }));
    if (state.workspace === 'memory') bindMemory(rootEl);
  };

  const refreshSidebar = () => {
    const sb = $('[data-sidebar-root]');
    if (sb) { sb.innerHTML = tplSidebar(); bindSidebar(sb); }
  };

  const bindSidebar = (rootEl) => {
    $$('[data-session-id]', rootEl).forEach((row) => row.addEventListener('click', (e) => {
      const b = e.target.closest('button');
      if (!b || b.dataset.action === 'open-session') {
        state.activeId = row.dataset.sessionId;
        const session = state.sessions.find((item) => item.id === state.activeId);
        state.activeProjectId = session?.projectId || null;
        state.workspace = 'chat';
        state.draftNew = false;
        state.noteId = null;
        LS.set('oc-clone-active', state.activeId);
        persistWorkspace();
        render();
      }
    }));
    $('[data-action="new-session"]', rootEl)?.addEventListener('click', () => {
      state.workspace = 'chat';
      state.noteId = null;
      state.panel = null;
      state.draftNew = true;
      persistWorkspace();
      render();
    });
    $$('[data-workspace]', rootEl).forEach((button) => button.addEventListener('click', () => {
      state.workspace = button.dataset.workspace;
      state.noteId = null;
      state.panel = null;
      if (state.workspace !== 'chat') state.activeProjectId = null;
      persistWorkspace();
      render();
    }));
    $$('[data-project-open]', rootEl).forEach((button) => button.addEventListener('click', (event) => {
      event.stopPropagation();
      state.modal = 'project-edit';
      state.editProjectId = button.dataset.projectOpen;
      state.modalAnchor = null;
      state.draftFolders = null;
      render();
    }));
    const openSidebarPopup = (button, modal, above = false) => {
      const rect = button.getBoundingClientRect();
      state.modalAnchor = { x: rect.x, y: rect.y, w: rect.width, h: rect.height, above };
      state.modal = modal;
      if (modal === 'project') state.draftFolders = null;
      render();
    };
    $$('[data-action="add-project"]', rootEl).forEach((button) => button.addEventListener('click', (event) => openSidebarPopup(event.currentTarget, 'project')));
    $('[data-action="archive-popup"]', rootEl)?.addEventListener('click', (event) => openSidebarPopup(event.currentTarget, 'archive'));
    $('[data-action="search-sessions"]', rootEl)?.addEventListener('click', (event) => openSidebarPopup(event.currentTarget, 'search'));
    $$('[data-session-menu]', rootEl).forEach((button) => button.addEventListener('click', (event) => {
      event.stopPropagation();
      state.menuId = button.dataset.sessionMenu;
      openSidebarPopup(event.currentTarget, 'chat-menu');
    }));
    $$('[data-project-menu]', rootEl).forEach((button) => button.addEventListener('click', (event) => {
      event.stopPropagation();
      state.menuId = button.dataset.projectMenu;
      openSidebarPopup(event.currentTarget, 'project-menu');
    }));
    $('[data-chats-toggle]', rootEl)?.addEventListener('click', () => {
      state.chatsCollapsed = !state.chatsCollapsed;
      LS.set('oc-clone-chats-collapsed', state.chatsCollapsed);
      const list = $('[data-chats-list]', rootEl);
      if (list) list.hidden = state.chatsCollapsed;
      const toggle = $('[data-chats-toggle]', rootEl);
      if (toggle) {
        toggle.setAttribute('aria-expanded', String(!state.chatsCollapsed));
        const chevron = toggle.querySelector('span');
        if (chevron) chevron.classList.toggle('-rotate-90', state.chatsCollapsed);
      }
    });
    const handle = $('[data-action="resize-sidebar"]', rootEl);
    if (handle) {
      handle.addEventListener('mousedown', (e) => {
        e.preventDefault();
        const startX = e.clientX, startW = state.sidebarW;
        const move = (ev) => {
          const w = Math.min(420, Math.max(180, startW + (ev.clientX - startX)));
          state.sidebarW = w;
          LS.set('oc-clone-sbw', w);
          const aside = $('aside', rootEl);
          const content = aside ? aside.querySelector('[data-sidebar-content]') : null;
          [aside, content].forEach((el) => {
            if (!el) return;
            el.style.width = w + 'px';
            el.style.minWidth = w + 'px';
            el.style.maxWidth = w + 'px';
          });
          if (aside) aside.style.setProperty('--oc-left-sidebar-width', w + 'px');
        };
        const up = () => { document.removeEventListener('mousemove', move); document.removeEventListener('mouseup', up); };
        document.addEventListener('mousemove', move);
        document.addEventListener('mouseup', up);
      });
    }
    $$('[aria-label="Display mode"]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.colorMode = state.colorMode === 'dark' ? 'light' : 'dark';
      LS.set('oc-clone-color-mode', state.colorMode);
      applyTheme();
    }));
    $$('[aria-label="Command palette"]', rootEl).forEach((b) => b.addEventListener('click', togglePalette));
    $$('[aria-label="Settings"]', rootEl).forEach((b) => b.addEventListener('click', () => { state.settings = true; render(); }));
    $$('[aria-label="Open sessions"]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.sidebarOpen = !state.sidebarOpen;
      refreshSidebar();
    }));
  };

  const closeMenu = () => { state.modal = null; state.modalAnchor = null; state.menuId = null; state.editProjectId = null; state.draftFolders = null; };
  const archiveSessionById = (id) => {
    const index = state.sessions.findIndex((s) => s.id === id);
    if (index >= 0) {
      const [session] = state.sessions.splice(index, 1);
      state.archivedSessions.unshift(session);
      if (state.activeId === id) state.activeId = state.sessions[0]?.id || null;
      LS.set('oc-clone-sessions', state.sessions);
      LS.set('oc-clone-archived-sessions', state.archivedSessions);
    }
    closeMenu();
    render();
  };
  const deleteSessionById = (id) => {
    state.sessions = state.sessions.filter((s) => s.id !== id);
    if (state.activeId === id) state.activeId = state.sessions[0]?.id || null;
    LS.set('oc-clone-sessions', state.sessions);
    LS.set('oc-clone-active', state.activeId);
    closeMenu();
    render();
  };
  const deleteProjectById = (id) => {
    state.projects = state.projects.filter((p) => p.id !== id);
    state.sessions = state.sessions.filter((s) => s.projectId !== id);
    if (state.activeProjectId === id) state.activeProjectId = null;
    if (state.activeId && !state.sessions.some((s) => s.id === state.activeId)) state.activeId = state.sessions[0]?.id || null;
    LS.set('oc-clone-projects', state.projects);
    LS.set('oc-clone-sessions', state.sessions);
    LS.set('oc-clone-active-project', state.activeProjectId);
    LS.set('oc-clone-active', state.activeId);
    closeMenu();
    render();
  };

  const bindActionModal = (rootEl) => {
    const closeActionModal = () => { closeMenu(); render(); };
    $$('[data-action="close-modal"]', rootEl).forEach((button) => button.addEventListener('click', closeActionModal));
    rootEl.addEventListener('mousedown', (event) => { if (event.target === rootEl) closeActionModal(); });
    $('[data-project-form]', rootEl)?.addEventListener('submit', (event) => {
      event.preventDefault();
      const name = $('[data-project-name]', rootEl).value.trim();
      const folders = [...(state.draftFolders || [])];
      const error = $('[data-project-error]', rootEl);
      if (!name || !folders.length) {
        if (error) error.textContent = !name ? 'Give this project a title.' : 'Select at least one folder.';
        return;
      }
      if (state.modal === 'project-edit') {
        const editId = state.editProjectId;
        state.modal = null;
        state.modalAnchor = null;
        state.editProjectId = null;
        const project = state.projects.find((item) => item.id === editId);
        if (project) {
          const oldName = project.name;
          project.name = name;
          project.folders = folders;
          projectSessions(project.id).forEach((session) => {
            if (session.title === `New chat · ${oldName}`) session.title = `New chat · ${name}`;
          });
          LS.set('oc-clone-projects', state.projects);
          LS.set('oc-clone-sessions', state.sessions);
        }
        render();
        return;
      }
      const project = { id: uid(), name, folders, createdAt: new Date().toISOString() };
      state.projects.push(project);
      LS.set('oc-clone-projects', state.projects);
      state.activeProjectId = project.id;
      state.workspace = 'chat';
      state.draftNew = true;
      state.modal = null;
      state.modalAnchor = null;
      persistWorkspace();
      render();
    });
    const addDraftFolder = (name) => {
      if (!name) return;
      if (!Array.isArray(state.draftFolders)) state.draftFolders = [];
      if (!state.draftFolders.includes(name)) {
        state.draftFolders.push(name);
        render();
      }
    };
    $('[data-action="pick-folder"]', rootEl)?.addEventListener('click', async () => {
      if (window.showDirectoryPicker) {
        try {
          const dir = await window.showDirectoryPicker({ mode: 'read' });
          addDraftFolder(dir.name);
        } catch {}
        return;
      }
      const input = document.createElement('input');
      input.type = 'file';
      input.webkitdirectory = true;
      input.multiple = true;
      input.addEventListener('change', () => {
        [...input.files].forEach((file) => {
          const relative = file.webkitRelativePath || '';
          const root = relative.split('/')[0];
          if (root) addDraftFolder(root);
        });
      });
      input.click();
    });
    $('[data-project-folder-list]', rootEl)?.addEventListener('click', (event) => {
      const button = event.target.closest('[data-remove-folder]');
      if (!button) return;
      const index = Number(button.dataset.removeFolder);
      if (Array.isArray(state.draftFolders) && index >= 0 && index < state.draftFolders.length) {
        state.draftFolders.splice(index, 1);
        render();
      }
    });
    $$('[data-menu-action]', rootEl).forEach((button) => button.addEventListener('click', () => {
      const action = button.dataset.menuAction;
      const id = state.menuId;
      if (state.modal === 'chat-menu') {
        if (action === 'rename') { state.modal = 'rename-session'; state.modalAnchor = null; render(); return; }
        if (action === 'archive') { archiveSessionById(id); return; }
        if (action === 'delete') { deleteSessionById(id); return; }
      }
      if (state.modal === 'project-menu') {
        if (action === 'new-chat') {
          const project = state.projects.find((item) => item.id === id);
          if (project) {
            state.activeProjectId = project.id;
            state.workspace = 'chat';
            state.noteId = null;
            state.panel = null;
            state.draftNew = true;
            persistWorkspace();
          }
          closeMenu();
          render();
          return;
        }
        if (action === 'rename') { state.modal = 'project-edit'; state.editProjectId = id; state.modalAnchor = null; state.draftFolders = null; render(); return; }
        if (action === 'delete') { deleteProjectById(id); return; }
      }
      closeMenu();
      render();
    }));
    $('[data-rename-form]', rootEl)?.addEventListener('submit', (event) => {
      event.preventDefault();
      const name = $('[data-rename-name]', rootEl).value.trim();
      const error = $('[data-rename-error]', rootEl);
      if (!name) {
        if (error) error.textContent = 'Give this chat a title.';
        return;
      }
      const session = state.sessions.find((item) => item.id === state.menuId);
      const id = state.menuId;
      closeMenu();
      if (session) {
        session.title = name;
        LS.set('oc-clone-sessions', state.sessions);
      }
      if (state.activeId === id) LS.set('oc-clone-active', state.activeId);
      render();
    });
    $$('[data-action="restore-session"]', rootEl).forEach((button) => button.addEventListener('click', () => {
      const index = state.archivedSessions.findIndex((s) => s.id === button.dataset.sessionId);
      if (index < 0) return;
      const [session] = state.archivedSessions.splice(index, 1);
      state.sessions.unshift(session);
      LS.set('oc-clone-sessions', state.sessions);
      LS.set('oc-clone-archived-sessions', state.archivedSessions);
      render();
    }));
    $$('[data-action="delete-archived"]', rootEl).forEach((button) => button.addEventListener('click', () => {
      state.archivedSessions = state.archivedSessions.filter((s) => s.id !== button.dataset.sessionId);
      LS.set('oc-clone-archived-sessions', state.archivedSessions);
      render();
    }));
    const search = $('[data-session-search]', rootEl);
    search?.addEventListener('input', () => {
      const query = search.value.toLowerCase();
      $$('[data-search-session]', rootEl).forEach((button) => { button.hidden = !button.textContent.toLowerCase().includes(query); });
    });
    $$('[data-search-session]', rootEl).forEach((button) => button.addEventListener('click', () => {
      state.activeId = button.dataset.sessionId;
      const session = state.sessions.find((item) => item.id === state.activeId);
      state.activeProjectId = session?.projectId || null;
      state.workspace = 'chat';
      state.draftNew = false;
      state.modal = null;
      LS.set('oc-clone-active', state.activeId);
      persistWorkspace();
      render();
    }));
  };

  const bindNav = (rootEl) => {
    $$('[data-panel-btn]', rootEl).forEach((b) => b.addEventListener('click', () => {
      const label = b.dataset.panelBtn;
      if (state.panel === label) {
        state.panel = null;
      } else {
        state.panel = label;
        state.panelW = PANELS[label][1];
      }
      LS.set('oc-clone-panel', state.panel);
      render();
    }));
  };

  const bindPanel = (rootEl) => {
    $$('[data-trace-filter]', rootEl).forEach((b) => b.addEventListener('click', () => { state.traceFilter = b.dataset.traceFilter; render(); }));
    $$('[data-trace-toggle]', rootEl).forEach((b) => b.addEventListener('click', () => { const id = b.dataset.traceToggle; state.traceExpanded[id] = !state.traceExpanded[id]; render(); }));
    $('[data-action="close-panel"]', rootEl)?.addEventListener('click', () => { if (state.panel === 'Browser') window.ZenoNativeBrowser?.close(); state.panel = null; LS.set('oc-clone-panel', null); render(); });
    $('[data-action="expand-panel"]', rootEl)?.addEventListener('click', () => {
      const dlg = $('[data-panel-root]');
      const current = state.panelW;
      const max = Math.min(900, window.innerWidth - state.sidebarW - 100);
      state.panelW = current > 700 ? PANELS[state.panel][1] : max;
      render();
    });
    const handle = $('[data-action="resize-panel"]', rootEl);
    if (handle) {
      handle.addEventListener('mousedown', (e) => {
        e.preventDefault();
        const startX = e.clientX, startW = state.panelW;
        const move = (ev) => {
          const max = window.innerWidth - state.sidebarW - 120;
          const w = Math.min(max, Math.max(300, startW - (ev.clientX - startX)));
          state.panelW = w;
          LS.set('oc-clone-panel-w', w);
          const aside = $('[data-context-panel]', rootEl);
          aside.style.setProperty('--oc-context-panel-width', w + 'px');
          aside.style.width = 'min(var(--oc-context-panel-width), 100%)';
          if (state.panel === 'Browser') requestAnimationFrame(() => window.ZenoNativeBrowser?.syncBounds());
        };
        const up = () => { document.removeEventListener('mousemove', move); document.removeEventListener('mouseup', up); };
        document.addEventListener('mousemove', move);
        document.addEventListener('mouseup', up);
      });
    }
    const browserForm = $('[data-browser-form]', rootEl);
    if (browserForm) browserForm.addEventListener('submit', (event) => { event.preventDefault(); navigateBrowser($('[data-browser-url]', browserForm).value); });
    $$('[data-browser-action]', rootEl).forEach((button) => button.addEventListener('click', () => {
      const action = button.dataset.browserAction;
      if (window.ZenoNativeBrowser?.available()) {
        if (action === 'back') window.ZenoNativeBrowser.back();
        else if (action === 'forward') window.ZenoNativeBrowser.forward();
        else if (action === 'reload') window.ZenoNativeBrowser.reload();
        else if (action === 'home') navigateBrowser(BROWSER_HOME);
        return;
      }
      if (action === 'back' && state.browserHistoryIndex > 0) { state.browserHistoryIndex -= 1; state.browserUrl = state.browserHistory[state.browserHistoryIndex]; render(); }
      else if (action === 'forward' && state.browserHistoryIndex < state.browserHistory.length - 1) { state.browserHistoryIndex += 1; state.browserUrl = state.browserHistory[state.browserHistoryIndex]; render(); }
      else if (action === 'home') navigateBrowser(BROWSER_HOME);
      else if (action === 'reload') render();
    }));
    if (window.ZenoNativeBrowser?.available()) {
      window.onZenoBrowserState = (url) => { state.browserUrl = String(url || state.browserUrl); const input = $('[data-browser-url]', rootEl); if (input) input.value = state.browserUrl; };
      window.ZenoNativeBrowser.open(state.browserUrl || BROWSER_HOME);
      requestAnimationFrame(window.ZenoNativeBrowser.syncBounds);
    }
    // terminal input
    const tIn = $('[data-terminal-input]', rootEl);
    if (tIn) {
      tIn.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          const cmd = tIn.textContent.trim();
          if (!cmd) return;
          const term = tIn.closest('.min-h-0');
          const line = document.createElement('div');
          line.className = 'text-muted-foreground';
          line.textContent = 'command not found: ' + cmd;
          tIn.parentElement.parentElement.insertBefore(line, tIn.parentElement.nextSibling);
          tIn.textContent = '';
        }
      });
    }
  };

  let placeholderAnimationToken = 0;
  const getComposerText = (input) => {
    const clone = input.cloneNode(true);
    clone.querySelectorAll('.cm-placeholder, .cm-widgetBuffer').forEach((node) => node.remove());
    return clone.textContent.trim();
  };

  const startPlaceholderAnimation = (input) => {
    const token = ++placeholderAnimationToken;
    const placeholder = $('.cm-placeholder', input);
    if (!placeholder) return;
    let phraseIndex = Math.floor(Math.random() * PLACEHOLDER_PHRASES.length);
    let cursor = 0;
    let deleting = false;
    const tick = () => {
      if (token !== placeholderAnimationToken || !input.isConnected) return;
      const hasText = getComposerText(input).length > 0;
      const focused = document.activeElement === input;
      placeholder.style.display = hasText || focused ? 'none' : '';
      if (hasText || focused) return setTimeout(tick, 280);
      const phrase = PLACEHOLDER_PHRASES[phraseIndex];
      if (!deleting) {
        cursor = Math.min(phrase.length, cursor + 1);
        if (cursor === phrase.length) deleting = true;
      } else {
        cursor = Math.max(0, cursor - 1);
        if (cursor === 0) { deleting = false; phraseIndex = (phraseIndex + 1) % PLACEHOLDER_PHRASES.length; }
      }
      placeholder.textContent = phrase.slice(0, cursor);
      setTimeout(tick, deleting && cursor === phrase.length ? 1500 : deleting ? 35 : 58);
    };
    tick();
  };

  let activeDictation = null;
  const MIC_PERMISSION_KEY = 'oc-clone-microphone-permission';

  const microphonePermission = async () => {
    try {
      const status = await navigator.permissions?.query({ name: 'microphone' });
      return status?.state || LS.get(MIC_PERMISSION_KEY, 'prompt');
    } catch {
      return LS.get(MIC_PERMISSION_KEY, 'prompt');
    }
  };

  const requestMicrophone = async () => {
    const permission = await microphonePermission();
    if (permission === 'denied') throw new Error('Permissão do microfone bloqueada nas configurações do navegador.');
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    LS.set(MIC_PERMISSION_KEY, 'granted');
    return stream;
  };

  const blobToBase64 = async (blob) => {
    const bytes = new Uint8Array(await blob.arrayBuffer());
    let binary = '';
    for (let offset = 0; offset < bytes.length; offset += 0x8000) {
      binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
    }
    return btoa(binary);
  };

  let deepgramConfigCache = null;
  const loadDeepgramConfig = async () => {
    if (deepgramConfigCache) return deepgramConfigCache;
    const config = {
      apiKey: window.DEEPGRAM_API_KEY || '',
      language: window.DEEPGRAM_LANGUAGE || 'pt-BR',
      model: window.DEEPGRAM_MODEL || 'nova-3',
    };
    try {
      const response = await fetch('.env', { cache: 'no-store' });
      if (response.ok) {
        const text = await response.text();
        const parse = (name, fallback) => {
          const match = text.match(new RegExp(`^${name}=(.*)$`, 'm'));
          return match ? match[1].trim().replace(/^["']|["']$/g, '') || fallback : fallback;
        };
        config.apiKey = config.apiKey || parse('DEEPGRAM_API_KEY', '');
        config.language = parse('DEEPGRAM_LANGUAGE', config.language);
        config.model = parse('DEEPGRAM_MODEL', config.model);
      }
    } catch {}
    deepgramConfigCache = config;
    return config;
  };

  const deepgramTranscribe = async (blob, langOverride) => {
    const { apiKey, language, model } = await loadDeepgramConfig();
    if (!apiKey) throw new Error('DEEPGRAM_API_KEY não configurada (use o .env, window.DEEPGRAM_API_KEY ou o ZenoAgent.exe).');
    const url = new URL('https://api.deepgram.com/v1/listen');
    url.searchParams.set('model', model);
    url.searchParams.set('language', langOverride || language);
    url.searchParams.set('punctuate', 'true');
    url.searchParams.set('smart_format', 'true');
    const response = await fetch(url, {
      method: 'POST',
      headers: { Authorization: `Token ${apiKey}`, 'Content-Type': blob.type || 'audio/webm' },
      body: blob,
    });
    const payload = await response.json().catch(() => null);
    if (!response.ok) throw new Error(payload?.err_msg || payload?.message || `Falha no Deepgram (${response.status}).`);
    const transcript = payload?.results?.channels?.[0]?.alternatives?.[0]?.transcript?.trim();
    if (!transcript) throw new Error('Nenhuma fala foi reconhecida.');
    return transcript;
  };

  const transcribeAudio = async (blob, langOverride) => {
    const transcribe = window.go?.main?.App?.TranscribeAudio;
    if (typeof transcribe === 'function') {
      return String(await transcribe(await blobToBase64(blob), blob.type || 'audio/webm')).trim();
    }
    return deepgramTranscribe(blob, langOverride);
  };

  const setDictationUi = (rootEl, phase, message = '') => {
    const button = $('[data-action="dictation"]', rootEl);
    const label = $('[data-dictation-label]', rootEl);
    if (!button) return;
    const busy = phase === 'recording' || phase === 'transcribing';
    button.dataset.dictationPhase = phase;
    button.disabled = phase === 'transcribing';
    button.title = phase === 'recording' ? 'Stop dictation' : phase === 'transcribing' ? 'Transcribing' : 'Start dictation';
    button.setAttribute('aria-label', button.title);
    button.innerHTML = busy ? '<span class="oc-spinner" aria-hidden="true"></span>' : icon('oc-mic');
    if (label) {
      label.textContent = message;
      label.classList.toggle('is-visible', Boolean(message));
      label.dataset.dictationPhase = phase;
    }
  };

  const finishDictation = async (runtime) => {
    runtime.stream.getTracks().forEach((track) => track.stop());
    const blob = new Blob(runtime.chunks, { type: runtime.recorder.mimeType || runtime.mimeType || 'audio/webm' });
    try {
      if (!blob.size) throw new Error('Nenhum áudio foi capturado.');
      const text = await transcribeAudio(blob);
      if (!text) throw new Error('Nenhuma fala foi reconhecida.');
      const input = $('[data-composer-input]', runtime.rootEl) || $('[data-composer-input]');
      if (input) {
        const previous = getComposerText(input);
        input.innerText = previous ? `${previous} ${text}` : text;
        input.dispatchEvent(new Event('input', { bubbles: true }));
        input.focus();
      }
      setDictationUi(runtime.rootEl, 'idle');
    } catch (error) {
      setDictationUi(runtime.rootEl, 'error', error.message || 'Falha na transcrição.');
      setTimeout(() => setDictationUi(runtime.rootEl, 'idle'), 3200);
    } finally {
      if (activeDictation === runtime) activeDictation = null;
    }
  };

  const bindDictation = (rootEl) => {
    const button = $('[data-action="dictation"]', rootEl);
    if (!button) return;
    button.addEventListener('click', async () => {
      if (activeDictation?.phase === 'recording') {
        activeDictation.phase = 'transcribing';
        setDictationUi(rootEl, 'transcribing', 'Transcrevendo...');
        activeDictation.recorder.stop();
        return;
      }
      if (activeDictation) return;
      if (!navigator.mediaDevices?.getUserMedia || !window.MediaRecorder) {
        setDictationUi(rootEl, 'error', 'Microfone indisponível neste navegador.');
        setTimeout(() => setDictationUi(rootEl, 'idle'), 3200);
        return;
      }
      try {
        const stream = await requestMicrophone();
        const mimeType = ['audio/webm;codecs=opus', 'audio/webm', 'audio/mp4', 'audio/ogg;codecs=opus'].find((type) => !MediaRecorder.isTypeSupported || MediaRecorder.isTypeSupported(type));
        const recorder = mimeType ? new MediaRecorder(stream, { mimeType }) : new MediaRecorder(stream);
        const runtime = { rootEl, stream, recorder, mimeType, chunks: [], phase: 'recording' };
        activeDictation = runtime;
        recorder.addEventListener('dataavailable', (event) => { if (event.data.size) runtime.chunks.push(event.data); });
        recorder.addEventListener('stop', () => { void finishDictation(runtime); });
        recorder.addEventListener('error', () => { stream.getTracks().forEach((track) => track.stop()); activeDictation = null; setDictationUi(rootEl, 'error', 'Falha ao capturar o áudio.'); setTimeout(() => setDictationUi(rootEl, 'idle'), 3200); });
        recorder.start();
        setDictationUi(rootEl, 'recording', 'Gravando...');
      } catch (error) {
        setDictationUi(rootEl, 'error', error.message || 'Permissão do microfone negada.');
        setTimeout(() => setDictationUi(rootEl, 'idle'), 3200);
      }
    });
  };

  /* ---------- shared: models, delivery, slash helpers, voice mode ---------- */
  const SLASH_LANGS = ['pt-BR', 'en-US', 'es-ES'];

  const blackHoleSend = (payload) => {
    try {
      document.querySelector('.workspace-black-hole')?.contentWindow?.postMessage({ source: 'zeno-black-hole', ...payload }, '*');
    } catch {}
  };

  const setModel = (name) => {
    state.model = name;
    LS.set('oc-clone-model', name);
  };

  const setDesign = (name) => {
    state.design = name;
    LS.set('oc-clone-design', state.design);
  };

  const flashComposerHint = (form, text) => {
    const host = form || document;
    host.querySelector('[data-composer-hint]')?.remove();
    const hint = document.createElement('div');
    hint.dataset.composerHint = 'true';
    hint.className = 'zeno-composer-hint';
    hint.textContent = text;
    (form || document.body).appendChild(hint);
    setTimeout(() => hint.remove(), 2400);
  };

  const deliverUserText = (text) => {
    const clean = String(text || '').trim();
    if (!clean || state.typing) return false;
    if (state.draftNew) createSession(state.activeProjectId || null);
    const s = activeSession();
    if (!s) return false;
    s.messages.push({ id: 'msg_' + uid(), role: 'user', text: clean, time: fmtTime(now()) });
    state.liveTrace = [{ id: 'live_thinking', type: 'thinking', label: 'Thinking', icon: 'oc-brain-ai-3', text: 'Processando a solicitação…', meta: 'live' }];
    state.typing = true;
    render(true);
    setTimeout(() => {
      const reply = mockReply(clean);
      reply.id = 'msg_' + uid();
      s.messages.push(reply);
      state.liveTrace = null;
      state.typing = false;
      LS.set('oc-clone-sessions', state.sessions);
      render(true);
      if (state.voiceMode && voiceRuntime && !voiceRuntime.stopped) {
        voicePushLine(voiceRuntime, 'zeno', reply.text);
        void voiceSpeak(voiceRuntime, reply.text);
      }
    }, 2200 + Math.random() * 800);
    return true;
  };

  /* ---------- voice mode (Deepgram STT + black-hole reaction) ---------- */
  let voiceRuntime = null;

  const tplVoiceOverlay = () => {
    if (!state.voiceMode) return '';
    const runtime = voiceRuntime;
    const status = runtime?.status || 'starting';
    const statusText = {
      starting: 'Ativando microfone…',
      listening: 'Ouvindo — fale com o Zeno',
      transcribing: 'Transcrevendo…',
      speaking: 'Zeno está respondendo…',
      error: runtime?.error || 'Falha no modo voz.',
    }[status] || status;
    const lines = (runtime?.lines || []).slice(-6);
    return `<div class="zeno-voice-layer" data-voice-layer>
      <div class="zeno-voice-panel">
        <div class="zeno-voice-status">${status === 'listening' ? '<span class="zeno-voice-live-dot" aria-hidden="true"></span>' : status === 'speaking' || status === 'transcribing' || status === 'starting' ? '<span class="oc-spinner" aria-hidden="true"></span>' : ''}<span data-voice-status>${esc(statusText)}</span></div>
        <div class="zeno-voice-meter" aria-hidden="true"><div data-voice-meter></div></div>
        <div class="zeno-voice-transcript" data-voice-transcript>${lines.length ? lines.map((line) => line.who === 'you' ? `<div><strong>Você:</strong> ${esc(line.text)}</div>` : line.who === 'zeno' ? `<div><strong>Zeno:</strong> ${esc(line.text)}</div>` : `<div>${esc(line.text)}</div>`).join('') : '<span>Diga alguma coisa…</span>'}</div>
        <div class="zeno-voice-actions">
          <button type="button" data-action="voice-exit" class="zeno-voice-end">${icon('oc-stop', 'remixicon h-4 w-4')}<span>Encerrar conversa</span></button>
        </div>
        <div class="zeno-voice-lang">Deepgram · ${esc(state.voiceLang)} · comando /idioma troca o idioma</div>
      </div>
    </div>`;
  };

  const renderVoice = () => {
    const rootEl = $('[data-voice-root]');
    if (!rootEl) return;
    rootEl.innerHTML = tplVoiceOverlay();
    $('[data-action="voice-exit"]', rootEl)?.addEventListener('click', () => setVoiceMode(false));
  };

  const voicePushLine = (runtime, who, text) => {
    if (!runtime || runtime.stopped) return;
    runtime.lines = [...(runtime.lines || []), { who, text }].slice(-20);
    renderVoice();
  };

  const voiceSetStatus = (runtime, status, error) => {
    if (!runtime || runtime.stopped) return;
    runtime.status = status;
    if (error) runtime.error = error;
    renderVoice();
  };

  const setVoiceMode = (on) => {
    if (on && state.voiceMode && voiceRuntime) return;
    if (!on && !state.voiceMode) return;
    state.voiceMode = on;
    document.body.classList.toggle('zeno-voice-active', on);
    if (on) void startVoiceLoop();
    else stopVoiceLoop();
    render();
  };

  const stopVoiceLoop = () => {
    const runtime = voiceRuntime;
    voiceRuntime = null;
    if (!runtime) { blackHoleSend({ type: 'voice-off' }); return; }
    runtime.stopped = true;
    try { if (runtime.raf) cancelAnimationFrame(runtime.raf); } catch {}
    try { if (runtime.recorder?.state && runtime.recorder.state !== 'inactive') runtime.recorder.stop(); } catch {}
    try { runtime.stream?.getTracks?.().forEach((track) => track.stop()); } catch {}
    try { runtime.ctx?.close?.(); } catch {}
    try { window.speechSynthesis?.cancel?.(); } catch {}
    blackHoleSend({ type: 'voice-off' });
  };

  const voiceMeterLoop = (runtime) => {
    if (!runtime || runtime.stopped) return;
    try {
      runtime.analyser.getByteTimeDomainData(runtime.data);
      let sum = 0;
      for (let i = 0; i < runtime.data.length; i++) {
        const v = (runtime.data[i] - 128) / 128;
        sum += v * v;
      }
      const level = Math.min(1, Math.sqrt(sum / runtime.data.length) * 3.2);
      blackHoleSend({ type: 'voice', level: Number(level.toFixed(3)) });
      const meter = document.querySelector('[data-voice-meter]');
      if (meter) meter.style.width = `${Math.round(level * 100)}%`;
    } catch {}
    runtime.raf = requestAnimationFrame(() => voiceMeterLoop(runtime));
  };

  const transcribeVoiceChunk = async (runtime, blob) => {
    if (!runtime || runtime.stopped || runtime.transcribing || runtime.speaking) return;
    runtime.transcribing = true;
    try {
      const text = (await transcribeAudio(blob, state.voiceLang) || '').trim();
      if (text && !runtime.stopped) {
        voicePushLine(runtime, 'you', text);
        deliverUserText(text);
      }
    } catch (error) {
      const message = error?.message || '';
      if (!/reconhecida|áudio foi capturado|Nenhum áudio/i.test(message) && !runtime.stopped) {
        voicePushLine(runtime, 'sys', message || 'Falha na transcrição.');
      }
    } finally {
      runtime.transcribing = false;
    }
  };

  const withVoiceTimeout = (promise, ms, message) => Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error(message)), ms)),
  ]);

  const startVoiceLoop = async () => {
    stopVoiceLoop();
    const runtime = { status: 'starting', lines: [], transcribing: false, speaking: false, stopped: false };
    voiceRuntime = runtime;
    renderVoice();
    try {
      const stream = await withVoiceTimeout(requestMicrophone(), 20000, 'Tempo esgotado ao acessar o microfone. Verifique a permissão do navegador.');
      if (runtime.stopped) { stream.getTracks().forEach((track) => track.stop()); return; }
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (!AudioCtx) throw new Error('Áudio indisponível neste navegador.');
      const ctx = new AudioCtx();
      const source = ctx.createMediaStreamSource(stream);
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 512;
      source.connect(analyser);
      const mimeType = ['audio/webm;codecs=opus', 'audio/webm', 'audio/mp4', 'audio/ogg;codecs=opus'].find((type) => !window.MediaRecorder?.isTypeSupported || window.MediaRecorder.isTypeSupported(type));
      const recorder = mimeType ? new MediaRecorder(stream, { mimeType }) : new MediaRecorder(stream);
      Object.assign(runtime, { stream, ctx, analyser, data: new Uint8Array(analyser.fftSize), recorder, mimeType });
      recorder.addEventListener('dataavailable', (event) => {
        if (event.data?.size) void transcribeVoiceChunk(runtime, event.data);
      });
      recorder.addEventListener('error', () => voiceSetStatus(runtime, 'error', 'Falha ao capturar o áudio.'));
      recorder.start(4000);
      voiceSetStatus(runtime, 'listening');
      voiceMeterLoop(runtime);
    } catch (error) {
      voiceSetStatus(runtime, 'error', error?.message || 'Microfone indisponível.');
    }
  };

  const voiceSpeak = (runtime, text) => new Promise((resolve) => {
    const done = () => {
      if (runtime) {
        runtime.speaking = false;
        try { runtime.recorder?.resume?.(); } catch {}
        if (!runtime.stopped) voiceSetStatus(runtime, 'listening');
      }
      resolve();
    };
    try {
      const synth = window.speechSynthesis;
      const clean = String(text || '').replace(/[*_`#>\-[\]()]/g, '').trim().slice(0, 500);
      if (!synth || !clean) return done();
      if (runtime) {
        runtime.speaking = true;
        try { runtime.recorder?.pause?.(); } catch {}
        voiceSetStatus(runtime, 'speaking');
      }
      synth.cancel();
      const utterance = new SpeechSynthesisUtterance(clean);
      utterance.lang = state.voiceLang;
      try {
        const voices = synth.getVoices?.() || [];
        const match = voices.find((v) => v.lang === state.voiceLang) || voices.find((v) => (v.lang || '').startsWith((state.voiceLang || '').slice(0, 2)));
        if (match) utterance.voice = match;
      } catch {}
      let finished = false;
      const finish = () => { if (!finished) { finished = true; clearTimeout(safety); done(); } };
      const safety = setTimeout(finish, 30000);
      utterance.onend = finish;
      utterance.onerror = finish;
      synth.speak(utterance);
    } catch { done(); }
  });

  const bindComposer = (rootEl) => {
    const form = $('[data-composer-form]', rootEl);
    if (!form) return;
    const input = $('[data-composer-input]', form);
    if (!input) return;
    /* ---------- slash command menu (/, /model, /thinking, /voz, /idioma, /novo) ---------- */
    let slash = null; // { key, mode, items, index }
    const closeSlash = () => { slash = null; $('[data-slash-menu]', form)?.remove(); };
    const setInputText = (value) => {
      input.innerText = value;
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.focus();
    };
    const clearInput = () => setInputText('');
    const slashCompute = (text) => {
      if (!text.startsWith('/')) return null;
      const sub = text.match(/^\/(model|thinking|m|t)(?:\s+(.*))?$/s);
      if (sub) {
        const kind = sub[1] === 'm' ? 'model' : sub[1] === 't' ? 'thinking' : sub[1];
        const q = (sub[2] || '').trim().toLowerCase();
        if (kind === 'model') {
          return { mode: 'model', items: MODELS.filter((name) => name.toLowerCase().includes(q)).map((name) => ({ kind: 'model-opt', id: name, title: name, desc: name === state.model ? 'Em uso' : 'Trocar para este modelo', icon: 'oc-robot' })) };
        }
        return { mode: 'thinking', items: DESIGN_OPTIONS.filter(([name]) => name.toLowerCase().includes(q)).map(([name, description]) => ({ kind: 'thinking-opt', id: name, title: name, desc: description, icon: 'oc-brain-ai-3' })) };
      }
      const q = text.slice(1).trim().toLowerCase();
      const all = [
        { kind: 'goto-model', id: 'model', title: 'Modelo', desc: `Atual: ${state.model}`, icon: 'oc-robot' },
        { kind: 'goto-thinking', id: 'thinking', title: 'Thinking', desc: `Atual: ${state.design}`, icon: 'oc-brain-ai-3' },
        { kind: 'voice', id: 'voz', title: 'Conversar por voz', desc: `Deepgram · ${state.voiceLang}`, icon: 'oc-pulse' },
        { kind: 'lang', id: 'idioma', title: 'Idioma da voz', desc: `Atual: ${state.voiceLang} · alterna ${SLASH_LANGS.join(', ')}`, icon: 'oc-global' },
        { kind: 'new', id: 'novo', title: 'Novo chat', desc: 'Começar uma conversa em branco', icon: 'oc-chat-new' },
      ];
      return { mode: 'root', items: q ? all.filter((item) => `${item.id} ${item.title}`.toLowerCase().includes(q)) : all };
    };
    const paintSlashSelection = () => {
      if (!slash) return;
      $$('[data-slash-item]', form).forEach((b) => {
        const selected = Number(b.dataset.slashItem) === slash.index;
        b.classList.toggle('is-selected', selected);
        b.setAttribute('aria-selected', String(selected));
      });
    };
    const paintSlash = () => {
      const text = getComposerText(input);
      const computed = slashCompute(text);
      if (!computed || !computed.items.length) { closeSlash(); return; }
      const key = `${computed.mode}|${text}`;
      if (!slash || slash.key !== key) slash = { key, mode: computed.mode, items: computed.items, index: 0 };
      else slash.items = computed.items;
      slash.index = Math.min(Math.max(0, slash.index), slash.items.length - 1);
      $('[data-slash-menu]', form)?.remove();
      const menu = document.createElement('div');
      menu.dataset.slashMenu = 'true';
      menu.innerHTML = `<div class="zeno-slash-list" role="listbox" aria-label="Comandos">${slash.items.map((item, i) => `
        <button type="button" role="option" aria-selected="${i === slash.index}" data-slash-item="${i}" class="zeno-slash-item ${i === slash.index ? 'is-selected' : ''}">
          ${icon(item.icon, 'remixicon h-4 w-4')}
          <span class="min-w-0"><strong>${esc(item.title)}</strong><small>${esc(item.desc)}</small></span>
        </button>`).join('')}</div>`;
      form.appendChild(menu);
      $$('[data-slash-item]', menu).forEach((b) => {
        b.addEventListener('mousedown', (event) => { event.preventDefault(); applySlashItem(slash.items[Number(b.dataset.slashItem)]); });
        b.addEventListener('mousemove', () => {
          const i = Number(b.dataset.slashItem);
          if (slash && slash.index !== i) { slash.index = i; paintSlashSelection(); }
        });
      });
    };
    const applySlashItem = (item) => {
      if (!item) return;
      if (item.kind === 'goto-model') { setInputText('/model '); return; }
      if (item.kind === 'goto-thinking') { setInputText('/thinking '); return; }
      if (item.kind === 'model-opt') { setModel(item.id); closeSlash(); clearInput(); flashComposerHint(form, `Modelo: ${item.id}`); return; }
      if (item.kind === 'thinking-opt') { setDesign(item.id); closeSlash(); clearInput(); flashComposerHint(form, `Thinking: ${item.id}`); return; }
      if (item.kind === 'voice') { closeSlash(); clearInput(); setVoiceMode(true); return; }
      if (item.kind === 'lang') {
        state.voiceLang = SLASH_LANGS[(SLASH_LANGS.indexOf(state.voiceLang) + 1) % SLASH_LANGS.length];
        LS.set('oc-clone-voice-lang', state.voiceLang);
        closeSlash(); clearInput(); flashComposerHint(form, `Idioma da voz: ${state.voiceLang}`);
        return;
      }
      if (item.kind === 'new') {
        closeSlash(); clearInput();
        state.workspace = 'chat'; state.noteId = null; state.panel = null; state.draftNew = true;
        persistWorkspace(); render();
      }
    };
    const applySlashText = (text) => {
      const sub = text.match(/^\/(model|thinking|m|t|voz|voice|idioma|lang|novo|new)\s*(.*)$/si);
      if (!sub) return false;
      const raw = sub[1].toLowerCase();
      const arg = (sub[2] || '').trim();
      const kind = raw === 'm' ? 'model' : raw === 't' ? 'thinking' : raw === 'voice' ? 'voz' : raw === 'lang' ? 'idioma' : raw === 'new' ? 'novo' : raw;
      if (kind === 'model' || kind === 'thinking') {
        const pool = kind === 'model' ? MODELS : DESIGN_OPTIONS.map(([name]) => name);
        const found = pool.find((name) => name.toLowerCase() === arg.toLowerCase()) || (arg ? pool.find((name) => name.toLowerCase().includes(arg.toLowerCase())) : null);
        if (found) {
          if (kind === 'model') setModel(found); else setDesign(found);
          closeSlash(); clearInput(); flashComposerHint(form, `${kind === 'model' ? 'Modelo' : 'Thinking'}: ${found}`);
          return true;
        }
        if (!arg) { setInputText(kind === 'model' ? '/model ' : '/thinking '); return true; }
        return false;
      }
      if (kind === 'voz') { closeSlash(); clearInput(); setVoiceMode(true); return true; }
      if (kind === 'idioma') {
        if (arg) {
          const found = SLASH_LANGS.find((l) => l.toLowerCase() === arg.toLowerCase());
          if (!found) return false;
          state.voiceLang = found;
        } else {
          state.voiceLang = SLASH_LANGS[(SLASH_LANGS.indexOf(state.voiceLang) + 1) % SLASH_LANGS.length];
        }
        LS.set('oc-clone-voice-lang', state.voiceLang);
        closeSlash(); clearInput(); flashComposerHint(form, `Idioma da voz: ${state.voiceLang}`);
        return true;
      }
      if (kind === 'novo') {
        closeSlash(); clearInput();
        state.workspace = 'chat'; state.noteId = null; state.panel = null; state.draftNew = true;
        persistWorkspace(); render();
        return true;
      }
      return false;
    };
    $('[data-action="attach"]', form)?.addEventListener('click', () => {
      const picker = document.createElement('input');
      picker.type = 'file';
      picker.multiple = true;
      picker.style.display = 'none';
      picker.addEventListener('change', () => {
        const names = [...(picker.files || [])].map((f) => f.name).filter(Boolean);
        if (names.length) {
          const previous = getComposerText(input);
          setInputText(`${previous ? `${previous} ` : ''}${names.map((n) => `[anexo: ${n}]`).join(' ')}`);
        }
        picker.remove();
      });
      document.body.appendChild(picker);
      picker.click();
    });
    $('[data-action="voice-mode"]', form)?.addEventListener('click', () => setVoiceMode(true));
    const send = $('[data-action="send"]', form);
    const syncSend = () => {
      const has = getComposerText(input).length > 0;
      send.disabled = !has;
      send.classList.toggle('opacity-30', !has);
    };
    input.addEventListener('input', () => {
      const ph = $('.cm-placeholder', input);
      if (ph) ph.style.display = getComposerText(input) ? 'none' : '';
      syncSend();
      const scroller = input.closest('.cm-scroller');
      if (scroller) { scroller.style.maxHeight = '180px'; scroller.style.overflowY = input.scrollHeight > 180 ? 'auto' : 'hidden'; }
      paintSlash();
    });
    input.addEventListener('keydown', (e) => {
      if (slash?.items?.length) {
        if (e.key === 'ArrowDown') { e.preventDefault(); slash.index = (slash.index + 1) % slash.items.length; paintSlashSelection(); return; }
        if (e.key === 'ArrowUp') { e.preventDefault(); slash.index = (slash.index - 1 + slash.items.length) % slash.items.length; paintSlashSelection(); return; }
        if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); applySlashItem(slash.items[slash.index]); return; }
        if (e.key === 'Escape') { e.preventDefault(); closeSlash(); return; }
      } else if (e.key === 'Escape' && slash) { e.preventDefault(); closeSlash(); return; }
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); submit(); }
    });
    const submit = () => {
      const text = getComposerText(input);
      if (!text || state.typing) return;
      if (text.startsWith('/')) {
        if (text === '/') return;
        if (!applySlashText(text)) flashComposerHint(form, 'Comando desconhecido — digite / para ver as opções');
        return;
      }
      deliverUserText(text);
    };
    form.addEventListener('submit', (e) => { e.preventDefault(); submit(); });
    syncSend();
    startPlaceholderAnimation(input);
    input.addEventListener('focus', () => { const ph = $('.cm-placeholder', input); if (ph) ph.style.display = 'none'; });
    input.addEventListener('blur', () => { if (!getComposerText(input)) startPlaceholderAnimation(input); });
    let draggedQuickIndex = null;
    $$('[data-quick-action]', rootEl).forEach((item) => {
      item.addEventListener('dragstart', (event) => {
        draggedQuickIndex = Number(item.dataset.quickIndex);
        item.classList.add('is-dragging');
        event.dataTransfer.effectAllowed = 'move';
        event.dataTransfer.setData('text/plain', String(draggedQuickIndex));
      });
      item.addEventListener('dragend', () => { draggedQuickIndex = null; item.classList.remove('is-dragging'); });
      item.addEventListener('dragover', (event) => { event.preventDefault(); item.classList.add('is-drag-target'); });
      item.addEventListener('dragleave', () => item.classList.remove('is-drag-target'));
      item.addEventListener('drop', (event) => {
        event.preventDefault();
        item.classList.remove('is-drag-target');
        const from = draggedQuickIndex;
        const to = Number(item.dataset.quickIndex);
        if (from === null || from === to || !Array.isArray(state.quickActions)) return;
        const moved = state.quickActions.splice(from, 1)[0];
        state.quickActions.splice(to, 0, moved);
        LS.set('oc-clone-quick-actions', state.quickActions);
        render();
      });
      item.addEventListener('contextmenu', (event) => {
        event.preventDefault();
        const index = Number(item.dataset.quickIndex);
        const current = getQuickActions()[index];
        const next = window.prompt('Editar mensagem rápida', current?.[1] || '');
        if (next === null || !next.trim()) return;
        if (!Array.isArray(state.quickActions)) state.quickActions = CHIPS.map((item) => [...item]);
        state.quickActions[index][1] = next.trim();
        LS.set('oc-clone-quick-actions', state.quickActions);
        render();
      });
    });
    $$('[data-chip]', rootEl).forEach((c) => c.addEventListener('click', () => {
      const inp = document.querySelector('[data-composer-input]');
      if (inp) { inp.innerText = c.dataset.chip; inp.dispatchEvent(new Event('input')); inp.focus(); }
    }));
    $('[data-action="use-suggestion"]', rootEl)?.addEventListener('click', () => {
      const b = $('[data-action="use-suggestion"]', rootEl);
      const inp = document.querySelector('[data-composer-input]');
      if (inp && b) { inp.innerText = b.querySelector('.truncate').textContent; inp.dispatchEvent(new Event('input')); inp.focus(); }
    });
    $('[data-action="dismiss-suggestion"]', rootEl)?.addEventListener('click', () => {
      const b = $('[data-action="dismiss-suggestion"]', rootEl);
      b?.closest('.mb-1\\.5, [class*="mb-1"]')?.remove();
    });
    bindDictation(rootEl);
  };

  const refreshSettingsContent = (rootEl) => {
    const content = $('[data-settings-content]', rootEl);
    if (content) content.innerHTML = tplSettingsSection();
    $$('[data-settings-section]', rootEl).forEach((x) => {
      x.classList.toggle('bg-interactive-hover', x.dataset.settingsSection === state.settingsSection);
    });
    rootEl.dataset.sig = state.settings ? [state.settingsSection, state.colorMode, state.lightTheme, state.darkTheme].join('|') : '';
  };

  const bindSettings = (rootEl) => {
    $('[data-action="close-settings"]', rootEl)?.addEventListener('click', () => { state.settings = false; render(); });
    rootEl.addEventListener('mousedown', (e) => { if (e.target === rootEl) { state.settings = false; render(); } });
    $$('[data-settings-section]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.settingsSection = b.dataset.settingsSection;
      refreshSettingsContent(rootEl);
    }));
    $$('[data-radio]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.colorMode = b.dataset.radio.toLowerCase();
      LS.set('oc-clone-color-mode', state.colorMode);
      applyTheme();
      refreshSettingsContent(rootEl);
    }));
    $$('[data-select-menu]', rootEl).forEach((b) => b.addEventListener('click', (e) => {
      e.stopPropagation();
      closeMenus();
      const key = b.dataset.selectMenu;
      const title = key === 'light-theme' ? 'Light Theme' : key === 'dark-theme' ? 'Dark Theme' : key === 'lang' ? 'Language' : key === 'timefmt' ? 'Time Format' : key === 'orientation' ? 'Install Orientation' : key === 'startup' ? 'Startup' : 'Options';
      const opts = key === 'light-theme' ? themeList('light').map((t) => ({ id: t.id, name: t.name })) : key === 'dark-theme' ? themeList('dark').map((t) => ({ id: t.id, name: t.name })) : key === 'lang' ? [{ id: 'en', name: 'English' }] : key === 'timefmt' ? [{ id: 'auto', name: 'Auto' }] : key === 'orientation' ? [{ id: 'system', name: 'Follow system' }] : [{ id: 'last', name: 'Continue last session' }];
      const current = key === 'light-theme' ? state.lightTheme : key === 'dark-theme' ? state.darkTheme : opts[0].id;
      const rect = b.getBoundingClientRect();
      const menu = document.createElement('div');
      menu.innerHTML = tplSelectMenu(rect, title, opts, current, key);
      const el = menu.firstElementChild;
      el.style.position = 'fixed';
      el.style.top = Math.min(rect.bottom + 4, window.innerHeight - 200) + 'px';
      el.style.left = Math.max(8, Math.min(rect.left, window.innerWidth - 240)) + 'px';
      document.body.appendChild(el);
      const close = (ev) => { if (!el.contains(ev.target)) { el.remove(); document.removeEventListener('mousedown', close, true); } };
      setTimeout(() => document.addEventListener('mousedown', close, true), 0);
      el.addEventListener('mousedown', (ev) => ev.stopPropagation());
      $$('[data-menu-option]', el).forEach((o) => o.addEventListener('click', () => {
        const val = o.dataset.value;
        if (key === 'light-theme') { state.lightTheme = val; LS.set('oc-clone-light-theme', val); }
        if (key === 'dark-theme') { state.darkTheme = val; LS.set('oc-clone-dark-theme', val); }
        el.remove();
        applyTheme();
        refreshSettingsContent(rootEl);
      }));
    }));
    $('[data-action="reload-themes"]', rootEl)?.addEventListener('click', () => { applyTheme(); });
  };

  const closeMenus = () => $$('[data-menu-popup]').forEach((m) => m.remove());

  /* ---------- mock replies ---------- */
  const DIR_OUTPUT = [
    'Directory: C:\\Users\\GM METELURGICA',
    'Mode     LastWriteTime         Length Name',
    '----     -------------         ------ ----',
    'd-----  29/07/2026  00:30      .agents',
    'd-----  29/03/2026  00:52      .aws',
    'd-----  21/03/2026  17:10      .bun',
    'd-----  03/08/2026  16:13      .buzz',
    'd-----  05/08/2026  16:05      .config',
    'd-----  22/05/2026  09:41      .ollama',
    'd-----  05/08/2026  17:30      Pictures',
    'd-----  12/04/2026  14:02      AppData',
    'd-----  30/06/2026  11:19      Downloads',
    '-a----  05/08/2026  16:05      ntuser.dat',
  ];

  const mockReply = (text) => {
    const t = text.toLowerCase();
    const time = fmtTime(now());
    const dur = (1.2 + Math.random() * 6).toFixed(1) + 's';
    const base = {
      role: 'assistant', time, model: state.model, agent: 'build', duration: dur,
      id: 'msg_' + uid(),
    };
    if (t.includes('array') || t.includes('foreach') || t.includes('map')) {
      return { ...base, thinking: 'The user wants to iterate over an array. A classic for loop with an index is the most direct answer — access each element by position from 0 to length-1.', text: 'Percorrer um array com `for` acessa cada elemento pelo índice, do primeiro ao último.', code: { lang: 'js', lines: [['const', 'keyword'], [' ', null], ['nums', 'variable'], [' =', 'operator'], [' [1', 'number'], [',', 'operator'], [' 2', 'number'], [',', 'operator'], [' 3', 'number'], ['];', 'operator'], ['\n', null], ['for', 'keyword'], [' (', null], ['let', 'keyword'], [' ', null], ['i', 'variable'], [' =', 'operator'], [' 0', 'number'], [';', 'operator'], [' ', null], ['i', 'variable'], [' <', 'operator'], [' ', null], ['nums', 'variable'], ['.length', 'operator'], [';', 'operator'], [' ', null], ['i', 'variable'], ['++', 'operator'], [')', 'operator'], [' ', null], ['console.log', 'function'], ['(', null], ['nums', 'variable'], ['[', 'operator'], ['i', 'variable'], [']', 'operator'], [')', 'operator'], [';', 'operator']] }, recap: `Percorrer arrays com \`for\` clássico usa o índice \`i\` de 0 até \`array.length - 1\`, acessando \`array[i]\` a cada iteração.`, suggestion: 'E como fazer o mesmo com `forEach` ou `map`?' };
    }
    if (t.includes('arquivo') || t.includes('file') || t.includes('list') || t.includes('bash') || t.includes('tool')) {
      return { ...base, thinking: 'The user asks to list files. I should use the shell tool to run a directory listing and report the output back.', tools: [{ title: 'Shell Command', cmd: 'Get-ChildItem -Path "C:\\Users\\GM METELURGICA"', duration: '5.6s', output: DIR_OUTPUT }], text: 'Aqui está a listagem do diretório atual usando **Shell Command**:\n\nO diretório contém pastas de configuração e projetos, incluindo `Pictures`, `AppData`, `Downloads` e `.ollama`.', recap: 'Listagem do diretório via shell: contém .agents, .aws, .bun, .buzz, Pictures, AppData, Downloads e mais.', suggestion: 'Liste também os arquivos do projeto GUI Zeno.' };
    }
    if (t.includes('função') || t.includes('function') || t.includes('python')) {
      return { ...base, thinking: 'They want a function example. A named function with parameters and a return statement is the clearest illustration in JavaScript.', text: 'Uma **função** em JavaScript é um bloco reutilizável de código que recebe entradas (parâmetros) e pode devolver um resultado com `return`.', code: { lang: 'js', lines: [['function', 'keyword'], [' ', null], ['soma', 'variable'], ['(', null], ['a', 'variable'], [',', 'operator'], [' ', null], ['b', 'variable'], [')', 'operator'], [' ', null], ['{', 'operator'], ['\n', null], ['  ', null], ['return', 'keyword'], [' ', null], ['a', 'variable'], [' +', 'operator'], [' ', null], ['b', 'variable'], [';', 'operator'], ['\n', null], ['}', 'operator']] }, recap: 'Funções encapsulam lógica reutilizável com parâmetros e retorno via `return`.', suggestion: 'Explique arrow functions e quando usá-las.' };
    }
    return { ...base, thinking: 'The request is open-ended. I will acknowledge it, restate the core ask briefly, and offer to go deeper with concrete examples.', text: 'Entendi seu pedido: **"' + text.slice(0, 60) + (text.length > 60 ? '…' : '') + '"**. Em resumo, é possível fazer isso de forma direta e eficiente, separando o problema em etapas pequenas e verificando cada resultado parcial antes de avançar. Se quiser, posso detalhar com exemplos de código.', recap: 'Resposta gerada para: "' + text.slice(0, 48) + '". Foco em solução direta com verificação por etapas.', suggestion: 'Pode dar um exemplo prático disso?' };
  };

  /* ---------- palette ---------- */
  const togglePalette = () => {
    let pal = $('#oc-clone-palette');
    if (pal) { pal.remove(); return; }
    pal = document.createElement('div');
    pal.id = 'oc-clone-palette';
    pal.className = 'fixed inset-0 z-[100] flex items-start justify-center pt-[15vh] bg-black/30 backdrop-blur-[2px] oc-backdrop';
    pal.innerHTML = `
      <div class="oc-dialog w-full max-w-xl overflow-hidden rounded-xl border border-border/80 bg-popover text-popover-foreground shadow-2xl">
        <div class="flex items-center gap-2 border-b border-border/70 px-3">
          ${icon('oc-search', 'remixicon h-4 w-4 text-muted-foreground')}
          <input id="oc-clone-palette-input" class="h-11 w-full bg-transparent text-sm outline-none placeholder:text-muted-foreground" placeholder="Type a command or search sessions…" autocomplete="off">
          <kbd class="rounded border border-border px-1.5 py-0.5 text-[10px] text-muted-foreground">ESC</kbd>
        </div>
        <div id="oc-clone-palette-results" class="max-h-[40vh] overflow-y-auto p-1.5">
          ${state.sessions.map((s) => `
          <button type="button" data-pal-session="${s.id}" class="flex w-full items-center gap-2 rounded-md px-2.5 py-2 text-left text-sm text-foreground">
            ${icon('oc-chat-new', 'remixicon h-4 w-4 text-muted-foreground')}
            <span class="truncate">${esc(s.title)}</span>
            ${s.id === state.activeId ? '<span class="ml-auto rounded-full bg-primary/10 px-2 py-0.5 text-[10px] text-primary">active</span>' : ''}
          </button>`).join('')}
          <div class="mt-1 border-t border-border/60 pt-1">
            ${[['oc-palette', 'Toggle theme'], ['oc-settings-3', 'Open settings']].map(([ic, label]) => `
            <button type="button" data-pal-cmd="${label}" class="flex w-full items-center gap-2 rounded-md px-2.5 py-2 text-left text-sm text-muted-foreground hover:text-foreground">
              ${icon(ic, 'remixicon h-4 w-4')}<span>${label}</span>
            </button>`).join('')}
          </div>
        </div>
      </div>`;
    pal.addEventListener('mousedown', (e) => { if (e.target === pal) pal.remove(); });
    document.body.appendChild(pal);
    $('#oc-clone-palette-input').focus();
    $('#oc-clone-palette-input').addEventListener('input', (e) => {
      const q = e.target.value.toLowerCase();
      $$('[data-pal-session]', pal).forEach((b) => { b.style.display = b.textContent.toLowerCase().includes(q) ? '' : 'none'; });
    });
    $('#oc-clone-palette-input').addEventListener('keydown', (e) => {
      if (e.key === 'Escape') pal.remove();
      if (e.key === 'Enter') { const first = $('[data-pal-session]:not([style*="none"])', pal); if (first) first.click(); }
    });
    $$('[data-pal-session]', pal).forEach((b) => b.addEventListener('click', () => {
      state.activeId = b.dataset.palSession;
      state.draftNew = false;
      LS.set('oc-clone-active', state.activeId);
      pal.remove();
      render();
    }));
    $$('[data-pal-cmd]', pal).forEach((b) => b.addEventListener('click', () => {
      const label = b.dataset.palCmd;
      pal.remove();
      if (label === 'Toggle theme') { state.colorMode = state.colorMode === 'dark' ? 'light' : 'dark'; LS.set('oc-clone-color-mode', state.colorMode); applyTheme(); }
      if (label === 'Open settings') { state.settings = true; render(); }
    }));
  };

  /* ---------- boot ---------- */
  const boot = () => {
    applyTheme();
    $('#root').innerHTML = `
      <div class="h-full text-foreground bg-background">
        <div class="main-content-safe-area relative flex h-[100dvh] bg-background">
          <div class="flex flex-col gap-2 text-center sm:text-left sr-only">
            <h2 class="typography-markdown leading-none font-semibold text-foreground">Command Palette</h2>
            <p class="text-muted-foreground typography-ui-label">Search files, sessions, and commands.</p>
          </div>
          ${tplTopbar()}
          <div class="flex flex-1 overflow-hidden">
            <div data-sidebar-root class="flex">${tplSidebar()}</div>
            <div class="relative flex flex-1 min-w-0 flex-col overflow-hidden bg-background">
              <header class="header-safe-area relative z-10 bg-background" style="--padding-scale: 1;" aria-hidden="true"></header>
              <div class="relative flex flex-1 min-h-0 overflow-hidden workspace-stage">
                <iframe class="workspace-black-hole" src="black-hole-threejs.html" title="Zeno Agent background" aria-hidden="true" tabindex="-1"></iframe>
                <div class="workspace-gradient" aria-hidden="true"></div>
                <div class="relative z-10 flex flex-1 min-w-0 flex-col overflow-hidden border-t border-border bg-transparent">
                  <div class="relative flex flex-1 min-h-0 overflow-hidden bg-transparent">
                    <main class="flex-1 overflow-hidden bg-transparent relative"><div id="chat-root" class="h-full"></div></main>
                    <div data-panel-root class="flex min-h-0 bg-transparent">${tplPanel()}</div>
                  </div>
                </div>
                <div class="relative z-20 border-t border-border h-full bg-transparent">
                  <div data-nav-root class="h-full bg-transparent">${tplNav()}</div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
      <div data-settings-root></div>
      <div data-action-modal-root></div>
      <div data-variant-root></div>
      <div data-voice-root></div>`;

    bindGlobal();
    const sb = $('[data-sidebar-root]');
    sb.innerHTML = tplSidebar();
    bindSidebar(sb);
    const nav = $('[data-nav-root]');
    nav.innerHTML = tplNav();
    bindNav(nav);
    const panel = $('[data-panel-root]');
    panel.innerHTML = tplPanel();
    bindPanel(panel);
    const chat = $('#chat-root');
    chat.innerHTML = tplChat();
    bindChat(chat);
    bindComposer(chat);
    const scroller = $('[data-scrollbar="chat"]', chat);
    if (scroller) scroller.scrollTop = scroller.scrollHeight;

    const splash = $('#initial-loading');
    setTimeout(() => { if (splash) { splash.classList.add('fade-out'); setTimeout(() => splash.remove(), 400); } }, 350);
  };

  const bindGlobal = () => {
    $('[data-topbar-toggle]')?.addEventListener('click', () => {
      state.sidebarOpen = !state.sidebarOpen;
      refreshSidebar();
    });
    document.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        togglePalette();
      }
      if ((e.ctrlKey || e.metaKey) && e.key === ',') {
        e.preventDefault();
        state.settings = !state.settings;
        render();
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'n') {
        e.preventDefault();
        state.workspace = 'chat';
        state.noteId = null;
        state.panel = null;
        state.draftNew = true;
        persistWorkspace();
        render();
      }
      if (e.key === 'Escape') {
        if (state.voiceMode) { setVoiceMode(false); return; }
        closeMenus();
        const variantRoot = $('[data-variant-root]');
        if (variantRoot?.firstElementChild) variantRoot.innerHTML = '';
        if (state.noteId) { state.noteId = null; render(); return; }
        if (state.modal) { state.modal = null; state.modalAnchor = null; render(); return; }
        if (state.settings) { state.settings = false; render(); }
      }
    });
  };

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
