// Stdio MCP server for the KenshiPure live game probe. Forwards three read-only tools to the
// plugin's TCP port on 127.0.0.1 (default 47800, override with KENSHI_PROBE_PORT). No dependencies.
// Protocol: newline-delimited JSON-RPC 2.0 on stdin/stdout (MCP stdio transport).
import net from 'node:net';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

const HOST = '127.0.0.1';
const PORT = Number(process.env.KENSHI_PROBE_PORT || 47800);
const TIMEOUT_MS = 6000;

// One request line to the plugin, one reply line back.
export function probeRequest(obj, port = PORT) {
  return new Promise((resolve, reject) => {
    const sock = net.connect({ host: HOST, port });
    let buf = '';
    let done = false;
    const finish = (fn, v) => { if (done) return; done = true; sock.destroy(); fn(v); };
    sock.setTimeout(TIMEOUT_MS, () => finish(reject, new Error('the game did not answer in time')));
    sock.on('error', (e) => finish(reject, new Error(e.code === 'ECONNREFUSED'
      ? `cannot reach the game probe on ${HOST}:${port} (is Kenshi running with KenshiPure?)`
      : e.message)));
    sock.on('connect', () => sock.write(JSON.stringify(obj) + '\n'));
    sock.on('data', (d) => {
      buf += d.toString('utf8');
      const nl = buf.indexOf('\n');
      if (nl >= 0) {
        try { finish(resolve, JSON.parse(buf.slice(0, nl))); }
        catch { finish(reject, new Error('unreadable reply from the probe')); }
      }
    });
    sock.on('close', () => finish(reject, new Error('probe closed the connection without an answer')));
  });
}

const WHO = { type: 'string', description: 'own characters: "all", "selected", "first" or an exact name' };
const obj = (properties) => ({ type: 'object', properties });

export const TOOLS = [
  { name: 'kenshi_status', cmd: 'status',
    description: 'Read-only. State of the running Kenshi game with the KenshiPure plugin: build, frames, paused, frameSpeed, timeStamp, inGameHours, camera position, selected character, number of player characters, whether write commands are enabled.',
    inputSchema: obj({}) },
  { name: 'kenshi_characters', cmd: 'characters',
    description: 'Read-only. The player-owned characters: name, faction, world position (x, y, z), selected, dead, unconscious, money, blood, maxBlood, hunger, knockoutTimer. Needs a loaded game.',
    inputSchema: obj({}) },
  { name: 'kenshi_nearby', cmd: 'nearby',
    description: 'Read-only. All loaded characters (any faction, animals too) within `radius` world units of the first own character, nearest first, with the same fields as kenshi_characters plus distance.',
    inputSchema: obj({ radius: { type: 'integer', minimum: 1, description: 'default 3000' }, limit: { type: 'integer', minimum: 1, maximum: 200, description: 'default 30' } }) },
  { name: 'kenshi_health', cmd: 'health',
    description: 'Read-only. Per body part health of own characters (flesh, stun, maxHealth, hpMult, percent) plus blood and hunger.',
    inputSchema: obj({ who: { ...WHO, description: WHO.description + ' (default all)' } }) },
  { name: 'kenshi_inventory', cmd: 'inventory',
    description: 'Read-only. Items in an own character\'s inventory sections (section, item, quantity).',
    inputSchema: obj({ who: { ...WHO, description: WHO.description + ' (default first)' } }) },
  { name: 'kenshi_find_items', cmd: 'find_items',
    description: 'Read-only. Search the game data for item templates by name (case-insensitive substring); returns name and string ID for kenshi_spawn_item.',
    inputSchema: obj({ query: { type: 'string' }, category: { type: 'string', description: 'item (default), weapon, armour, or an itemType number' }, limit: { type: 'integer', minimum: 1, maximum: 300 } }) },
  { name: 'kenshi_heal', cmd: 'heal',
    description: 'CHANGES THE GAME (needs probe_write=1). Sets every body part of own characters to its max health and blood to max, stops bleeding. Only when the user asks.',
    inputSchema: obj({ who: { ...WHO, description: WHO.description + ' (default all)' }, stun: { type: 'string', enum: ['zero', 'keep'], description: 'stun damage: zero = full heal (default), keep = leave it' } }) },
  { name: 'kenshi_teleport', cmd: 'teleport',
    description: 'CHANGES THE GAME (needs probe_write=1). Moves own characters to x/y/z (absolute) or by dx/dy/dz (relative). Only when the user asks; read the position again afterwards.',
    inputSchema: obj({ who: { ...WHO, description: WHO.description + ' (default selected)' }, x: { type: 'number' }, y: { type: 'number' }, z: { type: 'number' }, dx: { type: 'number' }, dy: { type: 'number' }, dz: { type: 'number' } }) },
  { name: 'kenshi_spawn_item', cmd: 'spawn_item',
    description: 'CHANGES THE GAME (needs probe_write=1). Creates an item (exact name or string ID from kenshi_find_items) in an own character\'s inventory. Only when the user asks.',
    inputSchema: obj({ name: { type: 'string' }, category: { type: 'string', description: 'item (default), weapon, armour' }, quantity: { type: 'integer', minimum: 1, maximum: 100 }, who: { ...WHO, description: WHO.description + ' (default selected)' } }) },
  { name: 'kenshi_log_tail', cmd: 'log_tail',
    description: 'Read-only. The last lines of the KenshiPure plugin log (default 50, max 500).',
    inputSchema: obj({ lines: { type: 'integer', minimum: 1, maximum: 500 } }) },
];

