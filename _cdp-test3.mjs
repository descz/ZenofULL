// CDP smoke test for the composer "+" hover menu and spacing.
const PORT = process.env.CDP_PORT || '9333';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const targets = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
const page = targets.find((t) => t.type === 'page' && t.url.includes('index.html'));
if (!page) { console.error('no page'); process.exit(1); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((res) => ws.addEventListener('open', res));
let id = 0; const pending = new Map();
ws.addEventListener('message', (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
const send = (method, params = {}) => new Promise((res) => { const mid = ++id; pending.set(mid, res); ws.send(JSON.stringify({ id: mid, method, params })); });
const evaluate = async (expression) => {
  const res = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (res.result?.exceptionDetails) throw new Error(JSON.stringify(res.result.exceptionDetails));
  return res.result?.result?.value;
};
await send('Runtime.enable');
await evaluate(`localStorage.clear()`);
await send('Page.enable');
await send('Page.reload', { ignoreCache: true });
await sleep(1600);

const out = {};
out.spacing = await evaluate(`(() => {
  const editor = document.querySelector('.composer-editor');
  const footer = document.querySelector('[data-chat-input-footer]');
  const cs = getComputedStyle(editor);
  const fs = getComputedStyle(footer);
  return { paddingTop: cs.paddingTop, paddingBottom: cs.paddingBottom, minHeight: cs.minHeight, footerMinHeight: fs.minHeight, footerPaddingTop: fs.paddingTop, footerPaddingBottom: fs.paddingBottom };
})()`);

out.menu = await evaluate(`(() => {
  const menu = document.querySelector('.zeno-attach-menu');
  const cs = getComputedStyle(menu);
  return {
    items: [...document.querySelectorAll('[data-attach-mode]')].map(b => b.dataset.attachMode),
    defaultOpacity: cs.opacity,
    defaultVisibility: cs.visibility,
    panelHidden: document.querySelector('[data-attach-panel]').hidden,
  };
})()`);

out.plusClick = await evaluate(`(() => {
  document.querySelector('[data-action="attach"]').click();
  const panel = document.querySelector('[data-attach-panel]');
  const wrap = document.querySelector('[data-attach-wrap]');
  return {
    isOpen: wrap.classList.contains('is-open'),
    mode: panel.dataset.attachMode,
    hasDrop: !!panel.querySelector('[data-attach-drop]'),
    hasBrowse: !!panel.querySelector('[data-attach-browse]'),
    panelVisibility: getComputedStyle(panel).visibility,
  };
})()`);

out.model = await evaluate(`(() => {
  // close then open model panel via the hover-menu button
  document.querySelector('[data-attach-wrap]').classList.remove('is-open');
  document.querySelector('[data-attach-mode="model"]').click();
  const opts = [...document.querySelectorAll('[data-attach-model]')].map(b => b.dataset.attachModel);
  const target = opts.find(n => n !== 'DeepSeek V4 Pro') || opts[1];
  document.querySelector('[data-attach-model="' + target + '"]').click();
  return {
    options: opts,
    picked: target,
    storedModel: JSON.parse(localStorage.getItem('oc-clone-model')),
    menuLabel: document.querySelector('[data-attach-mode="model"] small').textContent,
    panelHiddenAfterPick: document.querySelector('[data-attach-panel]').hidden,
  };
})()`);

out.thinking = await evaluate(`(() => {
  document.querySelector('[data-attach-mode="thinking"]').click();
  const opts = [...document.querySelectorAll('[data-attach-thinking]')].map(b => b.dataset.attachThinking);
  const target = 'High';
  document.querySelector('[data-attach-thinking="' + target + '"]').click();
  return {
    optionsCount: opts.length,
    picked: target,
    storedDesign: JSON.parse(localStorage.getItem('oc-clone-design')),
    menuLabel: document.querySelector('[data-attach-mode="thinking"] small').textContent,
  };
})()`);

out.escape = await evaluate(`(() => {
  document.querySelector('[data-action="attach"]').click();
  const wrap = document.querySelector('[data-attach-wrap]');
  const input = document.querySelector('[data-composer-input]');
  input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
  return { closedByEscape: !wrap.classList.contains('is-open') };
})()`);

console.log(JSON.stringify(out, null, 2));
ws.close();
