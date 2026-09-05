# Zenox Scraper — Reference: Strategies & Techniques

## User Agents (Realistic)

### Desktop Chrome
```
Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36
Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36
Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36
Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36
```

### Desktop Firefox
```
Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0
Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:128.0) Gecko/20100101 Firefox/128.0
Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:127.0) Gecko/20100101 Firefox/127.0
```

### Edge
```
Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 Edg/125.0.0.0
```

### Mobile
```
Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1
Mozilla/5.0 (Linux; Android 14; SM-S928B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36
```

## Fingerprint Spoofing

O stealth mode do scraper engine injeta via `add_init_script`:

```javascript
// Remove webdriver flag
Object.defineProperty(navigator, 'webdriver', { get: () => undefined });

// Fake plugins (bots normalmente têm 0 plugins)
Object.defineProperty(navigator, 'plugins', {
  get: () => [1, 2, 3, 4, 5].map(() => ({ name: 'Chrome PDF Plugin' }))
});

// Fake languages
Object.defineProperty(navigator, 'languages', { get: () => ['pt-BR', 'pt', 'en-US'] });

// Chrome object (Selenium/Playwright real não tem)
window.chrome = { runtime: {} };

// Permissions API (headless retorna denied para tudo)
window.navigator.permissions.query = (parameters) => (
  parameters.name === 'notifications' ?
  Promise.resolve({ state: Notification.permission }) :
  originalQuery(parameters)
);
```

## Anti-Bot Evasion

### Layer 1: Fingerprint
- UA realista
- Viewport variado
- Locale + timezone consistentes
- WebGL/Canvas: não fingir (Playwright tem WebGL nativo)

### Layer 2: Comportamento
- Scroll incremental (simula leitura humana)
- Delays com jitter (0.5x a 1.5x do valor configurado)
- Hover em elementos antes de clicar
- Mouse movements via `page.mouse.move()` com trajetória

### Layer 3: Sessão
- Cookies persistidos entre páginas
- Referer real nas navegações internas
- Cabeçalhos de navegação completa (não só o de página principal)

### Layer 4: Infra
- Proxies residenciais via `--proxy-list`
- Rotação de UA a cada request
- Distribuição temporal dos requests

## Pagination Patterns

### URL-based (mais confiável)
```
/page/1 → /page/2 → /page/3
?page=1 → ?page=2
&offset=0 → &offset=20
/category/food?page=1
```

### Next-button (SPAs)
```python
await page.click("button[aria-label='Next']")
await page.click("a.pagination-next")
await page.click("li.next a")
await page.wait_for_url("**/page/2")
```

### Infinite Scroll
```python
# Scroll incremental com checkpoint de altura
prev = 0
while True:
    await page.evaluate("window.scrollTo(0, document.body.scrollHeight)")
    await page.wait_for_timeout(1500)
    height = await page.evaluate("document.body.scrollHeight")
    if height == prev:
        break  # Fim do conteúdo
    prev = height
```

### "Load More" button
```python
while await page.locator("button:has-text('Load More'), button:has-text('Carregar mais')").is_visible():
    await page.click("button:has-text('Load More')")
    await page.wait_for_timeout(1000)
```

## Data Extraction Strategies

### JSON-LD (SEO data — mais estruturado)
```javascript
JSON.parse(document.querySelector('script[type="application/ld+json"]').textContent)
```
Sites de e-commerce costumam ter Product, Offer, AggregateRating em JSON-LD.

### React/Next.js __NEXT_DATA__
```javascript
JSON.parse(document.getElementById('__NEXT_DATA__').textContent)
```

### Vue __INITIAL_STATE__
```javascript
window.__INITIAL_STATE__ || document.querySelector('#app').__vue_app__
```

### HTML tables
```javascript
// Cabeçalhos + linhas em um clique
Array.from(document.querySelectorAll('table tr')).map(tr =>
  Array.from(tr.querySelectorAll('th, td')).map(cell => cell.innerText.trim())
)
```

### Attribute extraction
```javascript
// Pegar links, imagens, data-attributes
document.querySelectorAll('a.product-link').forEach(a => ({ url: a.href, title: a.dataset.title }))
```