// Copies the arguments a tool declares; strings must not contain quotes or backslashes (the plugin's
// request parser is deliberately simple).
export function buildRequest(tool, args) {
  const req = { cmd: tool.cmd };
  for (const [k, spec] of Object.entries(tool.inputSchema.properties)) {
    const v = args?.[k];
    if (v === undefined) continue;
    if (spec.type === 'string' && typeof v === 'string') {
      if (/["\\\n]/.test(v)) throw new Error(`argument ${k} must not contain quotes, backslashes or newlines`);
      req[k] = v;
    } else if (spec.type === 'integer' && Number.isInteger(v)) req[k] = v;
    else if (spec.type === 'number' && Number.isFinite(v)) req[k] = v;
  }
  return req;
}

export async function callTool(name, args, port = PORT) {
  const tool = TOOLS.find((t) => t.name === name);
  if (!tool) return { isError: true, content: [{ type: 'text', text: `unknown tool ${name}` }] };
  let req;
  try { req = buildRequest(tool, args); } catch (e) { return { isError: true, content: [{ type: 'text', text: e.message }] }; }
  try {
    const reply = await probeRequest(req, port);
    const text = tool.cmd === 'log_tail' && reply.ok ? reply.text : JSON.stringify(reply, null, 2);
    return { isError: reply.ok === false, content: [{ type: 'text', text }] };
  } catch (e) {
    return { isError: true, content: [{ type: 'text', text: e.message }] };
  }
}

export async function handle(msg, port = PORT) {
  const { id, method, params } = msg;
  if (id === undefined) return null;   // notification: no reply
  const ok = (result) => ({ jsonrpc: '2.0', id, result });
  switch (method) {
    case 'initialize':
      return ok({ protocolVersion: params?.protocolVersion || '2024-11-05', capabilities: { tools: {} },
                  serverInfo: { name: 'kenshi-probe', version: '0.1.0' } });
    case 'ping': return ok({});
    case 'tools/list':
      return ok({ tools: TOOLS.map(({ name, description, inputSchema }) => ({ name, description, inputSchema })) });
    case 'tools/call': return ok(await callTool(params?.name, params?.arguments, port));
    default: return { jsonrpc: '2.0', id, error: { code: -32601, message: `method not found: ${method}` } };
  }
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const rl = readline.createInterface({ input: process.stdin });
  rl.on('line', async (line) => {
    if (!line.trim()) return;
    let msg;
    try { msg = JSON.parse(line); } catch { return; }
    const reply = await handle(msg);
    if (reply) process.stdout.write(JSON.stringify(reply) + '\n');
  });
}
