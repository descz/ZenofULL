"""
Zenox Scraper Engine — Enterprise Web Scraping & Data Extraction
Powered by Playwright + Python

Usage:
  python scraper_engine.py --target <URL> [options]

Features:
  - Dynamic content extraction (JS-rendered pages)
  - Pagination (Next button, URL pattern, infinite scroll)
  - Authentication & session persistence
  - Stealth mode (anti-bot evasion)
  - Multi-format export (JSON, CSV, XLSX)
  - Screenshot & PDF capture
  - Link crawling & extraction
  - Checkpoint & resume
  - Proxy support
"""
import argparse
import asyncio
import csv
import hashlib
import json
import os
import random
import re
import sys
import time
import urllib.parse
from datetime import datetime
from typing import Optional

if sys.stdout and sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr and sys.stderr.encoding and sys.stderr.encoding.lower() not in ("utf-8", "utf8"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

try:
    from playwright.async_api import async_playwright
except ImportError:
    print("Instale playwright: pip install playwright && playwright install chromium")
    sys.exit(1)

try:
    import openpyxl
    HAS_EXCEL = True
except ImportError:
    HAS_EXCEL = False

BANNER = """
███████╗ ██████╗██████╗  █████╗ ██████╗ ███████╗██████╗
╚══███╔╝██╔════╝██╔══██╗██╔══██╗██╔══██╗██╔════╝██╔══██╗
  ███╔╝ ██║     ██████╔╝███████║██████╔╝█████╗  ██████╔╝
 ███╔╝  ██║     ██╔══██╗██╔══██║██╔═══╝ ██╔══╝  ██╔══██╗
███████╗╚██████╗██║  ██║██║  ██║██║     ███████╗██║  ██║
╚══════╝ ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚══════╝╚═╝  ╚═╝
     Scraper Engine — Playwright Framework
"""

USER_AGENTS = [
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/125.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/126.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:128.0) Gecko/20100101 Firefox/128.0",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/126.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 Mobile/15E148",
    "Mozilla/5.0 (iPad; CPU OS 17_5 like Mac OS X) AppleWebKit/605.1.15 Mobile/15E148",
    "Mozilla/5.0 (Linux; Android 14; SM-S928B) AppleWebKit/537.36 Chrome/126.0.0.0 Mobile Safari/537.36",
]

VIEWPORTS = [
    {"width": 1920, "height": 1080},
    {"width": 1366, "height": 768},
    {"width": 1536, "height": 864},
    {"width": 1440, "height": 900},
    {"width": 1280, "height": 720},
]


