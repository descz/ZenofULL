---
name: zenox-scraper
description: >-
  Enterprise-grade web scraping and data extraction framework using Playwright.
  Use this skill when the user wants to extract data from websites, scrape dynamic
  content, crawl pages, monitor changes, capture screenshots, or convert web pages
  to structured data (CSV, JSON, XLSX). Handles JavaScript-rendered content,
  pagination, authentication, login walls, infinite scroll, CAPTCHA detection,
  anti-bot countermeasures, and large-scale extraction with stealth mode.
  Triggers on: "scrape", "extract", "crawl", "spider", "collect data",
  "get all", "download from site", "harvest", "data mining",
  "web scraping", "automated extraction".
---

# Zenox Scraper Framework

Framework enterprise de scraping e extração de dados com Playwright + Python.

## Princípios

- **Respeito ao robots.txt.** Sempre verifique antes de escalar.
- **Rate limiting inteligente.** Delay adaptativo entre requests para não sobrecarregar.
- **Stealth mode ativo.** Headers realísticos, fingerprint spoofing, e anti-detecção.
- **Dados estruturados sempre.** Saída em JSON, CSV, ou XLSX.
- **Resiliência total.** Retry com backoff exponencial, checkpoint, e resume.

## Fluxo de Decisão

```
Usuário quer dados de um site →
  ├── É página única estática?
  │     ├─ Sim → Extração direta com selectors
  │     └─ Não → Requer scraping dinâmico
  │
  ├── Tem paginação/infinite scroll?
  │     ├─ Sim → Usar scroll + coleta incremental
  │     └─ Não → Extração única
  │
  ├── Precisa de login?
  │     ├─ Sim → Fluxo de autenticação + sessão persistente
  │     └─ Não → Acesso direto
  │
  └── Tem anti-bot?
        ├─ Sim → Stealth mode + fingerprint rotation
        └─ Não → Scraping padrão
```

## Uso

Sempre use o script `scripts/scraper_engine.py` como motor principal.
NÃO leia o source do script a menos que precise de customização extrema.

### Extração Simples

```bash
python scripts/scraper_engine.py --target <URL> --output ./data --format json
```

### Extração com Selectors Específicos

```bash
python scripts/scraper_engine.py --target <URL> --selector "div.product" --fields "title:h1, price:.price, link:a@href" --output ./produtos
```

### Crawl Multi-página

```bash
python scripts/scraper_engine.py --target <URL> --crawl --max-pages 100 --output ./crawl --delay 2
```

### Scraping com Login

```bash
python scripts/scraper_engine.py --target <URL> --auth --auth-url <login_url> --username-field email --password-field pass --username <email> --password <pass> --output ./data
```

### Stealth Mode (Anti-bot)

```bash
python scripts/scraper_engine.py --target <URL> --stealth --rotate-ua --output ./stealth_data
```

### Extrair Todas as URLs

```bash
python scripts/scraper_engine.py --target <URL> --extract-links --output ./links.json
```

### Screenshot + PDF

```bash
python scripts/scraper_engine.py --target <URL> --screenshot --pdf --output ./captures
```

## Helper Script (Python + Playwright)

```python
from playwright.sync_api import sync_playwright
import json, time

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    context = browser.new_context(
        user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/120.0.0.0",
        viewport={"width": 1920, "height": 1080},
        locale="pt-BR",
        timezone_id="America/Sao_Paulo"
    )
    page = context.new_page()

    page.goto("<URL>", wait_until="networkidle")

    # Extrair dados estruturados
    data = page.evaluate("""() => {
        const items = document.querySelectorAll('<selector>');
        return Array.from(items).map(item => ({
            title: item.querySelector('h2')?.innerText?.trim(),
            price: item.querySelector('.price')?.innerText?.trim(),
            link: item.querySelector('a')?.href
        }));
    }""")

    with open("output.json", "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

    browser.close()
```

## Estratégias de Extração

### Selectors
| Tipo | Exemplo | Uso |
|------|---------|-----|
| CSS | `div.card h2.title` | Elementos estruturados |
| Text | `text=Nome do Produto` | Match por texto visível |
| XPath | `//div[@class="item"]//a` | Navegação complexa |
| Role | `role=button[name="Next"]` | Acessibilidade |
| JS Evaluate | `page.evaluate()` | Dados em scripts/JSON-LD |

### Pagination
- **Botão "Next":** `page.click('text=Próximo')` ou `role=button[name="Next"]`
- **URL pattern:** `/page/1/` → `/page/2/` (navegação direta)
- **Infinite scroll:** `page.evaluate('window.scrollTo(0, document.body.scrollHeight)')` + wait
- **Load more:** `page.click('text=Carregar mais')` ou `button:has-text("Load")`

### Anti-Bot Bypass
- **Stealth:** `--stealth` flag ativa fingerprint spoofing
- **UA Rotation:** `--rotate-ua` alterna entre 50+ user agents reais
- **Viewport Randomization:** tamanhos de tela variados
- **Human Behavior:** movimentos de mouse, delays aleatórios, scroll natural
- **Cookie Persistence:** sessão mantida entre requests
- **Proxy Rotation:** suporte a proxies via `--proxy-list`

### Wait Strategies
| Estratégia | Comando | Quando usar |
|-----------|---------|-------------|
| networkidle | `wait_for_load_state('networkidle')` | SPA, JS pesado |
| selector | `wait_for_selector('.item')` | Elemento específico |
| timeout | `wait_for_timeout(2000)` | Fallback |
| function | `wait_for_function('()=>window.loaded')` | Custom event |
| navigation | `wait_for_url('**/page/2')` | Após clique |

### Dados Estruturados
- **JSON-LD:** `page.evaluate('JSON.parse(document.querySelector("script[type=application/ld+json]")?.textContent || "null")')`
- **Meta tags:** `page.evaluate('[...document.querySelectorAll("meta")].map(m => ({name: m.name, content: m.content}))')`
- **Tables:** extrair `<table>` para lista de dicts
- **Lists:** extrair `<ul>/<ol>` + `<li>` para arrays

## Output Format

```json
{
  "metadata": {
    "target": "<URL>",
    "timestamp": "2026-07-29T23:00:00Z",
    "pages_scraped": 42,
    "total_items": 1050,
    "duration_seconds": 84.5
  },
  "data": [
    {
      "title": "...",
      "price": "R$ 49,90",
      "link": "...",
      "extracted_at": "2026-07-29T23:00:00Z"
    }
  ],
  "errors": [
    {"page": 5, "error": "Timeout", "retry": true}
  ]
}
```

## Export Options

| Formato | Flag | Descrição |
|---------|------|-----------|
| JSON | `--format json` | Estruturado, aninhado |
| CSV | `--format csv` | Planilha plana |
| XLSX | `--format xlsx` | Excel com formatação |
| Markdown | `--format md` | Relatório legível |

## Recuperação de Falhas

- **Checkpoints:** a cada 10 páginas, salva estado
- **Resume:** `--resume checkpoint.json` continua de onde parou
- **Retry:** 3 tentativas com backoff exponencial (1s, 2s, 4s)
- **Log:** verbose mode com `--verbose` para debug

Consulte `references/strategies.md` para:
- Lista completa de user agents
- Padrões de fingerprint
- Estratégias para sites específicos (Amazon, Mercado Livre, etc.)
- Técnicas de evasão de rate limiting
