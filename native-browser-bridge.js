(() => {
  'use strict';
  const native = () => typeof window.ZenoOpenBrowser === 'function';
  const nativeUrl = (value) => {
    const raw = String(value || '').trim();
    if (!raw || raw === 'about:blank') return 'about:blank';
    if (/^https?:\/\//i.test(raw)) return raw;
    if (/^[\w.-]+\.[a-z]{2,}(\/.*)?$/i.test(raw)) return `https://${raw}`;
    return `https://www.startpage.com/sp/search?query=${encodeURIComponent(raw)}`;
  };
  const syncBounds = () => {
    if (!native() || typeof window.ZenoSetBrowserBounds !== 'function') return;
    const host = document.querySelector('[data-native-browser-host]');
    if (!host) return;
    const rect = host.getBoundingClientRect();
    window.ZenoSetBrowserBounds(Math.round(rect.left), Math.round(rect.top), Math.max(1, Math.round(rect.width)), Math.max(1, Math.round(rect.height)));
  };
  window.ZenoNativeBrowser = {
    available: native,
    open(value) { if (native()) { window.ZenoOpenBrowser(nativeUrl(value)); requestAnimationFrame(syncBounds); return true; } return false; },
    navigate(value) { if (native()) { window.ZenoNavigateBrowser(nativeUrl(value)); return true; } return false; },
    back() { if (native()) window.ZenoBrowserBack(); },
    forward() { if (native()) window.ZenoBrowserForward(); },
    reload() { if (native()) window.ZenoBrowserReload(); },
    close() { if (native() && typeof window.ZenoCloseBrowser === 'function') window.ZenoCloseBrowser(); },
    syncBounds,
  };
  window.addEventListener('resize', () => requestAnimationFrame(syncBounds));
  new MutationObserver(() => requestAnimationFrame(syncBounds)).observe(document.documentElement, { childList: true, subtree: true, attributes: true });
})();