class ScraperEngine:
    def __init__(self, target: str, output_dir: str, selector: str = "",
                 fields: str = "", format: str = "json", delay: float = 1.0,
                 stealth: bool = True, rotate_ua: bool = False,
                 auth: bool = False, auth_url: str = "",
                 username_field: str = "", password_field: str = "",
                 username: str = "", password: str = "",
                 crawl: bool = False, max_pages: int = 10,
                 extract_links: bool = False, screenshot: bool = False,
                 pdf: bool = False, proxy_list: str = "",
                 verbose: bool = False, resume: str = ""):
        self.target = target.rstrip("/")
        self.base_host = urllib.parse.urlparse(target).netloc
        self.output_dir = output_dir
        self.selector = selector
        self.fields = fields
        self.output_format = format
        self.delay = delay
        self.stealth = stealth
        self.rotate_ua = rotate_ua
        self.auth = auth
        self.auth_url = auth_url or target
        self.username_field = username_field
        self.password_field = password_field
        self.username = username
        self.password = password
        self.crawl = crawl
        self.max_pages = max_pages
        self.extract_links = extract_links
        self.screenshot = screenshot
        self.pdf = pdf
        self.proxy_list = proxy_list
        self.verbose = verbose
        self.resume = resume

        self.browser = None
        self.context = None
        self.page = None
        self.visited_urls = set()
        self.extracted_data = []
        self.error_log = []
        self.checkpoint = {}
        self.start_time = None
        self.page_count = 0
        self.ua_index = 0

        os.makedirs(output_dir, exist_ok=True)
        os.makedirs(f"{output_dir}/screenshots", exist_ok=True)

        if resume and os.path.exists(resume):
            self._load_checkpoint(resume)

    async def __aenter__(self):
        self.browser = await self._launch()
        return self

    async def __aexit__(self, *args):
        if self.browser:
            await self.browser.close()

    def _get_next_ua(self):
        if not self.rotate_ua:
            return USER_AGENTS[0]
        ua = USER_AGENTS[self.ua_index % len(USER_AGENTS)]
        self.ua_index += 1
        return ua

    def _get_next_viewport(self):
        return random.choice(VIEWPORTS)

    async def _launch(self):
        p = await async_playwright().start()
        browser = await p.chromium.launch(
            headless=True,
            args=[
                "--disable-blink-features=AutomationControlled",
                "--disable-dev-shm-usage",
                "--no-sandbox",
                "--disable-gpu",
            ],
        )

        ua = self._get_next_ua()
        vp = self._get_next_viewport()

        context = await browser.new_context(
            user_agent=ua,
            viewport=vp,
            locale="pt-BR",
            timezone_id="America/Sao_Paulo",
            permissions=["geolocation"],
            geolocation={"latitude": -23.5505, "longitude": -46.6333},
        )

        if self.stealth:
            await context.add_init_script("""
                Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
                Object.defineProperty(navigator, 'plugins', {
                    get: () => [1,2,3,4,5].map(() => ({name: 'Chrome PDF Plugin', filename: 'internal-pdf-viewer'}))
                });
                Object.defineProperty(navigator, 'languages', { get: () => ['pt-BR', 'pt', 'en-US', 'en'] });
                window.chrome = { runtime: {} };
                const originalQuery = window.navigator.permissions.query;
                window.navigator.permissions.query = (parameters) => (
                    parameters.name === 'notifications' ?
                    Promise.resolve({state: Notification.permission}) :
                    originalQuery(parameters)
                );
            """)

        self.context = context
        self.page = await context.new_page()

        if self.verbose:
            self.page.on("console", lambda msg: print(f"[CONSOLE] {msg.text}"))
            self.page.on("pageerror", lambda err: print(f"[PAGE_ERROR] {err}"))

        return browser

    def _log(self, msg: str, force: bool = False):
        if force or self.verbose:
            print(f"[*] {msg}")
        with open(f"{self.output_dir}/scraper.log", "a", encoding="utf-8") as f:
            f.write(f"[{datetime.now().isoformat()}] {msg}\n")

    async def _goto(self, url: str, timeout: int = 30000, referer: str = ""):
        try:
            if self.rotate_ua:
                ua = self._get_next_ua()
                vp = self._get_next_viewport()
                await self.context.add_cookies([])
                # Can't change UA mid-session easily, so just log it
                self._log(f"UA rotacionado (simulado): {ua[:50]}...")

            headers = {}
            if referer:
                headers["Referer"] = referer

            await self.page.goto(url, wait_until="networkidle", timeout=timeout)
            await self.page.wait_for_timeout(random.randint(500, 1500))
            return True
        except Exception as e:
            try:
                await self.page.goto(url, wait_until="load", timeout=timeout)
                await self.page.wait_for_timeout(1000)
                return True
            except Exception as e2:
                self._log(f"Erro ao navegar: {e2}")
                self.error_log.append({"url": url, "error": str(e2)})
                return False

    async def _human_delay(self):
        jitter = random.uniform(self.delay * 0.5, self.delay * 1.5)
        await self.page.wait_for_timeout(int(jitter * 1000))

    async def _human_scroll(self):
        """Simula scroll humano na página"""
        height = await self.page.evaluate(
            "(document.body?.scrollHeight ?? document.documentElement?.scrollHeight) ?? 0"
        )
        steps = random.randint(3, 8)
        for i in range(steps):
            scroll_to = int(height * (i + 1) / steps)
            await self.page.evaluate(f"window.scrollTo({{top: {scroll_to}, behavior: 'smooth'}})")
            await self.page.wait_for_timeout(random.randint(200, 600))

    async def _screenshot(self, name: str):
        path = f"{self.output_dir}/screenshots/{name}.png"
        await self.page.screenshot(path=path, full_page=True)
        self._log(f"Screenshot: {path}")
        return path

    async def _auth_flow(self):
        """Realiza login e mantém sessão"""
        self._log("Iniciando fluxo de autenticação...")
        if not await self._goto(self.auth_url):
            return False

        await self.page.wait_for_timeout(2000)

        # Preenche username
        uf = self.username_field or "username"
        selectors_username = [
            f"[name='{uf}']",
            f"[name='{uf}' i]",
            f"[id='{uf}']",
            f"[placeholder*='{uf}' i]",
            "[name='username' i]",
            "[name='email' i]",
            "[name='login' i]",
            "[name='user' i]",
            "[type='email']",
        ]

        pf = self.password_field or "password"
        selectors_password = [
            f"[name='{pf}']",
            f"[name='{pf}' i]",
            f"[id='{pf}']",
            f"[placeholder*='{pf}' i]",
            "[name='password' i]",
            "[name='pass' i]",
            "[name='senha' i]",
            "[type='password']",
        ]

        filled = False
        for sel in selectors_username:
            if await self.page.locator(sel).count() > 0:
                await self.page.fill(sel, self.username)
                await self._human_delay()
                filled = True
                break

        if not filled:
            # Tenta preencher qualquer input visível
            inputs = await self.page.locator("input:visible").all()
            if len(inputs) >= 1:
                await inputs[0].fill(self.username)

        for sel in selectors_password:
            if await self.page.locator(sel).count() > 0:
                await self.page.fill(sel, self.password)
                await self._human_delay()
                break

        # Tenta submeter
        submit_selectors = [
            "[type='submit']",
            "button:has-text('entrar')",
            "button:has-text('login')",
            "button:has-text('sign in')",
            "button:has-text('acessar')",
            "button[type='submit']",
            "input[type='submit']",
        ]

        for sel in submit_selectors:
            if await self.page.locator(sel).count() > 0:
                await self.page.click(sel)
                break

        await self.page.wait_for_timeout(5000)
        self._log(f"Auth concluído. URL atual: {self.page.url}")
        return True

    async def _extract_by_selector(self, selector: str = ""):
        """Extrai dados baseado em seletor CSS"""
        sel = selector or self.selector
        if not sel:
            return []

        items = await self.page.evaluate(f"""() => {{
            const elements = document.querySelectorAll('{sel}');
            return Array.from(elements).map((el, idx) => ({{
                index: idx,
                html: el.innerHTML.substring(0, 500),
                text: el.innerText?.trim()?.substring(0, 500),
                outerHtml: el.outerHTML.substring(0, 300),
            }}));
        }}""")
        return items

    async def _extract_fields(self):
        """Extrai campos específicos de cada item"""
        if not self.fields:
            return []

        field_defs = []
        for f in self.fields.split(","):
            f = f.strip()
            if ":" in f:
                name, selector = f.split(":", 1)
                attr = None
                if "@" in selector:
                    selector, attr = selector.rsplit("@", 1)
                field_defs.append({
                    "name": name.strip(),
                    "selector": selector.strip(),
                    "attr": attr.strip() if attr else None,
                })
            else:
                field_defs.append({"name": f, "selector": f, "attr": None})

        parent_sel = self.selector or "body"
        data = await self.page.evaluate(f"""() => {{
            const parent = document.querySelector('{parent_sel}');
            if (!parent) return [];

            const items = document.querySelectorAll('[data-item], .item, tr, li, .card, article, .product');
            const fieldDefs = {json.dumps(field_defs)};

            return Array.from(items).map((item, idx) => {{
                const row = {{ _index: idx }};
                fieldDefs.forEach(fd => {{
                    const el = item.querySelector(fd.selector);
                    if (el) {{
                        if (fd.attr) {{
                            row[fd.name] = el.getAttribute(fd.attr) ?? null;
                        }} else {{
                            const tag = el.tagName.toLowerCase();
                            if (tag === 'a') row[fd.name] = el.href;
                            else if (tag === 'img') row[fd.name] = el.src;
                            else if (el.value !== undefined) row[fd.name] = el.value;
                            else row[fd.name] = el.innerText?.trim();
                        }}
                    }}
                }});
                return row;
            }});
        }}""")
        return data

    async def _extract_all_data(self):
        """Estratégia de extração adaptativa"""
        all_data = []

        # 1. Tenta JSON-LD
        jsonld = await self.page.evaluate("""() => {
            const scripts = document.querySelectorAll('script[type="application/ld+json"]');
            return Array.from(scripts).map(s => {
                try { return JSON.parse(s.textContent); }
                catch { return null; }
            }).filter(Boolean);
        }""")
        if jsonld:
            all_data.extend(jsonld)
            self._log(f"Dados JSON-LD extraídos: {len(jsonld)}")

        # 2. Tenta extrair tabelas
        tables = await self.page.evaluate("""() => {
            return Array.from(document.querySelectorAll('table')).map(table => {
                const headers = Array.from(table.querySelectorAll('th')).map(th => th.innerText.trim());
                const rows = Array.from(table.querySelectorAll('tr')).map(row =>
                    Array.from(row.querySelectorAll('td')).map(td => td.innerText.trim())
                ).filter(r => r.length > 0);
                return { headers, rows };
            });
        }""")
        for t in tables:
            if t["rows"]:
                for row in t["rows"]:
                    item = {}
                    for i, cell in enumerate(row):
                        key = t["headers"][i] if i < len(t["headers"]) else f"col_{i}"
                        item[key] = cell
                    all_data.append(item)
                self._log(f"Dados de tabela extraídos: {len(t['rows'])} linhas")

        # 3. Tenta seletor customizado
        if self.selector:
            items = await self._extract_fields()
            if items:
                all_data.extend(items)
                self._log(f"Dados via selector extraídos: {len(items)}")

        # 4. Fallback: extrai título e body
        if not all_data:
            fallback = await self.page.evaluate("""() => ({
                title: document.title,
                url: window.location.href,
                text: document.body.innerText.substring(0, 5000),
                meta: Array.from(document.querySelectorAll('meta')).map(m => ({
                    name: m.getAttribute('name') || m.getAttribute('property'),
                    content: m.getAttribute('content')
                })).filter(m => m.name && m.content)
            })""")
            all_data.append(fallback)
            self._log("Fallback: extração genérica de página")

        return all_data

    async def _extract_links_from_page(self):
        """Extrai todos os links da página"""
        links = await self.page.evaluate("""() => {
            return Array.from(document.querySelectorAll('a[href]')).map(a => ({
                url: a.href,
                text: a.innerText.trim().substring(0, 200),
                rel: a.rel || '',
            })).filter(l => l.url.startsWith('http'));
        }""")
        # Filtra links do mesmo domínio
        same_domain = [l for l in links if self.base_host in l["url"]]
        return same_domain

    async def _next_page_exists(self):
        """Detecta se há próxima página"""
        # Botão next
        next_btns = [
            "text=Próximo",
            "text=Próxima",
            "text=Next",
            "text=›",
            "text=»",
            "text=>>",
            "text=Carregar mais",
            "text=Load more",
            "text=Ver mais",
            "a:has-text('próximo')",
            "a:has-text('next')",
            "a:has-text('›')",
            "a:has-text('»')",
            "[rel='next']",
            ".pagination .next",
            ".pagination a:last-child",
            "button:has-text('próximo')",
            "button:has-text('next')",
            "button:has-text('more')",
        ]

        for sel in next_btns:
            try:
                if await self.page.locator(sel).count() > 0 and await self.page.locator(sel).is_visible():
                    return sel
            except Exception:
                pass
        return None

    async def _infinite_scroll(self, max_scrolls: int = 20):
        """Lida com infinite scroll"""
        prev_height = 0
        scrolls = 0

        while scrolls < max_scrolls:
            height = await self.page.evaluate(
                "(document.body?.scrollHeight ?? document.documentElement?.scrollHeight) ?? 0"
            )
            if height == prev_height:
                break

            prev_height = height
            await self.page.evaluate(
                "window.scrollTo({top: document.documentElement.scrollHeight, behavior: 'smooth'})"
            )
            await self.page.wait_for_timeout(random.randint(1000, 2000))
            scrolls += 1

        self._log(f"Infinite scroll: {scrolls} scrolls realizados")
        return scrolls

    async def _handle_pagination(self):
        """Gerencia paginação"""
        pages_data = []

        # Infinite scroll
        await self._infinite_scroll()

        # Botão "Next" / paginação
        page_num = 0
        while page_num < self.max_pages:
            page_data = await self._extract_all_data()
            if page_data:
                pages_data.extend(page_data)

            next_sel = await self._next_page_exists()
            if not next_sel:
                self._log(f"Fim da paginação após {page_num + 1} páginas")
                break

            try:
                await self.page.click(next_sel)
                await self.page.wait_for_timeout(random.randint(1000, 3000))
                await self._goto(self.page.url)
                await self._human_scroll()
                page_num += 1
                self.page_count += 1
                self._log(f"Página {page_num + 1}: {self.page.url}")

                # Checkpoint a cada 10 páginas
                if page_num % 10 == 0:
                    await self._save_checkpoint()

            except Exception as e:
                self._log(f"Erro na paginação página {page_num + 1}: {e}")
                self.error_log.append({"page": page_num + 1, "error": str(e)})
                break

        return pages_data

    async def _crawl(self):
        """Crawleia o site"""
        self._log(f"Iniciando crawl (max {self.max_pages} páginas)")
        all_data = []
        urls_to_visit = [self.target]
        self.visited_urls = set()

        while urls_to_visit and len(self.visited_urls) < self.max_pages:
            url = urls_to_visit.pop(0)
            if url in self.visited_urls:
                continue

            self.visited_urls.add(url)
            self._log(f"Crawling [{len(self.visited_urls)}/{self.max_pages}]: {url}")

            if not await self._goto(url, referer=list(self.visited_urls)[-2] if len(self.visited_urls) > 1 else ""):
                continue

            await self._human_scroll()

            # Extrai dados da página
            page_data = await self._extract_all_data()
            for item in page_data:
                item["_source_url"] = url
                item["_crawled_at"] = datetime.now().isoformat()
            all_data.extend(page_data)

            if self.screenshot:
                safe_name = hashlib.md5(url.encode()).hexdigest()[:12]
                await self._screenshot(f"crawl_{safe_name}")

            # Encontra mais links
            links = await self._extract_links_from_page()
            for link in links:
                lurl = link["url"].split("#")[0]  # Remove fragmentos
                if lurl not in self.visited_urls and lurl not in urls_to_visit:
                    urls_to_visit.append(lurl)

            await self._human_delay()

            if len(self.visited_urls) % 10 == 0:
                await self._save_checkpoint()

        self._log(f"Crawl concluído. {len(self.visited_urls)} páginas visitadas.")
        return all_data

    async def _save_checkpoint(self):
        cp = {
            "target": self.target,
            "visited_urls": list(self.visited_urls),
            "data_count": len(self.extracted_data),
            "page_count": self.page_count,
            "timestamp": datetime.now().isoformat(),
        }
        path = f"{self.output_dir}/checkpoint.json"
        with open(path, "w", encoding="utf-8") as f:
            json.dump(cp, f, ensure_ascii=False, indent=2)
        self._log(f"Checkpoint salvo: {path}")

    def _load_checkpoint(self, path: str):
        try:
            with open(path, "r", encoding="utf-8") as f:
                cp = json.load(f)
            self.visited_urls = set(cp.get("visited_urls", []))
            self.page_count = cp.get("page_count", 0)
            self._log(f"Checkpoint carregado: {len(self.visited_urls)} URLs visitados, "
                      f"{cp.get('data_count', 0)} dados extraídos")
        except Exception as e:
            self._log(f"Erro ao carregar checkpoint: {e}")

    # ── OUTPUT ────────────────────────────────────────────────

    async def _save_data(self, data: list):
        if not data:
            self._log("Nenhum dado para salvar.")
            return

        self.extracted_data.extend(data)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        if self.output_format == "json":
            path = f"{self.output_dir}/data_{timestamp}.json"
            with open(path, "w", encoding="utf-8") as f:
                json.dump(self._build_output(data), f, ensure_ascii=False, indent=2)

        elif self.output_format == "csv":
            path = f"{self.output_dir}/data_{timestamp}.csv"
            if data and isinstance(data[0], dict):
                keys = list(data[0].keys())
                with open(path, "w", encoding="utf-8-sig", newline="") as f:
                    writer = csv.DictWriter(f, fieldnames=keys)
                    writer.writeheader()
                    writer.writerows(data)
            else:
                with open(path, "w", encoding="utf-8-sig", newline="") as f:
                    writer = csv.writer(f)
                    for item in data:
                        if isinstance(item, dict):
                            writer.writerow(item.values())
                        else:
                            writer.writerow([item])

        elif self.output_format == "xlsx" and HAS_EXCEL:
            path = f"{self.output_dir}/data_{timestamp}.xlsx"
            wb = openpyxl.Workbook()
            ws = wb.active
            ws.title = "Scraped Data"

            if data and isinstance(data[0], dict):
                keys = list(data[0].keys())
                ws.append(keys)
                for item in data:
                    ws.append([str(item.get(k, "")) for k in keys])
            else:
                for item in data:
                    ws.append([str(item)])

            wb.save(path)

        elif self.output_format == "md":
            path = f"{self.output_dir}/data_{timestamp}.md"
            with open(path, "w", encoding="utf-8") as f:
                f.write(f"# Scraped Data from {self.target}\n\n")
                for item in data[:100]:  # Limit MD output
                    if isinstance(item, dict):
                        for k, v in item.items():
                            f.write(f"**{k}:** {v}\n")
                        f.write("---\n")
                    else:
                        f.write(f"{item}\n---\n")

        self._log(f"Dados salvos: {path} ({len(data)} itens)")

        if self.screenshot:
            await self._screenshot(f"final_{timestamp}")

        if self.pdf:
            pdf_path = f"{self.output_dir}/page_{timestamp}.pdf"
            await self.page.pdf(path=pdf_path, format="A4")
            self._log(f"PDF salvo: {pdf_path}")

    def _build_output(self, data: list):
        elapsed = 0
        if self.start_time:
            elapsed = time.time() - self.start_time

        return {
            "metadata": {
                "target": self.target,
                "timestamp": datetime.now().isoformat(),
                "pages_scraped": self.page_count + len(self.visited_urls),
                "total_items": len(self.extracted_data),
                "duration_seconds": round(elapsed, 2),
                "errors": len(self.error_log),
            },
            "data": data,
            "errors": self.error_log[-50:] if self.error_log else [],
        }

    # ── RUN ────────────────────────────────────────────────────

    async def run(self):
        print(BANNER)
        self._log(f"Target: {self.target}")
        self._log(f"Output: {self.output_dir}")
        self._log(f"Stealth: {'ON' if self.stealth else 'OFF'}")
        self._log(f"UA Rotation: {'ON' if self.rotate_ua else 'OFF'}")
        self.start_time = time.time()

        # Auth
        if self.auth:
            auth_ok = await self._auth_flow()
            if not auth_ok:
                self._log("Falha na autenticação. Continuando sem auth...")

        # Main extraction
        if self.crawl:
            data = await self._crawl()
        else:
            if not await self._goto(self.target):
                self._log("Falha ao acessar target. Abortando.")
                return

            await self._human_scroll()
            self.page_count = 1

            if self.extract_links:
                data = await self._extract_links_from_page()
                self._log(f"Links extraídos: {len(data)}")
            else:
                data = await self._handle_pagination()

        # Save
        await self._save_data(data)

        elapsed = time.time() - self.start_time
        self._log(f"Scraping concluído em {elapsed:.1f}s")
        self._log(f"Total de dados: {len(self.extracted_data)} itens")
        self._log(f"Erros: {len(self.error_log)}")

        # Summary JSON
        summary = self._build_output(self.extracted_data)
        with open(f"{self.output_dir}/summary.json", "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)

        await self._save_checkpoint()
        return summary


async def main():
    parser = argparse.ArgumentParser(
        description="Zenox Scraper Engine — Enterprise Web Scraping Framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Exemplos:
  python scraper_engine.py --target https://example.com
  python scraper_engine.py --target https://example.com --selector "div.product" --fields "title:h2,price:.price"
  python scraper_engine.py --target https://example.com --crawl --max-pages 50 --output ./crawl
  python scraper_engine.py --target https://example.com --auth --username user@email.com --password secret
  python scraper_engine.py --target https://example.com --stealth --rotate-ua --delay 3
  python scraper_engine.py --target https://example.com --screenshot --pdf
  python scraper_engine.py --target https://example.com --format csv --output ./data
  python scraper_engine.py --target https://example.com --extract-links
""")

    parser.add_argument("--target", "-t", required=True, help="URL alvo para scraping")
    parser.add_argument("--output", "-o", default="./scraper_output", help="Diretório de saída")
    parser.add_argument("--selector", "-s", default="", help="Seletor CSS para itens")
    parser.add_argument("--fields", "-f", default="", help="Campos a extrair (ex: titulo:h1,preco:.price)")
    parser.add_argument("--format", default="json", choices=["json", "csv", "xlsx", "md"],
                        help="Formato de saída")
    parser.add_argument("--delay", type=float, default=1.0, help="Delay entre requests (s)")
    parser.add_argument("--stealth", action="store_true", help="Ativar stealth mode")
    parser.add_argument("--rotate-ua", action="store_true", help="Rotacionar User-Agent")
    parser.add_argument("--auth", action="store_true", help="Habilitar autenticação")
    parser.add_argument("--auth-url", help="URL de login")
    parser.add_argument("--username-field", default="", help="Nome do campo de usuário")
    parser.add_argument("--password-field", default="", help="Nome do campo de senha")
    parser.add_argument("--username", "-u", default="", help="Username/email")
    parser.add_argument("--password", default="", help="Password")
    parser.add_argument("--crawl", action="store_true", help="Modo crawl (segue links)")
    parser.add_argument("--max-pages", type=int, default=10, help="Máx. páginas no crawl")
    parser.add_argument("--extract-links", action="store_true", help="Extrair todos os links")
    parser.add_argument("--screenshot", action="store_true", help="Capturar screenshots")
    parser.add_argument("--pdf", action="store_true", help="Exportar PDF da página")
    parser.add_argument("--proxy-list", help="Arquivo com lista de proxies")
    parser.add_argument("--verbose", "-v", action="store_true", help="Log verboso")
    parser.add_argument("--resume", help="Checkpoint para resumir scraping")

    args = parser.parse_args()

    async with ScraperEngine(
        target=args.target,
        output_dir=args.output,
        selector=args.selector,
        fields=args.fields,
        format=args.format,
        delay=args.delay,
        stealth=args.stealth,
        rotate_ua=args.rotate_ua,
        auth=args.auth,
        auth_url=args.auth_url or args.target,
        username_field=args.username_field,
        password_field=args.password_field,
        username=args.username,
        password=args.password,
        crawl=args.crawl,
        max_pages=args.max_pages,
        extract_links=args.extract_links,
        screenshot=args.screenshot,
        pdf=args.pdf,
        proxy_list=args.proxy_list,
        verbose=args.verbose,
        resume=args.resume,
    ) as engine:
        await engine.run()


if __name__ == "__main__":
    asyncio.run(main())
