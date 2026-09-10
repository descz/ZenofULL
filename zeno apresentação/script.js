// ===== Navbar scroll =====
const navbar = document.getElementById("navbar");
const onScroll = () => navbar.classList.toggle("scrolled", window.scrollY > 10);
window.addEventListener("scroll", onScroll, { passive: true });
onScroll();

// ===== Mobile menu =====
const toggle = document.getElementById("navToggle");
const mobile = document.getElementById("navMobile");
toggle.addEventListener("click", () => {
  const open = mobile.classList.toggle("open");
  toggle.classList.toggle("open", open);
  toggle.setAttribute("aria-expanded", open);
});
mobile.querySelectorAll("a").forEach((a) =>
  a.addEventListener("click", () => {
    mobile.classList.remove("open");
    toggle.classList.remove("open");
    toggle.setAttribute("aria-expanded", "false");
  })
);

// ===== Reveal on scroll =====
const io = new IntersectionObserver(
  (entries) => {
    entries.forEach((e) => {
      if (e.isIntersecting) {
        e.target.classList.add("visible");
        io.unobserve(e.target);
      }
    });
  },
  { threshold: 0.12 }
);
document.querySelectorAll(".reveal").forEach((el) => io.observe(el));

// ===== Presentation tabs =====
document.querySelectorAll("#presTabs .pres-tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("#presTabs .pres-tab").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".pres-pane").forEach((p) => p.classList.remove("active"));
    btn.classList.add("active");
    const pane = document.querySelector(`.pres-pane[data-pane="${btn.dataset.tab}"]`);
    if (pane) pane.classList.add("active");
  });
});

// ===== i18n =====
const LANGS = [
  { code: "pt", label: "Português", short: "PT", html: "pt-BR" },
  { code: "en", label: "English", short: "EN", html: "en" },
  { code: "hi", label: "हिन्दी", short: "HI", html: "hi" },
  { code: "zh", label: "中文", short: "ZH", html: "zh-CN" },
  { code: "ja", label: "日本語", short: "JA", html: "ja" },
  { code: "ru", label: "Русский", short: "RU", html: "ru" },
];

