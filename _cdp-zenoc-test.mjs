// Teste CDP da integração ZenoC (backend real → UI).
// Uso: node _cdp-zenoc-test.mjs   (com Chrome em --remote-debugging-port=9333)
const PORT = process.env.CDP_PORT || '9333';
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const targets = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
const page = targets.find((t) => t.type === 'page' && t.url.includes('index.html'));
if (!page) { console.error('no page found at CDP'); process.exit(1); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve) => ws.addEventListener('open', resolve));
let id = 0;
const pending = new Map();
const consoleErrors = [];
ws.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) { pending.get(message.id)(message); pending.delete(message.id); return; }
  if (message.method === 'Runtime.exceptionThrown') {
    consoleErrors.push(message.params?.exceptionDetails?.exception?.description || 'exception');
  }
  if (message.method === 'Runtime.consoleAPICalled' && message.params?.type === 'error') {
    consoleErrors.push((message.params.args || []).map((a) => a.value || a.description || '').join(' '));
  }
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
await evaluate('localStorage.clear()');
await send('Page.reload', { ignoreCache: true });
await sleep(2600);

const out = { consoleErrors: [] };
out.boot = await evaluate(`(() => ({
  rootFilled: document.querySelector('#chat-root').children.length > 0,
  composer: !!document.querySelector('[data-composer-input]'),
  attachModes: [...document.querySelectorAll('[data-attach-mode]')].map((b) => b.dataset.attachMode),
}))()`);

// Abre Settings → Models
out.models = await evaluate(`(async () => {
  document.querySelector('[data-action="open-settings"]').click();
  await new Promise((r) => setTimeout(r, 300));
  document.querySelector('[data-settings-section="Models"]').click();
  await new Promise((r) => setTimeout(r, 600));
  const content = document.querySelector('[data-settings-content]');
  return {
    hasTitle: !!content && content.textContent.includes('Models'),
    online: !!content && /ZenoC 1\\.[0-9.]+ online/.test(content.textContent),
    modelChips: [...content.querySelectorAll('[data-action="models-pick"]')].map((b) => b.dataset.model),
    baseUrl: content.querySelector('[data-models-field="base_url"]')?.value || '',
    statusText: (content.textContent.match(/ZenoC[^\\n]{0,40}/) || [''])[0].trim(),
  };
})()`);

// Fecha settings e abre a aba Modelo do composer (deve listar os modelos do backend)
out.attachModels = await evaluate(`(async () => {
  document.querySelector('[data-action="close-settings"]').click();
  await new Promise((r) => setTimeout(r, 300));
  document.querySelector('[data-attach-mode="model"]').click();
  const options = [...document.querySelectorAll('[data-attach-model]')].map((b) => b.dataset.attachModel);
  document.querySelector('[data-attach-wrap]').classList.remove('is-open');
  return { options };
})()`);

// Memory: barra do backend e notas sincronizadas
out.memory = await evaluate(`(async () => {
  document.querySelector('[data-workspace="memory"]').click();
  await new Promise((r) => setTimeout(r, 1200));
  const bar = document.querySelector('.memory-backend-bar');
  return {
    barText: bar ? bar.textContent.replace(/\\s+/g, ' ').trim() : '',
    notes: [...document.querySelectorAll('[data-memory-note]')].map((n) => n.dataset.memoryNote).filter((id) => id.startsWith('mem_')).length,
    hasRefresh: !!document.querySelector('[data-action="memory-refresh"]'),
  };
})()`);

// Volta ao chat e envia uma mensagem (chave dummy → erro do provider, mas com Pensamento)
out.chat = await evaluate(`(async () => {
  document.querySelector('[data-new-chat]')?.click?.();
  const newChat = document.querySelector('[data-action="new-session"]');
  if (newChat) newChat.click();
  await new Promise((r) => setTimeout(r, 300));
  const input = document.querySelector('[data-composer-input]');
  input.innerText = 'Diga apenas OK';
  input.dispatchEvent(new Event('input', { bubbles: true }));
  const form = document.querySelector('[data-composer-form]');
  form.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
  await new Promise((r) => setTimeout(r, 6000));
  const messages = [...document.querySelectorAll('[data-message-role="assistant"]')];
  const last = messages[messages.length - 1];
  return {
    userMessages: document.querySelectorAll('[data-message-role="user"]').length,
    assistantMessages: messages.length,
    lastText: last ? last.textContent.replace(/\\s+/g, ' ').slice(0, 320) : '',
    hasThinking: !!last && last.textContent.includes('Pensamento'),
    hasToolRow: !!last && !!last.querySelector('[data-tool-key]'),
    hasError: !!last && !!last.querySelector('.zeno-run-error'),
  };
})()`);

await sleep(500);
out.consoleErrors = consoleErrors.slice(0, 12);
console.log(JSON.stringify(out, null, 2));
ws.close();
