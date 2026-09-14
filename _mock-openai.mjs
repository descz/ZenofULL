// Mock de provider OpenAI-compatível para testar o agente ZenoC ponta a ponta.
// GET  /v1/models
// POST /v1/chat/completions  (turnos: run_command → memory_remember → resposta final)
import http from 'node:http';

const port = Number(process.argv[2] || 8123);

const readBody = (req) => new Promise((resolve) => {
  let body = '';
  req.on('data', (chunk) => { body += chunk; });
  req.on('end', () => resolve(body));
});

const completion = (message, finish = 'stop') => JSON.stringify({
  id: 'chatcmpl-mock',
  object: 'chat.completion',
  created: Math.floor(Date.now() / 1000),
  model: 'mock-gpt',
  choices: [{ index: 0, message, finish_reason: finish }],
  usage: { prompt_tokens: 42, completion_tokens: 17, total_tokens: 59 },
});

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${port}`);
  if (req.method === 'GET' && url.pathname.endsWith('/models')) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ object: 'list', data: [{ id: 'mock-gpt', object: 'model' }, { id: 'mock-reasoner', object: 'model' }] }));
    return;
  }
  if (req.method === 'POST' && url.pathname.endsWith('/chat/completions')) {
    const raw = await readBody(req);
    let payload = {};
    try { payload = JSON.parse(raw); } catch {}
    const messages = Array.isArray(payload.messages) ? payload.messages : [];
    const toolResults = messages.filter((m) => m.role === 'tool').length;
    console.log(`[mock] chat.completions tools=${toolResults} messages=${messages.length}`);
    res.writeHead(200, { 'Content-Type': 'application/json' });
    if (toolResults === 0) {
      res.end(completion({
        role: 'assistant',
        content: null,
        tool_calls: [{
          id: 'call_shell_1',
          type: 'function',
          function: { name: 'run_command', arguments: JSON.stringify({ command: 'echo zeno-tool-ok' }) },
        }],
      }, 'tool_calls'));
      return;
    }
    if (toolResults === 1) {
      res.end(completion({
        role: 'assistant',
        content: null,
        tool_calls: [{
          id: 'call_mem_1',
          type: 'function',
          function: {
            name: 'memory_remember',
            arguments: JSON.stringify({ title: 'Memória do mock', content: 'Nota criada durante o teste de integração do ZenoC.', kind: 'memory', scope: 'global', tags_json: '["teste","integracao"]' }),
          },
        }],
      }, 'tool_calls'));
      return;
    }
    res.end(completion({
      role: 'assistant',
      content: 'Tudo pronto! Executei o comando no shell e registrei a memória no grafo do Zeno.',
    }));
    return;
  }
  res.writeHead(404, { 'Content-Type': 'application/json' });
  res.end('{"error":"not found"}');
});

server.listen(port, '127.0.0.1', () => console.log(`[mock] OpenAI-compatible em http://127.0.0.1:${port}/v1`));
