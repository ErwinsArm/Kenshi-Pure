// Tests for the probe MCP server against a fake plugin endpoint (no game needed):
//   node --test mcp/*.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { handle, buildRequest, TOOLS } from './kenshi-probe.mjs';

function fakePlugin() {
  return new Promise((resolve) => {
    const server = net.createServer((s) => {
      let buf = '';
      s.on('data', (d) => {
        buf += d;
        let nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
          const req = JSON.parse(buf.slice(0, nl)); buf = buf.slice(nl + 1);
          const reply = req.cmd === 'status' ? { ok: true, frames: 42, paused: false }
            : req.cmd === 'characters' ? { ok: true, characters: [{ name: 'Beep', x: 1, y: 2, z: 3 }] }
            : req.cmd === 'nearby' ? { ok: true, radius: req.radius, limit: req.limit }
            : req.cmd === 'log_tail' ? { ok: true, text: `lines=${req.lines}\n` }
            : { ok: false, error: 'unknown' };
          s.write(JSON.stringify(reply) + '\n');
        }
      });
    });
    server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
  });
}

test('tools/list names every tool', async () => {
  const r = await handle({ jsonrpc: '2.0', id: 1, method: 'tools/list' });
  assert.deepEqual(r.result.tools.map((t) => t.name), ['kenshi_status', 'kenshi_characters', 'kenshi_nearby',
    'kenshi_health', 'kenshi_inventory', 'kenshi_find_items', 'kenshi_heal', 'kenshi_teleport', 'kenshi_spawn_item',
    'kenshi_log_tail']);
});

test('buildRequest keeps declared, well-typed arguments only and refuses quotes', () => {
  const tp = TOOLS.find((t) => t.name === 'kenshi_teleport');
  assert.deepEqual(buildRequest(tp, { who: 'Gecko', dx: 1.5, x: 'no', radius: 3 }), { cmd: 'teleport', who: 'Gecko', dx: 1.5 });
  const sp = TOOLS.find((t) => t.name === 'kenshi_spawn_item');
  assert.throws(() => buildRequest(sp, { name: 'a"b' }), /quotes/);
});

test('tools/call forwards to the plugin', async () => {
  const { server, port } = await fakePlugin();
  try {
    const call = (name, args) => handle({ jsonrpc: '2.0', id: 2, method: 'tools/call', params: { name, arguments: args } }, port);
    const s = await call('kenshi_status', {});
    assert.equal(JSON.parse(s.result.content[0].text).frames, 42);
    const c = await call('kenshi_characters', {});
    assert.equal(JSON.parse(c.result.content[0].text).characters[0].name, 'Beep');
    const l = await call('kenshi_log_tail', { lines: 7 });
    assert.equal(l.result.content[0].text, 'lines=7\n');
    assert.equal(l.result.isError, false);
    const n = await call('kenshi_nearby', { radius: 500, limit: 3, lines: 9 });
    assert.deepEqual(JSON.parse(n.result.content[0].text), { ok: true, radius: 500, limit: 3 });
  } finally { server.close(); }
});

test('no game running gives a readable error, not a crash', async () => {
  const { server, port } = await fakePlugin();
  await new Promise((r) => server.close(r));   // port now closed
  const r = await handle({ jsonrpc: '2.0', id: 3, method: 'tools/call', params: { name: 'kenshi_status' } }, port);
  assert.equal(r.result.isError, true);
  assert.match(r.result.content[0].text, /cannot reach the game probe/);
});

test('notifications get no reply; unknown methods get an error', async () => {
  assert.equal(await handle({ jsonrpc: '2.0', method: 'notifications/initialized' }), null);
  const r = await handle({ jsonrpc: '2.0', id: 9, method: 'nope' });
  assert.equal(r.error.code, -32601);
});

test('stdio: the real process answers initialize', async () => {
  const p = spawn(process.execPath, [fileURLToPath(new URL('./kenshi-probe.mjs', import.meta.url))]);
  const out = await new Promise((resolve) => {
    p.stdout.once('data', (d) => resolve(d.toString()));
    p.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: '2024-11-05' } }) + '\n');
  });
  p.kill();
  assert.equal(JSON.parse(out).result.serverInfo.name, 'kenshi-probe');
});
