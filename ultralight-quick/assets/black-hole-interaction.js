(() => {
  'use strict';
  let dragging = false;
  let lastX = 0;
  let lastY = 0;
  const frame = () => document.querySelector('.workspace-black-hole');
  const send = (payload) => frame()?.contentWindow?.postMessage({ source: 'zeno-black-hole', ...payload }, '*');
  const isControl = (target) => !!target.closest('button, input, textarea, [contenteditable="true"], [data-quick-action], a');

  document.addEventListener('pointerdown', (event) => {
    if (!document.querySelector('.empty-chat-stage') || isControl(event.target)) return;
    dragging = true;
    lastX = event.clientX;
    lastY = event.clientY;
  }, true);
  document.addEventListener('pointermove', (event) => {
    if (!dragging) return;
    const dx = event.clientX - lastX;
    const dy = event.clientY - lastY;
    lastX = event.clientX;
    lastY = event.clientY;
    send({ type: 'drag', dx, dy });
  }, true);
  document.addEventListener('pointerup', () => { dragging = false; }, true);
  document.addEventListener('pointercancel', () => { dragging = false; }, true);
  document.addEventListener('wheel', (event) => {
    if (!document.querySelector('.empty-chat-stage') || isControl(event.target)) return;
    event.preventDefault();
    send({ type: 'wheel', deltaY: event.deltaY });
  }, { capture: true, passive: false });
})();
