# Zeno Browser nativo

Shell desktop baseado em **Ultralight/WebCore**, sem Chrome, Chromium, CEF, Electron ou WebView2.

## Build

1. Crie uma conta e baixe o SDK Windows em <https://ultralig.ht/download>.
2. Extraia o SDK em `ultralight-quick/SDK`.
3. Execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\ultralight-quick\build.ps1
```

Binário esperado:

```text
ultralight-quick/build/out/ZenoBrowser/ZenoBrowser.exe
```

## Realidade técnica

- O motor é WebCore/JavaScriptCore, não Chromium.
- A UI do Zeno já é empacotada em `ultralight-quick/assets`.
- O painel Browser vira uma segunda `View` nativa: sites reais não passam por iframe e ignoram `X-Frame-Options`/`frame-ancestors` como um navegador normal.
- A barra da UI controla URL, voltar, avançar, recarregar, fechar e redimensionar por bridge JS↔C++.
- O SDK é obrigatório e exige login/licença; não pode ser fabricado pelo repositório.
- Ultralight é leve, mas não garante compatibilidade integral com DRM, extensões, WebRTC ou todos os sites modernos.