const I18N = {
pt: {
nav_pres: "Apresentação", nav_loop: "Como ele pensa", nav_eco: "O que ele sabe fazer", nav_why: "Por que Zeno", nav_faq: "FAQ",
cta: "Entrar na Comunidade",
hero_title: "Um assistente de IA que <em>faz</em> o trabalho por você",
hero_sub: "Peça no seu idioma: <strong>pesquisar, organizar dados, criar planilhas, transcrever áudios, mexer em arquivos</strong>. O Zeno executa — sozinho, no seu computador. E quanto mais você usa, melhor ele fica.",
hero_how: "Ver como ele pensa",
hero_note: "Preço será anunciado exclusivamente na comunidade · Acesso antecipado para membros",
cap_home: "Tela inicial do Zeno — é só pedir o que você precisa.",
s1d: "tarefas de teste resolvidas<br />do início ao fim",
s2d: "dos gastos evitados<br />graças ao cache inteligente",
s3d: "ferramentas prontas<br />para usar no dia a dia",
s4d: "dos testes do produto<br />passando, sempre",
pres_kicker: "APRESENTAÇÃO", pres_title: "Veja o Zeno por dentro",
pres_sub: "Telas reais do produto, sem maquetes: onde tudo começa e onde tudo é lembrado.",
tab_home: "Tela inicial", tab_mem: "Memória",
cap_mem: "A Memória do Zeno — tudo que você sabe e ensinou, organizado num mapa vivo.",
loop_kicker: "COMO ELE PENSA", loop_title: "O Loop Dialético",
loop_sub: "Inspirado na <strong>dialética dos filósofos gregos</strong>: em vez de aceitar a primeira resposta, o Zeno <strong>propõe, questiona e verifica</strong> — repetindo o ciclo até acertar. É por isso que ele entrega resultado de nível alto, até rodando com modelos de IA pequenos.",
tese_tag: "Tese", tese_t: "Propor", tese_d: "Faz a tarefa e apresenta o primeiro resultado.",
anti_tag: "Antítese", anti_t: "Questionar", anti_d: "Desconfia do próprio trabalho: testa, confere e procura erros.",
sin_tag: "Síntese", sin_t: "Verificar", sin_d: "Corrige e entrega só o que passou na checagem. Se não passou, o ciclo recomeça.",
loopback: "repete até o resultado ficar certo — com limites de tempo e pontos de segurança",
d1t: "Ele corrige os próprios erros", d1d: "Quando algo dá errado no meio do caminho, o Zeno percebe, entende o problema e tenta de novo do jeito certo.",
d2t: "Ele confere antes de mexer", d2d: "Nada de edição no escuro: o Zeno lê o que está lá antes de alterar e mostra exatamente o que mudou.",
d3t: "Ele divide tarefas grandes", d3d: "Trabalhos longos viram uma lista de passos — e o Zeno segue o plano, marcando cada etapa concluída.",
eco_kicker: "O QUE ELE SABE FAZER", eco_title: "Um agente só. Ajudinha<br />de mais ninguém.",
eco_sub: "Conecta com ferramentas, aprende habilidades novas, monta uma equipe quando a tarefa é grande e <strong>lembra de você</strong>. Tudo no mesmo app.",
e1t: "Conecta com suas ferramentas", e1d: "O Zeno conversa com os serviços e aplicativos que você já usa — e-mail, agenda, planilhas e muito mais — de forma direta e segura.",
e2t: "Aprende habilidades novas", e2d: "Você ensina, ele aprende. O Zeno aceita \"habilidades\" criadas por você ou pela comunidade — e fica mais útil a cada uma.",
e3t: "Trabalha em equipe", e3d: "Para tarefas grandes, o Zeno monta uma equipe interna: um escreve, outro revisa e outro testa. Você recebe só o resultado pronto.",
e4t: "Lembra de você", e4d: "Suas preferências, seus projetos e o que você já ensinou ficam guardados. Na próxima sessão, o Zeno já sabe como você gosta de trabalhar.",
wc_kicker: "POR QUE ZENO", wc_title: "Feito para trabalhar<br />por você",
w1t: "Executa, não só responde", w1d: "Ele navega na web, clica, preenche, organiza arquivos e entrega o resultado pronto — não para em conselhos.",
w2t: "Seguro e no seu controle", w2d: "Trabalha isolado do resto do sistema e você acompanha cada passo. Nada acontece por trás das costas.",
w3t: "Leve e rápido", w3d: "Tecnologia própria, sem navegador pesado por trás. Abre rápido e não engole o seu computador.",
w4t: "Custo sob controle", w4d: "Engenharia inteligente para gastar pouco a cada tarefa — e um modo econômico para quem quer máximo desempenho por mínimo custo.",
w5t: "Seus dados são seus", w5d: "A memória e os arquivos ficam no seu computador, salvos de um jeito que você consegue abrir e ler. Nada escondido.",
w6t: "Testado de verdade", w6d: "Cada versão passa por bateria de testes antes de chegar até você. O que chega é o que funciona.",
st_kicker: "COMO FUNCIONA", st_title: "Simples assim", st_sub: "Do zero ao primeiro trabalho concluído em minutos.",
p1t: "Entre na comunidade", p1d: "Acesse nosso <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\"><strong>grupo no Telegram</strong></a>, acompanhe o desenvolvimento, <strong>tutoriais e o lançamento</strong>. O acesso antecipado sai por lá primeiro.",
p2t: "Baixe e abra o Zeno", p2d: "Instalação simples no Windows, sem configuração complicada. Abra e comece a conversar na hora.",
p3t: "Peça em qualquer linguagem", p3d: "\"Pesquise os preços e monte uma planilha\", \"resuma esse áudio\", \"arruma esse código\" — em português, inglês ou qualquer outro idioma. Sem comandos esquisitos.",
p4t: "Receba pronto", p4d: "O Zeno executa, confere e entrega o resultado — e anota o que aprendeu para a próxima tarefa ser ainda melhor.",
pe_kicker: "PARA QUEM É", pe_title: "Qualquer pessoa que trabalha na web",
a1t: "Profissionais", a1d: "Pesquisas de preço, relatórios e mensagens repetitivas: <strong>entregue o trabalho chato</strong> para o Zeno e ganhe seu dia de volta.",
a2t: "Estudantes e Pesquisadores", a2d: "Junte informações de <strong>várias fontes ao mesmo tempo</strong> e receba resumos e materiais organizados, prontos para estudar.",
a3t: "Quem mexe com código", a3d: "O Zeno lê o projeto, faz o ajuste, <strong>roda os testes</strong> e só entrega o que funciona de verdade.",
a4t: "Analistas", a4d: "Dados espalhados pela internet viram <strong>planilhas e relatórios prontos</strong> — sem programar nada.",
a5t: "Criadores de Conteúdo", a5d: "Pesquise referências, organize ideias e gere <strong>resumos e roteiros</strong> com um pedido só.",
a6t: "Você mesmo", a6d: "Compras online, pesquisas do dia a dia, áudios para transcrever: <strong>peça e pronto</strong>. Simples assim.",
c_kicker: "ACESSO ANTECIPADO", c_title: "Preço ainda não anunciado",
c_sub: "Estamos finalizando o Zeno. <strong>Preços e o lançamento serão divulgados primeiro na comunidade</strong> — entre agora e garanta vantagens de quem chegou antes.",
cc_title: "Comunidade Zeno no Telegram", cc_text: "Tutoriais, bastidores do desenvolvimento, anúncios de versões e <strong>preço em primeira mão</strong>.",
perk1t: "Anúncio do preço em primeira mão", perk1d: "Saiba dos planos antes de todo mundo",
perk2t: "Acesso antecipado", perk2d: "Beta e testes de novas versões",
perk3t: "Suporte e habilidades", perk3d: "Tutoriais, dúvidas e skills do grupo",
f_kicker: "PERGUNTAS", f_title: "Alguma dúvida? Aqui está a resposta",
f_sub: "Ainda ficou com alguma dúvida? <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">Pergunte na comunidade</a>",
q1: "O que é o Zeno, em uma frase?", a1: "É um assistente de IA que <strong>faz</strong> o trabalho por você no computador: pesquisa, organiza dados, cria arquivos e aprende com você — você só pede no seu idioma.",
q2: "O que ele consegue fazer por mim?", a2: "Coisas como: pesquisar preços e montar uma planilha, resumir um monte de páginas ou áudios, organizar seus arquivos, criar relatórios e até ajustar códigos com testes. Se é algo que dá para fazer na web ou no computador, dá para pedir ao Zeno.",
q3: "O que é o loop dialético?", a3: "É o jeito do Zeno pensar, inspirado na dialética dos filósofos gregos: ele propõe uma solução, <strong>questiona o próprio trabalho</strong> (testa e confere) e só entrega o que passou na verificação. Por isso ele acerta bastante, até usando modelos de IA pequenos.",
q4: "Ele lembra do que eu faço?", a4: "Sim. O Zeno guarda suas preferências, seus projetos e o que você ensinou numa <strong>memória visual</strong> — um mapa do seu conhecimento que cresce com o uso. E fica tudo no seu computador.",
q5: "É seguro?", a5: "Sim. O Zeno trabalha isolado do resto do sistema, você acompanha cada passo e seus dados ficam <strong>salvos no seu computador</strong> — em arquivos que você consegue abrir e ler quando quiser.",
q6: "Quando o Zeno será lançado?", a6: "Estamos nos ajustes finais da versão 1.0. As datas de beta e lançamento serão anunciadas <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">na nossa comunidade do Telegram</a> — membros recebem tudo em primeira mão.",
q7: "Quanto vai custar?", a7: "O preço ainda não foi definido publicamente. Valores, planos e condições de acesso antecipado serão divulgados exclusivamente para a comunidade. Entre no grupo para não perder.",
q8: "Em quais sistemas funciona?", a8: "O Zeno está sendo lançado primeiro para <strong>Windows</strong>. Outras plataformas podem vir depois, conforme a demanda da comunidade.",
fin_title: "Pronto para entrar no horizonte<br />de eventos?",
fin_text: "<strong>Entre na comunidade</strong> agora e seja dos primeiros a experimentar o Zeno. Novidades, preço e acesso antecipado saem por lá primeiro.",
tag1: "FÁCIL DE USAR", tag2: "APRENDE COM VOCÊ", tag3: "TUDO NO SEU COMPUTADOR",
footer: "© 2026 Zeno · Todos os direitos reservados",
meta_desc: "Zeno é um assistente de IA para desktop que executa tarefas por você: pesquisa na web, organiza dados, transcreve áudios e aprende com você. Entre na comunidade e saiba primeiro.",
alt_home: "Tela inicial do Zeno, com o buraco negro e o campo para pedir tarefas",
alt_mem: "Tela de Memória do Zeno, mostrando um mapa do conhecimento conectado"
},
en: {
nav_pres: "Presentation", nav_loop: "How it thinks", nav_eco: "What it can do", nav_why: "Why Zeno", nav_faq: "FAQ",
cta: "Join the Community",
hero_title: "An AI assistant that <em>does</em> the work for you",
hero_sub: "Ask in your language: <strong>search, organize data, build spreadsheets, transcribe audio, handle files</strong>. Zeno executes — on its own, on your computer. And the more you use it, the better it gets.",
hero_how: "See how it thinks",
hero_note: "Price will be announced exclusively in the community · Early access for members",
cap_home: "Zeno's home screen — just ask for what you need.",
s1d: "test tasks solved<br />end to end",
s2d: "of costs avoided<br />thanks to smart caching",
s3d: "ready-to-use tools<br />for everyday work",
s4d: "of product tests<br />always passing",
pres_kicker: "PRESENTATION", pres_title: "See Zeno from the inside",
pres_sub: "Real product screens, no mockups: where everything starts and where everything is remembered.",
tab_home: "Home screen", tab_mem: "Memory",
cap_mem: "Zeno's Memory — everything you know and taught, organized in a living map.",
loop_kicker: "HOW IT THINKS", loop_title: "The Dialectic Loop",
loop_sub: "Inspired by the <strong>dialectic of the Greek philosophers</strong>: instead of accepting the first answer, Zeno <strong>proposes, questions and verifies</strong> — repeating the cycle until it gets it right. That is why it delivers top-level results, even running on small AI models.",
tese_tag: "Thesis", tese_t: "Propose", tese_d: "Does the task and presents the first result.",
anti_tag: "Antithesis", anti_t: "Question", anti_d: "Questions its own work: tests, double-checks and hunts for errors.",
sin_tag: "Synthesis", sin_t: "Verify", sin_d: "Fixes and delivers only what passed verification. If it did not pass, the cycle restarts.",
loopback: "repeats until the result is right — with time limits and safety checkpoints",
d1t: "It fixes its own mistakes", d1d: "When something goes wrong midway, Zeno notices, understands the problem and tries again the right way.",
d2t: "It checks before touching", d2d: "No blind edits: Zeno reads what is there before changing anything and shows exactly what changed.",
d3t: "It breaks down big tasks", d3d: "Long jobs become a checklist — and Zeno follows the plan, ticking off each finished step.",
eco_kicker: "WHAT IT CAN DO", eco_title: "One agent. No help<br />needed.",
eco_sub: "Connects to tools, learns new skills, builds a team when the task is big and <strong>remembers you</strong>. All in the same app.",
e1t: "Connects to your tools", e1d: "Zeno talks to the services and apps you already use — email, calendar, spreadsheets and much more — directly and securely.",
e2t: "Learns new skills", e2d: "You teach, it learns. Zeno accepts \"skills\" created by you or the community — and gets more useful with each one.",
e3t: "Works as a team", e3d: "For big tasks, Zeno builds an internal team: one writes, another reviews, another tests. You only receive the finished result.",
e4t: "Remembers you", e4d: "Your preferences, projects and everything you have taught stay saved. Next session, Zeno already knows how you like to work.",
wc_kicker: "WHY ZENO", wc_title: "Built to work<br />for you",
w1t: "It executes, not just answers", w1d: "It browses the web, clicks, fills in, organizes files and delivers the finished result — it does not stop at advice.",
w2t: "Safe and under your control", w2d: "Works isolated from the rest of the system and you follow every step. Nothing happens behind your back.",
w3t: "Light and fast", w3d: "Its own technology, no heavy browser behind it. Opens fast and does not eat your computer.",
w4t: "Cost under control", w4d: "Smart engineering to spend little on each task — plus an economy mode for maximum performance at minimum cost.",
w5t: "Your data is yours", w5d: "Memory and files stay on your computer, saved in a way you can open and read. Nothing hidden.",
w6t: "Truly tested", w6d: "Every version goes through a battery of tests before reaching you. What arrives is what works.",
st_kicker: "HOW IT WORKS", st_title: "That simple", st_sub: "From zero to the first finished job in minutes.",
p1t: "Join the community", p1d: "Visit our <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\"><strong>Telegram group</strong></a>, follow development, <strong>tutorials and the launch</strong>. Early access comes out there first.",
p2t: "Download and open Zeno", p2d: "Simple install on Windows, no complicated setup. Open it and start chatting right away.",
p3t: "Ask in any language", p3d: "\"Research prices and build a spreadsheet\", \"summarize this audio\", \"fix this code\" — in Portuguese, English or any other language. No weird commands.",
p4t: "Receive it done", p4d: "Zeno executes, double-checks and delivers the result — and notes what it learned so the next task is even better.",
pe_kicker: "WHO IT IS FOR", pe_title: "Anyone who works on the web",
a1t: "Professionals", a1d: "Price research, reports and repetitive messages: <strong>hand the boring work over</strong> to Zeno and win your day back.",
a2t: "Students & Researchers", a2d: "Gather information from <strong>multiple sources at once</strong> and get organized summaries and materials, ready to study.",
a3t: "People who code", a3d: "Zeno reads the project, makes the fix, <strong>runs the tests</strong> and only delivers what truly works.",
a4t: "Analysts", a4d: "Data scattered across the internet becomes <strong>ready spreadsheets and reports</strong> — without programming anything.",
a5t: "Content Creators", a5d: "Research references, organize ideas and generate <strong>summaries and scripts</strong> with a single request.",
a6t: "You yourself", a6d: "Online shopping, everyday research, audio to transcribe: <strong>ask and done</strong>. That simple.",
c_kicker: "EARLY ACCESS", c_title: "Price not announced yet",
c_sub: "We are finishing Zeno. <strong>Prices and the launch will be announced in the community first</strong> — join now and lock in early-bird perks.",
cc_title: "Zeno Community on Telegram", cc_text: "Tutorials, behind-the-scenes development, version announcements and <strong>first-hand pricing</strong>.",
perk1t: "First-hand price announcement", perk1d: "Hear about plans before everyone else",
perk2t: "Early access", perk2d: "Beta and new-version testing",
perk3t: "Support & skills", perk3d: "Tutorials, questions and group skills",
f_kicker: "QUESTIONS", f_title: "Any questions? Here is the answer",
f_sub: "Still have questions? <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">Ask in the community</a>",
q1: "What is Zeno, in one sentence?", a1: "It is an AI assistant that <strong>does</strong> the work for you on your computer: it researches, organizes data, creates files and learns from you — you just ask in your language.",
q2: "What can it do for me?", a2: "Things like: researching prices and building a spreadsheet, summarizing lots of pages or audio, organizing your files, creating reports and even fixing code with tests. If it can be done on the web or on your computer, you can ask Zeno.",
q3: "What is the dialectic loop?", a3: "It is how Zeno thinks, inspired by the dialectic of the Greek philosophers: it proposes a solution, <strong>questions its own work</strong> (tests and double-checks) and only delivers what passed verification. That is why it is right so often, even using small AI models.",
q4: "Does it remember what I do?", a4: "Yes. Zeno keeps your preferences, projects and what you taught in a <strong>visual memory</strong> — a map of your knowledge that grows with use. And it all stays on your computer.",
q5: "Is it safe?", a5: "Yes. Zeno works isolated from the rest of the system, you follow every step and your data stays <strong>saved on your computer</strong> — in files you can open and read whenever you want.",
q6: "When will Zeno launch?", a6: "We are putting the finishing touches on version 1.0. Beta and launch dates will be announced <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">in our Telegram community</a> — members hear everything first-hand.",
q7: "How much will it cost?", a7: "The price has not been set publicly yet. Pricing, plans and early-access terms will be shared exclusively with the community. Join the group so you do not miss it.",
q8: "Which systems does it run on?", a8: "Zeno is launching first for <strong>Windows</strong>. Other platforms may come later, based on community demand.",
fin_title: "Ready to step beyond the<br />event horizon?",
fin_text: "<strong>Join the community</strong> now and be among the first to try Zeno. News, pricing and early access come out there first.",
tag1: "EASY TO USE", tag2: "LEARNS FROM YOU", tag3: "ALL ON YOUR COMPUTER",
footer: "© 2026 Zeno · All rights reserved",
meta_desc: "Zeno is a desktop AI assistant that does the work for you: web research, data organization, audio transcription, and it learns from you. Join the community and hear first.",
alt_home: "Zeno's home screen, with the black hole and the field to request tasks",
alt_mem: "Zeno's Memory screen, showing a connected knowledge map"
},
hi: {
nav_pres: "प्रस्तुति", nav_loop: "यह कैसे सोचता है", nav_eco: "यह क्या कर सकता है", nav_why: "क्यों Zeno", nav_faq: "FAQ",
cta: "समुदाय से जुड़ें",
hero_title: "एक AI सहायक जो आपके लिए काम <em>करता</em> है",
hero_sub: "अपनी भाषा में कहें: <strong>खोजें, डेटा व्यवस्थित करें, स्प्रेडशीट बनाएं, ऑडियो ट्रांसक्राइब करें, फ़ाइलें संभालें</strong>। Zeno खुद करता है — आपके कंप्यूटर पर। और जितना आप इसे इस्तेमाल करेंगे, उतना बेहतर होगा।",
hero_how: "देखें यह कैसे सोचता है",
hero_note: "कीमत की घोषणा केवल समुदाय में होगी · सदस्यों के लिए शुरुआती एक्सेस",
cap_home: "Zeno की होम स्क्रीन — बस बताएं आपको क्या चाहिए।",
s1d: "शुरू से अंत तक हल किए गए<br />12/12 टेस्ट कार्य",
s2d: "स्मार्ट कैशिंग से<br />74.7% खर्च की बचत",
s3d: "रोज़मर्रा के काम के लिए<br />33 तैयार टूल",
s4d: "उत्पाद के 100% टेस्ट<br />हमेशा पास",
pres_kicker: "प्रस्तुति", pres_title: "Zeno को अंदर से देखें",
pres_sub: "उत्पाद की असली स्क्रीन, कोई नकली नहीं: जहां सब शुरू होता है और जहां सब याद रहता है।",
tab_home: "होम स्क्रीन", tab_mem: "मेमोरी",
cap_mem: "Zeno की मेमोरी — जो आप जानते हैं और सिखाया, जीवंत नक्शे में व्यवस्थित।",
loop_kicker: "यह कैसे सोचता है", loop_title: "द्वंद्वात्मक चक्र",
loop_sub: "<strong>यूनानी दार्शनिकों के द्वंद्ववाद</strong> से प्रेरित: पहले जवाब को स्वीकार करने के बजाय, Zeno <strong>प्रस्ताव करता है, सवाल उठाता है और सत्यापित करता है</strong> — सही होने तक चक्र दोहराता है। इसीलिए छोटे AI मॉडलों पर भी यह बेहतरीन परिणाम देता है।",
tese_tag: "पक्ष", tese_t: "प्रस्ताव", tese_d: "कार्य करता है और पहला परिणाम प्रस्तुत करता है।",
anti_tag: "प्रतिपक्ष", anti_t: "सवाल", anti_d: "अपने काम पर सवाल उठाता है: परीक्षण करता है, जांचता है और गलतियां खोजता है।",
sin_tag: "संश्लेषण", sin_t: "सत्यापन", sin_d: "सुधारता है और केवल वही देता है जो जांच में पास हुआ। पास न हुआ तो चक्र फिर शुरू।",
loopback: "सही परिणाम तक दोहराता है — समय सीमा और सुरक्षा जांच के साथ",
d1t: "अपनी गलतियां खुद सुधारता है", d1d: "बीच में कुछ गलत हो तो Zeno समझता है, समस्या पहचानता है और सही तरीके से फिर कोशिश करता है।",
d2t: "छूने से पहले जांचता है", d2d: "अंधेरे में बदलाव नहीं: बदलने से पहले Zeno पढ़ता है कि वहां क्या है और ठीक-ठीक दिखाता है क्या बदला।",
d3t: "बड़े काम तोड़ता है", d3d: "लंबे काम चरणों की सूची बन जाते हैं — और Zeno योजना पर चलता है, हर पूरा चरण टिक करता है।",
eco_kicker: "यह क्या कर सकता है", eco_title: "एक एजेंट। किसी और की<br />मदद नहीं।",
eco_sub: "टूल से जुड़ता है, नई स्किल सीखता है, बड़े काम पर टीम बनाता है और <strong>आपको याद रखता है</strong>। सब एक ही ऐप में।",
e1t: "आपके टूल से जुड़ता है", e1d: "Zeno उन सेवाओं और ऐप से बात करता है जो आप पहले से इस्तेमाल करते हैं — ईमेल, कैलेंडर, स्प्रेडशीट और बहुत कुछ — सीधे और सुरक्षित।",
e2t: "नई स्किल सीखता है", e2d: "आप सिखाएं, यह सीखे। Zeno आपके या समुदाय की बनाई \"स्किल\" स्वीकार करता है — और हर एक से और उपयोगी होता है।",
e3t: "टीम में काम करता है", e3d: "बड़े कामों के लिए Zeno अंदरूनी टीम बनाता है: एक लिखता है, दूसरा जांचता है, तीसरा टेस्ट करता है। आपको सिर्फ तैयार परिणाम मिलता है।",
e4t: "आपको याद रखता है", e4d: "आपकी पसंद, प्रोजेक्ट और जो आपने सिखाया, सब सहेजा रहता है। अगले सत्र में Zeno पहले से जानता है आपको कैसे काम पसंद है।",
wc_kicker: "क्यों ZENO", wc_title: "आपके लिए काम करने<br />को बना",
w1t: "करता है, सिर्फ बताता नहीं", w1d: "वेब पर घूमता है, क्लिक करता है, भरता है, फ़ाइलें व्यवस्थित करता है और तैयार परिणाम देता है — सलाह पर नहीं रुकता।",
w2t: "सुरक्षित और आपके नियंत्रण में", w2d: "सिस्टम के बाकी हिस्से से अलग काम करता है और आप हर कदम देखते हैं। पीठ पीछे कुछ नहीं होता।",
w3t: "हल्का और तेज़", w3d: "अपनी तकनीक, पीछे कोई भारी ब्राउज़र नहीं। तेज़ खुलता है और आपका कंप्यूटर नहीं खाता।",
w4t: "खर्च काबू में", w4d: "हर काम पर कम खर्च की स्मार्ट इंजीनियरिंग — और कम से कम लागत पर ज़्यादा प्रदर्शन चाहने वालों के लिए किफायती मोड।",
w5t: "आपका डेटा आपका है", w5d: "मेमोरी और फ़ाइलें आपके कंप्यूटर पर रहती हैं, ऐसे सहेजी जिन्हें आप खोल और पढ़ सकते हैं। कुछ छिपा नहीं।",
w6t: "सच में टेस्टेड", w6d: "हर वर्शन आप तक पहुंचने से पहले टेस्टों से गुज़रता है। जो पहुंचता है वही काम करता है।",
st_kicker: "यह कैसे काम करता है", st_title: "बस इतना आसान", st_sub: "शून्य से पहला पूरा काम मिनटों में।",
p1t: "समुदाय से जुड़ें", p1d: "हमारे <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\"><strong>Telegram ग्रुप</strong></a> में आएं, विकास, <strong>ट्यूटोरियल और लॉन्च</strong> देखें। शुरुआती एक्सेस सबसे पहले वहीं मिलेगा।",
p2t: "Zeno डाउनलोड करें और खोलें", p2d: "Windows पर आसान इंस्टॉल, कोई जटिल सेटअप नहीं। खोलें और तुरंत बात शुरू करें।",
p3t: "किसी भी भाषा में कहें", p3d: "\"कीमतें खोजकर स्प्रेडशीट बनाओ\", \"यह ऑडियो सारांश करो\", \"यह कोड ठीक करो\" — पुर्तगाली, अंग्रेज़ी या किसी भी भाषा में। कोई अजीब कमांड नहीं।",
p4t: "तैयार पाएं", p4d: "Zeno करता है, जांचता है और परिणाम देता है — और जो सीखा नोट करता है ताकि अगला काम और बेहतर हो।",
pe_kicker: "किसके लिए है", pe_title: "वेब पर काम करने वाला हर कोई",
a1t: "पेशेवर", a1d: "कीमत खोज, रिपोर्ट और दोहराए जाने वाले संदेश: <strong>उबाऊ काम</strong> Zeno को दें और अपना दिन वापस पाएं।",
a2t: "छात्र और शोधकर्ता", a2d: "<strong>एक साथ कई स्रोतों</strong> से जानकारी जोड़ें और पढ़ने को तैयार सारांश और सामग्री पाएं।",
a3t: "कोड करने वाले", a3d: "Zeno प्रोजेक्ट पढ़ता है, सुधार करता है, <strong>टेस्ट चलाता है</strong> और सिर्फ वही देता है जो सच में काम करता है।",
a4t: "विश्लेषक", a4d: "इंटरनेट पर बिखरा डेटा <strong>तैयार स्प्रेडशीट और रिपोर्ट</strong> बन जाता है — बिना कुछ प्रोग्राम किए।",
a5t: "कंटेंट क्रिएटर", a5d: "संदर्भ खोजें, विचार व्यवस्थित करें और एक ही अनुरोध में <strong>सारांश और स्क्रिप्ट</strong> बनाएं।",
a6t: "आप खुद", a6d: "ऑनलाइन खरीदारी, रोज़ की खोज, ट्रांसक्राइब करने वाले ऑडियो: <strong>कहें और हो गया</strong>। बस इतना आसान।",
c_kicker: "शुरुआती एक्सेस", c_title: "कीमत अभी घोषित नहीं",
c_sub: "हम Zeno को अंतिम रूप दे रहे हैं। <strong>कीमत और लॉन्च की घोषणा सबसे पहले समुदाय में होगी</strong> — अभी जुड़ें और पहले आने वालों के फायदे पाएं।",
cc_title: "Telegram पर Zeno समुदाय", cc_text: "ट्यूटोरियल, विकास की झलक, वर्शन घोषणाएं और <strong>सबसे पहले कीमत</strong>।",
perk1t: "सबसे पहले कीमत की घोषणा", perk1d: "सबसे पहले योजनाएं जानें",
perk2t: "शुरुआती एक्सेस", perk2d: "बीटा और नए वर्शन का परीक्षण",
perk3t: "सहायता और स्किल", perk3d: "ट्यूटोरियल, सवाल और ग्रुप की स्किल",
f_kicker: "सवाल", f_title: "कोई सवाल? यहां जवाब है",
f_sub: "और सवाल हैं? <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">समुदाय में पूछें</a>",
q1: "एक वाक्य में Zeno क्या है?", a1: "यह AI सहायक है जो आपके कंप्यूटर पर आपके लिए काम <strong>करता</strong> है: खोजता है, डेटा व्यवस्थित करता है, फ़ाइलें बनाता है और आपसे सीखता है — आप बस अपनी भाषा में कहें।",
q2: "मेरे लिए क्या कर सकता है?", a2: "जैसे: कीमतें खोजकर स्प्रेडशीट बनाना, कई पेज या ऑडियो का सारांश, फ़ाइलें व्यवस्थित करना, रिपोर्ट बनाना और टेस्ट के साथ कोड ठीक करना। वेब या कंप्यूटर पर जो हो सकता है, Zeno से कह सकते हैं।",
q3: "द्वंद्वात्मक चक्र क्या है?", a3: "Zeno के सोचने का तरीका है, यूनानी दार्शनिकों के द्वंद्ववाद से प्रेरित: समाधान प्रस्ताव करता है, <strong>अपने काम पर सवाल उठाता है</strong> (टेस्ट और जांच करता है) और सिर्फ वही देता है जो सत्यापन में पास हुआ। इसीलिए छोटे AI मॉडलों पर भी यह काफी सटीक है।",
q4: "क्या यह याद रखता है मैं क्या करता हूं?", a4: "हां। Zeno आपकी पसंद, प्रोजेक्ट और सिखाया हुआ <strong>विज़ुअल मेमोरी</strong> में रखता है — आपके ज्ञान का नक्शा जो इस्तेमाल से बढ़ता है। और सब आपके कंप्यूटर पर रहता है।",
q5: "क्या सुरक्षित है?", a5: "हां। Zeno सिस्टम के बाकी हिस्से से अलग काम करता है, आप हर कदम देखते हैं और आपका डेटा <strong>आपके कंप्यूटर पर सहेजा</strong> रहता है — ऐसी फ़ाइलों में जिन्हें आप कभी भी खोल और पढ़ सकते हैं।",
q6: "Zeno कब लॉन्च होगा?", a6: "हम वर्शन 1.0 को अंतिम रूप दे रहे हैं। बीटा और लॉन्च की तारीखें <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">हमारे Telegram समुदाय</a> में घोषित होंगी — सदस्य सब सबसे पहले सुनेंगे।",
q7: "कीमत क्या होगी?", a7: "कीमत सार्वजनिक रूप से तय नहीं हुई है। कीमत, प्लान और शुरुआती एक्सेस की शर्तें सिर्फ समुदाय को बताई जाएंगी। छूट न जाए इसलिए ग्रुप से जुड़ें।",
q8: "किन सिस्टम पर चलेगा?", a8: "Zeno पहले <strong>Windows</strong> के लिए आ रहा है। समुदाय की मांग पर बाद में अन्य प्लेटफॉर्म आ सकते हैं।",
fin_title: "इवेंट होराइज़न में कदम रखने<br />को तैयार?",
fin_text: "अभी <strong>समुदाय से जुड़ें</strong> और Zeno आज़माने वालों में पहले बनें। खबरें, कीमत और शुरुआती एक्सेस सबसे पहले वहीं आएंगे।",
tag1: "इस्तेमाल में आसान", tag2: "आपसे सीखता है", tag3: "सब आपके कंप्यूटर पर",
footer: "© 2026 Zeno · सर्वाधिकार सुरक्षित",
meta_desc: "Zeno डेस्कटॉप AI सहायक है जो आपके लिए काम करता है: वेब खोज, डेटा व्यवस्था, ऑडियो ट्रांसक्रिप्शन, और आपसे सीखता है। समुदाय से जुड़ें और सबसे पहले जानें।",
alt_home: "ब्लैक होल और कार्य अनुरोध फ़ील्ड के साथ Zeno की होम स्क्रीन",
alt_mem: "जुड़े ज्ञान मानचित्र वाली Zeno की मेमोरी स्क्रीन"
},
zh: {
nav_pres: "演示", nav_loop: "工作原理", nav_eco: "功能亮点", nav_why: "为何选择 Zeno", nav_faq: "常见问题",
cta: "加入社区",
hero_title: "为你<em>真正做事</em>的 AI 助手",
hero_sub: "用你的语言下达指令：<strong>搜索、整理数据、制作表格、转录音频、处理文件</strong>。Zeno 独立执行——就在你的电脑上。而且用得越多，它越懂你。",
hero_how: "看看它如何思考",
hero_note: "价格将仅在社区公布 · 成员优先体验",
cap_home: "Zeno 主界面——说出你的需求即可。",
s1d: "从头到尾解决的<br />12/12 项测试任务",
s2d: "智能缓存节省的<br />74.7% 费用",
s3d: "日常工作即开即用的<br />33 个工具",
s4d: "产品测试 100% 通过<br />始终如一",
pres_kicker: "产品演示", pres_title: "走进 Zeno",
pres_sub: "两张真实截图，没有模型：一切开始的地方，和一切被记住的地方。",
tab_home: "主界面", tab_mem: "记忆",
cap_mem: "Zeno 的记忆——你所知、所教的一切，汇成一张活的知识地图。",
loop_kicker: "思考方式", loop_title: "思辨循环",
loop_sub: "灵感来自<strong>古希腊哲学家的辩证法</strong>：不接受第一个答案，Zeno <strong>提出、质疑并验证</strong>——不断重复，直到做对。这就是它即使运行在小型 AI 模型上，也能交出高水平答卷的原因。",
tese_tag: "正题", tese_t: "提议", tese_d: "执行任务并给出第一个结果。",
anti_tag: "反题", anti_t: "质疑", anti_d: "对自己的工作提出质疑：测试、核查、寻找错误。",
sin_tag: "合题", sin_t: "验证", sin_d: "修正后只交付通过验证的结果。未通过，循环重新开始。",
loopback: "重复直到结果正确——设有时间限制和安全检查点",
d1t: "会自己改错", d1d: "中途出错时，Zeno 能察觉、理解问题，并用正确的方式再试一次。",
d2t: "动手前先核对", d2d: "从不盲改：修改前先读取现有内容，并清楚展示改了什么。",
d3t: "会拆解大任务", d3d: "长期工作变成步骤清单——Zeno 按计划推进，逐项打勾完成。",
eco_kicker: "能力亮点", eco_title: "一个智能体。不需要<br />任何帮手。",
eco_sub: "连接工具、学习新技能、大任务时组建团队，还<strong>记得你</strong>。全部在同一个应用里。",
e1t: "连接你的工具", e1d: "Zeno 与你常用的服务和应用对话——邮件、日历、表格等等——直接又安全。",
e2t: "学习新技能", e2d: "你教，它学。Zeno 接受你或社区创建的“技能”——每多一个，就更能干一点。",
e3t: "组队协作", e3d: "大任务面前，Zeno 会组建内部团队：一个写、一个审、一个测。你只管收成品。",
e4t: "记得你", e4d: "你的偏好、项目和你教过的一切都会保存。下一次，它已经知道你的工作习惯。",
wc_kicker: "为何 ZENO", wc_title: "为你工作<br />而生",
w1t: "只做事，不只动嘴", w1d: "上网浏览、点击、填写、整理文件，交付成品——不止于建议。",
w2t: "安全可控", w2d: "与系统其余部分隔离运行，每一步你都看得见。绝不在背后搞小动作。",
w3t: "轻快", w3d: "自研技术，背后没有笨重浏览器。启动快，不吃电脑。",
w4t: "花费可控", w4d: "精巧的工程让每次任务花得少——还有节能模式，花最少的钱办最多的事。",
w5t: "数据属于你", w5d: "记忆和文件都存在你的电脑里，以你能打开阅读的方式保存。没有任何隐藏。",
w6t: "真经过测试", w6d: "每个版本都要过一轮测试才交到你手上。到你手里的，就是能用的。",
st_kicker: "使用流程", st_title: "就这么简单", st_sub: "几分钟，从零到第一个成品。",
p1t: "加入社区", p1d: "来我们的 <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\"><strong>Telegram 群组</strong></a>，关注开发、<strong>教程和发布</strong>。抢先体验最先在那里放出。",
p2t: "下载并打开 Zeno", p2d: "Windows 上安装简单，无复杂配置。打开就能聊。",
p3t: "用任何语言下指令", p3d: "“查价格做个表格”、“总结这段音频”、“修一下这段代码”——中文、英文或任何语言都行。不用记奇怪命令。",
p4t: "收获成品", p4d: "Zeno 执行、核查并交付结果——还会记下学到的东西，让下一次更好。",
pe_kicker: "适用人群", pe_title: "每个在网上工作的人",
a1t: "职场人", a1d: "比价、报告和重复消息：把<strong>枯燥的工作</strong>交给 Zeno，把时间赢回来。",
a2t: "学生和研究者", a2d: "<strong>同时</strong>汇总多方信息，拿到整理好的总结和资料，直接开学。",
a3t: "写代码的人", a3d: "Zeno 读项目、改代码、<strong>跑测试</strong>，只交付真正能跑的东西。",
a4t: "分析师", a4d: "散落在网上的数据变成<strong>现成的表格和报告</strong>——不用写一行代码。",
a5t: "内容创作者", a5d: "查资料、理思路，一个请求生成<strong>总结和脚本</strong>。",
a6t: "你自己", a6d: "网购、日常搜索、待转录的音频：<strong>说句话就行</strong>。就这么简单。",
c_kicker: "抢先体验", c_title: "价格尚未公布",
c_sub: "我们正在收尾 Zeno。<strong>价格和发布将最先在社区公布</strong>——现在加入，锁定先行者福利。",
cc_title: "Telegram 上的 Zeno 社区", cc_text: "教程、开发幕后、版本公告和<strong>第一手价格</strong>。",
perk1t: "第一手价格公告", perk1d: "比所有人先知道方案",
perk2t: "抢先体验", perk2d: "内测和新版本试用",
perk3t: "支持与技能", perk3d: "教程、答疑和群技能",
f_kicker: "常见问题", f_title: "有疑问？这里有答案",
f_sub: "还有疑问？<a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">去社区提问</a>",
q1: "一句话：Zeno 是什么？", a1: "它是替你在电脑上<strong>做事</strong>的 AI 助手：搜索、整理数据、创建文件，还向你学习——你只需用自己的语言下指令。",
q2: "它能为我做什么？", a2: "比如：查价格做表格、总结一堆网页或音频、整理文件、生成报告，甚至带测试修代码。网上或电脑上能做的事，都可以交给 Zeno。",
q3: "什么是思辨循环？", a3: "这是 Zeno 的思考方式，灵感来自古希腊哲学家的辩证法：提出方案，<strong>质疑自己的工作</strong>（测试与核查），只交付通过验证的结果。所以即使在小型 AI 模型上，它也很少出错。",
q4: "它记得我做过什么吗？", a4: "记得。Zeno 把你的偏好、项目和你教的东西存在<strong>可视化记忆</strong>里——一张随使用不断生长的知识地图。而且全部存在你的电脑上。",
q5: "安全吗？", a5: "安全。Zeno 与系统其余部分隔离运行，每一步你都看得见，数据<strong>保存在你的电脑</strong>上——存在你随时能打开阅读的文件里。",
q6: "Zeno 何时发布？", a6: "我们正在打磨 1.0 版本。内测和发布时间将在<a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">Telegram 社区</a>公布——成员第一时间知晓。",
q7: "多少钱？", a7: "价格尚未公开。定价、套餐和抢先体验条件只会在社区公布。加入群组，别错过。",
q8: "支持哪些系统？", a8: "Zeno 首发 <strong>Windows</strong>。其他平台将根据社区需求陆续推出。",
fin_title: "准备好踏入视界<br />之内了吗？",
fin_text: "现在<strong>加入社区</strong>，成为第一批体验 Zeno 的人。动态、价格和抢先体验最先在那里发布。",
tag1: "简单易用", tag2: "越用越懂你", tag3: "全在你的电脑上",
footer: "© 2026 Zeno · 版权所有",
meta_desc: "Zeno 是替你做事的桌面 AI 助手：网页搜索、数据整理、音频转录，还向你学习。加入社区，第一时间获取消息。",
alt_home: "带有黑洞和任务输入框的 Zeno 主界面",
alt_mem: "展示互联知识地图的 Zeno 记忆界面"
},
ja: {
nav_pres: "デモ", nav_loop: "考え方", nav_eco: "できること", nav_why: "Zeno の理由", nav_faq: "FAQ",
cta: "コミュニティに参加",
hero_title: "仕事を<em>代わりにやる</em> AI アシスタント",
hero_sub: "あなたの言語で頼むだけ：<strong>検索、データ整理、表の作成、音声の文字起こし、ファイル操作</strong>。Zeno が単独で実行——あなたのパソコンの中で。使うほど、賢くなります。",
hero_how: "考え方を見る",
hero_note: "価格はコミュニティでのみ発表 · メンバーは先行アクセス",
cap_home: "Zeno のホーム画面——必要なことを頼むだけ。",
s1d: "最初から最後まで解決した<br />12/12 のテストタスク",
s2d: "賢いキャッシュで削減した<br />74.7% のコスト",
s3d: "日常使いできる<br />33 のツール",
s4d: "製品テストは 100% パス<br />いつも",
pres_kicker: "プレゼンテーション", pres_title: "Zeno の中をのぞく",
pres_sub: "モックなしの実画面 2 枚：すべてが始まる場所と、すべてが記憶される場所。",
tab_home: "ホーム画面", tab_mem: "メモリー",
cap_mem: "Zeno のメモリー——あなたの知識と教えを生きた地図に。",
loop_kicker: "考え方", loop_title: "弁証法ループ",
loop_sub: "<strong>古代ギリシャ哲学の弁証法</strong>に着想：最初の答えを鵜呑みにせず、Zeno は<strong>提案し、疑い、検証する</strong>——正解するまで繰り返します。小さな AI モデルでも高いレベルの成果を出せるのはこのためです。",
tese_tag: "テーゼ", tese_t: "提案する", tese_d: "タスクを実行し、最初の結果を示します。",
anti_tag: "アンチテーゼ", anti_t: "疑う", anti_d: "自分の仕事を疑います：テストし、確認し、誤りを探します。",
sin_tag: "ジンテーゼ", sin_t: "検証する", sin_d: "修正し、検証を通ったものだけ届けます。通らなければ最初からやり直し。",
loopback: "正しくなるまで繰り返す——時間制限と安全チェック付き",
d1t: "自分のミスを直す", d1d: "途中で失敗しても Zeno は気づき、問題を理解して正しい方法でもう一度挑戦します。",
d2t: "触る前に確認する", d2d: "闇雲な編集はしません：変更前に現状を読み、何が変わったか正確に示します。",
d3t: "大きな仕事を分ける", d3d: "長い仕事は手順リストに——Zeno は計画通りに進め、完了ごとにチェックします。",
eco_kicker: "できること", eco_title: "ひとつのエージェント。助けは<br />いらない。",
eco_sub: "ツールとつながり、新しいスキルを学び、大きな仕事ではチームを組み、<strong>あなたを覚えます</strong>。すべて同じアプリで。",
e1t: "ツールとつながる", e1d: "Zeno は使い慣れたサービスやアプリと直接やり取り——メール、カレンダー、表計算など——安全に。",
e2t: "新しいスキルを学ぶ", e2d: "教えれば覚えます。あなたやコミュニティが作った「スキル」を受け入れ、ひとつ増えるごとに賢くなります。",
e3t: "チームで働く", e3d: "大きな仕事では内部チームを結成：書く人、直す人、試す人。あなたが受け取るのは完成品だけ。",
e4t: "あなたを覚える", e4d: "好みも案件も教えたことも保存されます。次回、Zeno はあなたの働き方をすでに知っています。",
wc_kicker: "ZENO の理由", wc_title: "あなたのために働く<br />ために生まれた",
w1t: "答えるだけでなく実行する", w1d: "Web を巡り、クリックし、入力し、ファイルを整理して完成品を届けます——助言では終わりません。",
w2t: "安全で管理下に", w2d: "システムの他と隔離して動作し、全工程あなたが見届けます。裏で何かが起きることはありません。",
w3t: "軽くて速い", w3d: "独自技術で、重いブラウザは背後にいません。すぐ開き、パソコンを重くしません。",
w4t: "コスト管理", w4d: "1 タスクあたり少なく済む賢い設計——最小コストで最大性能の節約モード付き。",
w5t: "データはあなたのもの", w5d: "メモリーもファイルもあなたのパソコンに、開いて読める形で保存。隠し事なし。",
w6t: "本当にテスト済み", w6d: "あなたに届く前に全バージョンがテストを通過します。届くのは動くものだけ。",
st_kicker: "使い方", st_title: "たったこれだけ", st_sub: "ゼロから最初の完成品まで数分で。",
p1t: "コミュニティに参加", p1d: "<a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\"><strong>Telegram グループ</strong></a>へ。開発や<strong>チュートリアル、リリース</strong>を追跡。先行アクセスはここが最速。",
p2t: "Zeno を入手して開く", p2d: "Windows に簡単インストール、面倒な設定なし。開いてすぐ話せます。",
p3t: "どんな言語でも頼める", p3d: "「価格を調べて表にして」「この音声を要約して」「このコードを直して」——日本語、英語、その他どの言語でも。変なコマンド不要。",
p4t: "完成品を受け取る", p4d: "Zeno が実行・確認して結果を届け、学んだことを記録して次はさらに上手に。",
pe_kicker: "対象", pe_title: "Web で働くすべての人",
a1t: "ビジネスパーソン", a1d: "価格調査も報告書も繰り返しの連絡も：<strong>面倒な仕事</strong>は Zeno に渡して、時間を取り戻しましょう。",
a2t: "学生・研究者", a2d: "<strong>複数の情報源を同時</strong>にまとめ、学べる状態の要約と資料を受け取ります。",
a3t: "コードを書く人", a3d: "Zeno が案件を読み、修正し、<strong>テストを実行</strong>。本当に動くものだけ届けます。",
a4t: "アナリスト", a4d: "ネットに散らばるデータが<strong>完成済みの表と報告書</strong>に——プログラミング不要。",
a5t: "クリエイター", a5d: "資料を探し、アイデアを整理し、ひとつの依頼で<strong>要約と台本</strong>を生成。",
a6t: "あなた自身", a6d: "ネット通販も日常の調べものも文字起こしも：<strong>頼むだけ</strong>。それだけです。",
c_kicker: "先行アクセス", c_title: "価格は未発表",
c_sub: "Zeno を仕上げています。<strong>価格とリリースはコミュニティで最速発表</strong>——今入って先行特典を確保。",
cc_title: "Telegram の Zeno コミュニティ", cc_text: "チュートリアル、開発の裏側、バージョン発表と<strong>最速の価格情報</strong>。",
perk1t: "最速の価格発表", perk1d: "誰より先にプランを知る",
perk2t: "先行アクセス", perk2d: "ベータと新版テスト",
perk3t: "サポートとスキル", perk3d: "チュートリアル、質問、グループのスキル",
f_kicker: "質問", f_title: "疑問は？ここに答えがあります",
f_sub: "まだ疑問が？<a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">コミュニティで質問</a>",
q1: "一文で言うと Zeno とは？", a1: "パソコンであなたの代わりに仕事を<strong>する</strong> AI アシスタントです：調べ、データを整理し、ファイルを作り、あなたから学びます——あなたの言語で頼むだけ。",
q2: "何をしてくれるの？", a2: "例えば：価格調査と表の作成、大量ページや音声の要約、ファイル整理、報告書作成、テスト付きのコード修正まで。Web やパソコンでできることなら Zeno に頼めます。",
q3: "弁証法ループとは？", a3: "ギリシャ哲学の弁証法に着想した Zeno の考え方です：案を出し、<strong>自分の仕事を疑い</strong>（テストと確認）、検証を通ったものだけ届けます。小さな AI モデルでもよく当たるのはこのためです。",
q4: "私のことを覚えるの？", a4: "はい。好みも案件も教えたことも<strong>見えるメモリー</strong>に——使うほど育つあなたの知識地図に保存。しかも全部あなたのパソコンの中。",
q5: "安全？", a5: "はい。Zeno はシステムの他と隔離して動作し、全工程あなたが見届け、データは<strong>あなたのパソコンに保存</strong>——いつでも開いて読めるファイルで。",
q6: "リリースはいつ？", a6: "バージョン 1.0 を仕上げ中です。ベータと公開日は<a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">Telegram コミュニティ</a>で発表——メンバーが最速で知ります。",
q7: "いくら？", a7: "価格は未公表です。料金・プラン・先行条件はコミュニティ限定で発表します。見逃さないようグループへ。",
q8: "対応 OS は？", a8: "まず <strong>Windows</strong> で登場します。他 OS はコミュニティの要望次第で。",
fin_title: "事象の地平線の向こうへ<br />行く準備は？",
fin_text: "今<strong>コミュニティに参加</strong>して Zeno 最速体験組に。新着・価格・先行アクセスはここが最速。",
tag1: "使いやすい", tag2: "あなたから学ぶ", tag3: "すべてあなたのパソコンに",
footer: "© 2026 Zeno · 無断転載禁止",
meta_desc: "Zeno はあなたの代わりに働くデスクトップ AI アシスタント：Web 調査、データ整理、音声文字起こし、あなたから学習。コミュニティに参加して最速で知ろう。",
alt_home: "ブラックホールとタスク入力欄のある Zeno ホーム画面",
alt_mem: "つながる知識地図を示す Zeno メモリー画面"
},
ru: {
nav_pres: "Презентация", nav_loop: "Как он думает", nav_eco: "Что он умеет", nav_why: "Почему Zeno", nav_faq: "FAQ",
cta: "Вступить в сообщество",
hero_title: "ИИ-ассистент, который <em>делает</em> работу за вас",
hero_sub: "Просите на своём языке: <strong>искать, систематизировать данные, составлять таблицы, расшифровывать аудио, работать с файлами</strong>. Zeno выполняет — сам, на вашем компьютере. И чем больше вы им пользуетесь, тем лучше он становится.",
hero_how: "Как он думает",
hero_note: "Цена будет объявлена только в сообществе · Ранний доступ для участников",
cap_home: "Главный экран Zeno — просто попросите, что нужно.",
s1d: "тестовых задач, решённых<br />от начала до конца",
s2d: "расходов сэкономлено<br />благодаря умному кешу",
s3d: "готовых инструмента<br />для повседневной работы",
s4d: "тестов продукта<br />всегда проходят",
pres_kicker: "ПРЕЗЕНТАЦИЯ", pres_title: "Загляните внутрь Zeno",
pres_sub: "Два настоящих экрана, без макетов: место, где всё начинается, и место, где всё запоминается.",
tab_home: "Главный экран", tab_mem: "Память",
cap_mem: "Память Zeno — всё, что вы знаете и чему научили, в живой карте.",
loop_kicker: "КАК ОН ДУМАЕТ", loop_title: "Диалектический цикл",
loop_sub: "Вдохновлено <strong>диалектикой греческих философов</strong>: вместо того чтобы принимать первый ответ, Zeno <strong>предлагает, сомневается и проверяет</strong> — повторяя цикл, пока не получится. Поэтому он выдаёт результат высокого уровня даже на небольших ИИ-моделях.",
tese_tag: "Тезис", tese_t: "Предложить", tese_d: "Выполняет задачу и показывает первый результат.",
anti_tag: "Антитезис", anti_t: "Усомниться", anti_d: "Сомневается в собственной работе: тестирует, перепроверяет и ищет ошибки.",
sin_tag: "Синтез", sin_t: "Проверить", sin_d: "Исправляет и отдаёт только то, что прошло проверку. Не прошло — цикл начинается заново.",
loopback: "повторяет, пока результат не станет верным — с лимитами времени и контрольными точками",
d1t: "Сам исправляет свои ошибки", d1d: "Если что-то пошло не так, Zeno замечает, понимает проблему и пробует снова — правильным способом.",
d2t: "Проверяет, прежде чем трогать", d2d: "Никаких правок вслепую: Zeno читает, что там было, прежде чем менять, и точно показывает, что изменилось.",
d3t: "Делит большие задачи", d3d: "Длинная работа превращается в список шагов — и Zeno идёт по плану, отмечая каждый выполненный этап.",
eco_kicker: "ЧТО ОН УМЕЕТ", eco_title: "Один агент. Никакой<br />помощи не нужно.",
eco_sub: "Подключается к инструментам, учит новые навыки, собирает команду для большой задачи и <strong>помнит вас</strong>. Всё в одном приложении.",
e1t: "Подключается к вашим инструментам", e1d: "Zeno общается с сервисами и приложениями, которыми вы уже пользуетесь, — почта, календарь, таблицы и многое другое — напрямую и безопасно.",
e2t: "Учит новые навыки", e2d: "Вы учите — он учится. Zeno принимает «навыки», созданные вами или сообществом, — и с каждым становится полезнее.",
e3t: "Работает в команде", e3d: "Для больших задач Zeno собирает внутреннюю команду: один пишет, другой проверяет, третий тестирует. Вы получаете только готовый результат.",
e4t: "Помнит вас", e4d: "Ваши предпочтения, проекты и всё, чему вы научили, сохраняются. В следующий раз Zeno уже знает, как вам нравится работать.",
wc_kicker: "ПОЧЕМУ ZENO", wc_title: "Создан, чтобы работать<br />за вас",
w1t: "Выполняет, а не только отвечает", w1d: "Лазит по сети, кликает, заполняет, раскладывает файлы и отдаёт готовый результат — а не ограничивается советами.",
w2t: "Безопасно и под вашим контролем", w2d: "Работает изолированно от остальной системы, и вы видите каждый шаг. Ничего за спиной не происходит.",
w3t: "Лёгкий и быстрый", w3d: "Собственная технология, без тяжёлого браузера позади. Открывается быстро и не съедает компьютер.",
w4t: "Расходы под контролем", w4d: "Умная инженерия, чтобы тратить мало на каждую задачу, — плюс экономный режим для максимума за минимум.",
w5t: "Ваши данные — ваши", w5d: "Память и файлы остаются на вашем компьютере, сохранённые так, что вы можете их открыть и прочитать. Ничего скрытого.",
w6t: "По-настоящему протестирован", w6d: "Каждая версия проходит батарею тестов, прежде чем попасть к вам. Доходит то, что работает.",
st_kicker: "КАК ЭТО РАБОТАЕТ", st_title: "Проще простого", st_sub: "От нуля до первой готовой работы за минуты.",
p1t: "Вступите в сообщество", p1d: "Заходите в нашу <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\"><strong>группу в Telegram</strong></a>, следите за разработкой, <strong>уроками и запуском</strong>. Ранний доступ появляется там первым.",
p2t: "Скачайте и откройте Zeno", p2d: "Простая установка на Windows, без сложной настройки. Откройте и сразу общайтесь.",
p3t: "Просите на любом языке", p3d: "«Найди цены и собери таблицу», «перескажи это аудио», «почини этот код» — на португальском, английском или любом другом языке. Никаких странных команд.",
p4t: "Получите готовое", p4d: "Zeno выполняет, перепроверяет и отдаёт результат — и записывает выученное, чтобы следующая задача вышла ещё лучше.",
pe_kicker: "ДЛЯ КОГО", pe_title: "Для всех, кто работает в сети",
a1t: "Профессионалы", a1d: "Поиск цен, отчёты и повторяющиеся сообщения: <strong>отдайте скучную работу</strong> Zeno и верните себе день.",
a2t: "Студенты и исследователи", a2d: "Собирайте информацию <strong>из нескольких источников сразу</strong> и получайте готовые конспекты и материалы для учёбы.",
a3t: "Те, кто кодит", a3d: "Zeno читает проект, вносит правку, <strong>гоняет тесты</strong> и отдаёт только то, что действительно работает.",
a4t: "Аналитики", a4d: "Разбросанные по интернету данные превращаются в <strong>готовые таблицы и отчёты</strong> — без строчки кода.",
a5t: "Авторы контента", a5d: "Ищите источники, раскладывайте идеи и создавайте <strong>конспекты и сценарии</strong> одним запросом.",
a6t: "Вы сами", a6d: "Онлайн-покупки, повседневный поиск, аудио для расшифровки: <strong>попросили — готово</strong>. Проще некуда.",
c_kicker: "РАННИЙ ДОСТУП", c_title: "Цена пока не объявлена",
c_sub: "Мы доводим Zeno до ума. <strong>Цены и запуск будут объявлены сначала в сообществе</strong> — вступайте сейчас и забирайте преимущества первопроходцев.",
cc_title: "Сообщество Zeno в Telegram", cc_text: "Уроки, закулисье разработки, анонсы версий и <strong>цены из первых рук</strong>.",
perk1t: "Объявление цены из первых рук", perk1d: "Узнайте о тарифах раньше всех",
perk2t: "Ранний доступ", perk2d: "Бета и тестирование новых версий",
perk3t: "Поддержка и навыки", perk3d: "Уроки, вопросы и навыки группы",
f_kicker: "ВОПРОСЫ", f_title: "Есть вопросы? Вот ответы",
f_sub: "Остались вопросы? <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">Спросите в сообществе</a>",
q1: "Что такое Zeno — одной фразой?", a1: "Это ИИ-ассистент, который <strong>делает</strong> работу за вас на компьютере: ищет, систематизирует данные, создаёт файлы и учится у вас — вы просто просите на своём языке.",
q2: "Что он может для меня?", a2: "Например: найти цены и собрать таблицу, пересказать кучу страниц или аудио, разложить файлы, создать отчёты и даже починить код с тестами. Если это можно сделать в сети или на компьютере — можно поручить Zeno.",
q3: "Что за диалектический цикл?", a3: "Так Zeno думает — по мотивам диалектики греческих философов: он предлагает решение, <strong>сомневается в собственной работе</strong> (тестирует и перепроверяет) и отдаёт только то, что прошло проверку. Поэтому он так часто прав, даже на небольших ИИ-моделях.",
q4: "Он помнит, что я делаю?", a4: "Да. Zeno хранит ваши предпочтения, проекты и выученное в <strong>визуальной памяти</strong> — карте ваших знаний, которая растёт с использованием. И всё это — на вашем компьютере.",
q5: "Это безопасно?", a5: "Да. Zeno работает изолированно от остальной системы, вы видите каждый шаг, а ваши данные <strong>сохранены на вашем компьютере</strong> — в файлах, которые вы можете открыть и прочитать когда угодно.",
q6: "Когда выйдет Zeno?", a6: "Мы наводим последние штрихи на версию 1.0. Даты беты и запуска будут объявлены <a href=\"https://t.me/+3y64CbF6Om0xNjg5\" target=\"_blank\" rel=\"noopener\">в нашем Telegram-сообществе</a> — участники узнают всё из первых рук.",
q7: "Сколько будет стоить?", a7: "Цена публично пока не определена. Тарифы, планы и условия раннего доступа будут объявлены только для сообщества. Вступайте в группу, чтобы не пропустить.",
q8: "На каких системах работает?", a8: "Zeno сначала выходит для <strong>Windows</strong>. Другие платформы — позже, по запросам сообщества.",
fin_title: "Готовы шагнуть за горизонт<br />событий?",
fin_text: "<strong>Вступайте в сообщество</strong> сейчас и станьте одними из первых, кто попробует Zeno. Новости, цены и ранний доступ появляются там первыми.",
tag1: "ПРОСТО ПОЛЬЗОВАТЬСЯ", tag2: "УЧИТСЯ У ВАС", tag3: "ВСЁ НА ВАШЕМ КОМПЬЮТЕРЕ",
footer: "© 2026 Zeno · Все права защищены",
meta_desc: "Zeno — настольный ИИ-ассистент, который делает работу за вас: поиск в сети, систематизация данных, расшифровка аудио, и он учится у вас. Вступайте в сообщество и узнавайте первыми.",
alt_home: "Главный экран Zeno с чёрной дырой и полем для задач",
alt_mem: "Экран памяти Zeno со связанной картой знаний"
}
};