### API interception
```python
# Interceptar XHR/fetch que a página faz
page.on("response", lambda res: (
    json.loads(res.text()) if "api" in res.url and res.headers.get("content-type", "").startswith("application/json") else None
))
```
Muitas vezes a API retorna dados mais limpos que o DOM. Sempre cheque `page.on("response")` para endpoints `/api/` antes de parsear HTML.

## Login Walls

### Form selectors comuns (auto-detecção)
| Campo | Selectors |
|-------|-----------|
| Username | `[name=username]`, `[name=email]`, `[name=login]`, `[name=user]`, `[type=email]` |
| Password | `[type=password]`, `[name=password]`, `[name=senha]`, `[name=pass]` |
| Submit | `[type=submit]`, `button:has-text('Entrar')`, `button:has-text('Login')` |

### OAuth/SSO (Google, GitHub)
```python
# Clique no provider e deixe o fluxo continuar em popup
with page.expect_popup() as popup_info:
    await page.click("button:has-text('Continue with Google')")
popup = await popup_info.value
await popup.wait_for_load_state()
```

### Two-factor (manual override)
- Pausar scraping (`input("Press Enter after 2FA...")`)
- Captcha: instruir usuário a resolver manualmente com browser headed

## CAPTCHA Detection

Sinais:
- Elemento `iframe[src*="recaptcha"]`, `.g-recaptcha`, `.h-captcha`
- Texto na página: "verify", "I'm not a robot", "captcha"
- Meta refresh com delay

Handling:
- Não tentar resolver automaticamente (perde tempo)
- Usar browser headed e pedir ao usuário
- Ou pular a página e logar o erro

## Rate Limiting Evasion

### Delay adaptativo
```python
import random, time
def adaptive_delay(base=2.0):
    jitter = random.uniform(base * 0.5, base * 1.5)
    # Se detectar 429, dobra o delay
    time.sleep(jitter)
```

### Backoff exponencial em erros
```python
attempts = 0
while attempts < 3:
    try:
        response = request()
        break
    except RateLimitError:
        time.sleep(2 ** attempts * 30)  # 30s, 60s, 120s
        attempts += 1
```

### Detecção de bloqueio
- Status 429, 403, 503
- HTML com "blocked", "captcha", "unusual traffic", "access denied"
- Página retorna content-length muito menor que o normal
- Redirect para `/challenge/` ou `/blocked`

## Proxies

Formato do arquivo `--proxy-list` (uma por linha):
```
http://user:pass@ip:port
socks5://ip:port
http://ip:port
```

```python
# Playwright com proxy
browser = p.chromium.launch(proxy={"server": "http://ip:port", "username": "u", "password": "p"})
```

## Estratégias por Tipo de Site

### E-commerce (Mercado Livre, Amazon, Shopee)
- Pegar dados via JSON-LD/API sempre que possível
- Paginação via URL quando existir
- Cuidado com infinite scroll agressivo
- Variar delay — ML bloqueia rapidamente

### Redes Sociais
- Sempre logado (senão mostra 10% do conteúdo)
- Rate limiting brutal — usar delays longos (5-10s)
- Preferir API interception

### Sites Gov/Notícias
- Paginação simples por URL
- RSS/JSON feeds são ouro — checar antes

### SPAs (React/Vue/Angular)
- Sempre `wait_until="networkidle"`
- Dados frequentemente em `__NEXT_DATA__` ou API JSON
- Usar `--crawl` para mapear rotas

## Common Pitfalls

1. **Esquecer de esperar o JS**: `networkidle` antes de qualquer extração
2. **Selectors quebrados**: páginas mudam — usar `data-*` attrs quando existirem
3. **Não paginar**: verificar se o "Next" existe SEMPRE
4. **Block por fingerprint**: checar o status 200 vs conteúdo real (pagina de bloqueio retorna 200 às vezes)
5. **Perder dados por re-scrape**: usar `_source_url` e timestamp em cada item
6. **Escalar rápido demais**: 1 request a cada 2s é o mínimo humano. 1000 páginas = 30+ min
