// Teste CDP ponta a ponta com provider mock: tools + memória + Pensamento.
const PORT = process.env.CDP_PORT || '9333';
const API = 'http://localhost:8080';
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const targets = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
const page = targets.find((t) => t.type === 'page' && t.url.includes('index.html'));
if (!page) { console.error('no page'); process.exit(1); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve) => ws.addEventListener('open', resolve));
let id = 0;
const pending = new Map();
const consoleErrors = [];
ws.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) { pending.get(message.id)(message); pending.delete(message.id); return; }
  if (message.method === 'Runtime.exceptionThrown') consoleErrors.push(message.params?.exceptionDetails?.exception?.description || 'exception');
});
const send = (method, params = {}) => new Promise((resolve) => {
  const messageId = ++id;
  pending.set(messageId, resolve);
  ws.send(JSON.stringify({ id: messageId, method, params }));
});
const evaluate = async (expression) => {
  const response = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (response.result?.exceptionDetails) throw new Error(JSON.stringify(response.result.exceptionDetails));
  return response.result?.result?.value;
};
await send('Runtime.enable');
await send('Page.enable');

// Configura o provider mock direto no backend
const configResponse = await fetch(`${API}/api/zenoc/config`, {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ provider: 'openai', base_url: 'http://127.0.0.1:8123/v1', api_key: 'mock-key', model: 'mock-gpt', workspace: '.', agent_mode: 'full' }),
});
console.log('config:', configResponse.status);

await evaluate('localStorage.clear()');
await send('Page.reload', { ignoreCache: true });
await sleep(2600);

const out = {};
out.models = await evaluate(`(async () => {
  const payload = await (await fetch('/api/zenoc/models?refresh=1')).json();
  return { models: payload.models, error: payload.error, source: payload.source };
})()`);

out.chat = await evaluate(`(async () => {
  const input = document.querySelector('[data-composer-input]');
  input.innerText = 'Rode o teste completo';
  input.dispatchEvent(new Event('input', { bubbles: true }));
  document.querySelector('[data-composer-form]').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
  await new Promise((r) => setTimeout(r, 12000));
  const messages = [...document.querySelectorAll('[data-message-role="assistant"]')];
  const last = messages[messages.length - 1];
  const toolKeys = last ? [...last.querySelectorAll('[data-tool-key]')].map((el) => el.dataset.toolKey) : [];
  return {
    assistantMessages: messages.length,
    lastText: last ? last.textContent.replace(/\\s+/g, ' ').slice(0, 400) : '',
    hasThinking: !!last && last.textContent.includes('Pensamento'),
    toolKeys,
    toolTitles: last ? [...last.querySelectorAll('.group\\\\/tool span:first-child')].map((el) => el.textContent.trim()).slice(0, 6) : [],
    hasError: !!last && !!last.querySelector('.zeno-run-error'),
    footer: last ? (last.textContent.match(/mock-gpt/) || [''])[0] : '',
  };
})()`);

out.memory = await evaluate(`(async () => {
  document.querySelector('[data-workspace="memory"]').click();
  await new Promise((r) => setTimeout(r, 1200));
  const titles = [...document.querySelectorAll('[data-memory-note] .memory-node-label')].map((el) => el.textContent);
  return { hasMockNote: titles.includes('Memória do mock'), total: titles.length };
})()`);

out.consoleErrors = consoleErrors.slice(0, 10);
console.log(JSON.stringify(out, null, 2));
ws.close();