// ===== Language selector =====
const langBtn = document.getElementById("langBtn");
const langMenu = document.getElementById("langMenu");
const langCur = document.getElementById("langCur");

function buildLangMenu() {
  langMenu.innerHTML = "";
  LANGS.forEach((l) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "lang-opt";
    b.dataset.lang = l.code;
    b.setAttribute("role", "menuitem");
    b.innerHTML = `<span class="lang-flag">${l.short}</span><span>${l.label}</span>`;
    b.addEventListener("click", () => {
      setLang(l.code);
      closeLangMenu();
    });
    langMenu.appendChild(b);
  });
}

function openLangMenu() {
  langMenu.classList.add("open");
  langBtn.classList.add("open");
  langBtn.setAttribute("aria-expanded", "true");
}
function closeLangMenu() {
  langMenu.classList.remove("open");
  langBtn.classList.remove("open");
  langBtn.setAttribute("aria-expanded", "false");
}

if (langBtn && langMenu) {
  buildLangMenu();
  langBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    langMenu.classList.contains("open") ? closeLangMenu() : openLangMenu();
  });
  document.addEventListener("click", (e) => {
    if (!e.target.closest(".lang-wrap")) closeLangMenu();
  });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") closeLangMenu();
  });
}

function detectLang() {
  try {
    const saved = localStorage.getItem("zeno-lang");
    if (saved && I18N[saved]) return saved;
  } catch (e) {}
  const nav = (navigator.language || "en").toLowerCase();
  for (const l of LANGS) {
    if (nav === l.code || nav.startsWith(l.code + "-") || (l.code === "zh" && nav.startsWith("zh"))) return l.code;
  }
  return "pt";
}

function setLang(code) {
  if (!I18N[code]) code = "pt";
  const dict = I18N[code];
  const fallback = I18N.pt;
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const k = el.getAttribute("data-i18n");
    el.innerHTML = dict[k] !== undefined ? dict[k] : (fallback[k] !== undefined ? fallback[k] : el.innerHTML);
  });
  document.querySelectorAll("[data-i18n-alt]").forEach((el) => {
    const k = el.getAttribute("data-i18n-alt");
    el.setAttribute("alt", dict[k] !== undefined ? dict[k] : (fallback[k] !== undefined ? fallback[k] : el.getAttribute("alt")));
  });
  const meta = document.querySelector('[data-i18n-meta="meta_desc"]');
  if (meta && dict.meta_desc) meta.setAttribute("content", dict.meta_desc);
  const lang = LANGS.find((l) => l.code === code) || LANGS[0];
  document.documentElement.lang = lang.html;
  if (langCur) langCur.textContent = lang.short;
  document.querySelectorAll(".lang-opt").forEach((o) => {
    o.classList.toggle("active", o.dataset.lang === code);
  });
  try { localStorage.setItem("zeno-lang", code); } catch (e) {}
}

setLang(detectLang());
