---
name: google-dorks
description: >-
  Buscas avançadas com Google Dorks (operadores Google/Bing/GitHub) para
  pesquisar, investigar e coletar informações públicas na web. Use quando o
  usuário quiser fazer busca avançada, encontrar arquivos expostos, painéis
  de login, diretórios abertos, credenciais vazadas, API keys públicas,
  conteúdo indexado por engano, ou qualquer pesquisa OSINT/recon. Também usada
  como ferramenta de reconhecimento na fase inicial de pentests autorizados.
  Triggers: "google dorks", "dork", "buscas avançadas", "osint", "busca por
  site:", "encontrar arquivos expostos", "painel de login", "credenciais
  vazadas", "api key exposta", "recon", "footprinting".
user-invocable: true
---

# Google Dorks — Buscas Avançadas

> **Uma linha:** dorks são **operadores de busca** que transformam o
> Google/Bing/GitHub em um scanner de superfície pública. Tudo aqui é
> informação **já indexada e pública** — nada invade, quebra ou acessa
> sistemas.

## Sumário

1. [Como usar](#1-como-usar)
2. [Operadores essenciais](#2-operadores-essenciais)
3. [Catálogo de dorks por objetivo](#3-catálogo-de-dorks-por-objetivo)
4. [GitHub dorks](#4-github-dorks)
5. [Combinações avançadas](#5-combinações-avançadas)
6. [Integração com a ferramenta de busca](#6-integração-com-a-ferramenta-de-busca)
7. [Integração com pentest (recon)](#7-integração-com-pentest-recon)
8. [Limitações e alternativas](#8-limitações-e-alternativas)
9. [Ética e autorização](#9-ética-e-autorização)
10. [Checklist de 60 segundos](#10-checklist-de-60-segundos)

---

## 1. Como usar

1. **Identifique o objetivo** → escolha o dork na tabela da [seção 3](#3-catálogo-de-dorks-por-objetivo)
2. **Substitua `[ALVO]`** pelo domínio/palavra-chave desejada
3. **Rode na ferramenta de busca** (websearch) ou direto no Google/Bing
4. **Refine com operadores combinados** (seção 5) e **valide** cada resultado antes de usar

> **Regra de sintaxe:** operadores não têm espaço entre o `:` e o valor
> (`site:exemplo.com` ✅ · `site: exemplo.com` ❌). Parênteses agrupam;
> `OR` (maiúsculo) faz união; aspas forçam frase exata; `-` exclui.

user-invocable: true
---

## 2. Operadores essenciais

| Operador | Função | Exemplo |
|---|---|---|
| `site:` | Restringe a um domínio/URL | `site:exemplo.com` |
| `inurl:` | Palavra na URL | `inurl:admin` |
| `intitle:` | Palavra no título | `intitle:"index of"` |
| `intext:` | Palavra no corpo da página | `intext:"password ="` |
| `allinurl:` / `allintitle:` / `allintext:` | Todas as palavras no campo | `allintitle:admin login` |
| `filetype:` / `ext:` | Tipo de arquivo | `filetype:pdf` |
| `inanchor:` | Texto do link | `inanchor:login` |
| `cache:` | Versão em cache do Google | `cache:exemplo.com` |
| `link:` | Páginas que linkam para a URL | `link:exemplo.com` |
| `related:` | Sites similares | `related:exemplo.com` |
| `info:` | Resumo da página | `info:exemplo.com` |
| `define:` | Definição do termo | `define:osint` |
| `numrange:` | Faixa numérica | `numrange:1000-2000` |
| `*` | Curinga | `"inscrição * de dados"` |
| `"..."` | Frase exata | `"index of /backup"` |
| `-` | Exclui termo | `-site:exemplo.com` |
| `OR` / `|` | União de termos | `filetype:sql OR filetype:bak` |
| `..` | Faixa numérica (Google) | `"preço" 100..500` |

---

## 3. Catálogo de dorks por objetivo

> Substitua `[ALVO]` pelo domínio, organização ou tema.

### Arquivos expostos / indexação indevida

| Objetivo | Dork |
|---|---|
| Listagem de diretório aberta | `intitle:"index of" site:[ALVO]` |
| Diretório de backup | `intitle:"index of" "backup" site:[ALVO]` |
| Arquivos de configuração | `filetype:conf OR filetype:cfg site:[ALVO]` |
| Arquivos de ambiente (env) | `filetype:env OR "laravel .env" site:[ALVO]` |
| Scripts de banco (SQL) | `filetype:sql site:[ALVO]` |
| Planilhas sensíveis | `filetype:xlsx OR filetype:xls site:[ALVO]` |
| Documentos internos | `filetype:docx OR filetype:pdf intext:"confidencial" site:[ALVO]` |
| Dumps de banco (geral) | `"Dumping data for table" filetype:sql` |
| Arquivos de log | `filetype:log intext:"password" site:[ALVO]` |
| Pacotes/arquivos compactados | `filetype:zip OR filetype:rar OR filetype:7z site:[ALVO]` |

### Painéis de login e áreas administrativas

| Objetivo | Dork |
|---|---|
| Painel admin genérico | `inurl:admin intitle:login site:[ALVO]` |
| Painel de gerenciamento | `inurl:manager OR inurl:dashboard site:[ALVO]` |
| Login de sistema legado | `intitle:"sign in" OR intitle:"log in" site:[ALVO]` |
| Acesso a CMS (WordPress) | `inurl:wp-admin OR inurl:wp-login site:[ALVO]` |
| Páginas de configuração | `inurl:config intext:"username" intext:"password" site:[ALVO]` |
| Portais de funcionários | `intitle:"employee portal" OR inurl:staff site:[ALVO]` |

### Credenciais e dados sensíveis indexados

| Objetivo | Dork |
|---|---|
| Strings de conexão | `intext:"connectionstring" filetype:config site:[ALVO]` |
| Senhas em textos | `intext:"password =" OR intext:"passwd =" site:[ALVO]` |
| Chaves de API | `intext:"api_key" OR intext:"api-key" OR intext:"apikey" site:[ALVO]` |
| Tokens e segredos | `intext:"secret" intext:"token" filetype:json site:[ALVO]` |
| Arquivos .git expostos | `intext:"index of" ".git"` |
| Credenciais em logs | `"password" filetype:log intext:"login failed"` |

### Vulnerabilidades e superfícies de ataque (recon)

| Objetivo | Dork |
|---|---|
| Páginas PHP potencialmente vulneráveis | `inurl:php?id= site:[ALVO]` |
| Parâmetros de upload | `inurl:upload OR inurl:filemanager site:[ALVO]` |
| Formulários de login (alvo de brute-force) | `inurl:login.php OR inurl:signin site:[ALVO]` |
| Endpoints de API | `inurl:api/v1 OR inurl:/api/ site:[ALVO]` |
| Swagger/OpenAPI expostos | `inurl:swagger OR intitle:"swagger ui" site:[ALVO]` |
| Documentação técnica vazada | `filetype:pdf intext:"architecture" intext:"password" site:[ALVO]` |

### OSINT / pessoas e organizações

| Objetivo | Dork |
|---|---|
| Perfis de uma pessoa | `"nome sobrenome" (inurl:linkedin OR inurl:instagram)` |
| E-mails indexados | `"@[ALVO].com" filetype:pdf OR filetype:xlsx` |
| Menções públicas | `intext:"nome sobrenome" site:twitter.com` |
| CVs expostos | `filetype:pdf "curriculum" OR "cv" "nome sobrenome"` |

user-invocable: true
---

## 4. GitHub dorks

Busca em código com filtros do GitHub — **mais eficiente que Google** para
código, pois pesquisa no conteúdo dos repositórios.

| Operador | Função | Exemplo |
|---|---|---|
| `extension:` | Extensão de arquivo | `extension:env` |
| `language:` | Linguagem | `language:python` |
| `path:` | Caminho do arquivo | `path:config` |
| `size:` | Tamanho do arquivo | `size:>1000` |
| `user:` / `org:` | Autor/orgação | `org:exemplo` |
| `pushed:` | Última atualização | `pushed:>2026-01-01` |
| `created:` | Data de criação | `created:<2025-01-01` |
| `stars:` / `fork:` | Popularidade | `stars:>100` |

| Objetivo | Dork |
|---|---|
| Secrets no código | `"BEGIN RSA PRIVATE KEY"` |
| Chaves AWS | `AKIA[0-9A-Z]{16}` (regex) |
| .env commitado | `filename:.env` + `extension:env` |
| Chaves de API em JS | `"api_key" extension:js` |
| Tokens em arquivos de config | `"token" path:config language:yaml` |
| Senhas em código | `password = "..." language:python` |
| Dados em repositórios | `"data" extension:sql org:[ALVO]` |

> GitHub exige autenticação para buscas pesadas — use a API com token se
> for recorrente.

---

## 5. Combinações avançadas

```text
# Encontrar credenciais em backups de um domínio
"index of" "backup" filetype:zip site:[ALVO]

# Painéis de admin com login em subdomínios
inurl:admin (intitle:login OR intitle:"sign in") site:[ALVO]

# Chaves AWS em repositórios de uma organização
"AKIA" extension:txt OR extension:log org:[ALVO]

# Strings de conexão em arquivos de config
intext:"Server=" intext:"User ID=" intext:"Password=" filetype:config site:[ALVO]

# Formulários de login com parâmetro GET (potencial SQLi)
inurl:login.php?id= site:[ALVO]
```

**Técnica de aprofundamento:**

1. Comece amplo: `site:[ALVO]` → mapeie subdomínios e tipos de arquivo
2. Aprofunde por tipo: `site:[ALVO] filetype:pdf` → depois `xlsx` → depois `sql`
3. Cruze achados: domínio encontrado em dump → busque `intext:"@"[ALVO]` para e-mails
4. Valide cada achado **antes** de usar (o resultado do buscador pode ser lixo/indexado de outra época)

user-invocable: true
---

## 6. Integração com a ferramenta de busca

A skill foi feita para rodar com a ferramenta `websearch` do agente:

- **Antes da busca:** monte a query com operadores conforme o objetivo da seção 3
- **Depois do resultado:** repita com refinamento (`-`, `OR`, `filetype:`)
- **Multi-motor:** Google limita dorks agressivos; rode a mesma dork no Bing (`prefix:`) e no GitHub para cobertura
- **Sempre cite o dork usado** no relatório da resposta, para o usuário poder reproduzir

### Fluxo recomendado (recon de alvo autorizado)

```text
1. site:[ALVO]                    → inventário inicial
2. site:[ALVO] filetype:pdf/xlsx/docx  → documentos
3. intitle:"index of" site:[ALVO] → diretórios abertos
4. inurl:admin/login site:[ALVO]  → superfícies de auth
5. intext:"@[ALVO]" filetype:txt  → e-mails
6. Cruze com GitHub dorks se for código aberto
```

---

## 7. Integração com pentest (recon)

Usar junto com a skill `zenox-pentest` na **Fase 1 (Reconnaissance)**:

| Fase do pentest | Dork correspondente |
|---|---|
| Tech stack | `inurl:wp- OR inurl:drupal OR inurl:jsp site:[ALVO]` |
| Superfície de auth | `inurl:login OR inurl:admin site:[ALVO]` |
| Arquivos expostos | `intitle:"index of" site:[ALVO]` |
| Dados vazados | `intext:"@[ALVO]" filetype:pdf` |
| Endpoints de API | `inurl:api site:[ALVO]` |

Regras de autorização da zenox-pentest se aplicam integralmente:
**nunca explore um achado sem autorização escrita do proprietário do alvo.**

user-invocable: true
---

## 8. Limitações e alternativas

| Limitação | Detalhe |
|---|---|
| **Rate limiting** | Google bloqueia dorks agressivos em sequência rápida; espaçe as buscas |
| **Indexação defasada** | O Google não indexa tudo; ausência de resultado ≠ inexistência |
| **Resultados filtrados** | Google remove conteúdo sensível após denúncias; resultados podem sumir |
| **Recaptcha** | Sequências longas disparam CAPTCHA — use a ferramenta websearch (que contorna com crawling) |

**Alternativas/motores extras:**

- **Bing:** `prefix:` (title), `site:`, `filetype:` — mais permissivo com volume
- **GitHub search:** melhor para código (seção 4)
- **Shodan / Censys:** dispositivos e serviços (não indexado por Google)
- **The Wayback Machine:** versões antigas de páginas removidas

---

## 9. Ética e autorização

- Dorks operam **somente sobre informação pública já indexada** — sem invasão, sem brute-force, sem acesso a sistemas
- Para uso em **pentest/OSINT de terceiros**: exige **autorização escrita** do proprietário (mesma regra da skill `zenox-pentest`)
- **Não** use para acessar, validar ou explorar credenciais/dados encontrados de terceiros
- Relate achados sensíveis ao proprietário do domínio (disclosure responsável)

user-invocable: true
---

## 10. Checklist de 60 segundos

- [ ] Objetivo identificado (arquivo / painel / credencial / superfície)?
- [ ] Dork construído com operador correto (`site:` + `filetype:` + `intext:`)?
- [ ] Sintaxe válida (sem espaço após `:`, `OR` maiúsculo, `-` para excluir)?
- [ ] `[ALVO]` substituído?
- [ ] Resultados **validados** antes de usar (não é lixo indexado)?
- [ ] Autorização para uso em alvo de terceiros?
- [ ] Dork citado no relatório para reprodução?

---

## Fontes

- [Google Search Operators — Google](https://support.google.com/websearch/answer/2466433)
- [Google Hacking Database (GHDB) — Exploit-DB](https://www.exploit-db.com/google-hacking-database)
- [GitHub Search — docs](https://docs.github.com/en/search-github)
- [Bing Advanced Search Operators](https://help.bing.microsoft.com/)
