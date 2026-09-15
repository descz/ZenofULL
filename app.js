/* OpenChamber UI — static clone (no server, no build). Vanilla JS. */
(() => {
  'use strict';

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  const now = () => new Date();
  const fmtTime = (d) => {
    const f = state.timeFormat || 'auto';
    if (f === '12h') return d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', hour12: true });
    if (f === '24h') return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false });
    return d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  };
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

  /* ---------- servidor C (proxy + config) ---------- */
  const API_BASE = (location.protocol === 'file:') ? 'http://localhost:8080' : '';
  const api = {
    async postConfig(cfg) {
      try { await fetch(`${API_BASE}/api/config`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cfg) }); } catch {}
    },
  };

  /* ---------- ZenoC backend (agente real em C11) ----------
     Toda comunicação passa pelo servidor local (/api/zenoc/*). Quando o
     backend não responde (file:// sem servidor, build antigo), a UI segue
     no modo simulado sem quebrar nenhuma tela. */
  const ZenoBackend = {
    ok: false,
    status: null,
    lastError: '',
    async fetchJSON(path, options) {
      const response = await fetch(`${API_BASE}${path}`, options);
      const payload = await response.json().catch(() => null);
      if (!response.ok) throw new Error(payload?.error || `HTTP ${response.status}`);
      return payload;
    },
    async detect() {
      try {
        this.status = await this.fetchJSON('/api/zenoc/status');
        this.ok = true;
        this.lastError = '';
        return this.status;
      } catch (error) {
        this.ok = false;
        this.status = null;
        this.lastError = error?.message || 'backend indisponível';
        return null;
      }
    },
    models(refresh = false) {
      return this.fetchJSON(`/api/zenoc/models${refresh ? '?refresh=1' : ''}`);
    },
    tools() {
      return this.fetchJSON('/api/zenoc/tools');
    },
    skills() {
      return this.fetchJSON('/api/zenoc/skills');
    },
    saveConfig(config) {
      return this.fetchJSON('/api/zenoc/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(config) });
    },
    notes() {
      return this.fetchJSON('/api/zenoc/memory/notes');
    },
    addNote(note) {
      return this.fetchJSON('/api/zenoc/memory/notes', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(note) });
    },
    updateNote(note) {
      return this.fetchJSON('/api/zenoc/memory/notes/update', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(note) });
    },
    deleteNote(id) {
      return this.fetchJSON('/api/zenoc/memory/notes/delete', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ id }) });
    },
    links() {
      return this.fetchJSON('/api/zenoc/memory/links');
    },
    addLink(from, to, label = '') {
      return this.fetchJSON('/api/zenoc/memory/links', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ from, to, label }) });
    },
    cancel() {
      return this.fetchJSON('/api/zenoc/cancel', { method: 'POST' });
    },
    /* Chat com streaming SSE: cada evento vira uma chamada de onEvent. */
    async chat(payload, onEvent, signal) {
      const response = await fetch(`${API_BASE}/api/zenoc/chat`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
        signal,
      });
      if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`);
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = '';
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let boundary = buffer.indexOf('\n\n');
        while (boundary >= 0) {
          const frame = buffer.slice(0, boundary);
          buffer = buffer.slice(boundary + 2);
          for (const line of frame.split('\n')) {
            if (!line.startsWith('data:')) continue;
            const raw = line.slice(5).trim();
            if (!raw) continue;
            try { onEvent(JSON.parse(raw)); } catch {}
          }
          boundary = buffer.indexOf('\n\n');
        }
      }
    },
  };

  /* Carrega config do servidor ANTES do state ser inicializado (sincrono, sem reload).
     Só aplica se houver conteúdo real; falha silenciosa em file:// ou sem servidor. */
  try {
    const xhr = new XMLHttpRequest();
    xhr.open('GET', `${API_BASE}/api/config`, false);
    xhr.send(null);
    if (xhr.status === 200) {
      const srv = JSON.parse(xhr.responseText);
      if (srv && typeof srv === 'object' && Object.keys(srv).length) {
        Object.keys(srv).forEach((k) => { try { localStorage.setItem(k, JSON.stringify(srv[k])); } catch {} });
      }
    }
  } catch {}

  /* Persiste mudancias relevantes no servidor */
  let _configDebounce = null;
  const persistServer = () => {
    clearTimeout(_configDebounce);
    _configDebounce = setTimeout(() => {
      const payload = {};
      for (let i = 0; i < localStorage.length; i++) {
        const k = localStorage.key(i);
        if (k && k.startsWith('oc-clone-')) {
          try { payload[k] = JSON.parse(localStorage.getItem(k)); } catch { payload[k] = localStorage.getItem(k); }
        }
      }
      api.postConfig(payload);
    }, 800);
  };
  const origSet = LS.set;
  LS.set = (k, v) => { origSet(k, v); if (k.startsWith('oc-clone-')) persistServer(); };

  /* ---------- mock data ---------- */
  const SEED_SESSIONS = [
    {
      id: 'ses_clone_1', title: 'Teste Clone', active: true,
      createdAt: new Date(Date.now() - 19 * 60000).toISOString(),
      messages: [
        { role: 'user', text: 'Explique o que é um for loop em JavaScript com um exemplo curto de código. Uma linha só.', time: '4:33 PM' },
        {
          role: 'assistant', time: '4:33 PM', model: 'DeepSeek V4 Pro', agent: 'build', duration: '0:11',
          text: '**For loop** em JavaScript repete um bloco de código enquanto uma condição for verdadeira, com inicialização, condição e incremento.',
          code: { lang: 'js', lines: [['for', 'keyword'], [' (', null], ['let', 'keyword'], [' ', null], ['i', 'variable'], [' =', 'operator'], [' 0', 'number'], [';', 'operator'], [' ', null], ['i', 'variable'], [' <', 'operator'], [' 5', 'number'], [';', 'operator'], [' ', null], ['i', 'variable'], ['++', 'operator'], [') ', null], ['console.log', 'function'], ['(', null], ['i', 'variable'], [')', null], [';', 'operator'], [' // imprime 0 1 2 3 4', 'comment']] },
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

  let MODELS = ['DeepSeek V4 Pro', 'gpt-4o-mini', 'claude-sonnet-4-5', 'gemini-2.5-flash'];
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

  /* ---------- Usage: preço (USD / 1M tokens) e agregação ---------- */
  const USAGE_PRICES = {
    'gpt-4o-mini': [0.15, 0.6], 'gpt-4o': [2.5, 10], 'gpt-4.1': [2, 8], 'gpt-4.1-mini': [0.4, 1.6],
    'gpt-5': [1.25, 10], 'gpt-5-mini': [0.25, 2], 'gpt-5.6-luna': [1.25, 10], 'o3': [2, 8], 'o4-mini': [1.1, 4.4],
    'claude-sonnet-4-5': [3, 15], 'claude-opus-4-1': [15, 75], 'claude-haiku-4-5': [1, 5],
    'gemini-2.5-flash': [0.3, 2.5], 'gemini-2.5-pro': [1.25, 10],
    'deepseek-v4-pro': [0.27, 1.1], 'deepseek-v4-flash': [0.1, 0.4], 'deepseek-v4.1-flash': [0.1, 0.4],
    'deepseek-flash': [0.1, 0.4], 'deepseek-v4-flash-vision-exp': [0.1, 0.4], 'deepseek-chat': [0.27, 1.1], 'deepseek-reasoner': [0.55, 2.2],
    'glm-5.3-flash': [0.14, 0.56], 'glm-5': [0.6, 2.2], 'glm-5.1': [0.5, 2], 'glm-5.2': [0.5, 2], 'glm-5.3': [0.6, 2.2],
    'kimi-k3': [0.6, 2.5], 'kimi-k2.7-code': [0.55, 2.2], 'kimi-k2.6': [0.55, 2.2], 'kimi-k2.5': [0.5, 2], 'kimi-k2': [0.5, 2],
    'minimax-m3': [0.3, 1.2], 'minimax-m2.7': [0.3, 1.2], 'minimax-m2.5': [0.3, 1.2], 'minimax-m2': [0.3, 1.2],
    'qwen3.7-max': [1.2, 6], 'qwen3.8-max': [1.2, 6], 'qwen3.8-flash': [0.22, 0.88],
    'qwen3.7-plus': [0.4, 2], 'qwen3.6-plus': [0.4, 2], 'qwen3.5-plus': [0.4, 2],
    'longcat-2.0': [0.3, 1.2], 'mimo-v2-pro': [0.4, 1.6], 'mimo-v2-omni': [0.4, 1.6], 'mimo-v2.5-pro': [0.4, 1.6], 'mimo-v2.5': [0.35, 1.4],
    'grok-4.5': [3, 15], 'grok-4.6': [3, 15], 'hy3': [0.6, 2.2], 'hy3-preview': [0.6, 2.2], 'hy4-preview': [0.9, 3.6],
    'omen-alpha': [2, 8], 'muse-spark-1.3-contributor': [0.5, 2], 'muse-spark-1.2-contributor': [0.5, 2],
  };
  const usagePrice = (model) => {
    const key = String(model || '').toLowerCase().trim();
    if (USAGE_PRICES[key]) return USAGE_PRICES[key];
    const hit = Object.keys(USAGE_PRICES).find((k) => key.includes(k) || (key.length > 3 && k.includes(key)));
    return hit ? USAGE_PRICES[hit] : [0.5, 1.5];
  };
  const usageCostOf = (model, tin, tout) => {
    const [pin, pout] = usagePrice(model);
    return ((tin || 0) / 1e6) * pin + ((tout || 0) / 1e6) * pout;
  };
  const fmtUSD = (v) => `$${(v || 0) > 0 && (v || 0) < 0.01 ? (v).toFixed(4) : (v || 0).toFixed(2)}`;
  const fmtTokens = (v) => {
    const n = Number(v) || 0;
    if (n >= 1e6) return `${(n / 1e6).toFixed(2)}M`;
    if (n >= 1e3) return `${(n / 1e3).toFixed(1)}k`;
    return String(n);
  };
  const usageAggregate = () => {
    const byModel = new Map();
    const sessions = state.sessions.map((session) => {
      let tokensIn = 0, tokensOut = 0, cost = 0, runs = 0;
      const models = new Set();
      (session.messages || []).forEach((m) => {
        if (m.role !== 'assistant') return;
        const tin = Number(m.tokensIn) || 0;
        const tout = Number(m.tokensOut) || 0;
        if (!tin && !tout) return;
        runs++;
        tokensIn += tin;
        tokensOut += tout;
        const model = m.model || state.model || 'desconhecido';
        models.add(model);
        cost += usageCostOf(model, tin, tout);
        const entry = byModel.get(model) || { model, tokensIn: 0, tokensOut: 0, cost: 0, runs: 0 };
        entry.tokensIn += tin;
        entry.tokensOut += tout;
        entry.cost += usageCostOf(model, tin, tout);
        entry.runs++;
        byModel.set(model, entry);
      });
      return {
        id: session.id, title: session.title || 'Sem título', tokensIn, tokensOut, cost, runs,
        models: [...models], updatedAt: session.updatedAt || session.createdAt || '',
      };
    }).filter((s) => s.runs > 0).sort((a, b) => (b.updatedAt > a.updatedAt ? 1 : -1));
    const models = [...byModel.values()].sort((a, b) => b.cost - a.cost);
    const total = {
      cost: models.reduce((t, m) => t + m.cost, 0),
      tokensIn: models.reduce((t, m) => t + m.tokensIn, 0),
      tokensOut: models.reduce((t, m) => t + m.tokensOut, 0),
      runs: models.reduce((t, m) => t + m.runs, 0),
    };
    return { sessions, models, total };
  };
  const usageDailySeries = () => {
    const days = new Map();
    const key = (ts) => {
      const d = new Date(ts);
      return d.toLocaleDateString([], { day: '2-digit', month: '2-digit' });
    };
    state.sessions.forEach((session) => {
      const fallback = session.updatedAt || session.createdAt;
      (session.messages || []).forEach((m) => {
        if (m.role !== 'assistant') return;
        const tin = Number(m.tokensIn) || 0;
        const tout = Number(m.tokensOut) || 0;
        if (!tin && !tout) return;
        const ts = m.createdAt || fallback;
        if (!ts) return;
        const day = key(ts);
        days.set(day, (days.get(day) || 0) + usageCostOf(m.model || state.model, tin, tout));
      });
    });
    const list = [...days.entries()].map(([day, cost]) => ({ day, cost }));
    /* Preenche os últimos 14 dias (custo zero) para o eixo ficar contínuo. */
    const out = [];
    const today = new Date();
    for (let i = 13; i >= 0; i--) {
      const d = new Date(today.getTime() - i * 86400000);
      const k = d.toLocaleDateString([], { day: '2-digit', month: '2-digit' });
      const hit = list.find((x) => x.day === k);
      out.push({ day: k, cost: hit ? hit.cost : 0 });
    }
    return out;
  };

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
    colorMode: LS.get('oc-clone-color-mode', 'dark'),
    design: LS.get('oc-clone-design', 'Default'),
    model: LS.get('oc-clone-model', 'DeepSeek V4 Pro'),
    voiceMode: false,
    voiceLang: LS.get('oc-clone-voice-lang', (navigator.language || 'pt-BR').startsWith('en') ? 'en-US' : (navigator.language || 'pt-BR').startsWith('es') ? 'es-ES' : 'pt-BR'),
    voiceProvider: LS.get('oc-clone-voice-provider', 'deepgram'),
    voiceName: LS.get('oc-clone-voice-name', 'aura-asteria-en'),
    voiceSttModel: LS.get('oc-clone-voice-stt', 'nova-3'),
    voiceTtsModel: LS.get('oc-clone-voice-tts', 'aura-asteria-en'),
    voiceAutoSpeak: LS.get('oc-clone-voice-autospeak', true),
    appLang: LS.get('oc-clone-app-lang', 'en'),
    timeFormat: LS.get('oc-clone-time-format', 'auto'),
    installOrientation: LS.get('oc-clone-install-orientation', 'system'),
    installAppName: LS.get('oc-clone-install-app-name', 'OpenChamber'),
    settingsChat: LS.get('oc-clone-settings-chat', { fontSize: 'medium', enterBehavior: 'send', showSuggestions: true, compactMode: false }),
    settingsNotif: LS.get('oc-clone-settings-notif', { enabled: true, sound: true, mentionOnly: false, desktop: true }),
    shortcuts: LS.get('oc-clone-shortcuts', { newChat: 'Ctrl+N', palette: 'Ctrl+K', settings: 'Ctrl+,', voice: 'Ctrl+Shift+V' }),
    remotes: LS.get('oc-clone-remotes', []),
    plugins: LS.get('oc-clone-plugins', []),
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
    settingsSection: 'Chat',
    usageChart: LS.get('oc-clone-usage-chart', 'bars'),
    remoteFormOpen: false,
    expandedTools: {},
    traceExpanded: {},
    traceFilter: 'all',
    liveTrace: null,
    quickActions: LS.get('oc-clone-quick-actions', CHIPS),
    browserUrl: 'https://lite.duckduckgo.com/lite/',
    browserHistory: ['https://lite.duckduckgo.com/lite/'],
    browserHistoryIndex: 0,
    modal: null,
    archivedSessions: LS.get('oc-clone-archived-sessions', []),
    projects: LS.get('oc-clone-projects', []),
    modalAnchor: null,
    workspace: LS.get('oc-clone-workspace', 'chat'),
    activeProjectId: LS.get('oc-clone-active-project', null),
    memoryNotes: LS.get('oc-clone-memory-notes', MEMORY_NOTES),
    memoryEdges: LS.get('oc-clone-memory-edges', MEMORY_EDGES),
    /* Config visual da rede de memória (cores por tipo, espaçamento, rótulos) */
    memoryConfig: { mcp: '#4ade80', skill: '#63b3ff', spacing: 1, labels: false, ...LS.get('oc-clone-memory-config', {}) },
    memoryConfigOpen: false,
    memoryLayoutPending: true,
    noteId: null,
    memoryEditor: null,
    chatsCollapsed: LS.get('oc-clone-chats-collapsed', false) === true,
    editProjectId: null,
    /* Integração ZenoC */
    backendOk: false,
    backend: null,
    backendSkills: [],
    liveRun: null,
    modelsLoading: false,
    modelsError: '',
    modelsFetchedAt: 0,
    agentMode: LS.get('oc-clone-agent-mode', 'full'),
    requireApproval: LS.get('oc-clone-require-approval', false),
  };

  if (!['chat', 'memory'].includes(state.workspace)) state.workspace = 'chat';
  /* Normaliza shapes de builds antigas para os novos blocos de settings. */
  if (typeof state.settingsChat !== 'object' || !state.settingsChat) state.settingsChat = { fontSize: 'medium', enterBehavior: 'send', showSuggestions: true, compactMode: false };
  if (typeof state.settingsNotif !== 'object' || !state.settingsNotif) state.settingsNotif = { enabled: true, sound: true, mentionOnly: false, desktop: true };
  state.shortcuts = { newChat: 'Ctrl+N', palette: 'Ctrl+K', settings: 'Ctrl+,', voice: 'Ctrl+Shift+V', ...(typeof state.shortcuts === 'object' && state.shortcuts ? state.shortcuts : {}) };
  if (!Array.isArray(state.remotes)) state.remotes = [];
  if (!Array.isArray(state.plugins)) state.plugins = [];
  if (!['deepgram', 'openai'].includes(state.voiceProvider)) state.voiceProvider = 'deepgram';
  {
    let sessionsTouched = false;
    state.sessions.forEach((session) => {
      if (!session.createdAt) { session.createdAt = new Date().toISOString(); sessionsTouched = true; }
      if (!Array.isArray(session.messages)) { session.messages = []; sessionsTouched = true; }
      /* Garante id estável em toda mensagem: os botões de ação (reverter,
         bifurcar, copiar) localizam a mensagem pelo id. */
      session.messages.forEach((message) => {
        if (!message.id) { message.id = 'msg_' + uid(); sessionsTouched = true; }
      });
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

  /* ---------- integração ZenoC: helpers de UI ---------- */
  const TOOL_TITLES = {
    run_command: 'Shell Command', list_workspace: 'List Files', read_text_file: 'Read File',
    write_text_file: 'Write File', append_text_file: 'Append File', replace_in_file: 'Edit File',
    create_directory: 'Create Folder', search_workspace: 'Search Files', glob_workspace: 'Glob Files',
    start_background_job: 'Background Job', list_background_jobs: 'List Jobs', tail_background_job: 'Read Job',
    cancel_background_job: 'Cancel Job', memory_remember: 'Memory Remember', memory_search: 'Memory Search',
    memory_list: 'Memory List', memory_link: 'Memory Link', http_request: 'HTTP Request', scrape_url: 'Read URL',
    browser_navigate: 'Browse URL', git_status: 'Git Status', git_diff: 'Git Diff', git_log: 'Git Log',
    git_checkpoint: 'Git Checkpoint', run_tests: 'Run Tests', codebase_structure: 'Codebase Scan',
    find_symbol: 'Find Symbol', mcp_call: 'MCP Call', update_plan: 'Update Plan', spawn_subagent: 'Subagent',
    load_skill: 'Load Skill', assert_true: 'Assert', assert_contains: 'Assert', db_query: 'Memory Query',
    db_insert: 'Memory Insert', vision_analyze: 'Vision',
  };
  const prettyTool = (name) => TOOL_TITLES[name] || String(name || 'tool').replace(/[_-]+/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
  const toolDurations = (tool) => {
    if (!tool.startedAt) return '';
    return `${Math.max(0.1, (Date.now() - tool.startedAt) / 1000).toFixed(1)}s`;
  };
  const toolArgsSummary = (args) => {
    if (args == null) return '';
    if (typeof args === 'string') return args;
    const keys = ['command', 'relative_path', 'path', 'query', 'pattern', 'url', 'title', 'tool_name', 'task', 'plan_markdown'];
    for (const key of keys) {
      const value = args[key];
      if (typeof value === 'string' && value.trim()) return value.trim();
      if (typeof value === 'number') return String(value);
    }
    try { return JSON.stringify(args); } catch { return ''; }
  };
  const toolOutputLines = (text) => String(text || '').replace(/\r\n/g, '\n').split('\n').slice(0, 200);

  const NOTE_ACCENTS = { note: '#8b7cff', memory: '#65d7c1', mcp: '#4ade80', skill: '#63b3ff', decision: '#f0ad67', pattern: '#e785b9', tool_sequence: '#92d36e' };
  const NOTE_KINDS = { note: 'Note', memory: 'Memory', mcp: 'MCP', skill: 'Skill', decision: 'Decision', pattern: 'Pattern', tool_sequence: 'Pattern', auto: 'Memory' };
  const KIND_FROM_TAG = Object.fromEntries(Object.entries(NOTE_KINDS).map(([kind, tag]) => [tag, kind]));
  /* Cor do nó: MCP = verde claro, Skill = azul claro (configuráveis); demais tipos
   * usam o acento padrão ou o cor persistida na nota. */
  const noteAccent = (note) => {
    const kind = note.kind || KIND_FROM_TAG[note.tag] || '';
    if (kind === 'mcp') return state.memoryConfig.mcp || '#4ade80';
    if (kind === 'skill') return state.memoryConfig.skill || '#63b3ff';
    return NOTE_ACCENTS[kind] || note.accent || '#8b7cff';
  };
  const prettyJson = (text) => {
    try { return JSON.stringify(JSON.parse(text), null, 2); } catch { return String(text || ''); }
  };
  const memoryKindLabel = (kind) => (kind === 'mcp' ? 'MCP / JSON' : kind === 'skill' ? 'Skill / Markdown' : kind === 'memory' ? 'Memory / Markdown' : NOTE_KINDS[kind] ? `${NOTE_KINDS[kind]} / Markdown` : 'Markdown');
  const memoryTemplateFor = (kind) => {
    if (kind === 'mcp') return JSON.stringify({ name: 'novo-mcp', transport: 'stdio', command: 'node server.js', tools: [] }, null, 2);
    if (kind === 'skill') return '# Nome da skill\n\n## Quando usar\n- \n\n## Como usar\n1. ';
    if (kind === 'memory') return '# Memória\n\nRegistre aqui o contexto duradouro.';
    return '# Untitled note\n\nStart writing here.';
  };
  const memoryTitleFor = (kind) => ({ mcp: 'Novo MCP', skill: 'Nova skill', memory: 'Nova memória' }[kind] || 'Untitled note');
  const mulberry32 = (seed) => () => {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  /* Layout "rede neural" estilo referência: árvore radial. O hub (nó mais
   * conectado) fica no centro, filhos espalham em anéis por profundidade,
   * dentro do arco angular do pai; componentes isolados ganham setores
   * próprios. Nós fixados (arrastados) preservam a posição. Coordenadas %. */
  const layoutMemoryGraph = (notes, edges, spacing = 1) => {
    const n = notes.length;
    if (!n) return;
    const index = new Map(notes.map((note, i) => [note.id, i]));
    const adj = Array.from({ length: n }, () => []);
    edges.forEach(([a, b]) => {
      const ia = index.get(a), ib = index.get(b);
      if (ia === undefined || ib === undefined || ia === ib) return;
      adj[ia].push(ib); adj[ib].push(ia);
    });
    const W = 1600, H = 900, CX = W / 2, CY = H / 2, R_MAX = Math.min(W, H) / 2 - 64;
    const random = mulberry32(0x5e00 + n * 7);
    /* Componentes conexos (BFS) */
    const visited = new Array(n).fill(false);
    const parent = new Array(n).fill(-1);
    const depth = new Array(n).fill(0);
    const components = [];
    for (let seed = 0; seed < n; seed++) {
      if (visited[seed]) continue;
      const comp = { nodes: [seed], root: seed };
      visited[seed] = true;
      let frontier = [seed];
      while (frontier.length) {
        const next = [];
        for (const v of frontier) for (const w of adj[v]) if (!visited[w]) {
          visited[w] = true; parent[w] = v; depth[w] = depth[v] + 1;
          comp.nodes.push(w); next.push(w);
        }
        frontier = next;
      }
      components.push(comp);
    }
    components.sort((a, b) => b.nodes.length - a.nodes.length);
    /* Raio por anel: profundidade efetiva limitada (cadeias longas dobram
     * para dentro dos anéis visuais) + fit de arco por contagem. */
    const DMAX = Math.max(4, Math.min(7, Math.ceil(Math.sqrt(n) * 1.1)));
    const dEffOf = (d) => Math.min(d, DMAX);
    const depthCount = new Array(DMAX + 1).fill(0);
    depth.forEach((d) => { depthCount[dEffOf(d)] += 1; });
    const ringRadius = [0];
    for (let d = 1; d <= DMAX; d++) {
      const fit = (depthCount[d] * 46) / (2 * Math.PI);
      ringRadius[d] = Math.max(fit, R_MAX * 0.94 * Math.pow(d / DMAX, 0.75), ringRadius[d - 1] + 26);
    }
    if (ringRadius[DMAX] > R_MAX * 0.98) {
      const kScale = (R_MAX * 0.94) / ringRadius[DMAX];
      for (let d = 1; d <= DMAX; d++) ringRadius[d] *= kScale;
    }
    const angleOf = new Array(n).fill(0), radiusOf = new Array(n).fill(0);
    /* Folhas por subárvore distribuem os arcos */
    const leafOf = (v) => {
      const kids = adj[v].filter((w) => parent[w] === v);
      const value = kids.length ? kids.reduce((sum, w) => sum + leafOf(w), 0) : 1;
      return value;
    };
    const assignTree = (root, a0, a1, rStart) => {
      const kids = adj[root].filter((w) => parent[w] === root);
      if (!kids.length) return;
      const total = kids.reduce((sum, w) => sum + leafOf(w), 0);
      const rNext = Math.min(R_MAX, ringRadius[dEffOf(depth[kids[0]])]);
      let cursor = a0;
      kids.forEach((w) => {
        const span = (a1 - a0) * (leafOf(w) / (total || 1));
        angleOf[w] = cursor + span / 2;
        radiusOf[w] = rNext;
        assignTree(w, cursor, cursor + span, rNext);
        cursor += span;
      });
    };
    /* Orçamento angular por componente, ponderado por sqrt(tamanho) */
    const weights = components.map((comp) => Math.sqrt(comp.nodes.length));
    const weightTotal = weights.reduce((a, b) => a + b, 0) || 1;
    let sectorStart = random() * Math.PI * 2;
    const golden = Math.PI * (3 - Math.sqrt(5));
    let looseIndex = 0;
    components.forEach((comp, ci) => {
      const span = (Math.PI * 2) * (weights[ci] / weightTotal);
      const hub = comp.root;
      const compDepth = Math.max(...comp.nodes.map((v) => dEffOf(depth[v])));
      if (ci === 0) {
        angleOf[hub] = sectorStart + span / 2; radiusOf[hub] = 0;
      } else {
        /* Componentes satélites: hub num anel interno, filhos irradiam */
        angleOf[hub] = sectorStart + span / 2;
        radiusOf[hub] = compDepth > 0 ? ringRadius[Math.min(1, DMAX)] : 0;
      }
      if (compDepth > 0) assignTree(hub, sectorStart, sectorStart + span, radiusOf[hub]);
      /* Nós sem aresta (avulsos, inclusive componentes de tamanho 1):
       * anel externo com espaçamento áureo */
      const isSolo = comp.nodes.length === 1;
      comp.nodes.forEach((v) => {
        if (!isSolo && (v === hub || parent[v] >= 0)) return;
        angleOf[v] = golden * (looseIndex++ + random() * 0.4);
        radiusOf[v] = R_MAX * (0.8 + random() * 0.15);
      });
      sectorStart += span;
    });
    notes.forEach((note, i) => {
      if (note.pinned && typeof note.x === 'number' && typeof note.y === 'number') return;
      note.x = Math.max(6, Math.min(94, (CX + Math.cos(angleOf[i]) * radiusOf[i]) / 16));
      note.y = Math.max(9, Math.min(91, (CY + Math.sin(angleOf[i]) * radiusOf[i]) / 9));
    });
    /* Relaxamento tangencial: o raio de cada nó vem da árvore radial (nuvem
     * redonda garantida); as forças só ajustam o ângulo para destancar. */
    const K = 120 * spacing;
    const isFixed = notes.map((note) => !!note.pinned);
    const posAngle = angleOf.slice();
    const posRadius = radiusOf.slice();
    for (let step = 0; step < 46; step++) {
      const temp = 0.12 * (1 - step / 46) + 0.01;
      const tang = new Array(n).fill(0);
      for (let i = 0; i < n; i++) {
        const r = posRadius[i];
        if (r <= 1 || isFixed[i]) continue;
        const cx = CX + Math.cos(posAngle[i]) * r;
        const cy = CY + Math.sin(posAngle[i]) * r;
        let fx = 0, fy = 0;
        for (let j = 0; j < n; j++) {
          if (j === i) continue;
          const rj = posRadius[j];
          const jx = CX + Math.cos(posAngle[j]) * rj;
          const jy = CY + Math.sin(posAngle[j]) * rj;
          let ddx = jx - cx, ddy = jy - cy;
          let d2 = ddx * ddx + ddy * ddy;
          if (d2 < 150) { ddx = random() - 0.5; ddy = random() - 0.5; d2 = 150; }
          const d = Math.sqrt(d2);
          /* repulsão (afasta de j) */
          fx -= ((K * K) / d) * (ddx / d);
          fy -= ((K * K) / d) * (ddy / d);
        }
        edges.forEach(([a, b]) => {
          const ia = index.get(a), ib = index.get(b);
          if (ia === undefined || ib === undefined || ia === ib) return;
          const me = index.get(notes[i].id);
          const other = ia === me ? ib : (ib === me ? ia : -1);
          if (other < 0 || isFixed[other]) return;
          const ox = CX + Math.cos(posAngle[other]) * posRadius[other];
          const oy = CY + Math.sin(posAngle[other]) * posRadius[other];
          const dx = ox - cx, dy = oy - cy;
          const d = Math.sqrt(dx * dx + dy * dy) || 0.01;
          const f = (d - K * 0.95) * 0.5;
          fx += (dx / d) * f;
          fy += (dy / d) * f;
        });
        /* componente tangencial (ux,uy aponta para fora) */
        const ux = (cx - CX) / r, uy = (cy - CY) / r;
        tang[i] = fx * -uy + fy * ux;
      }
      for (let i = 0; i < n; i++) {
        if (isFixed[i] || posRadius[i] <= 1) continue;
        posAngle[i] += Math.max(-temp, Math.min(temp, tang[i] * 0.0005));
      }
    }
    notes.forEach((note, i) => {
      if (isFixed[i]) return;
      const jitter = 1 + (random() - 0.5) * 0.09;
      const r = posRadius[i] > 1 ? posRadius[i] * jitter : posRadius[i];
      note.x = Math.max(6, Math.min(94, (CX + Math.cos(posAngle[i]) * r) / 16));
      note.y = Math.max(9, Math.min(91, (CY + Math.sin(posAngle[i]) * r) / 9));
    });
    LS.set('oc-clone-memory-notes', state.memoryNotes);
  };
  const graphDegrees = (notes, edges) => {
    const map = new Map();
    edges.forEach(([a, b]) => {
      if (a === b) return;
      map.set(a, (map.get(a) || 0) + 1); map.set(b, (map.get(b) || 0) + 1);
    });
    return map;
  };
  const relTimeFromMs = (ms) => {
    if (!ms) return 'agora';
    return relTime({ updatedAt: new Date(ms).toISOString() }) || 'agora';
  };
  const notePositionFor = (id) => {
    let hash = 7;
    for (const char of String(id)) hash = (hash * 31 + char.charCodeAt(0)) >>> 0;
    const angle = (hash % 360) * Math.PI / 180;
    const ring = 16 + ((hash >> 5) % 26);
    return {
      x: Math.max(5, Math.min(95, 50 + Math.cos(angle) * ring)),
      y: Math.max(8, Math.min(92, 50 + Math.sin(angle) * ring * 0.62)),
    };
  };
  const mapBackendNote = (note, source = 'ZenoC') => ({
    id: String(note.id || 'mem_' + uid()),
    title: String(note.title || 'Nota'),
    kind: NOTE_KINDS[note.kind] ? note.kind : (note.kind || 'memory'),
    tag: NOTE_KINDS[note.kind] || (note.kind ? String(note.kind) : 'Memory'),
    excerpt: String(note.content || '').replace(/[`*_#>\n]/g, ' ').trim().slice(0, 116),
    content: String(note.content || ''),
    accent: NOTE_ACCENTS[note.kind] || '#8b7cff',
    scope: note.scope || 'global',
    tags: Array.isArray(note.tags) ? note.tags : [],
    updated: relTimeFromMs(note.created_ms),
    by: source,
    backend: true,
  });
  const mergeBackendNotes = (notes, source = 'ZenoC') => {
    if (!Array.isArray(notes) || !notes.length) return 0;
    const known = new Map(state.memoryNotes.map((note) => [note.id, note]));
    let added = 0;
    notes.forEach((raw) => {
      const mapped = mapBackendNote(raw, source);
      const found = known.get(mapped.id);
      if (found) { Object.assign(found, mapped, { x: found.x, y: found.y, pinned: found.pinned }); }
      else { const pos = notePositionFor(mapped.id); state.memoryNotes.push({ ...mapped, x: pos.x, y: pos.y }); known.set(mapped.id, true); added++; }
    });
    if (added) LS.set('oc-clone-memory-notes', state.memoryNotes);
    return added;
  };
  const mergeBackendLinks = (links) => {
    if (!Array.isArray(links) || !links.length) return 0;
    const known = new Set(state.memoryEdges.map((edge) => edge.join('|')));
    let added = 0;
    links.forEach((link) => {
      const from = String(link.from || '');
      const to = String(link.to || '');
      if (!from || !to || from === to) return;
      const key = [from, to].join('|');
      if (known.has(key)) return;
      known.add(key);
      state.memoryEdges.push([from, to]);
      added++;
    });
    if (added) LS.set('oc-clone-memory-edges', state.memoryEdges);
    return added;
  };
  const applyModels = (payload) => {
    const list = [];
    if (payload && payload.model) list.push(payload.model);
    (payload && Array.isArray(payload.models) ? payload.models : []).forEach((model) => { if (model && !list.includes(model)) list.push(model); });
    if (list.length) {
      MODELS = list;
      state.modelsFetchedAt = payload.updated_ms || Date.now();
      state.modelsError = payload.error || '';
    }
    return list;
  };
  const refreshMemoryFromBackend = async () => {
    if (!state.backendOk) return;
    try {
      const [notes, links, skills] = await Promise.all([ZenoBackend.notes(), ZenoBackend.links(), ZenoBackend.skills()]);
      let changed = mergeBackendNotes(notes);
      if (Array.isArray(skills)) {
        state.backendSkills = skills;
        changed += mergeBackendNotes(skills.map((skill) => ({
          id: `skill-${skill.id}`,
          title: skill.name || skill.id,
          kind: 'skill',
          scope: 'skills',
          tags: skill.category ? [skill.category] : [],
          content: `# ${skill.name || skill.id}\n\n${skill.description || ''}\n\n\`${skill.path || ''}\``,
        })), 'Skills');
      }
      changed += mergeBackendLinks(links);
      if (changed) state.memoryLayoutPending = true;
      if (changed && state.workspace === 'memory') render();
    } catch {}
  };
  const initBackend = async () => {
    const status = await ZenoBackend.detect();
    if (!status) return;
    state.backendOk = true;
    state.backend = status;
    if (status.agent_mode) state.agentMode = status.agent_mode;
    if (status.require_approval !== undefined) state.requireApproval = !!status.require_approval;
    if (typeof status.model === 'string' && status.model) {
      state.model = status.model;
      LS.set('oc-clone-model', state.model);
    }
    try { applyModels(await ZenoBackend.models(false)); } catch {}
    await refreshMemoryFromBackend();
    render();
  };

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
  const BROWSER_HOME = 'https://lite.duckduckgo.com/lite/';
  /* Domínio com TLD (ex.: site.com, foo.net/x, sub.dominio.site:8080/a) */
  const TLD_RE = /^(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z]{2,}(?::\d+)?(?:[\/?#].*)?$/i;
  const LOCAL_RE = /^(?:localhost|(?:\d{1,3}\.){3}\d{1,3})(?::\d+)?(?:[\/?#].*)?$/i;
  const resolveBrowserUrl = (value) => {
    const raw = String(value || '').trim();
    if (!raw || raw === 'about:blank') return BROWSER_HOME;
    if (/^https?:\/\//i.test(raw)) return raw;
    if (LOCAL_RE.test(raw)) return `http://${raw}`;
    if (TLD_RE.test(raw)) return `https://${raw}`;
    return `https://lite.duckduckgo.com/lite/?q=${encodeURIComponent(raw)}`;
  };

  /* ---------- browser: janela/guia dedicada "ZENO AGENT" ---------- */
  /* Abre (ou reutiliza) uma janela/guia nomeada e navega nela. O navegador
     agrupa pela mesma name, entao Go sucessivos reutilizam a mesma guia. */
  const ZENO_WINDOW = 'ZENO AGENT';
  let zenoWin = null;
  const openInZenoWindow = (url) => {
    try {
      if (zenoWin && !zenoWin.closed) {
        zenoWin.location.href = url;
        zenoWin.focus();
      } else {
        /* sem 'noopener': precisamos da referencia para reusar/focar a guia.
           O navegador agrupa pela mesma name, entao Go seguinte reutiliza a guia. */
        zenoWin = window.open(url, ZENO_WINDOW);
      }
      return true;
    } catch { return false; }
  };

  const navigateBrowser = (value) => {
    const url = resolveBrowserUrl(value);
    state.browserUrl = url;
    state.browserHistory = state.browserHistory.slice(0, state.browserHistoryIndex + 1);
    state.browserHistory.push(url);
    state.browserHistoryIndex = state.browserHistory.length - 1;
    openInZenoWindow(url);
    render();
  };

  const tplBrowserPanel = () => {
    const toolbar = `<form data-browser-form class="micro-browser-toolbar flex items-center gap-1.5 border-b border-border/70 p-2"><button type="button" data-browser-action="back" class="micro-browser-control" title="Back">${icon('oc-arrow-left', 'remixicon h-3.5 w-3.5')}</button><button type="button" data-browser-action="forward" class="micro-browser-control" title="Forward">${icon('oc-arrow-right', 'remixicon h-3.5 w-3.5')}</button><button type="button" data-browser-action="reload" class="micro-browser-control" title="Reload">${icon('oc-refresh', 'remixicon h-3.5 w-3.5')}</button><button type="button" data-browser-action="home" class="micro-browser-control" title="Home">${icon('oc-home', 'remixicon h-3.5 w-3.5')}</button><div class="micro-browser-address flex min-w-0 flex-1 items-center gap-1.5 rounded-md border px-2">${icon('oc-global', 'remixicon h-3 w-3')}<input data-browser-url value="${esc(state.browserUrl || BROWSER_HOME)}" class="min-w-0 flex-1 bg-transparent py-1.5 text-xs outline-none" placeholder="URL ou pesquisa"></div><button type="submit" class="micro-browser-go rounded-md px-2.5 py-1.5 text-xs font-medium">Go</button></form>`;
    return `<div class="micro-browser flex h-full min-h-0 flex-col">${toolbar}
      <div class="flex h-full min-h-0 flex-1 flex-col items-center justify-center gap-4 p-8 text-center">
        <div class="flex h-14 w-14 items-center justify-center rounded-2xl border border-border/70 bg-[var(--surface-elevated)]">${icon('oc-global', 'remixicon h-7 w-7 text-muted-foreground')}</div>
        <div>
          <h3 class="text-sm font-medium text-foreground">Guia ZENO AGENT</h3>
          <p class="mx-auto mt-1 max-w-[34ch] text-xs leading-relaxed text-muted-foreground">Digite uma URL e clique em Go: o site abre numa guia do seu navegador com o nome <strong>ZENO AGENT</strong>. Os Go seguintes reutilizam a mesma guia.</p>
        </div>
        <button type="button" data-action="open-zeno-window" class="inline-flex items-center justify-center gap-2 rounded-md bg-primary px-4 py-2 text-sm font-medium text-primary-foreground transition-colors hover:opacity-90">${icon('oc-global', 'remixicon h-4 w-4')}Abrir guia ZENO AGENT</button>
        <p class="text-[11px] text-muted-foreground/70">Dica: se o navegador bloquear o pop-up, permita pop-ups para este site.</p>
      </div></div>`;
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
    if (!['system', 'light', 'dark'].includes(state.colorMode)) {
      state.colorMode = LS.get('oc-clone-color-mode', 'dark');
      if (!['system', 'light', 'dark'].includes(state.colorMode)) state.colorMode = 'dark';
    }
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
  /* Estilo Claude Code: label bold + comando inline, sem caixa; output em bloco com borda ao expandir */
  const tplToolRow = (tool, toolKey, phase) => {
    const expanded = !!state.expandedTools[toolKey];
    const typing = phase === 'typing-cmd';
    const shownCmd = typing ? tool.cmd.slice(0, Math.max(6, Math.round(tool.cmd.length * (tool.progress || 0)))) : tool.cmd;
    return `
    <div class="py-1">
      <div class="group/tool flex items-baseline gap-2 cursor-pointer" role="button" tabindex="0" data-tool-key="${toolKey}">
        <span class="flex-shrink-0 text-[13px] font-semibold text-foreground" title="${esc(tool.title)}">${esc(tool.title)}</span>
        <span class="min-w-0 flex-1 truncate text-[13px] leading-6 text-muted-foreground" title="${esc(tool.cmd)}">${esc(shownCmd)}${typing ? '<span class="tool-caret"></span>' : ''}</span>
        ${tool.running ? '<span class="oc-spinner" aria-hidden="true"></span>' : (tool.duration ? `<span class="flex-shrink-0 text-[12px] tabular-nums text-muted-foreground/60">${esc(tool.duration)}</span>` : '')}
      </div>
      ${expanded ? `
      <div class="mt-1.5 rounded-lg border border-border/70 bg-[var(--surface-elevated)] px-3.5 py-3">
        <pre class="whitespace-pre-wrap break-words font-mono text-[12px] leading-relaxed text-foreground/85">$ ${esc(tool.cmd)}\n\n${tool.output.map((l) => mdHighlight(l, 'text')).join('\n')}</pre>
      </div>` : ''}
    </div>`;
  };

  /* Bloco "Pensamento" — raciocínio + ferramentas do agente ZenoC.
     Substitui o antigo resumo "Explorado N leituras, M pesquisas" e usa a
     mesma renderização de tools (tplToolRow) para os passos aparecerem
     exatamente quando o runtime os executa. */
  const tplThinkingBlock = (m) => {
    const key = 'think_' + m.id;
    const streaming = !!m.streaming;
    const expanded = streaming || !!state.expandedTools[key];
    const tools = m.tools || [];
    const running = tools.filter((tool) => tool.running).length;
    const parts = [];
    if (tools.length) parts.push(`${tools.length} ${tools.length === 1 ? 'ferramenta' : 'ferramentas'}`);
    if (m.thinking) parts.push('raciocínio');
    const summary = streaming && !tools.length && !m.thinking
      ? 'Pensando…'
      : parts.join(' · ') || (streaming ? 'Trabalhando…' : 'Raciocínio');
    const thinkingText = m.thinking || '';
    return `
    <div class="py-1">
      <div class="group/tool flex items-center gap-1.5 cursor-pointer" role="button" tabindex="0" data-tool-key="${key}">
        <span class="text-[13px] font-semibold text-foreground">Pensamento</span>
        <span class="text-[13px] text-muted-foreground">${esc(summary)}</span>
        ${streaming ? `<span class="oc-spinner" aria-hidden="true"></span>${running ? `<span class="text-[12px] text-muted-foreground/70">${running} em execução</span>` : ''}` : ''}
        <span class="text-muted-foreground/60 transition-transform duration-150 ${expanded ? 'rotate-90' : ''}">${icon('oc-arrow-right-s', 'remixicon h-3.5 w-3.5')}</span>
      </div>
      ${expanded ? `
      <div class="mt-1.5 pl-0.5">
        ${thinkingText ? `<div class="text-[13px] leading-relaxed text-muted-foreground" style="white-space: pre-wrap;">${renderMarkdown(thinkingText)}</div>` : ''}
        ${tools.length ? `<div class="mt-1.5">${tools.map((tool, index) => tplToolRow(tool, `${m.id}_t${index}`, 'done')).join('')}</div>` : ''}
      </div>` : ''}
    </div>`;
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


  /* ---------- templates: composer ---------- */
  const getQuickActions = () => {
    const actions = state.quickActions;
    const base = Array.isArray(actions) && actions.every((item) => Array.isArray(item) && item.length >= 2)
      ? actions
      : CHIPS;
    const pluginChips = activePluginButtons().map((b) => [b.icon || 'oc-puzzle-2', b.label || b.action || b.id]);
    return [...base, ...pluginChips].slice(0, 18);
  };

  const tplComposer = (big, session) => {
    const suggestion = state.settingsChat.showSuggestions === false ? null : session && session.messages.length ? session.messages[session.messages.length - 1].suggestion : null;
    const project = activeProject();
    const chatInput = () => {
      /* Modo voz: sem pop-up — as ondas aparecem aqui, na caixa do composer. */
      if (state.voiceMode) return `
      <div data-composer-shell="true" class="flex flex-col relative overflow-visible border border-border/80 shadow-[0_4px_16px_-4px_rgb(0_0_0_/_0.12)] focus-within:ring-1 focus-within:ring-primary/50" style="border-radius: var(--radius-xl); background-color: var(--surface-elevated);">
        ${tplVoiceInline()}
      </div>`;
      return `
      <div data-composer-shell="true" class="flex flex-col relative overflow-visible border border-border/80 shadow-[0_4px_16px_-4px_rgb(0_0_0_/_0.12)] focus-within:ring-1 focus-within:ring-primary/50" style="border-radius: var(--radius-xl); background-color: var(--surface-elevated);">
        <div class="relative flex flex-col">
          <div class="overflow-hidden">
            <div class="flex items-center gap-1 px-3 pt-1 flex-wrap relative z-10"></div>
            <div class="relative overflow-hidden">
              <div data-testid="chat-input" data-chat-input="true" class="composer-editor w-full [&_.cm-editor]:h-full min-h-[44px] px-3 relative z-10 pt-2.5 pb-1 typography-markdown md:typography-ui-label">
                <div class="cm-editor ͼ1 ͼ2 ͼ4 ͼp ͼo">
                  <div class="cm-scroller" style="max-height: 180px;">
                    <div spellcheck="false" autocorrect="off" autocapitalize="none" writingsuggestions="false" translate="no" contenteditable="true" style="tab-size: 4;" class="cm-content cm-lineWrapping" role="textbox" aria-multiline="true" aria-placeholder="Digite uma instrução para o Zeno Agent" data-composer-input><div class="cm-line"><img class="cm-widgetBuffer" aria-hidden="true"><span class="cm-placeholder" data-animated-placeholder aria-hidden="true" contenteditable="false" style="pointer-events: none;"></span><br></div></div>
                  </div>
                </div>
              </div>
            </div>
          </div>
          <div class="bg-transparent flex-shrink-0 px-2.5 py-1 flex items-center justify-between gap-1" data-chat-input-footer="true" style="border-bottom-left-radius: var(--radius-xl); border-bottom-right-radius: var(--radius-xl);">
            <div class="flex items-center gap-1">
              <div class="zeno-attach-wrap" data-attach-wrap>
                <button type="button" data-action="attach" class="zeno-icon-btn" title="Adicionar contexto" aria-label="Adicionar contexto" aria-haspopup="menu">${icon('oc-add-circle', 'remixicon h-[18px] w-[18px]')}</button>
                <div class="zeno-attach-menu zeno-attach-radial" role="menu" aria-label="Adicionar contexto">
                  <svg class="zeno-attach-radial-arrows" viewBox="0 0 200 140" aria-hidden="true" focusable="false">
                    <defs>
                      <marker id="zeno-attach-arrowhead" viewBox="0 0 10 10" refX="7.5" refY="5" markerWidth="5.5" markerHeight="5.5" orient="auto-start-reverse">
                        <path d="M0 1.2 8 5 0 8.8" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" />
                      </marker>
                    </defs>
                    <path d="M95.1 130.6 59.2 77.9" marker-end="url(#zeno-attach-arrowhead)" />
                    <path d="M100 129 100 65" marker-end="url(#zeno-attach-arrowhead)" />
                    <path d="M104.9 130.6 140.8 77.9" marker-end="url(#zeno-attach-arrowhead)" />
                  </svg>
                  <button type="button" role="menuitem" class="zeno-attach-node zeno-attach-node--left" data-attach-mode="model" style="--node-x:44px;--node-y:58px" aria-label="Modelo" title="Modelo">${icon('oc-robot', 'remixicon h-[18px] w-[18px]')}<span class="zeno-attach-node-label">Modelo<small class="zeno-attach-node-value">${esc(state.model)}</small></span></button>
                  <button type="button" role="menuitem" class="zeno-attach-node zeno-attach-node--top" data-attach-mode="thinking" style="--node-x:100px;--node-y:40px" aria-label="Thinking" title="Thinking">${icon('oc-brain-ai-3', 'remixicon h-[18px] w-[18px]')}<span class="zeno-attach-node-label">Thinking<small class="zeno-attach-node-value">${esc(state.design)}</small></span></button>
                  <button type="button" role="menuitem" class="zeno-attach-node zeno-attach-node--right" data-attach-mode="files" style="--node-x:156px;--node-y:58px" aria-label="Arquivos" title="Arquivos">${icon('oc-file-add', 'remixicon h-[18px] w-[18px]')}<span class="zeno-attach-node-label">Arquivos<small class="zeno-attach-node-value">Adicionar ao contexto</small></span></button>
                </div>
                <div class="zeno-attach-panel" data-attach-panel hidden></div>
              </div>
            </div>
            <div class="flex items-center gap-1">
              <span data-dictation-label aria-live="polite"></span>
              <button type="button" data-action="voice-mode" class="zeno-icon-btn" title="Conversar por voz" aria-label="Conversar por voz">${icon('oc-pulse', 'remixicon h-[18px] w-[18px]')}</button>
              <button type="button" data-action="dictation" data-dictation-phase="idle" class="zeno-icon-btn" title="Ditar mensagem" aria-label="Ditar mensagem">${icon('oc-mic', 'remixicon h-[18px] w-[18px]')}</button>
              ${state.liveRun
                ? `<button type="button" data-action="cancel-run" class="zeno-icon-btn" aria-label="Parar o agente" title="Parar o agente">${icon('oc-stop', 'remixicon h-[18px] w-[18px]')}</button>`
                : `<button type="submit" data-action="send" class="zeno-icon-btn" aria-label="Send message">${icon('oc-send-plane-2', 'remixicon h-[18px] w-[18px]')}</button>`}
            </div>
          </div>
        </div>
      </div>`;
    };

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
    <div class="group w-full pt-2 pb-0" data-message-id="${m.id || uid()}" data-message-role="user">
      <div class="chat-message-column relative">
        <div class="relative flex justify-end group/user-shell">
          <div class="max-w-[85%]">
            <div class="px-4 py-2 shadow-none border border-primary/5" style="background-color: var(--chat-user-message-bg); border-bottom-right-radius: var(--radius-sm);">
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
            ${(m.attachments && m.attachments.length) ? `<div class="zeno-attachment-list" data-message-id="${m.id || ''}">${m.attachments.map((a, i) => `<button type="button" class="zeno-attachment-chip" data-attachment-index="${i}" title="Abrir ${esc(a.name)}">${icon('oc-file-text', 'remixicon h-3.5 w-3.5')}<span class="zeno-attachment-name">${esc(a.name)}</span><span class="zeno-attachment-size">${esc(a.size || 'md')}</span></button>`).join('')}</div>` : ''}
            <div class="group/user-actions flex h-8 items-start justify-end pt-2">
              <div class="flex items-center justify-end gap-1 translate-x-0 pointer-events-none opacity-0 transition-opacity duration-150 group-hover/message:pointer-events-auto group-hover/message:opacity-100 group-hover/user-actions:pointer-events-auto group-hover/user-actions:opacity-100 group-hover/user-shell:pointer-events-auto group-hover/user-shell:opacity-100">
                <span class="mr-1 flex items-center gap-1 text-sm tabular-nums text-muted-foreground/60" aria-label="Message time: ${m.time || fmtTime(now())}">${icon('oc-time', 'remixicon h-3.5 w-3.5')}<span class="message-footer__label">${m.time || fmtTime(now())}</span></span>
                ${[['oc-arrow-go-back', 'Revert to this message', 'revert'], ['oc-git-branch', 'Fork from this message', 'branch'], ['oc-file-copy', 'Copy message text', 'copy']].map(([i, l, a]) => `
                <button data-slot="tooltip-trigger" data-msg-action="${a}" class="group relative inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-[10px] [corner-shape:squircle] supports-[corner-shape:squircle]:rounded-[50px] typography-ui-label font-medium lowercase tracking-[0.01em] shrink-0 select-none transition-[background-color,border-color,color,opacity] duration-150 ease-out outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 size-9 h-6 w-6 text-muted-foreground bg-transparent hover:text-foreground" type="button" title="${l}" aria-label="${l}">${icon(i, 'remixicon h-3 w-3')}</button>`).join('')}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>`;

  const tplAssistantMsg = (m) => {
    let body = '';
    if (m.thinking || (m.tools && m.tools.length)) body += tplThinkingBlock(m);
    if (m.text) body += renderMarkdown(m.text);
    if (m.error) body += `<div class="zeno-run-error" role="alert">${icon('oc-error-warning', 'remixicon h-4 w-4')}<div><strong>O agente não conseguiu concluir.</strong><span>${esc(m.error)}</span></div></div>`;
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
            <div class="mt-2 mb-1 flex flex-wrap items-center justify-start gap-x-3 gap-y-1.5" style="container: message-footer / inline-size;">
              <div class="flex min-w-0 flex-wrap items-center gap-x-2.5 gap-y-1 text-sm text-muted-foreground/60">
                <span class="truncate">${esc(m.model)}</span>
                <span class="flex items-center gap-1">${icon('oc-ai-agent', 'remixicon h-3.5 w-3.5 flex-shrink-0')}<span class="message-footer__label">${esc(m.agent)}</span></span>
                <span class="text-sm text-muted-foreground/60 tabular-nums flex items-center gap-1" aria-label="Message time: ${m.time}">${icon('oc-time', 'remixicon h-3.5 w-3.5')}<span class="message-footer__label">${m.time}</span></span>
              </div>
              <div class="flex items-center gap-1.5 pointer-events-none opacity-0 transition-opacity duration-150 focus-within:pointer-events-auto focus-within:opacity-100 group-hover/message:pointer-events-auto group-hover/message:opacity-100" data-message-action-group="true">
                ${[['oc-file-copy', 'Copy message text', 'copy'], ['oc-image-download', 'Download as image', 'download-image'], ['oc-booklet', 'Export markdown', 'export-md'], ['oc-chat-new', 'Continue in new session', 'branch']].map(([i, l, a]) => `
                <button data-slot="tooltip-trigger" data-msg-action="${a}" class="group relative inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-[10px] [corner-shape:squircle] supports-[corner-shape:squircle]:rounded-[50px] typography-ui-label font-medium lowercase tracking-[0.01em] shrink-0 select-none transition-[background-color,border-color,color,opacity] duration-150 ease-out outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4 size-9 h-8 w-8 text-muted-foreground bg-transparent hover:text-foreground" type="button" title="${l}" aria-label="${l}">${icon(i, 'remixicon h-3.5 w-3.5')}</button>`).join('')}
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
              <span class="truncate text-sm text-muted-foreground">${esc(state.model)}</span>
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

  /* Nó da rede: bolinha com halo colorido por tipo (MCP verde, Skill azul).
   * O nome NÃO é renderizado por padrão — só dentro do painelzinho de ações
   * (ou globalmente se "Mostrar nomes" estiver ativo na configuração). */
  const tplMemoryNode = (note, degree = 0) => {
    const x = Math.round(note.x * 16), y = Math.round(note.y * 9);
    const r = Math.round((note.root ? 9 : 5.5) + Math.min(9, degree * 1.8));
    const accent = noteAccent(note);
    const showLabel = !!state.memoryConfig.labels;
    const title = note.title.length > 24 ? `${note.title.slice(0, 23)}…` : note.title;
    const panelW = Math.round(Math.max(116, Math.min(236, 34 + title.length * 6.6)));
    const cx = panelW / 2;
    return `<g data-memory-note="${esc(note.id)}" class="memory-node ${note.root ? 'memory-node-root' : ''}" style="--memory-node-accent:${accent}" transform="translate(${x} ${y})" tabindex="0" role="button" aria-label="Open ${esc(note.title)}">
      <circle class="memory-node-halo" r="${r + 8}"></circle>
      <circle cx="0" cy="0" r="${r}" class="memory-node-shape"></circle>
      ${state.memoryConfig.labels ? `<text y="${r + 16}" text-anchor="middle" class="memory-node-label">${esc(title)}</text>` : ''}
      <g class="memory-node-actions" transform="translate(${-cx} ${-r - 62})">
        <text x="${cx}" y="12" text-anchor="middle" class="memory-node-title">${esc(title)}</text>
        <rect width="${panelW}" height="30" y="18" rx="7" class="memory-node-actions-bg"></rect>
        <g data-memory-action="edit" transform="translate(${cx - 30} 33)"><rect x="-12" y="-12" width="24" height="24" fill="transparent"></rect><use href="#oc-edit" x="-7" y="-7" width="14" height="14"></use><title>Edit</title></g>
        <g data-memory-action="view" transform="translate(${cx} 33)"><rect x="-12" y="-12" width="24" height="24" fill="transparent"></rect><use href="#oc-eye" x="-7" y="-7" width="14" height="14"></use><title>View</title></g>
        <g data-memory-action="delete" transform="translate(${cx + 30} 33)"><rect x="-12" y="-12" width="24" height="24" fill="transparent"></rect><use href="#oc-delete-bin" x="-7" y="-7" width="14" height="14"></use><title>Delete</title></g>
      </g>
    </g>`;
  };

  const tplMemoryNoteModal = () => {
    const note = memoryNoteById(state.noteId);
    if (!note) return '';
    const editing = state.memoryEditor?.id === note.id;
    const kind = state.memoryEditor?.kind || note.kind || KIND_FROM_TAG[note.tag] || 'note';
    return `<div class="memory-note-layer" data-note-layer>
      <div class="memory-note-backdrop" data-note-backdrop></div>
      <article role="dialog" aria-modal="true" aria-label="${esc(note.title)}" class="memory-note-dialog oc-dialog">
        <header class="memory-note-header">
          <div class="min-w-0">
            <div class="memory-note-kicker"><span class="memory-node-dot" style="--memory-node-accent: ${noteAccent(note)}"></span>${esc(note.tag)} <span>/</span> ${esc(memoryKindLabel(kind))}</div>
            ${editing ? `<input data-memory-title class="memory-editor-title" value="${esc(state.memoryEditor.title)}" aria-label="Note title">` : `<h2>${esc(note.title)}</h2>`}
          </div>
          <div class="memory-note-actions">
            ${editing ? `<select data-memory-kind class="memory-editor-kind" aria-label="Tipo da memória">${['note', 'memory', 'mcp', 'skill'].map((k) => `<option value="${k}" ${kind === k ? 'selected' : ''}>${NOTE_KINDS[k]}${k === 'mcp' ? ' (JSON)' : k === 'skill' ? ' (MD)' : ''}</option>`).join('')}</select><button type="button" data-action="save-memory-note" class="memory-editor-save">Save</button><button type="button" data-action="discard-memory-note" class="memory-editor-discard">Discard</button>` : `<button type="button" data-action="edit-memory-note" class="memory-icon-button" title="Edit note" aria-label="Edit note">${icon('oc-edit', 'remixicon h-4 w-4')}</button>`}
            <button type="button" data-action="close-note" class="memory-icon-button" title="Close note" aria-label="Close note">${icon('oc-close', 'remixicon h-4 w-4')}</button>
          </div>
        </header>
        <div class="memory-note-scroll">
          <div class="memory-note-content">${editing ? `<textarea data-memory-content class="memory-editor-content ${kind === 'mcp' ? 'is-json' : ''}" aria-label="Note content" spellcheck="false">${esc(state.memoryEditor.content)}</textarea>` : kind === 'mcp' ? `<pre class="memory-json-view">${esc(prettyJson(note.content))}</pre>` : renderMarkdown(note.content)}</div>
          <footer class="memory-note-footer"><span>Captured by ${esc(note.by || 'Zeno Agent')}</span><span>${esc(note.updated)}</span></footer>
        </div>
      </article>
    </div>`;
  };

  const memoryContentForEditor = (kind, content) => {
    if (kind !== 'mcp') return content;
    const parsed = (() => { try { return JSON.parse(content); } catch { return null; } })();
    return parsed ? JSON.stringify(parsed, null, 2) : content;
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
    const cfg = state.memoryConfig;
    if (state.memoryLayoutPending) {
      layoutMemoryGraph(notes, state.memoryEdges, Number(cfg.spacing) || 1);
      state.memoryLayoutPending = false;
    }
    const degrees = graphDegrees(notes, state.memoryEdges);
    const positions = new Map(notes.map((note) => [note.id, note]));
    const edges = state.memoryEdges.map(([from, to], index) => {
      const a = positions.get(from), b = positions.get(to);
      if (!a || !b) return '';
      const ax = a.x * 16, ay = a.y * 9, bx = b.x * 16, by = b.y * 9;
      return `<line data-memory-edge data-from="${esc(from)}" data-to="${esc(to)}" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}" class="memory-edge" style="--memory-edge-delay: ${index * 35}ms"></line>`;
    }).join('');
    const configPopover = `<div class="memory-config-wrap">
      <button type="button" data-action="memory-config" class="zeno-mini-btn" title="Configurar a rede">${icon('oc-settings-3', 'remixicon h-3 w-3')}Config</button>
      ${state.memoryConfigOpen ? `
      <div class="memory-config-popover" data-memory-config>
        <div class="memory-config-row"><span class="memory-config-dot" style="--memory-node-accent:${cfg.mcp}"></span>MCP <input type="color" data-memory-config-color="mcp" value="${esc(cfg.mcp)}"></div>
        <div class="memory-config-row"><span class="memory-config-dot" style="--memory-node-accent:${cfg.skill}"></span>Skill <input type="color" data-memory-config-color="skill" value="${esc(cfg.skill)}"></div>
        <div class="memory-config-row">Espaçamento <input type="range" min="0.7" max="1.9" step="0.1" data-memory-config-range="spacing" value="${esc(String(cfg.spacing))}"></div>
        <div class="memory-config-row"><label class="memory-config-check"><input type="checkbox" data-memory-config-check="labels" ${cfg.labels ? 'checked' : ''}> Mostrar nomes nos nós</label></div>
        <div class="memory-config-hint">MCP é formatado em JSON · Skill em Markdown · clique no nó para ver o nome</div>
        <button type="button" data-action="memory-relayout" class="zeno-mini-btn">Reorganizar rede</button>
      </div>` : ''}
    </div>`;
    return `<div class="memory-view" data-memory-view>
      <header class="memory-overlay-heading"><h1>Zeno Agent Memory</h1><p>Your knowledge, notes and skills connected in one living graph.</p></header>
      <div class="memory-backend-bar">
        <span class="zeno-status-dot ${state.backendOk ? 'is-on' : ''}"></span>
        <span>${state.backendOk ? 'ZenoC conectado' : 'Modo local'}</span>
        <span class="memory-backend-count">${notes.length} nota${notes.length === 1 ? '' : 's'} · ${state.memoryEdges.length} link${state.memoryEdges.length === 1 ? '' : 's'}${state.backendSkills && state.backendSkills.length ? ` · ${state.backendSkills.length} skill${state.backendSkills.length === 1 ? '' : 's'}` : ''}</span>
        <span class="memory-legend"><span class="memory-legend-item"><span class="memory-config-dot" style="--memory-node-accent:${cfg.mcp}"></span>MCP</span><span class="memory-legend-item"><span class="memory-config-dot" style="--memory-node-accent:${cfg.skill}"></span>Skill</span></span>
        <span class="flex-1"></span>
        ${configPopover}
        <button type="button" data-action="memory-refresh" class="zeno-mini-btn" title="Sincronizar com o agente">${icon('oc-refresh', 'remixicon h-3 w-3')}Atualizar</button>
        <button type="button" data-action="memory-new" class="zeno-mini-btn" title="Criar nota">${icon('oc-add', 'remixicon h-3 w-3')}Nova nota</button>
      </div>
      <div class="memory-map-viewport" data-memory-viewport>
        <svg class="memory-map-canvas" data-memory-canvas viewBox="0 0 1600 900" aria-label="Memory knowledge graph">
          <defs><pattern id="memory-grid" width="30" height="30" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="1" class="memory-grid-dot"></circle></pattern></defs>
          <rect width="1600" height="900" fill="url(#memory-grid)" class="memory-grid-surface" aria-hidden="true"></rect>
          <g data-memory-edges>${edges}</g>
          <g data-memory-nodes>${notes.map((note) => tplMemoryNode(note, degrees.get(note.id) || 0)).join('')}</g>
        </svg>
        <div class="memory-map-help">Arraste para mover · Scroll para zoom · Duplo clique abre · Botão direito cria · Direito no nó liga</div>
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
                        ${state.liveRun ? tplAssistantMsg(state.liveRun) : (state.typing ? tplTyping() : '')}
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
      ['Chat', 'oc-chat-ai-3'], ['Models', 'oc-robot'], ['Notifications', 'oc-notification-3'],
      ['Shortcuts', 'oc-command'], ['Voice', 'oc-mic'], ['Usage', 'oc-bar-chart-2'],
    ]],
    ['Workspace', [
      ['Projects', 'oc-folder'], ['Remote Instances', 'oc-server'], ['Plugins', 'oc-puzzle-2'],
    ]],
  ];

  /* Seletor segmentado com ✓ no item escolhido (ex.: color mode, tipo de gráfico). */
  const tplSegGroup = (label, attr, value, options) => `
    <div role="radiogroup" aria-label="${label}" class="zeno-seg-group">
      ${options.map(([val, text, ic]) => `
      <button type="button" role="radio" aria-checked="${value === val}" ${attr}="${val}" class="zeno-seg ${value === val ? 'is-on' : ''}" title="${esc(text)}">
        ${value === val ? icon(ic || 'oc-check', 'remixicon h-3.5 w-3.5') : ''}
        <span>${esc(text)}</span>
      </button>`).join('')}
    </div>`;

  const tplSettingsSelect = (label, value, options, dataMenu) => `
    <div class="flex min-w-0 max-w-[24rem] items-center gap-2">
      <button type="button" tabindex="0" role="combobox" aria-expanded="false" aria-haspopup="listbox" data-slot="select-trigger" data-size="settings" aria-label="${label}" data-select-menu="${dataMenu}" class="border-input flex items-center justify-between gap-2 rounded-md border bg-transparent typography-ui-label whitespace-nowrap shadow-none outline-none text-left focus-visible:outline-none h-8 min-h-8 px-3 w-full min-w-40 max-w-48">
        <span data-slot="select-value">${esc(value)}</span>
        <span aria-hidden="true">${icon('oc-arrow-down-s', 'remixicon size-4 opacity-50')}</span>
      </button>
    </div>`;

  /* ---------- settings: toggles & fields ---------- */
  const tplToggleRow = (label, desc, on, attr) => `
    <button type="button" ${attr} aria-pressed="${on ? 'true' : 'false'}" class="flex w-full items-center justify-between gap-3 rounded-lg border border-border/70 px-3 py-2.5 text-left transition-colors hover:border-border">
      <span class="min-w-0"><span class="block text-sm text-foreground">${esc(label)}</span>${desc ? `<span class="block text-xs text-muted-foreground">${esc(desc)}</span>` : ''}</span>
      <span class="zeno-toggle ${on ? 'is-on' : ''}" aria-hidden="true"><span></span></span>
    </button>`;

  const tplSettingsField = (label, inner) => `
    <div>
      <div class="mb-1.5 typography-meta font-medium text-muted-foreground">${esc(label)}</div>
      ${inner}
    </div>`;

  const tplNativeSelect = (attr, value, options) => `
    <select ${attr} class="h-8 w-full max-w-[24rem] rounded-md border border-border bg-transparent px-2 text-sm text-foreground outline-none" style="background: var(--background);">
      ${options.map(([v, l]) => `<option value="${esc(v)}" ${String(v) === String(value) ? 'selected' : ''}>${esc(l)}</option>`).join('')}
    </select>`;

  const tplSettingsInput = (attr, value, type = 'text', placeholder = '') => `
    <input ${attr} type="${type}" value="${esc(value || '')}" placeholder="${esc(placeholder)}" spellcheck="false" autocomplete="off" class="h-8 w-full max-w-[28rem] rounded-md border border-border bg-transparent px-3 text-sm text-foreground outline-none">`;

  const tplSettingsSection = () => {
    const sec = state.settingsSection;
    if (sec === 'Chat') {
      const c = state.settingsChat;
      const lightSel = themeList('light').find((t) => t.id === state.lightTheme) || { name: 'OpenChamber Mono' };
      const darkSel = themeList('dark').find((t) => t.id === state.darkTheme) || { name: 'OpenChamber Mono' };
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Chat</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Tema, idioma e comportamento do composer e das mensagens.</p>

        <div class="mt-8">
          <h3 class="typography-ui-label font-semibold text-foreground">Aparência</h3>
          <div class="mt-4 space-y-5">
            ${tplSettingsField('Modo de cor', tplSegGroup('Color Mode', 'data-color-mode', state.colorMode, [['system', 'System'], ['light', 'Light'], ['dark', 'Dark']]))}
            <div class="grid max-w-[34rem] grid-cols-1 gap-4 @3xl:grid-cols-2">
              ${tplSettingsField('Tema claro', tplSettingsSelect('Select light theme', lightSel.name, themeList('light'), 'light-theme'))}
              ${tplSettingsField('Tema escuro', tplSettingsSelect('Select dark theme', darkSel.name, themeList('dark'), 'dark-theme'))}
            </div>
            <div class="grid max-w-[34rem] grid-cols-1 gap-4 @3xl:grid-cols-2">
              ${tplSettingsField('Idioma', tplSettingsSelect('Select language', state.appLang === 'pt' ? 'Português' : state.appLang === 'es' ? 'Español' : 'English', [], 'lang'))}
              ${tplSettingsField('Formato de hora', tplSettingsSelect('Select time format', state.timeFormat === '12h' ? '12h' : state.timeFormat === '24h' ? '24h' : 'Auto', [], 'timefmt'))}
            </div>
            <div class="grid max-w-[34rem] grid-cols-1 gap-4 @3xl:grid-cols-2">
              ${tplSettingsField('Nome do app', tplSettingsInput('data-set-field="app.installName"', state.installAppName, 'text', 'OpenChamber'))}
              ${tplSettingsField('Orientação de instalação', tplSettingsSelect('Select orientation', state.installOrientation === 'portrait' ? 'Portrait' : state.installOrientation === 'landscape' ? 'Landscape' : 'Follow system', [], 'orientation'))}
            </div>
            <button type="button" class="inline-flex items-center justify-center gap-2 rounded-md border border-border px-3 h-8 text-sm text-foreground transition-colors" data-action="reload-themes">${icon('oc-refresh', 'remixicon h-3.5 w-3.5')}Recarregar temas</button>
          </div>
        </div>

        <div class="mt-8">
          <h3 class="typography-ui-label font-semibold text-foreground">Conversa</h3>
          <div class="mt-4 max-w-[28rem] space-y-5">
            ${tplSettingsField('Tamanho da fonte', tplNativeSelect('data-set-field="chat.fontSize"', c.fontSize, [['small', 'Pequena'], ['medium', 'Média'], ['large', 'Grande']]))}
            ${tplSettingsField('Comportamento do Enter', tplNativeSelect('data-set-field="chat.enterBehavior"', c.enterBehavior, [['send', 'Enter envia · Shift+Enter quebra linha'], ['newline', 'Enter quebra linha · Ctrl+Enter envia']]))}
            ${tplToggleRow('Sugestões de follow-up', 'Mostra a pílula de sugestão abaixo do composer.', c.showSuggestions, 'data-set-toggle="chat.showSuggestions"')}
            ${tplToggleRow('Modo compacto', 'Mensagens mais densas, menos respiro.', c.compactMode, 'data-set-toggle="chat.compactMode"')}
          </div>
        </div>
      </div>`;
    }
    if (sec === 'Notifications') {
      const n = state.settingsNotif;
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Notifications</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Quando o Zeno deve chamar sua atenção.</p>
        <div class="mt-9 max-w-[28rem] space-y-3.5">
          ${tplToggleRow('Enable notifications', 'Avisar quando uma resposta terminar.', n.enabled, 'data-set-toggle="notif.enabled"')}
          ${tplToggleRow('Sound', 'Tocar um som junto do aviso.', n.sound, 'data-set-toggle="notif.sound"')}
          ${tplToggleRow('Mentions only', 'Só avisar quando a resposta pedir sua ação.', n.mentionOnly, 'data-set-toggle="notif.mentionOnly"')}
          ${tplToggleRow('Desktop banner', 'Usar notificação do sistema operacional.', n.desktop, 'data-set-toggle="notif.desktop"')}
          <button type="button" data-action="notif-test" class="inline-flex items-center justify-center gap-2 rounded-md border border-border px-3 h-8 text-sm text-foreground transition-colors">Testar notificação</button>
        </div>
      </div>`;
    }
    if (sec === 'Shortcuts') {
      const s = state.shortcuts;
      const row = (label, key) => `
        <label class="flex items-center justify-between gap-3 rounded-lg border border-border/70 px-3 py-2.5">
          <span class="text-sm text-foreground">${esc(label)}</span>
          <input data-shortcut="${key}" value="${esc(s[key] || '')}" spellcheck="false" autocomplete="off"
            class="h-7 w-36 rounded-md border border-border bg-transparent px-2 text-right font-mono text-xs text-foreground outline-none">
        </label>`;
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Shortcuts</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Clique no campo e digite a nova combinação.</p>
        <div class="mt-9 max-w-[28rem] space-y-3.5">
          ${row('New chat', 'newChat')}
          ${row('Command palette', 'palette')}
          ${row('Settings', 'settings')}
          ${row('Voice mode', 'voice')}
          <button type="button" data-action="shortcuts-reset" class="inline-flex items-center justify-center gap-2 rounded-md border border-border px-3 h-8 text-sm text-foreground transition-colors">Restaurar padrão</button>
        </div>
      </div>`;
    }
    if (sec === 'Voice') {
      const isDeepgram = state.voiceProvider !== 'openai';
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Voice</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Fala com o Zeno: Deepgram é o padrão, OpenAI é a opção.</p>
        <div class="mt-8 max-w-[28rem] space-y-5">
          ${tplSettingsField('Provider', tplNativeSelect('data-set-field="voice.provider"', state.voiceProvider, [['deepgram', 'Deepgram (padrão)'], ['openai', 'OpenAI']]))}
          ${tplSettingsField('Agent language', tplNativeSelect('data-set-field="voice.lang"', state.voiceLang, [['pt-BR', 'Português (BR)'], ['en-US', 'English (US)'], ['es-ES', 'Español']]))}
          ${tplSettingsField('Voice', tplNativeSelect('data-set-field="voice.name"', state.voiceName, [['aura-asteria-en', 'Aura Asteria (Deepgram)'], ['aura-luna-en', 'Aura Luna (Deepgram)'], ['aura-orion-en', 'Aura Orion (Deepgram)'], ['alloy', 'Alloy (OpenAI)'], ['echo', 'Echo (OpenAI)'], ['shimmer', 'Shimmer (OpenAI)']]))}
          ${tplSettingsField('Speech-to-text model', tplNativeSelect('data-set-field="voice.stt"', state.voiceSttModel, [['nova-3', 'nova-3 (Deepgram)'], ['nova-2', 'nova-2 (Deepgram)'], ['whisper-1', 'whisper-1 (OpenAI)']]))}
          ${tplSettingsField('Text-to-speech model', tplNativeSelect('data-set-field="voice.tts"', state.voiceTtsModel, [['aura-asteria-en', 'Aura (Deepgram)'], ['tts-1', 'tts-1 (OpenAI)'], ['tts-1-hd', 'tts-1-hd (OpenAI)']]))}
          ${tplToggleRow('Auto-speak', 'O agente começa a falar automaticamente ao entrar em voz.', state.voiceAutoSpeak !== false, 'data-set-toggle="voice.autoSpeak"')}
          ${isDeepgram ? tplSettingsField('Deepgram API key', `<input data-set-field="voice.deepgramKey" type="password" value="${esc(LS.get('oc-clone-voice-deepgram-key', ''))}" placeholder="cole sua Deepgram API key…" autocomplete="off"
            class="h-8 w-full max-w-[24rem] rounded-md border border-border bg-transparent px-3 text-sm text-foreground outline-none">`) : ''}
          ${!isDeepgram ? tplSettingsField('OpenAI API key (opcional, só para provider OpenAI)', `<input data-set-field="voice.openaiKey" type="password" value="${esc(LS.get('oc-clone-voice-openai-key', ''))}" placeholder="sk-…" autocomplete="off"
            class="h-8 w-full max-w-[24rem] rounded-md border border-border bg-transparent px-3 text-sm text-foreground outline-none">`) : ''}
          <button type="button" data-action="voice-test" class="inline-flex items-center justify-center gap-2 rounded-md border border-border px-3 h-8 text-sm text-foreground transition-colors">${icon('oc-mic', 'remixicon h-3.5 w-3.5')}Testar voz</button>
        </div>
      </div>`;
    }
    if (sec === 'Models') {
      const backend = state.backend || {};
      const configuredModels = MODELS;
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Models</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Conecte a API OpenAI-compatível usada pelo agente ZenoC. Os modelos ficam disponíveis aqui, no menu /model e no botão de anexos.</p>
        ${state.modelsError ? `<div class="zeno-models-note mt-4">${esc(state.modelsError)}</div>` : ''}
        <div class="mt-8 grid max-w-[34rem] grid-cols-1 gap-5 @3xl:grid-cols-2 @3xl:gap-x-10">
          ${tplSettingsField('Provider', tplSettingsInput('data-models-field="provider"', backend.provider || 'openai', 'text', 'openai'))}
          ${tplSettingsField('Base URL', tplSettingsInput('data-models-field="base_url"', backend.base_url || 'https://api.openai.com/v1', 'text', 'https://api.openai.com/v1'))}
          ${tplSettingsField('API key', tplSettingsInput('data-models-field="api_key"', '', 'password', backend.api_key === 'configured' ? 'já configurada — deixe vazio para manter' : 'sk-…'))}
          ${tplSettingsField('Modelo padrão', tplSettingsInput('data-models-field="model"', backend.model || state.model, 'text', 'gpt-4o-mini'))}
          ${tplSettingsField('Modelos de fallback', tplSettingsInput('data-models-field="fallback_models"', backend.fallback_models || '', 'text', 'modelo-a,modelo-b'))}
          ${tplSettingsField('Workspace do agente', tplSettingsInput('data-models-field="workspace"', backend.workspace || '.', 'text', '.'))}
          ${tplSettingsField('Modo do agente', tplNativeSelect('data-models-field="agent_mode"', state.agentMode, [['full', 'Full (todas as ferramentas)'], ['minimal', 'Minimal (4 ferramentas)']]))}
        </div>
        <div class="mt-5 flex flex-wrap items-center gap-2">
          <button type="button" data-action="models-save" class="zeno-mini-btn">Salvar configuração</button>
          <button type="button" data-action="models-refresh" class="zeno-mini-btn" ${state.modelsLoading ? 'disabled' : ''}>${state.modelsLoading ? 'Buscando…' : 'Buscar modelos da API'}</button>
          <button type="button" data-action="models-test" class="zeno-mini-btn" ${state.modelsLoading ? 'disabled' : ''}>Testar conexão</button>
        </div>
        <div class="mt-8">
          <h3 class="typography-ui-label font-semibold text-foreground">Modelos disponíveis (${configuredModels.length})</h3>
          <div class="mt-3 flex max-w-[34rem] flex-wrap gap-1.5">
            ${configuredModels.map((name) => `<button type="button" data-action="models-pick" data-model="${esc(name)}" class="zeno-model-chip ${name === state.model ? 'is-on' : ''}">${name === state.model ? icon('oc-check', 'remixicon h-3 w-3') : ''}${esc(name)}</button>`).join('') || '<span class="text-xs text-muted-foreground">Nenhum modelo ainda. Informe a API key e clique em Buscar.</span>'}
          </div>
        </div>
      </div>`;
    }
    if (sec === 'Usage') {
      const agg = usageAggregate();
      const chart = state.usageChart || 'bars';
      const t = agg.total;
      const palette = ['#7c5cff', '#58b6ff', '#4ade80', '#fbbf24', '#f472b6', '#38bdf8', '#a78bfa', '#fb923c', '#34d399', '#f87171'];
      const modelColor = (name) => {
        const index = agg.models.findIndex((m) => m.model === name);
        return palette[index >= 0 ? index % palette.length : 0];
      };
      const daily = usageDailySeries();
      const maxDaily = Math.max(...daily.map((d) => d.cost), 1e-6);
      const maxCost = Math.max(...agg.models.map((m) => m.cost), 1e-6);

      const barsSvg = agg.models.length ? `
        <svg viewBox="0 0 420 ${Math.max(agg.models.length * 34 + 8, 42)}" class="zeno-usage-svg" role="img" aria-label="Gasto por modelo">
          ${agg.models.map((m, i) => {
            const y = 10 + i * 34;
            const w = Math.max(2, (m.cost / maxCost) * 240);
            const color = modelColor(m.model);
            return `
            <text x="0" y="${y + 11}" class="zeno-usage-label">${esc(String(m.model).slice(0, 18))}</text>
            <rect x="128" y="${y + 2}" width="240" height="14" rx="7" fill="rgb(255 255 255 / 0.06)"></rect>
            <rect x="128" y="${y + 2}" width="${w.toFixed(1)}" height="14" rx="7" fill="${color}"></rect>
            <text x="378" y="${y + 13}" class="zeno-usage-value">${fmtUSD(m.cost)}</text>`;
          }).join('')}
        </svg>` : '';

      const donutSvg = agg.models.length ? (() => {
        const radius = 52, circumference = 2 * Math.PI * radius;
        let offset = 0;
        const segs = agg.models.map((m) => {
          const frac = m.cost / (t.cost || 1);
          const seg = `<circle cx="70" cy="70" r="${radius}" fill="none" stroke="${modelColor(m.model)}" stroke-width="16" stroke-dasharray="${(frac * circumference).toFixed(2)} ${circumference.toFixed(2)}" stroke-dashoffset="${(-offset * circumference).toFixed(2)}" transform="rotate(-90 70 70)"></circle>`;
          offset += frac;
          return seg;
        }).join('');
        const legend = agg.models.map((m) => `
          <div class="zeno-usage-legend-item">
            <span class="zeno-usage-dot" style="background:${modelColor(m.model)}"></span>
            <span class="min-w-0 flex-1 truncate" title="${esc(m.model)}">${esc(m.model)}</span>
            <span class="zeno-usage-legend-num">${fmtUSD(m.cost)} · ${t.cost > 0 ? Math.round((m.cost / t.cost) * 100) : 0}%</span>
          </div>`).join('');
        return `
        <div class="flex flex-wrap items-center gap-5">
          <svg viewBox="0 0 140 140" class="zeno-usage-donut" role="img" aria-label="Distribuição por modelo">
            <circle cx="70" cy="70" r="${radius}" fill="none" stroke="rgb(255 255 255 / 0.06)" stroke-width="16"></circle>
            ${segs}
            <text x="70" y="66" text-anchor="middle" class="zeno-usage-donut-total">${fmtUSD(t.cost)}</text>
            <text x="70" y="82" text-anchor="middle" class="zeno-usage-donut-sub">total</text>
          </svg>
          <div class="flex min-w-[180px] flex-1 flex-col gap-1.5">${legend}</div>
        </div>`;
      })() : '';

      const daysSvg = daily.length ? `
        <svg viewBox="0 0 420 150" class="zeno-usage-svg" role="img" aria-label="Gasto por dia">
          ${[0.25, 0.5, 0.75].map((f) => `<line x1="34" x2="412" y1="${18 + 100 * (1 - f)}" y2="${18 + 100 * (1 - f)}" stroke="rgb(255 255 255 / 0.07)"></line>`).join('')}
          <polyline fill="none" stroke="#7c5cff" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"
            points="${daily.map((d, i) => `${34 + (i * 378) / Math.max(daily.length - 1, 1)},${(118 - (d.cost / maxDaily) * 100).toFixed(1)}`).join(' ')}"></polyline>
          ${daily.map((d, i) => {
            const x = 34 + (i * 378) / Math.max(daily.length - 1, 1);
            const y = (118 - (d.cost / maxDaily) * 100).toFixed(1);
            return `<circle cx="${x.toFixed(1)}" cy="${y}" r="${d.cost > 0 ? 3 : 1.5}" fill="#7c5cff"><title>${d.day}: ${fmtUSD(d.cost)}</title></circle>`;
          }).join('')}
          ${daily.map((d, i) => i % Math.ceil(daily.length / 7) === 0 ? `<text x="${(34 + (i * 378) / Math.max(daily.length - 1, 1)).toFixed(1)}" y="140" text-anchor="middle" class="zeno-usage-axis">${d.day}</text>` : '').join('')}
        </svg>` : '';

      const chartBody = chart === 'donut' ? donutSvg : chart === 'days' ? daysSvg : barsSvg;
      const sessionsList = agg.sessions.length ? agg.sessions.map((s) => `
        <button type="button" data-usage-session="${esc(s.id)}" class="zeno-usage-row" title="Abrir conversa">
          <span class="min-w-0 flex-1 text-left">
            <span class="zeno-usage-row-title">${esc(s.title)}</span>
            <span class="zeno-usage-row-sub">${s.models.map((m) => esc(m)).join(' · ') || '—'}</span>
          </span>
          <span class="zeno-usage-row-nums">
            <span class="zeno-usage-row-tokens">↑${fmtTokens(s.tokensIn)} ↓${fmtTokens(s.tokensOut)}</span>
            <span class="zeno-usage-row-cost">${fmtUSD(s.cost)}</span>
          </span>
        </button>`).join('') : '<div class="zeno-usage-empty">Nenhum uso registrado ainda. Converse com o agente e os custos aparecem aqui.</div>';

      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Usage</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Gasto por conversa e por modelo. Custo estimado pela tabela de preço por 1M tokens.</p>
        <div class="mt-6 grid max-w-[34rem] grid-cols-2 gap-3 @3xl:grid-cols-4">
          <div class="zeno-usage-card"><span class="zeno-usage-card-num">${fmtUSD(t.cost)}</span><span class="zeno-usage-card-label">gasto total</span></div>
          <div class="zeno-usage-card"><span class="zeno-usage-card-num">${fmtTokens(t.tokensIn + t.tokensOut)}</span><span class="zeno-usage-card-label">tokens</span></div>
          <div class="zeno-usage-card"><span class="zeno-usage-card-num">${agg.sessions.length}</span><span class="zeno-usage-card-label">conversas</span></div>
          <div class="zeno-usage-card"><span class="zeno-usage-card-num">${agg.models.length}</span><span class="zeno-usage-card-label">modelos</span></div>
        </div>
        <div class="mt-7">
          <div class="flex flex-wrap items-center justify-between gap-3">
            <h3 class="typography-ui-label font-semibold text-foreground">Gráficos</h3>
            ${tplSegGroup('Chart type', 'data-usage-chart', chart, [['bars', 'Barras', 'oc-bar-chart-2'], ['donut', 'Rosca', 'oc-donut-chart'], ['days', 'Dias', 'oc-pulse']])}
          </div>
          <div class="zeno-usage-chart mt-3">${chartBody || '<div class="zeno-usage-empty">Sem dados para o gráfico.</div>'}</div>
        </div>
        <div class="mt-7">
          <h3 class="typography-ui-label font-semibold text-foreground">Conversas</h3>
          <div class="mt-3 max-w-[34rem] space-y-1.5">${sessionsList}</div>
        </div>
      </div>`;
    }
    if (sec === 'Projects') {
      const blocks = state.projects.length ? state.projects.map((p) => {
        const chats = projectSessions(p.id).length;
        return `<div class="zeno-setting-block" data-entity="${p.id}">
          <div class="zeno-setting-block-head">
            <span class="zeno-block-name" title="${esc(p.name)}">${esc(p.name)}</span>
            <span class="zeno-block-actions">
              <button type="button" data-name-edit aria-label="Renomear" title="Renomear">${icon('oc-edit', 'remixicon h-3.5 w-3.5')}</button>
              <button type="button" data-delete aria-label="Apagar" title="Apagar" class="is-danger">${icon('oc-delete-bin', 'remixicon h-3.5 w-3.5')}</button>
            </span>
          </div>
          <div class="zeno-setting-block-meta">${chats} chat${chats === 1 ? '' : 's'}</div>
        </div>`;
      }).join('') : '<div class="zeno-blocks-empty">Os projetos aparecem aqui quando criados pelo agente ou pelo botão de projeto da barra lateral.</div>';
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Projects</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Renomeie pelo lápis; apague pelo lixeira (com confirmação).</p>
        <div class="zeno-settings-grid mt-6">${blocks}</div>
      </div>`;
    }
    if (sec === 'Remote Instances') {
      const cards = state.remotes.map((r) => `
        <div class="zeno-setting-block" data-entity="${r.id}">
          <div class="zeno-setting-block-head">
            <span class="zeno-block-name" title="${esc(r.name)}">${esc(r.name)}</span>
            <span class="zeno-block-actions">
              <button type="button" data-name-edit aria-label="Renomear" title="Renomear">${icon('oc-edit', 'remixicon h-3.5 w-3.5')}</button>
              <button type="button" data-delete aria-label="Apagar" title="Apagar" class="is-danger">${icon('oc-delete-bin', 'remixicon h-3.5 w-3.5')}</button>
            </span>
          </div>
          <div class="zeno-remote-target">${esc(r.type === 'colab' ? (r.url || 'colab') : `${r.user || 'root'}@${r.host}${r.port ? ':' + r.port : ''}`)}</div>
          <code class="zeno-remote-cmd">${esc(remoteConnectCmd(r))}</code>
          <div class="zeno-setting-block-row">
            ${r.password ? '<span class="zeno-setting-block-meta">senha configurada</span>' : ''}
            <span class="flex-1"></span>
            <button type="button" data-remote-copy="${r.id}" class="zeno-mini-btn">Copiar comando</button>
          </div>
        </div>`).join('');
      const addForm = `
        <form data-remote-ssh-form class="mt-3 max-w-[34rem] space-y-3">
          <div class="grid grid-cols-1 gap-3 sm:grid-cols-2">
            ${tplSettingsField('Nome da instância', tplSettingsInput('data-ssh-field="name"', '', 'text', 'ex.: prod-vps'))}
            ${tplSettingsField('Usuário SSH', tplSettingsInput('data-ssh-field="user"', '', 'text', 'ex.: root'))}
            ${tplSettingsField('Host', tplSettingsInput('data-ssh-field="host"', '', 'text', 'ex.: 203.0.113.10'))}
            ${tplSettingsField('Porta', tplSettingsInput('data-ssh-field="port"', '22', 'text', '22'))}
          </div>
          ${tplSettingsField('Senha SSH (opcional)', tplSettingsInput('data-ssh-field="password"', '', 'password', '••••••••'))}
          <div class="flex items-center gap-2">
            <button type="submit" class="zeno-mini-btn">Adicionar</button>
            <button type="button" data-remote-add-cancel class="zeno-mini-btn">Cancelar</button>
          </div>
        </form>`;
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Remote Instances</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Servidores SSH para rodar o Zeno longe daqui.</p>
        <div class="zeno-settings-grid mt-6">
          ${cards}
          <div class="zeno-setting-block zeno-add-block" data-remote-add role="button" tabindex="0" aria-label="Adicionar instância SSH">
            ${icon('oc-add', 'remixicon h-4 w-4')}<span>Adicionar instância SSH</span>
          </div>
        </div>
        ${state.remoteFormOpen ? addForm : ''}
        <p data-remote-error class="project-form-error" role="alert"></p>
      </div>`;
    }
    if (sec === 'Plugins') {
      const cards = state.plugins.length ? state.plugins.map((p) => `
        <div class="zeno-setting-block" data-entity="${p.id}">
          <div class="zeno-setting-block-head">
            <span class="zeno-block-name" title="${esc(p.name)}">${esc(p.name)}</span>
            <span class="zeno-block-actions">
              <button type="button" data-plugin-toggle="${p.id}" aria-pressed="${p.enabled ? 'true' : 'false'}" class="zeno-toggle ${p.enabled ? 'is-on' : ''}" title="${p.enabled ? 'Desativar' : 'Ativar'}" aria-label="Ativar plugin"><span></span></button>
              <button type="button" data-name-edit aria-label="Renomear" title="Renomear">${icon('oc-edit', 'remixicon h-3.5 w-3.5')}</button>
              <button type="button" data-delete aria-label="Apagar" title="Apagar" class="is-danger">${icon('oc-delete-bin', 'remixicon h-3.5 w-3.5')}</button>
            </span>
          </div>
          <p class="zeno-plugin-desc">${esc(p.description || 'Sem descrição.')}</p>
          <div class="zeno-setting-block-row">
            <span class="zeno-setting-block-meta">v${esc(p.version || '1.0.0')}</span>
            <span class="zeno-plugin-state ${p.enabled ? 'is-on' : ''}">${p.enabled ? 'Ativo' : 'Inativo'}</span>
          </div>
        </div>`).join('') : '<div class="zeno-blocks-empty">Peça ao agente no chat (\“crie um plugin…\”) e ele aparece aqui.</div>';
      const active = state.plugins.filter((p) => p.enabled);
      return `
      <div class="px-6 py-5">
        <h2 class="typography-h3 text-foreground">Plugins</h2>
        <p class="typography-meta mt-1 text-muted-foreground">Ative pelo interruptor, renomeie pelo lápis. Aplicados: ${active.length}.</p>
        <div class="zeno-settings-grid mt-6">${cards}</div>
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
    applyPluginCss();
    document.body.classList.toggle('zeno-voice-active', state.voiceMode);
    document.body.classList.toggle('zeno-compact', state.settingsChat.compactMode === true);
    document.body.dataset.chatFont = state.settingsChat.fontSize || 'medium';
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
    const panel = $('[data-panel-root]');
    if (panel) { panel.innerHTML = tplPanel(); bindPanel(panel); }
    const settings = $('[data-settings-root]');
    if (settings) {
      const sig = settingsSig();
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
    state.memoryEditor = { id: note.id, title: note.title, content: memoryContentForEditor(note.kind || KIND_FROM_TAG[note.tag] || 'note', note.content), kind: note.kind || KIND_FROM_TAG[note.tag] || 'note', isNew };
    render();
  };

  const createMemoryNoteAt = (x, y, kind = 'note') => {
    const note = { id: 'memory_' + uid(), title: memoryTitleFor(kind), kind, tag: NOTE_KINDS[kind] || 'Note', excerpt: '', content: memoryTemplateFor(kind), accent: NOTE_ACCENTS[kind] || '#fff', x, y, updated: 'agora', by: 'User' };
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
        if (draggedNode) {
          /* Persiste a posição arrastada e fixa o nó (não é re-layoutado). */
          const note = state.memoryNotes.find((item) => item.id === draggedNode.dataset.memoryNote);
          if (note) {
            const matrix = draggedNode.transform.baseVal.getItem(0).matrix;
            note.x = Math.max(4, Math.min(96, matrix.e / 16));
            note.y = Math.max(10, Math.min(90, matrix.f / 9));
            note.pinned = true;
            LS.set('oc-clone-memory-notes', state.memoryNotes);
          }
        }
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
            if (state.backendOk) void ZenoBackend.addLink(from, to).catch(() => {});
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
        if (state.backendOk && note.backend) void ZenoBackend.deleteNote(note.id).catch(() => {});
      }
      state.memoryDeleteId = null;
      render();
    });
    $('[data-action="memory-refresh"]', rootEl)?.addEventListener('click', () => { void refreshMemoryFromBackend().then(() => render()); });
    $('[data-action="memory-new"]', rootEl)?.addEventListener('click', () => createMemoryNoteAt(50, 52));
    /* Configuração da rede (cores MCP/Skill, espaçamento, rótulos) */
    $('[data-action="memory-config"]', rootEl)?.addEventListener('click', (event) => {
      event.stopPropagation();
      state.memoryConfigOpen = !state.memoryConfigOpen;
      render();
    });
    $('[data-memory-config]', rootEl)?.addEventListener('click', (event) => event.stopPropagation());
    $$('[data-memory-config-color]', rootEl).forEach((input) => input.addEventListener('change', () => {
      state.memoryConfig[input.dataset.memoryConfigColor] = input.value;
      LS.set('oc-clone-memory-config', state.memoryConfig);
      render();
    }));
    $$('[data-memory-config-range]', rootEl).forEach((input) => input.addEventListener('change', () => {
      state.memoryConfig.spacing = Number(input.value) || 1;
      LS.set('oc-clone-memory-config', state.memoryConfig);
      state.memoryLayoutPending = true;
      render();
    }));
    $$('[data-memory-config-check]', rootEl).forEach((input) => input.addEventListener('change', () => {
      state.memoryConfig.labels = input.checked;
      LS.set('oc-clone-memory-config', state.memoryConfig);
      render();
    }));
    $('[data-action="memory-relayout"]', rootEl)?.addEventListener('click', () => {
      state.memoryNotes.forEach((note) => { delete note.pinned; });
      state.memoryLayoutPending = true;
      state.memoryConfigOpen = false;
      LS.set('oc-clone-memory-notes', state.memoryNotes);
      render();
    });
    /* Troca de tipo do editor (MCP = JSON, Skill = Markdown) */
    $$('[data-memory-kind]', rootEl).forEach((select) => select.addEventListener('change', () => {
      if (!state.memoryEditor) return;
      const kind = select.value;
      const previous = state.memoryEditor.kind;
      state.memoryEditor.kind = kind;
      const content = String(state.memoryEditor.content || '');
      const isDefault = Object.keys(NOTE_KINDS).some((k) => memoryTemplateFor(k) === content.trim());
      if (isDefault || !content.trim()) {
        state.memoryEditor.title = memoryTitleFor(kind) !== 'Untitled note' ? memoryTitleFor(kind) : state.memoryEditor.title;
        state.memoryEditor.content = memoryTemplateFor(kind);
      } else if (kind === 'mcp') {
        state.memoryEditor.content = memoryContentForEditor('mcp', content);
      }
      render();
    }));
    $('[data-action="edit-memory-note"]', rootEl)?.addEventListener('click', () => openMemoryEditor(memoryNoteById(state.noteId)));
    $('[data-action="save-memory-note"]', rootEl)?.addEventListener('click', () => {
      const note = memoryNoteById(state.noteId);
      if (!note) return;
      const isNew = !!state.memoryEditor?.isNew;
      const kind = ($('[data-memory-kind]', rootEl)?.value) || state.memoryEditor?.kind || 'note';
      note.title = $('[data-memory-title]', rootEl).value.trim() || 'Untitled note';
      note.content = $('[data-memory-content]', rootEl).value;
      note.kind = kind;
      note.tag = NOTE_KINDS[kind] || 'Note';
      note.accent = noteAccent(note);
      note.excerpt = note.content.replace(/[`*_#\n]/g, ' ').trim().slice(0, 116);
      state.memoryEditor = null;
      LS.set('oc-clone-memory-notes', state.memoryNotes);
      render();
      if (!state.backendOk) return;
      if (isNew) {
        void ZenoBackend.addNote({ title: note.title, content: note.content, kind: note.kind || 'note', tags_json: '[]' }).then((saved) => {
          if (!saved || !saved.id) return;
          const index = state.memoryNotes.findIndex((item) => item.id === note.id);
          if (index < 0) return;
          const replacement = mapBackendNote(saved);
          replacement.x = note.x;
          replacement.y = note.y;
          state.memoryNotes[index] = replacement;
          state.memoryEdges = state.memoryEdges.map((edge) => edge.map((id) => id === note.id ? saved.id : id));
          LS.set('oc-clone-memory-notes', state.memoryNotes);
          LS.set('oc-clone-memory-edges', state.memoryEdges);
          render();
        }).catch(() => {});
      } else if (note.backend) {
        void ZenoBackend.updateNote({ id: note.id, title: note.title, content: note.content }).catch(() => {});
      }
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
    $$('[data-msg-action]', rootEl).forEach((button) => button.addEventListener('click', (event) => {
      event.preventDefault();
      const holder = button.closest('[data-message-id]');
      const id = holder?.dataset.messageId;
      if (!id) return;
      const action = button.dataset.msgAction;
      if (action === 'copy') { void copyMessageById(id); return; }
      if (action === 'revert') { revertSessionToMessage(id); return; }
      if (action === 'branch') { branchSessionFromMessage(id); return; }
      if (action === 'export-md') { exportMessageMarkdown(id); return; }
      if (action === 'download-image') { exportMessageImage(id); return; }
    }));
    $$('[data-attachment-index]', rootEl).forEach((chip) => chip.addEventListener('click', (event) => {
      event.preventDefault();
      const holder = chip.closest('[data-message-id]');
      const id = holder?.dataset.messageId;
      const attachment = attachmentFromMessage(id, Number(chip.dataset.attachmentIndex));
      openAttachmentPreview(attachment);
    }));
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
    $('[data-action="open-zeno-window"]', rootEl)?.addEventListener('click', () => openInZenoWindow(state.browserUrl || BROWSER_HOME));
    $$('[data-browser-action]', rootEl).forEach((button) => button.addEventListener('click', () => {
      const action = button.dataset.browserAction;
      if (window.ZenoNativeBrowser?.available()) {
        if (action === 'back') window.ZenoNativeBrowser.back();
        else if (action === 'forward') window.ZenoNativeBrowser.forward();
        else if (action === 'reload') window.ZenoNativeBrowser.reload();
        else if (action === 'home') navigateBrowser(BROWSER_HOME);
        return;
      }
      /* Guia ZENO AGENT: controlamos pela propria referencia da janela.
         Cross-origin so permite setar location; back/forward usam nosso historico. */
      const alive = zenoWin && !zenoWin.closed;
      if (action === 'back' && state.browserHistoryIndex > 0) {
        state.browserHistoryIndex -= 1;
        state.browserUrl = state.browserHistory[state.browserHistoryIndex];
        if (alive) { zenoWin.location.href = state.browserUrl; zenoWin.focus(); }
        else openInZenoWindow(state.browserUrl);
      } else if (action === 'forward' && state.browserHistoryIndex < state.browserHistory.length - 1) {
        state.browserHistoryIndex += 1;
        state.browserUrl = state.browserHistory[state.browserHistoryIndex];
        if (alive) { zenoWin.location.href = state.browserUrl; zenoWin.focus(); }
        else openInZenoWindow(state.browserUrl);
      } else if (action === 'reload') {
        if (alive) { zenoWin.location.href = state.browserUrl; zenoWin.focus(); }
        else openInZenoWindow(state.browserUrl || BROWSER_HOME);
      } else if (action === 'home') {
        navigateBrowser(BROWSER_HOME);
      }
      const input = $('[data-browser-url]', rootEl);
      if (input) input.value = state.browserUrl;
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

  let deepgramConfigCache = null;
  const loadDeepgramConfig = async () => {
    if (deepgramConfigCache) return deepgramConfigCache;
    const config = {
      apiKey: window.DEEPGRAM_API_KEY || LS.get('oc-clone-voice-deepgram-key', '') || '',
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
    if (!apiKey) throw new Error('DEEPGRAM_API_KEY não configurada (use o .env, window.DEEPGRAM_API_KEY ou o backend ZenoC).');
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

  let openaiKeyCache = null;
  const loadOpenaiKey = async () => {
    if (openaiKeyCache) return openaiKeyCache;
    let key = window.OPENAI_API_KEY || LS.get('oc-clone-voice-openai-key', '');
    if (!key) {
      try {
        const response = await fetch('.env', { cache: 'no-store' });
        if (response.ok) {
          const text = await response.text();
          const match = text.match(/^OPENAI_API_KEY=(.*)$/m);
          if (match) key = match[1].trim().replace(/^["']|["']$/g, '');
        }
      } catch {}
    }
    if (key) openaiKeyCache = key;
    return key;
  };

  const openaiTranscribe = async (blob, langOverride) => {
    const apiKey = await loadOpenaiKey();
    if (!apiKey) throw new Error('OPENAI_API_KEY não configurada (Voice → OpenAI → API key).');
    const form = new FormData();
    form.append('file', blob, 'audio.webm');
    form.append('model', state.voiceProvider === 'openai' && state.voiceSttModel ? state.voiceSttModel : 'whisper-1');
    if (langOverride || state.voiceLang) form.append('language', (langOverride || state.voiceLang).slice(0, 2).toLowerCase());
    const response = await fetch('https://api.openai.com/v1/audio/transcriptions', {
      method: 'POST',
      headers: { Authorization: `Bearer ${apiKey}` },
      body: form,
    });
    const payload = await response.json().catch(() => null);
    if (!response.ok) throw new Error(payload?.error?.message || `Falha no Whisper (${response.status}).`);
    const transcript = (payload?.text || '').trim();
    if (!transcript) throw new Error('Nenhuma fala foi reconhecida.');
    return transcript;
  };

  /* Backend de voz: somente o ZenoC (C11). O shell (webview/nativo) apenas
   * transporta o áudio até o provider com a chave do usuário; nenhuma lógica
   * de backend é escrita em outra linguagem. */
  const transcribeAudio = async (blob, langOverride) => {
    if (state.voiceProvider === 'openai') return openaiTranscribe(blob, langOverride);
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

  /* ---------- studio helpers (espelham o backend C puro) ---------- */
  const persistPlugins = () => LS.set('oc-clone-plugins', state.plugins);
  const persistRemotes = () => LS.set('oc-clone-remotes', state.remotes);

  const pluginSlugify = (text) => {
    const slug = String(text || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 48);
    return slug.length >= 2 ? slug : 'plugin';
  };

  const pluginCreate = (request, nameHint) => {
    const req = String(request || '').trim().slice(0, 400);
    if (!req) return null;
    const name = String(nameHint || '').trim().slice(0, 80) || (req.length > 40 ? req.slice(0, 40) + '…' : req);
    const slug = pluginSlugify(nameHint || req);
    const fn = `${slug.replace(/-/g, '_')}_run`;
    return {
      id: slug, name, version: '1.0.0', enabled: true, description: req,
      functions: [{ name: fn, description: req, parameters: { type: 'object' } }],
      buttons: [{ id: `${slug}-btn`, label: name, icon: 'oc-puzzle-2', action: `run-${slug}` }],
      agents: [{ name: `${name} Helper`, prompt: `Você é o agente do plugin ${name}: ${req}`, model: '' }],
      ui: { css: '', theme: 'auto' },
    };
  };

  const applyPluginCss = () => {
    let host = document.querySelector('style[data-plugins]');
    if (!host) {
      host = document.createElement('style');
      host.dataset.plugins = 'true';
      document.head.appendChild(host);
    }
    host.textContent = state.plugins.filter((p) => p.enabled && p.ui?.css).map((p) => `/* ${p.id} */\n${p.ui.css}`).join('\n');
  };

  const activePluginButtons = () => state.plugins.filter((p) => p.enabled).flatMap((p) => p.buttons || []).slice(0, 12);

  const remoteAdd = (type, name, target) => {
    const t = String(type || '').toLowerCase();
    const nm = String(name || '').trim();
    const tg = String(target || '').trim();
    if (!['vps', 'vm', 'colab'].includes(t) || !nm || !tg) return { error: 'Tipo deve ser vps, vm ou colab; nome e destino são obrigatórios.' };
    if (t === 'colab') {
      if (!/https?:\/\//i.test(tg)) return { error: 'Colab precisa de uma URL https.' };
      return { remote: { id: 'rem_' + uid(), name: nm, type: t, host: 'colab', user: '', port: 0, url: tg, createdAt: new Date().toISOString() } };
    }
    const m = tg.match(/^(?:([^@\s]+)@)?([^\s:@]+)(?::(\d+))?$/);
    if (!m) return { error: 'Destino inválido. Use [user@]host[:porta].' };
    const port = m[3] ? Number(m[3]) : 22;
    if (port < 1 || port > 65535) return { error: 'Porta inválida.' };
    return { remote: { id: 'rem_' + uid(), name: nm, type: t, host: m[2], user: m[1] || 'root', port, url: '', createdAt: new Date().toISOString() } };
  };

  const remoteConnectCmd = (r) => {
    if (!r) return '';
    if (r.type === 'colab') return r.url;
    return r.port && r.port !== 22 ? `ssh -p ${r.port} ${r.user}@${r.host}` : `ssh ${r.user}@${r.host}`;
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

  /* ---------- checkpoints: reverter / bifurcar / copiar mensagem ---------- */
  /* Snapshot do estado do workspace no momento em que a mensagem foi enviada.
     Permite reverter a conversa e os “arquivos” (projetos, plugins, memória)
     exatamente para aquele ponto. */
  const captureWorkspaceSnapshot = () => ({
    plugins: JSON.parse(JSON.stringify(state.plugins || [])),
    projects: JSON.parse(JSON.stringify(state.projects || [])),
    memoryNotes: JSON.parse(JSON.stringify(state.memoryNotes || [])),
    memoryEdges: JSON.parse(JSON.stringify(state.memoryEdges || [])),
    activeProjectId: state.activeProjectId ?? null,
    model: state.model,
    design: state.design,
  });

  const restoreWorkspaceSnapshot = (snap) => {
    if (!snap || typeof snap !== 'object') return;
    if (Array.isArray(snap.plugins)) {
      state.plugins = JSON.parse(JSON.stringify(snap.plugins));
      persistPlugins();
      applyPluginCss();
    }
    if (Array.isArray(snap.projects)) {
      state.projects = JSON.parse(JSON.stringify(snap.projects));
      LS.set('oc-clone-projects', state.projects);
    }
    if (Array.isArray(snap.memoryNotes)) {
      state.memoryNotes = JSON.parse(JSON.stringify(snap.memoryNotes));
      LS.set('oc-clone-memory-notes', state.memoryNotes);
    }
    if (Array.isArray(snap.memoryEdges)) {
      state.memoryEdges = JSON.parse(JSON.stringify(snap.memoryEdges));
      LS.set('oc-clone-memory-edges', state.memoryEdges);
    }
    if ('activeProjectId' in snap) state.activeProjectId = snap.activeProjectId;
    if (snap.model) setModel(snap.model);
    if (snap.design) setDesign(snap.design);
    persistWorkspace();
  };

  /* Texto puro de uma mensagem (inputs, reasoning, tools, outputs). */
  const messagePlainText = (m) => {
    if (!m) return '';
    const parts = [];
    if (m.thinking) parts.push(m.thinking);
    if (Array.isArray(m.tools) && m.tools.length) {
      parts.push(m.tools.map((t) => `$ ${t.cmd || ''}\n${(t.output || []).join('\n')}`).join('\n\n'));
    }
    if (m.text) parts.push(m.text);
    if (m.code) parts.push('```' + (m.code.lang || '') + '\n' + m.code.lines.map(([t]) => t).join('') + '\n```');
    return parts.join('\n\n').trim();
  };

  const copyTextToClipboard = async (text) => {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch {
      try {
        const area = document.createElement('textarea');
        area.value = text;
        area.style.position = 'fixed';
        area.style.opacity = '0';
        document.body.appendChild(area);
        area.select();
        const ok = document.execCommand('copy');
        area.remove();
        return ok;
      } catch { return false; }
    }
  };

  const messageById = (id) => activeSession()?.messages.find((m) => m.id === id) || null;

  const copyMessageById = async (id) => {
    const message = messageById(id);
    if (!message) return;
    const ok = await copyTextToClipboard(messagePlainText(message));
    const form = $('[data-composer-form]');
    if (form) flashComposerHint(form, ok ? 'Mensagem copiada' : 'Não foi possível copiar');
  };

  /* Markdown completo da conversa para anexar ao bifurcar. */
  const buildTranscriptMarkdown = (messages, session) => {
    const lines = [];
    lines.push(`# Histórico da conversa — ${session?.title || 'Chat'}`);
    lines.push('');
    lines.push(`> Bifurcado em ${now().toLocaleString()} · ${messages.length} mensagem(ns)`);
    lines.push('');
    messages.forEach((m, i) => {
      const who = m.role === 'user' ? 'Usuário' : 'Zeno';
      lines.push(`## ${i + 1}. ${who} · ${m.time || ''}`);
      lines.push('');
      if (m.role === 'assistant') {
        const meta = [m.model, m.agent && `agente: ${m.agent}`].filter(Boolean).join(' · ');
        if (meta) { lines.push(`*${meta}*`); lines.push(''); }
        if (m.thinking) { lines.push('### Raciocínio'); lines.push(''); lines.push(m.thinking); lines.push(''); }
        if (Array.isArray(m.tools) && m.tools.length) {
          lines.push('### Ferramentas');
          lines.push('');
          m.tools.forEach((t) => {
            lines.push(`**${t.title || 'Tool'}** — \`${t.cmd || ''}\`${t.duration ? ` (${t.duration})` : ''}`);
            lines.push('');
            lines.push('```text');
            lines.push(...(t.output || []));
            lines.push('```');
            lines.push('');
          });
        }
        if (m.text) { lines.push('### Resposta'); lines.push(''); lines.push(m.text); lines.push(''); }
        if (m.code) { lines.push('```' + (m.code.lang || '')); lines.push(m.code.lines.map(([t]) => t).join('')); lines.push('```'); lines.push(''); }
      } else {
        if (m.text) { lines.push(m.text); lines.push(''); }
        (m.attachments || []).forEach((a) => { lines.push(`**Anexo:** ${a.name}`); lines.push(''); });
      }
      lines.push('---');
      lines.push('');
    });
    return lines.join('\n');
  };

  const slugify = (text) => String(text || '').toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 40);

  const branchSessionFromMessage = (id) => {
    const origin = activeSession();
    if (!origin) return;
    const index = origin.messages.findIndex((m) => m.id === id);
    if (index < 0) return;
    const branchMessages = origin.messages.slice(0, index + 1);
    const markdown = buildTranscriptMarkdown(branchMessages, origin);
    const base = slugify(origin.title) || 'conversa-anterior';
    const session = createSession(origin.projectId || null);
    session.title = `Branch · ${origin.title}`.slice(0, 70);
    session.messages.push({
      id: 'msg_' + uid(),
      role: 'user',
      text: `Bifurquei **${origin.title}** a partir desta mensagem. O histórico completo da conversa anterior está no anexo abaixo — raciocínio, ferramentas e respostas incluídos.`,
      time: fmtTime(now()),
      attachments: [{
        name: `${base}.md`,
        kind: 'markdown',
        mime: 'text/markdown',
        content: markdown,
        size: `${Math.max(1, Math.round(markdown.length / 1024))} KB`,
      }],
      snapshot: captureWorkspaceSnapshot(),
    });
    state.activeId = session.id;
    state.draftNew = false;
    state.workspace = 'chat';
    state.noteId = null;
    state.panel = null;
    LS.set('oc-clone-sessions', state.sessions);
    LS.set('oc-clone-active', state.activeId);
    persistWorkspace();
    render();
    const form = $('[data-composer-form]');
    if (form) flashComposerHint(form, 'Conversa bifurcada para um novo chat');
  };

  const revertSessionToMessage = (id) => {
    const session = activeSession();
    if (!session) return;
    const index = session.messages.findIndex((m) => m.id === id);
    if (index < 0) return;
    const message = session.messages[index];
    const removed = session.messages.length - index;
    const ok = window.confirm(`Reverter para esta mensagem?\n\n${removed} mensagem(ns) serão removidas e os arquivos/estado do workspace voltam ao ponto em que ela foi enviada.`);
    if (!ok) return;
    session.messages = session.messages.slice(0, index);
    if (message.snapshot) restoreWorkspaceSnapshot(message.snapshot);
    state.typing = false;
    state.liveTrace = null;
    state.expandedTools = {};
    LS.set('oc-clone-sessions', state.sessions);
    render();
    const form = $('[data-composer-form]');
    const input = form ? $('[data-composer-input]', form) : null;
    if (input && message.text) {
      input.innerText = message.text;
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.focus();
    }
    if (form) flashComposerHint(form, 'Conversa revertida — edite e envie novamente');
  };

  /* ---------- anexos (.md) ---------- */
  const attachmentFromMessage = (messageId, index) => {
    const message = messageById(messageId);
    return message?.attachments?.[index] || null;
  };

  const downloadAttachment = (att) => {
    if (!att) return;
    try {
      const blob = new Blob([att.content || ''], { type: att.mime || 'text/markdown' });
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = att.name || 'anexo.md';
      document.body.appendChild(anchor);
      anchor.click();
      anchor.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1500);
    } catch { /* ignore */ }
  };

  const exportMessageMarkdown = (id) => {
    const message = messageById(id);
    if (!message) return;
    const heading = message.role === 'assistant' ? `# Resposta — ${message.model || 'Zeno'}` : '# Mensagem';
    const content = `${heading}\n\n_${message.time || ''}_\n\n${messagePlainText(message)}\n`;
    downloadAttachment({ name: `mensagem-${String(id).slice(-6)}.md`, mime: 'text/markdown', content });
  };

  const exportMessageImage = (id) => {
    const message = messageById(id);
    if (!message) return;
    const width = 900, pad = 40, lineHeight = 22, fontSize = 15;
    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d');
    const mono = `${fontSize}px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace`;
    ctx.font = mono;
    const wrap = (str, max) => {
      const out = [];
      String(str || '').split('\n').forEach((par) => {
        if (!par) { out.push(''); return; }
        let line = '';
        par.split(' ').forEach((word) => {
          const test = line ? `${line} ${word}` : word;
          if (ctx.measureText(test).width > max && line) { out.push(line); line = word; } else line = test;
        });
        out.push(line);
      });
      return out;
    };
    const lines = wrap(messagePlainText(message), width - pad * 2);
    canvas.width = width;
    canvas.height = Math.max(160, pad * 2 + lines.length * lineHeight + 40);
    ctx.fillStyle = '#0b0b0b';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#8a8a8a';
    ctx.font = '600 13px ui-sans-serif, system-ui, sans-serif';
    ctx.fillText(`${message.role === 'assistant' ? 'Zeno' : 'Você'} · ${message.time || ''}`, pad, pad - 6);
    ctx.fillStyle = '#e8e8e8';
    ctx.font = mono;
    lines.forEach((line, i) => ctx.fillText(line, pad, pad + 20 + i * lineHeight));
    canvas.toBlob((blob) => {
      if (!blob) return;
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = `mensagem-${String(id).slice(-6)}.png`;
      document.body.appendChild(anchor);
      anchor.click();
      anchor.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1500);
    });
  };

  const openAttachmentPreview = (att) => {
    if (!att) return;
    document.querySelector('[data-attachment-modal]')?.remove();
    const overlay = document.createElement('div');
    overlay.dataset.attachmentModal = 'true';
    overlay.className = 'zeno-attachment-modal';
    overlay.innerHTML = `
      <div class="zeno-attachment-backdrop" data-attachment-close></div>
      <article role="dialog" aria-modal="true" aria-label="${esc(att.name)}" class="zeno-attachment-dialog oc-dialog">
        <header class="zeno-attachment-header">
          <div class="zeno-attachment-title">${icon('oc-file-text', 'remixicon h-4 w-4')}<span>${esc(att.name)}</span><span class="zeno-attachment-size">${esc(att.size || 'md')}</span></div>
          <div class="zeno-attachment-header-actions">
            <button type="button" data-attachment-download class="zeno-attachment-download">${icon('oc-download', 'remixicon h-3.5 w-3.5')}Download</button>
            <button type="button" data-attachment-close class="memory-icon-button" aria-label="Close">${icon('oc-close', 'remixicon h-4 w-4')}</button>
          </div>
        </header>
        <div class="zeno-attachment-body">${renderMarkdown(att.content || '')}</div>
      </article>`;
    const onKey = (event) => {
      if (event.key === 'Escape') { overlay.remove(); document.removeEventListener('keydown', onKey); }
    };
    overlay.addEventListener('click', (event) => {
      if (event.target.closest('[data-attachment-close]')) { overlay.remove(); document.removeEventListener('keydown', onKey); return; }
      if (event.target.closest('[data-attachment-download]')) downloadAttachment(att);
    });
    document.addEventListener('keydown', onKey);
    document.body.appendChild(overlay);
  };

  /* ---------- execução do agente (ZenoC) e renderização ao vivo ---------- */
  const scheduleLiveRender = (() => {
    let pending = false;
    return () => {
      if (pending) return;
      pending = true;
      requestAnimationFrame(() => { pending = false; render(true); });
    };
  })();

  const applyRunEvent = (live, event) => {
    if (!event || !event.type) return;
    if (event.type === 'run_started') {
      live.runId = event.run_id || live.runId;
      if (event.model) live.model = event.model;
      return;
    }
    if (event.type === 'thinking') {
      const text = String(event.text || '').trim();
      if (text) live.thinking = live.thinking ? `${live.thinking}\n${text}` : text;
      return;
    }
    if (event.type === 'tool_started') {
      live.tools.push({
        id: 'tool_' + uid(), name: event.tool || 'tool', title: prettyTool(event.tool),
        cmd: toolArgsSummary(event.args), args: event.args, output: [], ok: null,
        running: true, startedAt: Date.now(), duration: '',
      });
      return;
    }
    if (event.type === 'tool_completed') {
      const name = event.tool || 'tool';
      let tool = null;
      for (let index = live.tools.length - 1; index >= 0; index--) {
        if (live.tools[index].name === name && live.tools[index].running) { tool = live.tools[index]; break; }
      }
      if (!tool) {
        tool = { id: 'tool_' + uid(), name, title: prettyTool(name), cmd: '', args: null, output: [], ok: null, running: true, startedAt: Date.now(), duration: '' };
        live.tools.push(tool);
      }
      tool.running = false;
      tool.ok = event.ok !== false;
      tool.duration = toolDurations(tool);
      tool.output = toolOutputLines(event.output);
      return;
    }
    if (event.type === 'text') {
      live.text += String(event.delta || '');
      return;
    }
    if (event.type === 'run_completed') {
      live.status = event.status || live.status;
      live.turns = event.turns;
      live.toolCalls = event.tool_calls;
      live.tokensIn = event.tokens_in;
      live.tokensOut = event.tokens_out;
      if (event.duration_ms) live.duration = `${(event.duration_ms / 1000).toFixed(1)}s`;
      return;
    }
    if (event.type === 'error') {
      live.error = event.message || 'Erro no agente.';
      return;
    }
    if (event.type === 'result' && event.result) {
      const result = event.result;
      live.status = result.status || live.status;
      live.runId = result.run_id || live.runId;
      live.turns = result.turns;
      live.toolCalls = result.tool_calls;
      live.tokensIn = result.tokens_in;
      live.tokensOut = result.tokens_out;
      if (result.model) live.model = result.model;
      if (!live.text && result.response) live.text = result.response;
      if (result.provider_error && result.status !== 'completed') live.error = result.provider_error;
      else if (result.status && result.status !== 'completed' && !live.error) live.error = result.response || result.status;
      if (result.duration_ms) live.duration = `${(result.duration_ms / 1000).toFixed(1)}s`;
    }
  };

  const finalizeRun = (session, live) => {
    if (state.liveRun !== live) return;
    live.streaming = false;
    live.tools.forEach((tool) => { if (tool.running) { tool.running = false; tool.duration = toolDurations(tool); } });
    if (!live.duration && live.startedAt) live.duration = `${((Date.now() - live.startedAt) / 1000).toFixed(1)}s`;
    if (!live.text && !live.thinking && !live.tools.length && !live.error) live.error = 'O agente terminou sem enviar conteúdo.';
    session.messages.push({
      id: live.id, role: 'assistant', time: fmtTime(now()), model: live.model, agent: live.agent,
      thinking: live.thinking,
      tools: live.tools.map((tool) => ({ title: tool.title, cmd: tool.cmd, output: tool.output, duration: tool.duration, ok: tool.ok })),
      text: live.text, error: live.error || '', duration: live.duration, createdAt: new Date().toISOString(),
      tokensIn: live.tokensIn, tokensOut: live.tokensOut, status: live.status,
    });
    state.liveRun = null;
    state.typing = false;
    LS.set('oc-clone-sessions', state.sessions);
    render(true);
    setTimeout(() => { void refreshMemoryFromBackend(); }, 400);
    if (state.voiceMode && voiceRuntime && !voiceRuntime.stopped) {
      if (live.text) {
        voicePushLine(voiceRuntime, 'zeno', live.text);
        if (state.voiceAutoSpeak !== false) void voiceSpeak(voiceRuntime, live.text);
      }
      if (voicePendingQueue.length) {
        const next = voicePendingQueue.shift();
        setTimeout(() => deliverUserText(next), 250);
      }
    }
  };

  const startBackendRun = (session, clean) => {
    const live = {
      id: 'msg_' + uid(), role: 'assistant', time: fmtTime(now()), model: state.model,
      agent: state.agentMode === 'minimal' ? 'plan' : 'build',
      thinking: '', tools: [], text: '', error: '', streaming: true,
      startedAt: Date.now(), duration: '', runId: '',
    };
    state.expandedTools['think_' + live.id] = true;
    state.liveRun = live;
    state.typing = true;
    render(true);
    ZenoBackend.chat({ message: clean, model: state.model, session: session.id }, (event) => {
      applyRunEvent(live, event);
      scheduleLiveRender();
    }).then(() => {
      finalizeRun(session, live);
    }).catch((error) => {
      live.error = live.error || error?.message || 'Falha de conexão com o agente ZenoC.';
      finalizeRun(session, live);
    });
  };

  const deliverUserText = (text) => {
    const clean = String(text || '').trim();
    if (!clean) return false;
    /* Em modo voz a conversa é contínua: em vez de descartar o que foi falado
     * durante a resposta do agente, enfileira para logo em seguida. */
    if (state.typing) {
      if (state.voiceMode) voicePendingQueue.push(clean);
      return state.voiceMode;
    }
    if (state.draftNew) createSession(state.activeProjectId || null);
    const s = activeSession();
    if (!s) return false;
    s.messages.push({ id: 'msg_' + uid(), role: 'user', text: clean, time: fmtTime(now()), createdAt: new Date().toISOString(), snapshot: captureWorkspaceSnapshot() });
    if (state.backendOk) {
      startBackendRun(s, clean);
      return true;
    }
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
        if (state.voiceAutoSpeak !== false) void voiceSpeak(voiceRuntime, reply.text);
      }
      if (state.voiceMode && voicePendingQueue.length) {
        const next = voicePendingQueue.shift();
        setTimeout(() => deliverUserText(next), 250);
      }
    }, 2200 + Math.random() * 800);
    return true;
  };

  /* ---------- voice mode (Deepgram STT + black-hole reaction) ---------- */
  let voiceRuntime = null;
  let voicePendingQueue = [];

  const voiceGreeting = () => {
    if ((state.voiceLang || '').startsWith('en')) return "Hi! I'm Zeno. Talk to me, I'm listening.";
    if ((state.voiceLang || '').startsWith('es')) return '¡Hola! Soy Zeno. Habla conmigo, te escucho.';
    return 'Olá! Sou o Zeno. Fale comigo, estou ouvindo.';
  };

  const voiceStatusText = (status, error) => ({
    starting: 'Ativando microfone…',
    listening: 'Ouvindo — fale com o Zeno',
    transcribing: 'Transcrevendo…',
    speaking: 'Zeno está respondendo…',
    error: error || 'Falha no modo voz.',
  }[status] || status);

  const voiceIndicator = (status) => status === 'listening'
    ? '<span class="zeno-voice-live-dot" aria-hidden="true"></span>'
    : status === 'starting' || status === 'transcribing' || status === 'speaking'
      ? '<span class="oc-spinner" aria-hidden="true"></span>'
      : '';

  /* Modo voz sem pop-up: as ondas de áudio aparecem dentro da caixa do composer. */
  const tplVoiceInline = () => {
    const runtime = voiceRuntime;
    const status = runtime?.status || 'starting';
    const providerLabel = state.voiceProvider === 'openai' ? 'OpenAI' : 'Deepgram';
    return `
      <div class="zeno-voice-inline" data-voice-inline>
        <div class="zeno-voice-inline-head">
          <span class="zeno-voice-inline-state" data-voice-inline-state="${status}"><span class="zeno-voice-indicator" data-voice-indicator>${voiceIndicator(status)}</span><span data-voice-status>${esc(voiceStatusText(status, runtime?.error))}</span></span>
          <span class="zeno-voice-inline-meta">${esc(providerLabel)} · ${esc(state.voiceLang)}</span>
          <button type="button" data-action="voice-exit" class="zeno-voice-inline-exit" title="Encerrar conversa" aria-label="Encerrar conversa">${icon('oc-stop', 'remixicon h-3.5 w-3.5')}</button>
        </div>
        <canvas class="zeno-voice-wave" data-voice-wave width="760" height="64" aria-hidden="true"></canvas>
        <div class="zeno-voice-meter" aria-hidden="true"><div data-voice-meter></div></div>
      </div>`;
  };

  const renderVoice = () => {
    const inline = $('[data-voice-inline]');
    if (!inline) return;
    const runtime = voiceRuntime;
    const status = runtime?.status || 'starting';
    const label = $('[data-voice-status]', inline);
    if (label) label.textContent = voiceStatusText(status, runtime?.error);
    const stateEl = $('[data-voice-inline-state]', inline);
    if (stateEl) stateEl.dataset.voiceInlineState = status;
    const indicator = $('[data-voice-indicator]', inline);
    if (indicator) indicator.innerHTML = voiceIndicator(status);
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

  const pushVoiceGreeting = () => {
    const session = activeSession();
    if (!session || session.messages.length) return;
    session.messages.push({
      id: 'msg_' + uid(),
      role: 'assistant',
      text: voiceGreeting(),
      time: fmtTime(now()),
      model: state.model,
      agent: 'build',
      duration: '—',
    });
    LS.set('oc-clone-sessions', state.sessions);
  };

  const setVoiceMode = (on) => {
    if (on && state.voiceMode && voiceRuntime) return;
    if (!on && !state.voiceMode) return;
    state.voiceMode = on;
    if (on) {
      /* Modo voz abre o chat: as transcrições da conversa ficam na sessão ativa
       * (cria uma quando ainda estamos no estado vazio). */
      state.workspace = 'chat';
      state.noteId = null;
      if (state.draftNew || !activeSession()) createSession(state.activeProjectId || null);
      pushVoiceGreeting();
      persistWorkspace();
    } else {
      voicePendingQueue = [];
    }
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
    try { clearInterval(runtime.ttsTimer); } catch {}
    try { if (runtime.recorder?.state && runtime.recorder.state !== 'inactive') runtime.recorder.stop(); } catch {}
    try { runtime.stream?.getTracks?.().forEach((track) => track.stop()); } catch {}
    try { runtime.ctx?.close?.(); } catch {}
    try { window.speechSynthesis?.cancel?.(); } catch {}
    blackHoleSend({ type: 'voice-off' });
  };

  const voiceDrawWave = (runtime, level) => {
    const canvas = document.querySelector('[data-voice-wave]');
    if (!canvas) return;
    try {
      const ctx = canvas.getContext('2d');
      const W = canvas.width, H = canvas.height;
      ctx.clearRect(0, 0, W, H);
      const bars = 56;
      const freq = runtime.freq || null;
      const mid = H / 2;
      for (let i = 0; i < bars; i++) {
        let v = level;
        if (freq) v = Math.min(1, (freq[Math.floor((i / bars) * freq.length)] / 255) * 1.4 + level * 0.25);
        const h = Math.max(3, v * (H * 0.86));
        const x = (i / bars) * W + (W / bars - 3) / 2;
        const grad = ctx.createLinearGradient(0, mid - h / 2, 0, mid + h / 2);
        grad.addColorStop(0, '#8ab6ff');
        grad.addColorStop(1, '#c9a6ff');
        ctx.fillStyle = grad;
        ctx.beginPath();
        if (ctx.roundRect) ctx.roundRect(x, mid - h / 2, 3, h, 2);
        else ctx.rect(x, mid - h / 2, 3, h);
        ctx.fill();
      }
    } catch {}
  };

  const voiceMeterLoop = (runtime) => {
    if (!runtime || runtime.stopped) return;
    try {
      runtime.analyser.getByteTimeDomainData(runtime.data);
      if (runtime.freq) runtime.analyser.getByteFrequencyData(runtime.freq);
      let sum = 0;
      for (let i = 0; i < runtime.data.length; i++) {
        const v = (runtime.data[i] - 128) / 128;
        sum += v * v;
      }
      let level = Math.min(1, Math.sqrt(sum / runtime.data.length) * 3.2);
      if (runtime.speaking) level = Math.min(1, level * 0.4 + (runtime.ttsLevel || 0.35));
      /* A área do chat vira o visualizador: o buraco negro se movimenta e as
       * partículas se agitam proporcionalmente à energia da voz. */
      blackHoleSend({ type: 'voice', level: Number(level.toFixed(3)) });
      if (level > 0.45) blackHoleSend({ type: 'drag', dx: (Math.random() - 0.5) * level * 6, dy: (Math.random() - 0.5) * level * 4 });
      const meter = document.querySelector('[data-voice-meter]');
      if (meter) meter.style.width = `${Math.round(level * 100)}%`;
      voiceDrawWave(runtime, level);
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
      Object.assign(runtime, { stream, ctx, analyser, data: new Uint8Array(analyser.fftSize), freq: new Uint8Array(analyser.frequencyBinCount), recorder, mimeType });
      recorder.addEventListener('dataavailable', (event) => {
        if (event.data?.size) void transcribeVoiceChunk(runtime, event.data);
      });
      recorder.addEventListener('error', () => voiceSetStatus(runtime, 'error', 'Falha ao capturar o áudio.'));
      recorder.start(4000);
      voiceSetStatus(runtime, 'listening');
      voiceMeterLoop(runtime);
      /* O agente cumprimenta falando; a mensagem já está no chat
       * (pushVoiceGreeting) e daí a conversa segue em loop contínuo. */
      if (state.voiceAutoSpeak !== false) void voiceSpeak(runtime, voiceGreeting());
    } catch (error) {
      voiceSetStatus(runtime, 'error', error?.message || 'Microfone indisponível.');
    }
  };

  /* TTS: Deepgram prioritário → OpenAI opcional → speechSynthesis local.
   * Durante a fala o buraco negro se agita (ttsLevel alimenta o medidor). */
  const voiceTtsAgitate = (runtime, on) => {
    if (!runtime) return;
    if (on) {
      runtime.ttsLevel = 0.5;
      try {
        clearInterval(runtime.ttsTimer);
        runtime.ttsTimer = setInterval(() => {
          if (!runtime || runtime.stopped || !runtime.speaking) return;
          runtime.ttsLevel = 0.3 + Math.random() * 0.55;
          blackHoleSend({ type: 'voice', level: Number(runtime.ttsLevel.toFixed(3)) });
          blackHoleSend({ type: 'drag', dx: (Math.random() - 0.5) * 5, dy: (Math.random() - 0.5) * 3 });
        }, 140);
      } catch {}
    } else {
      runtime.ttsLevel = 0;
      try { clearInterval(runtime.ttsTimer); } catch {}
      blackHoleSend({ type: 'voice', level: 0.05 });
    }
  };

  const voiceSpeakDeepgram = async (clean) => {
    const { apiKey, model } = await loadDeepgramConfig();
    if (!apiKey) return null;
    const ttsModel = state.voiceTtsModel || model || 'aura-asteria-en';
    const url = `https://api.deepgram.com/v1/speak?model=${encodeURIComponent(ttsModel)}`;
    const response = await fetch(url, {
      method: 'POST',
      headers: { Authorization: `Token ${apiKey}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ text: clean }),
    });
    if (!response.ok) return null;
    const blob = await response.blob();
    return blob && blob.size ? blob : null;
  };

  const voiceSpeakOpenai = async (clean) => {
    const apiKey = await loadOpenaiKey();
    if (!apiKey) return null;
    const response = await fetch('https://api.openai.com/v1/audio/speech', {
      method: 'POST',
      headers: { Authorization: `Bearer ${apiKey}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({
        model: state.voiceTtsModel && state.voiceProvider === 'openai' ? state.voiceTtsModel : 'tts-1',
        voice: state.voiceName || 'alloy',
        input: clean,
      }),
    });
    if (!response.ok) return null;
    const blob = await response.blob();
    return blob && blob.size ? blob : null;
  };

  const voiceSpeakNative = (clean) => new Promise((resolve) => {
    try {
      const synth = window.speechSynthesis;
      if (!synth || !clean) return resolve(false);
      synth.cancel();
      const utterance = new SpeechSynthesisUtterance(clean);
      utterance.lang = state.voiceLang;
      try {
        const voices = synth.getVoices?.() || [];
        const match = voices.find((v) => v.lang === state.voiceLang) || voices.find((v) => (v.lang || '').startsWith((state.voiceLang || '').slice(0, 2)));
        if (match) utterance.voice = match;
      } catch {}
      let finished = false;
      const safety = setTimeout(() => { if (!finished) { finished = true; resolve(true); } }, 30000);
      utterance.onend = () => { if (!finished) { finished = true; clearTimeout(safety); resolve(true); } };
      utterance.onerror = () => { if (!finished) { finished = true; clearTimeout(safety); resolve(false); } };
      synth.speak(utterance);
    } catch { resolve(false); }
  });

  const voiceSpeak = (runtime, text) => new Promise((resolve) => {
    const done = () => {
      if (runtime) {
        runtime.speaking = false;
        voiceTtsAgitate(runtime, false);
        try { runtime.recorder?.resume?.(); } catch {}
        if (!runtime.stopped) voiceSetStatus(runtime, 'listening');
      }
      resolve();
    };
    (async () => {
      try {
        const clean = String(text || '').replace(/[*_`#>\-[\]()]/g, '').trim().slice(0, 500);
        if (!clean) return done();
        if (runtime) {
          runtime.speaking = true;
          try { runtime.recorder?.pause?.(); } catch {}
          voiceSetStatus(runtime, 'speaking');
          voiceTtsAgitate(runtime, true);
        }
        try { window.speechSynthesis?.cancel?.(); } catch {}
        /* Prioridade: Deepgram; opção: OpenAI; fallback: voz local. */
        let audioBlob = null;
        if (state.voiceProvider === 'openai') {
          try { audioBlob = await voiceSpeakOpenai(clean); } catch {}
          if (!audioBlob) { try { audioBlob = await voiceSpeakDeepgram(clean); } catch {} }
        } else {
          try { audioBlob = await voiceSpeakDeepgram(clean); } catch {}
          if (!audioBlob) { try { audioBlob = await voiceSpeakOpenai(clean); } catch {} }
        }
        if (audioBlob && runtime && !runtime.stopped) {
          const url = URL.createObjectURL(audioBlob);
          await new Promise((playDone) => {
            const audio = new Audio(url);
            audio.onended = playDone;
            audio.onerror = playDone;
            setTimeout(playDone, 45000);
            audio.play().catch(playDone);
          });
          try { URL.revokeObjectURL(url); } catch {}
          return done();
        }
        if (audioBlob && (!runtime || runtime.stopped)) return done();
        await voiceSpeakNative(clean);
        return done();
      } catch { done(); }
    })();
  });

  const bindComposer = (rootEl) => {
    const form = $('[data-composer-form]', rootEl);
    if (!form) return;
    if (state.voiceMode) {
      $('[data-action="voice-exit"]', form)?.addEventListener('click', () => setVoiceMode(false));
      return;
    }
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
        { kind: 'voice', id: 'voz', title: 'Conversar por voz', desc: `${state.voiceProvider === 'openai' ? 'OpenAI' : 'Deepgram'} · ${state.voiceLang}`, icon: 'oc-pulse' },
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
    /* ---------- botão “+” e menu de contexto (modelo / thinking / arquivos) ---------- */
    const attachWrap = $('[data-attach-wrap]', form);
    const attachPanel = attachWrap ? $('[data-attach-panel]', attachWrap) : null;
    const attachFileNames = [];

    const appendFileNames = (names) => {
      if (!names.length) return;
      const previous = getComposerText(input);
      setInputText(`${previous ? `${previous} ` : ''}${names.map((n) => `[anexo: ${n}]`).join(' ')}`);
    };

    const renderAttachFiles = () => {
      if (!attachPanel) return;
      const list = $('[data-attach-file-list]', attachPanel);
      if (!list) return;
      list.innerHTML = attachFileNames.map((name) => `<span class="zeno-attach-file-chip">${icon('oc-file-text', 'remixicon h-3.5 w-3.5')}<span>${esc(name)}</span></span>`).join('');
    };

    const pickFiles = () => {
      const picker = document.createElement('input');
      picker.type = 'file';
      picker.multiple = true;
      picker.style.display = 'none';
      picker.addEventListener('change', () => {
        const names = [...(picker.files || [])].map((f) => f.name).filter(Boolean);
        attachFileNames.push(...names.filter((n) => !attachFileNames.includes(n)));
        appendFileNames(names);
        renderAttachFiles();
        picker.remove();
      });
      document.body.appendChild(picker);
      picker.click();
    };

    const closeAttachPanel = () => {
      if (!attachWrap) return;
      attachWrap.classList.remove('is-open');
      if (attachPanel) { attachPanel.hidden = true; attachPanel.innerHTML = ''; attachPanel.removeAttribute('data-attach-mode'); }
    };

    const syncAttachMenuLabels = () => {
      if (!attachWrap) return;
      const modelLabel = $('[data-attach-mode="model"] small', attachWrap);
      if (modelLabel) modelLabel.textContent = state.model;
      const thinkingLabel = $('[data-attach-mode="thinking"] small', attachWrap);
      if (thinkingLabel) thinkingLabel.textContent = state.design;
    };

    const attachPanelHeader = (title) => `<div class="zeno-attach-panel-head"><button type="button" class="zeno-attach-back" data-attach-back aria-label="Voltar">${icon('oc-arrow-left-s', 'remixicon h-4 w-4')}</button><span>${esc(title)}</span></div>`;

    const setupAttachDrop = () => {
      const drop = $('[data-attach-drop]', attachPanel);
      if (!drop) return;
      ['dragenter', 'dragover'].forEach((ev) => drop.addEventListener(ev, (event) => { event.preventDefault(); drop.classList.add('is-over'); }));
      ['dragleave', 'dragend'].forEach((ev) => drop.addEventListener(ev, () => drop.classList.remove('is-over')));
      drop.addEventListener('drop', (event) => {
        event.preventDefault();
        drop.classList.remove('is-over');
        const names = [...(event.dataTransfer?.files || [])].map((f) => f.name).filter(Boolean);
        attachFileNames.push(...names.filter((n) => !attachFileNames.includes(n)));
        appendFileNames(names);
        renderAttachFiles();
      });
    };

    const openAttachPanel = (mode) => {
      if (!attachWrap || !attachPanel) return;
      if (mode === 'model') {
        attachPanel.innerHTML = `${attachPanelHeader('Modelo')}<div class="zeno-attach-options">${MODELS.map((name) => `<button type="button" class="zeno-attach-option ${name === state.model ? 'is-selected' : ''}" data-attach-model="${esc(name)}"><span class="zeno-attach-item-text"><strong>${esc(name)}</strong><small>${name === state.model ? 'Em uso' : 'Trocar para este modelo'}</small></span>${name === state.model ? icon('oc-check', 'remixicon h-4 w-4 zeno-attach-check') : ''}</button>`).join('')}</div>`;
      } else if (mode === 'thinking') {
        attachPanel.innerHTML = `${attachPanelHeader('Thinking')}<div class="zeno-attach-options">${DESIGN_OPTIONS.map(([name, description]) => `<button type="button" class="zeno-attach-option ${name === state.design ? 'is-selected' : ''}" data-attach-thinking="${esc(name)}"><span class="zeno-attach-item-text"><strong>${esc(name)}</strong><small>${esc(description)}</small></span>${name === state.design ? icon('oc-check', 'remixicon h-4 w-4 zeno-attach-check') : ''}</button>`).join('')}</div>`;
      } else {
        attachPanel.innerHTML = `${attachPanelHeader('Adicionar arquivos')}<div class="zeno-attach-drop" data-attach-drop>${icon('oc-file-add', 'remixicon h-5 w-5')}<strong>Solte arquivos aqui</strong><small>ou clique para escolher</small></div><button type="button" class="zeno-attach-browse" data-attach-browse>${icon('oc-add-circle', 'remixicon h-4 w-4')}Escolher arquivos</button><div class="zeno-attach-file-list" data-attach-file-list></div>`;
      }
      attachPanel.hidden = false;
      attachPanel.dataset.attachMode = mode;
      attachWrap.classList.add('is-open');
      $('[data-attach-back]', attachPanel)?.addEventListener('click', closeAttachPanel);
      $$('[data-attach-model]', attachPanel).forEach((button) => button.addEventListener('click', () => {
        setModel(button.dataset.attachModel);
        syncAttachMenuLabels();
        closeAttachPanel();
        flashComposerHint(form, `Modelo: ${button.dataset.attachModel}`);
      }));
      $$('[data-attach-thinking]', attachPanel).forEach((button) => button.addEventListener('click', () => {
        setDesign(button.dataset.attachThinking);
        syncAttachMenuLabels();
        closeAttachPanel();
        flashComposerHint(form, `Thinking: ${button.dataset.attachThinking}`);
      }));
      if (mode === 'files') {
        $('[data-attach-drop]', attachPanel)?.addEventListener('click', pickFiles);
        $('[data-attach-browse]', attachPanel)?.addEventListener('click', pickFiles);
        renderAttachFiles();
        setupAttachDrop();
      }
    };

    if (attachWrap) {
      $('[data-attach-mode="model"]', attachWrap)?.addEventListener('click', () => openAttachPanel('model'));
      $('[data-attach-mode="thinking"]', attachWrap)?.addEventListener('click', () => openAttachPanel('thinking'));
      $('[data-attach-mode="files"]', attachWrap)?.addEventListener('click', () => openAttachPanel('files'));
      attachWrap.addEventListener('mouseleave', () => { if (attachWrap.classList.contains('is-open')) closeAttachPanel(); });
    }
    $('[data-action="attach"]', form)?.addEventListener('click', (event) => {
      event.preventDefault();
      if (attachWrap && attachWrap.classList.contains('is-open')) closeAttachPanel();
      else openAttachPanel('files');
    });
    form.addEventListener('click', (event) => {
      if (attachWrap && attachWrap.classList.contains('is-open') && !attachWrap.contains(event.target)) closeAttachPanel();
    });
    $('[data-action="voice-mode"]', form)?.addEventListener('click', () => setVoiceMode(true));
    $('[data-action="cancel-run"]', form)?.addEventListener('click', async () => {
      flashComposerHint(form, 'Parando o agente…');
      try { await ZenoBackend.cancel(); } catch {}
    });
    const send = $('[data-action="send"]', form);
    const syncSend = () => {
      if (!send) return;
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
      else if (e.key === 'Escape' && attachWrap?.classList.contains('is-open')) { e.preventDefault(); closeAttachPanel(); return; }
      if (e.key === 'Enter' && !e.shiftKey && state.settingsChat.enterBehavior !== 'newline') { e.preventDefault(); submit(); return; }
      if (e.key === 'Enter' && (e.ctrlKey || e.metaKey) && state.settingsChat.enterBehavior === 'newline') { e.preventDefault(); submit(); }
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

  const settingsSig = () => state.settings
    ? [state.settingsSection, state.colorMode, state.lightTheme, state.darkTheme,
       state.voiceProvider, state.voiceLang, state.voiceName, state.appLang,
       state.plugins.length, state.plugins.map((p) => `${p.id}:${p.enabled ? 1 : 0}:${p.name}`).join(','),
       state.remotes.length, state.projects.length, state.projects.map((p) => `${p.id}:${p.name}:${(p.folders || []).join('|')}`).join(',')].join('|')
    : '';

  const refreshSettingsContent = (rootEl) => {
    const content = $('[data-settings-content]', rootEl);
    if (content) content.innerHTML = tplSettingsSection();
    $$('[data-settings-section]', rootEl).forEach((x) => {
      x.classList.toggle('bg-interactive-hover', x.dataset.settingsSection === state.settingsSection);
    });
    rootEl.dataset.sig = settingsSig();
    bindSettingsSection(rootEl);
  };

  /* Liga os controles da aba ativa (chamado a cada troca de seção). */
  const refreshModels = async (rootEl, useRemote) => {
    state.modelsLoading = true;
    refreshSettingsContent(rootEl);
    try {
      const payload = await ZenoBackend.models(!!useRemote);
      applyModels(payload);
      state.modelsError = payload.error || '';
      const status = await ZenoBackend.detect();
      if (status) { state.backend = status; state.backendOk = true; }
    } catch (error) {
      state.modelsError = error.message || 'Falha ao buscar modelos.';
      await ZenoBackend.detect();
      state.backendOk = ZenoBackend.ok;
      state.backend = ZenoBackend.status || state.backend;
    }
    state.modelsLoading = false;
    refreshSettingsContent(rootEl);
    render();
  };

  /* ---------- confirm overlay: escurece a tela antes de apagar ---------- */
  const settingsConfirm = (rootEl, title, message, onConfirm) => {
    document.querySelector('[data-settings-confirm]')?.remove();
    const overlay = document.createElement('div');
    overlay.dataset.settingsConfirm = 'true';
    overlay.className = 'zeno-confirm-overlay';
    overlay.innerHTML = `
      <div class="zeno-confirm-card" role="dialog" aria-modal="true" aria-label="${esc(title)}">
        <h3>${esc(title)}</h3>
        <p>${esc(message)}</p>
        <div class="zeno-confirm-actions">
          <button type="button" data-confirm-cancel class="zeno-confirm-btn">Cancelar</button>
          <button type="button" data-confirm-ok class="zeno-confirm-btn is-danger">Apagar</button>
        </div>
      </div>`;
    const close = () => { overlay.remove(); document.removeEventListener('keydown', onKey); };
    const onKey = (e) => { if (e.key === 'Escape') close(); };
    overlay.addEventListener('mousedown', (e) => { if (e.target === overlay) close(); });
    overlay.querySelector('[data-confirm-cancel]').addEventListener('click', close);
    overlay.querySelector('[data-confirm-ok]').addEventListener('click', () => { close(); onConfirm(); });
    document.addEventListener('keydown', onKey);
    document.body.appendChild(overlay);
  };

  /* Renomear inline: o editor aparece no lugar do texto do bloco. */
  const bindInlineRename = (rootEl, commit) => {
    $$('[data-name-edit]', rootEl).forEach((btn) => btn.addEventListener('click', () => {
      const block = btn.closest('[data-entity]');
      const id = block?.dataset.entity;
      const span = block?.querySelector('.zeno-block-name');
      if (!block || !span || block.querySelector('.zeno-block-name-input')) return;
      const current = span.textContent;
      const input = document.createElement('input');
      input.type = 'text';
      input.className = 'zeno-block-name-input';
      input.value = current;
      input.setAttribute('aria-label', 'Renomear');
      span.replaceWith(input);
      input.focus();
      input.select();
      let cancelled = false;
      input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { e.preventDefault(); input.blur(); }
        else if (e.key === 'Escape') { cancelled = true; input.blur(); }
      });
      input.addEventListener('blur', () => {
        const value = input.value.trim();
        if (!cancelled && value && value !== current) commit(id, value.slice(0, 120));
        refreshSettingsContent(rootEl);
        render();
      }, { once: true });
    }));
  };

  const bindSettingsSection = (rootEl) => {
    /* Integração ZenoC: aba Models */
    $('[data-action="models-save"]', rootEl)?.addEventListener('click', async () => {
      const value = (field) => ($(`[data-models-field="${field}"]`, rootEl)?.value || '').trim();
      let baseUrl = value('base_url');
      if (baseUrl) {
        baseUrl = baseUrl.replace(/\/chat\/completions\/?$/i, '').replace(/\/+$/, '');
        if (!/^https?:\/\//i.test(baseUrl)) baseUrl = `https://${baseUrl}`;
      }
      const payload = {
        provider: value('provider') || 'openai',
        base_url: baseUrl,
        model: value('model'),
        fallback_models: value('fallback_models'),
        workspace: value('workspace') || '.',
        agent_mode: value('agent_mode') || state.agentMode,
        require_approval: state.requireApproval,
      };
      const apiKey = value('api_key');
      if (apiKey) payload.api_key = apiKey;
      if (!payload.base_url) { flashSettingsNote(rootEl, 'Informe a Base URL do provider.'); return; }
      try {
        await ZenoBackend.saveConfig(payload);
        state.agentMode = payload.agent_mode;
        LS.set('oc-clone-agent-mode', state.agentMode);
        if (payload.model) { state.model = payload.model; LS.set('oc-clone-model', state.model); }
        state.backendOk = true;
        await refreshModels(rootEl, false);
        flashSettingsNote(rootEl, 'Configuração salva.');
      } catch (error) {
        flashSettingsNote(rootEl, `Falha ao salvar: ${error.message}`);
      }
    });
    $('[data-action="models-refresh"]', rootEl)?.addEventListener('click', () => { void refreshModels(rootEl, true); });
    $('[data-action="models-test"]', rootEl)?.addEventListener('click', async () => {
      flashSettingsNote(rootEl, 'Testando conexão com o provider…');
      let answer = '';
      try {
        await ZenoBackend.chat({ message: 'Responda apenas com a palavra: pong', session: `probe_${uid()}` }, (event) => {
          if (event.type === 'text') answer += event.delta || '';
        });
        flashSettingsNote(rootEl, answer.trim() ? `Conexão OK — ${state.model} respondeu.` : 'Conexão estabelecida, mas o provider não devolveu texto.');
      } catch (error) {
        flashSettingsNote(rootEl, `Falha na conexão: ${error.message || 'erro desconhecido'}`);
      }
    });
    $$('[data-action="models-pick"]', rootEl).forEach((button) => button.addEventListener('click', async () => {
      state.model = button.dataset.model;
      LS.set('oc-clone-model', state.model);
      try { await ZenoBackend.saveConfig({ model: state.model }); } catch {}
      refreshSettingsContent(rootEl);
      render();
    }));

    /* Selects dropdown (light/dark theme, lang, timefmt, orientation, startup) */
    $$('[data-select-menu]', rootEl).forEach((b) => b.addEventListener('click', (e) => {
      e.stopPropagation();
      closeMenus();
      const key = b.dataset.selectMenu;
      const title = key === 'light-theme' ? 'Light Theme' : key === 'dark-theme' ? 'Dark Theme' : key === 'lang' ? 'Language' : key === 'timefmt' ? 'Time Format' : key === 'orientation' ? 'Install Orientation' : key === 'startup' ? 'Startup' : 'Options';
      const opts = key === 'light-theme' ? themeList('light').map((t) => ({ id: t.id, name: t.name })) : key === 'dark-theme' ? themeList('dark').map((t) => ({ id: t.id, name: t.name })) : key === 'lang' ? [{ id: 'en', name: 'English' }, { id: 'pt', name: 'Português' }, { id: 'es', name: 'Español' }] : key === 'timefmt' ? [{ id: 'auto', name: 'Auto' }, { id: '12h', name: '12h' }, { id: '24h', name: '24h' }] : key === 'orientation' ? [{ id: 'system', name: 'Follow system' }, { id: 'portrait', name: 'Portrait' }, { id: 'landscape', name: 'Landscape' }] : [{ id: 'last', name: 'Continue last session' }, { id: 'new', name: 'Start new session' }];
      const current = key === 'light-theme' ? state.lightTheme : key === 'dark-theme' ? state.darkTheme : key === 'lang' ? state.appLang : key === 'timefmt' ? state.timeFormat : key === 'orientation' ? state.installOrientation : opts[0].id;
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
        if (key === 'lang') { state.appLang = val; LS.set('oc-clone-app-lang', val); }
        if (key === 'timefmt') { state.timeFormat = val; LS.set('oc-clone-time-format', val); }
        if (key === 'orientation') { state.installOrientation = val; LS.set('oc-clone-install-orientation', val); }
        el.remove();
        applyTheme();
        refreshSettingsContent(rootEl);
        render();
      }));
    }));
    /* Toggles genéricos: chat.*, notif.*, voice.autoSpeak */
    $$('[data-set-toggle]', rootEl).forEach((b) => b.addEventListener('click', () => {
      const path = b.dataset.setToggle;
      if (path === 'chat.showSuggestions') { state.settingsChat.showSuggestions = !state.settingsChat.showSuggestions; LS.set('oc-clone-settings-chat', state.settingsChat); }
      else if (path === 'chat.compactMode') { state.settingsChat.compactMode = !state.settingsChat.compactMode; LS.set('oc-clone-settings-chat', state.settingsChat); }
      else if (path === 'notif.enabled') { state.settingsNotif.enabled = !state.settingsNotif.enabled; LS.set('oc-clone-settings-notif', state.settingsNotif); }
      else if (path === 'notif.sound') { state.settingsNotif.sound = !state.settingsNotif.sound; LS.set('oc-clone-settings-notif', state.settingsNotif); }
      else if (path === 'notif.mentionOnly') { state.settingsNotif.mentionOnly = !state.settingsNotif.mentionOnly; LS.set('oc-clone-settings-notif', state.settingsNotif); }
      else if (path === 'notif.desktop') { state.settingsNotif.desktop = !state.settingsNotif.desktop; LS.set('oc-clone-settings-notif', state.settingsNotif); }
      else if (path === 'voice.autoSpeak') { state.voiceAutoSpeak = state.voiceAutoSpeak === false; LS.set('oc-clone-voice-autospeak', state.voiceAutoSpeak); }
      else return;
      refreshSettingsContent(rootEl);
      render();
    }));
    /* Selects/inputs: chat.*, voice.*, notif test, atalhos */
    $$('[data-set-field]', rootEl).forEach((el) => el.addEventListener('change', () => {
      const field = el.dataset.setField;
      const value = el.value;
      if (field === 'chat.fontSize') { state.settingsChat.fontSize = value; LS.set('oc-clone-settings-chat', state.settingsChat); }
      else if (field === 'chat.enterBehavior') { state.settingsChat.enterBehavior = value; LS.set('oc-clone-settings-chat', state.settingsChat); }
      else if (field === 'voice.provider') {
        state.voiceProvider = value === 'openai' ? 'openai' : 'deepgram';
        LS.set('oc-clone-voice-provider', state.voiceProvider);
        if (state.voiceProvider === 'openai') {
          if (state.voiceSttModel === 'nova-3' || state.voiceSttModel === 'nova-2') { state.voiceSttModel = 'whisper-1'; LS.set('oc-clone-voice-stt', state.voiceSttModel); }
          if (state.voiceTtsModel === 'aura-asteria-en') { state.voiceTtsModel = 'tts-1'; LS.set('oc-clone-voice-tts', state.voiceTtsModel); }
          if (state.voiceName === 'aura-asteria-en') { state.voiceName = 'alloy'; LS.set('oc-clone-voice-name', state.voiceName); }
        }
      }
      else if (field === 'voice.lang') { state.voiceLang = value; LS.set('oc-clone-voice-lang', value); }
      else if (field === 'voice.name') { state.voiceName = value; LS.set('oc-clone-voice-name', value); }
      else if (field === 'voice.stt') { state.voiceSttModel = value; LS.set('oc-clone-voice-stt', value); }
      else if (field === 'voice.tts') { state.voiceTtsModel = value; LS.set('oc-clone-voice-tts', value); }
      else if (field === 'voice.openaiKey') { LS.set('oc-clone-voice-openai-key', value); }
      else if (field === 'voice.deepgramKey') { LS.set('oc-clone-voice-deepgram-key', value); deepgramConfigCache = null; }
      else if (field === 'app.installName') { state.installAppName = value.trim().slice(0, 60) || 'OpenChamber'; LS.set('oc-clone-install-app-name', state.installAppName); }
      else return;
      refreshSettingsContent(rootEl);
    }));
    $$('[data-shortcut]', rootEl).forEach((el) => el.addEventListener('change', () => {
      const key = el.dataset.shortcut;
      if (state.shortcuts[key] === undefined) return;
      state.shortcuts[key] = el.value.trim() || state.shortcuts[key];
      LS.set('oc-clone-shortcuts', state.shortcuts);
      refreshSettingsContent(rootEl);
    }));
    $('[data-action="notif-test"]', rootEl)?.addEventListener('click', () => {
      try {
        if (state.settingsNotif.desktop && 'Notification' in window) {
          if (Notification.permission === 'granted') new Notification('Zeno Agent', { body: 'Notificações funcionando.' });
          else if (Notification.permission !== 'denied') Notification.requestPermission().then((p) => { if (p === 'granted') new Notification('Zeno Agent', { body: 'Notificações funcionando.' }); });
          else flashSettingsNote(rootEl, 'Permissão de notificação bloqueada no navegador.');
        } else flashSettingsNote(rootEl, state.settingsNotif.enabled ? 'Notificações ativadas (banner do SO desligado).' : 'Ative “Enable notifications” primeiro.');
      } catch { flashSettingsNote(rootEl, 'Notificações indisponíveis neste runtime.'); }
    });
    $('[data-action="voice-test"]', rootEl)?.addEventListener('click', async () => {
      flashSettingsNote(rootEl, `Falando via ${state.voiceProvider === 'openai' ? 'OpenAI' : 'Deepgram'}…`);
      await voiceSpeakNative(voiceGreeting());
    });
    $('[data-action="shortcuts-reset"]', rootEl)?.addEventListener('click', () => {
      state.shortcuts = { newChat: 'Ctrl+N', palette: 'Ctrl+K', settings: 'Ctrl+,', voice: 'Ctrl+Shift+V' };
      LS.set('oc-clone-shortcuts', state.shortcuts);
      refreshSettingsContent(rootEl);
    });
    /* Projects / Remote Instances / Plugins: grid de blocos com renomear inline */
    bindInlineRename(rootEl, (id, name) => {
      const project = state.projects.find((p) => p.id === id);
      if (project) { project.name = name; LS.set('oc-clone-projects', state.projects); return; }
      const remote = state.remotes.find((r) => r.id === id);
      if (remote) { remote.name = name.slice(0, 80); persistRemotes(); return; }
      const plugin = state.plugins.find((p) => p.id === id);
      if (plugin) { plugin.name = name.slice(0, 80); persistPlugins(); }
    });
    /* Apagar: escurece a tela atrás e pede confirmação. */
    $$('[data-entity] [data-delete]', rootEl).forEach((btn) => btn.addEventListener('click', () => {
      const block = btn.closest('[data-entity]');
      const id = block?.dataset.entity;
      if (!id) return;
      const project = state.projects.find((p) => p.id === id);
      const remote = state.remotes.find((r) => r.id === id);
      const plugin = state.plugins.find((p) => p.id === id);
      const label = project?.name || remote?.name || plugin?.name || 'este item';
      settingsConfirm(rootEl, `Apagar “${label}”?`, 'Essa ação não pode ser desfeita.', () => {
        if (project) { deleteProjectById(id); return; }
        if (remote) { state.remotes = state.remotes.filter((r) => r.id !== id); persistRemotes(); }
        else if (plugin) { state.plugins = state.plugins.filter((p) => p.id !== id); persistPlugins(); applyPluginCss(); }
        render();
      });
    }));
    /* Remote Instances: bloco "adicionar" abre o formulário SSH. */
    const toggleRemoteForm = (open) => {
      state.remoteFormOpen = open;
      refreshSettingsContent(rootEl);
      if (open) $('[data-ssh-field="name"]', rootEl)?.focus();
    };
    const remoteAddEl = $('[data-remote-add]', rootEl);
    remoteAddEl?.addEventListener('click', () => toggleRemoteForm(!state.remoteFormOpen));
    remoteAddEl?.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleRemoteForm(!state.remoteFormOpen); } });
    $('[data-remote-add-cancel]', rootEl)?.addEventListener('click', () => toggleRemoteForm(false));
    $('[data-remote-ssh-form]', rootEl)?.addEventListener('submit', (e) => {
      e.preventDefault();
      const field = (f) => ($(`[data-ssh-field="${f}"]`, rootEl)?.value || '').trim();
      const name = field('name');
      const user = field('user') || 'root';
      const host = field('host');
      const port = Number(field('port')) || 22;
      const password = field('password');
      const errEl = $('[data-remote-error]', rootEl);
      if (!name || !host) { if (errEl) errEl.textContent = 'Informe nome e host da instância.'; return; }
      if (port < 1 || port > 65535) { if (errEl) errEl.textContent = 'Porta inválida (1–65535).'; return; }
      state.remotes.push({ id: 'rem_' + uid(), name: name.slice(0, 80), type: 'ssh', user, host, port, password, url: '', createdAt: new Date().toISOString() });
      persistRemotes();
      toggleRemoteForm(false);
      render();
    });
    /* Copiar comando ssh do bloco */
    $$('[data-remote-copy]', rootEl).forEach((b) => b.addEventListener('click', async () => {
      const remote = state.remotes.find((r) => r.id === b.dataset.remoteCopy);
      if (!remote) return;
      try { await navigator.clipboard.writeText(remoteConnectCmd(remote)); flashSettingsNote(rootEl, 'Comando copiado.'); }
      catch { flashSettingsNote(rootEl, remoteConnectCmd(remote)); }
    }));
    /* Plugins: ativar/desativar pelo interruptor do bloco */
    $$('[data-plugin-toggle]', rootEl).forEach((b) => b.addEventListener('click', () => {
      const plugin = state.plugins.find((p) => p.id === b.dataset.pluginToggle);
      if (!plugin) return;
      plugin.enabled = !plugin.enabled;
      persistPlugins();
      applyPluginCss();
      refreshSettingsContent(rootEl);
      render();
    }));
  };

  const flashSettingsNote = (rootEl, text) => {
    const content = $('[data-settings-content]', rootEl);
    if (!content) return;
    content.querySelector('[data-settings-note]')?.remove();
    const note = document.createElement('div');
    note.dataset.settingsNote = 'true';
    note.className = 'zeno-composer-hint';
    note.style.position = 'sticky';
    note.textContent = text;
    content.prepend(note);
    setTimeout(() => note.remove(), 2600);
  };

  const bindSettings = (rootEl) => {
    $('[data-action="close-settings"]', rootEl)?.addEventListener('click', () => { state.settings = false; render(); });
    rootEl.addEventListener('mousedown', (e) => { if (e.target === rootEl) { state.settings = false; render(); } });
    $$('[data-settings-section]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.settingsSection = b.dataset.settingsSection;
      refreshSettingsContent(rootEl);
    }));
    /* Modo de cor: seletor segmentado com ✓ no item escolhido. */
    $$('[data-color-mode]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.colorMode = b.dataset.colorMode;
      LS.set('oc-clone-color-mode', state.colorMode);
      applyTheme();
      refreshSettingsContent(rootEl);
      render();
    }));
    /* Tipo de gráfico do Usage. */
    $$('[data-usage-chart]', rootEl).forEach((b) => b.addEventListener('click', () => {
      state.usageChart = b.dataset.usageChart;
      LS.set('oc-clone-usage-chart', state.usageChart);
      refreshSettingsContent(rootEl);
    }));
    /* Clicar numa conversa do Usage abre ela no workspace. */
    $$('[data-usage-session]', rootEl).forEach((b) => b.addEventListener('click', () => {
      const id = b.dataset.usageSession;
      if (!state.sessions.some((s) => s.id === id)) return;
      state.activeId = id;
      state.draftNew = false;
      state.workspace = 'chat';
      state.settings = false;
      LS.set('oc-clone-active', id);
      persistWorkspace();
      render();
    }));
    /* Appearance/Chat: os selects de tema/idioma/formato/orientação usam o menu acima */
    $('[data-action="reload-themes"]', rootEl)?.addEventListener('click', () => { applyTheme(); refreshSettingsContent(rootEl); render(); });
    bindSettingsSection(rootEl);
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
    const base = {
      role: 'assistant', time, model: state.model, agent: 'build',
      id: 'msg_' + uid(),
    };
    /* Plugin builder: o agente gera o JSON, salva automaticamente e libera. */
    if (/(criar?|crie|create|novo)\s+(um\s+|uma\s+)?plugin/i.test(t)) {
      const hintMatch = text.match(/["“]([^"”]+)["”]/) || text.match(/chamad[oa]?\s+([^\n,.]+)/i) || text.match(/named?\s+([^\n,.]+)/i);
      const plugin = pluginCreate(text, hintMatch ? hintMatch[1].trim() : '');
      if (plugin) {
        if (state.plugins.some((p) => p.id === plugin.id)) plugin.id = `${plugin.id}-${uid().slice(0, 4)}`;
        state.plugins.push(plugin);
        persistPlugins();
        applyPluginCss();
        return { ...base, thinking: `The user asked for a new plugin. I generated the ${plugin.id} JSON, validated the required fields, and saved it so it is available immediately.`, text: `Pronto! Criei e salvei o plugin **${plugin.name}** (\`${plugin.id}\`) — já está ativo e disponível para uso.\n\n\`\`\`json\n${JSON.stringify(plugin, null, 2)}\n\`\`\``, suggestion: 'Liste meus plugins ativos.' };
      }
    }
    {
      const toggle = t.match(/(ativar?|ative|desativar?|desative)\s+(o\s+)?plugin\s+([a-z0-9][a-z0-9-]*)/i);
      if (toggle) {
        const plug = state.plugins.find((p) => p.id === toggle[3].toLowerCase());
        if (plug) {
          plug.enabled = /desativ/i.test(toggle[1]) ? false : true;
          persistPlugins();
          applyPluginCss();
          render();
          return { ...base, text: `Plugin **${plug.name}** ${plug.enabled ? 'ativado' : 'desativado'}.`, suggestion: 'Liste meus plugins ativos.' };
        }
      }
    }
    if (t.includes('array') || t.includes('foreach') || t.includes('map')) {
      return { ...base, thinking: 'The user wants to iterate over an array. A classic for loop with an index is the most direct answer — access each element by position from 0 to length-1.', text: 'Percorrer um array com `for` acessa cada elemento pelo índice, do primeiro ao último.', code: { lang: 'js', lines: [['const', 'keyword'], [' ', null], ['nums', 'variable'], [' =', 'operator'], [' [1', 'number'], [',', 'operator'], [' 2', 'number'], [',', 'operator'], [' 3', 'number'], ['];', 'operator'], ['\n', null], ['for', 'keyword'], [' (', null], ['let', 'keyword'], [' ', null], ['i', 'variable'], [' =', 'operator'], [' 0', 'number'], [';', 'operator'], [' ', null], ['i', 'variable'], [' <', 'operator'], [' ', null], ['nums', 'variable'], ['.length', 'operator'], [';', 'operator'], [' ', null], ['i', 'variable'], ['++', 'operator'], [')', 'operator'], [' ', null], ['console.log', 'function'], ['(', null], ['nums', 'variable'], ['[', 'operator'], ['i', 'variable'], [']', 'operator'], [')', 'operator'], [';', 'operator']] }, suggestion: 'E como fazer o mesmo com `forEach` ou `map`?' };
    }
    if (t.includes('arquivo') || t.includes('file') || t.includes('list') || t.includes('bash') || t.includes('tool')) {
      return { ...base, thinking: 'The user asks to list files. I should use the shell tool to run a directory listing and report the output back.', tools: [{ title: 'Shell Command', cmd: 'Get-ChildItem -Path "C:\\Users\\GM METELURGICA"', duration: '5.6s', output: DIR_OUTPUT }], text: 'Aqui está a listagem do diretório atual usando **Shell Command**:\n\nO diretório contém pastas de configuração e projetos, incluindo `Pictures`, `AppData`, `Downloads` e `.ollama`.', suggestion: 'Liste também os arquivos do projeto GUI Zeno.' };
    }
    if (t.includes('função') || t.includes('function') || t.includes('python')) {
      return { ...base, thinking: 'They want a function example. A named function with parameters and a return statement is the clearest illustration in JavaScript.', text: 'Uma **função** em JavaScript é um bloco reutilizável de código que recebe entradas (parâmetros) e pode devolver um resultado com `return`.', code: { lang: 'js', lines: [['function', 'keyword'], [' ', null], ['soma', 'variable'], ['(', null], ['a', 'variable'], [',', 'operator'], [' ', null], ['b', 'variable'], [')', 'operator'], [' ', null], ['{', 'operator'], ['\n', null], ['  ', null], ['return', 'keyword'], [' ', null], ['a', 'variable'], [' +', 'operator'], [' ', null], ['b', 'variable'], [';', 'operator'], ['\n', null], ['}', 'operator']] }, suggestion: 'Explique arrow functions e quando usá-las.' };
    }
    return { ...base, thinking: 'The request is open-ended. I will acknowledge it, restate the core ask briefly, and offer to go deeper with concrete examples.', text: 'Entendi seu pedido: **"' + text.slice(0, 60) + (text.length > 60 ? '…' : '') + '"**. Em resumo, é possível fazer isso de forma direta e eficiente, separando o problema em etapas pequenas e verificando cada resultado parcial antes de avançar. Se quiser, posso detalhar com exemplos de código.' + text.slice(0, 48) + '". Foco em solução direta com verificação por etapas.', suggestion: 'Pode dar um exemplo prático disso?' };
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
            ${[['oc-palette', 'Toggle theme'], ['oc-settings-3', 'Open settings'], ['oc-global', 'Toggle Browser panel']].map(([ic, label]) => `
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
      if (label === 'Toggle Browser panel') { state.panel = state.panel === 'Browser' ? null : 'Browser'; LS.set('oc-clone-panel', state.panel); render(); }
    }));
  };

  /* ---------- boot ---------- */
  const boot = () => {
    applyTheme();
    applyPluginCss();
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
              </div>
            </div>
          </div>
        </div>
      </div>
      <div data-settings-root></div>
      <div data-action-modal-root></div>
      <div data-variant-root></div>`;

    bindGlobal();
    const sb = $('[data-sidebar-root]');
    sb.innerHTML = tplSidebar();
    bindSidebar(sb);
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

    /* Conecta ao agente ZenoC em segundo plano. Se o backend não responder,
       tudo segue no modo simulado sem quebrar a UI. */
    void initBackend();
  };

  const bindGlobal = () => {
    const matchShortcut = (e, combo) => {
      const parts = String(combo || '').split('+').map((p) => p.trim().toLowerCase());
      if (!parts.length) return false;
      const key = parts[parts.length - 1];
      const wantCtrl = parts.includes('ctrl') || parts.includes('cmd');
      const wantShift = parts.includes('shift');
      const wantAlt = parts.includes('alt');
      if (wantCtrl && !(e.ctrlKey || e.metaKey)) return false;
      if (wantShift && !e.shiftKey) return false;
      if (wantAlt && !e.altKey) return false;
      return e.key.toLowerCase() === key;
    };
    $('[data-topbar-toggle]')?.addEventListener('click', () => {
      state.sidebarOpen = !state.sidebarOpen;
      refreshSidebar();
    });
    document.addEventListener('keydown', (e) => {
      if (matchShortcut(e, state.shortcuts.palette || 'Ctrl+K')) {
        e.preventDefault();
        togglePalette();
      }
      if (matchShortcut(e, state.shortcuts.settings || 'Ctrl+,')) {
        e.preventDefault();
        state.settings = !state.settings;
        render();
      }
      if (matchShortcut(e, state.shortcuts.newChat || 'Ctrl+N')) {
        e.preventDefault();
        state.workspace = 'chat';
        state.noteId = null;
        state.panel = null;
        state.draftNew = true;
        persistWorkspace();
        render();
      }
      if (matchShortcut(e, state.shortcuts.voice || 'Ctrl+Shift+V')) {
        e.preventDefault();
        setVoiceMode(!state.voiceMode);
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
