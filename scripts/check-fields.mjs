#!/usr/bin/env node
// Build check: KenshiPure's sources may touch game fields only through KP_FIELD (client/Sites.h),
// whose offset comes from client/sites.tsv per build. It fails on
//   (a) `v->m` / `v.m` where v is declared in the same file with a Kenshi class type, and
//   (b) a chained `)->m`, `).m`, `]->m`, `].m` (after KP_FIELD(...), a call or an index),
// when m is a data member of a Kenshi class in KenshiLib's headers (Include/kenshi, "// 0x.. Member").
// A line whose access is not a game field (an own struct or an Ogre type that shares the name) ends
// with `// kp-not-game: <why>`.
//
//   node scripts/check-fields.mjs [files...]      (default: client/*.cpp client/*.h)
import { readFileSync, readdirSync } from "node:fs";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const KENSHI = join(REPO, "tools", "build", "kenshilib-src", "KenshiLib-0.5.0", "Include", "kenshi");
// Members of Kenshi's container template lektor<T>: its layout is the template's, not a game class's.
const CONTAINER = new Set(["count", "maxSize", "stuff"]);

function walk(dir) {
  return readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(join(dir, e.name)) : e.name.endsWith(".h") ? [join(dir, e.name)] : []);
}

const MEMBER_RE = /(\w+)(\[[^\]]*\])?\s*;\s*\/\/\s*0x[0-9A-Fa-f]+ Member/g;
const CLASS_RE = /^\s*(?:class|struct)\s+(\w+)\b(?![^\n]*;\s*$)/gm;
const DECL_RE = /\b((?:\w+::)*(\w+))\s*(?:<[^<>]*>)?\s*[*&]*\s*(?:const\s+)?([A-Za-z_]\w*)\s*(?=[;=,)[(])/g;
const ACCESS_RE = /([A-Za-z_]\w*|\)|\])\s*(->|\.)\s*([A-Za-z_]\w*)\b(?!\s*\()/g;

export function gameMembers() {
  const names = new Set(), classes = new Set();
  for (const f of walk(KENSHI)) {
    const text = readFileSync(f, "latin1");
    for (const m of text.matchAll(MEMBER_RE)) names.add(m[1]);
    for (const m of text.matchAll(CLASS_RE)) classes.add(m[1]);
  }
  for (const c of CONTAINER) names.delete(c);
  return { names, classes };
}

const strip = (l) => l.replace(/\/\/.*$/, "").replace(/"(\\.|[^"\\])*"/g, '""').replace(/'(\\.|[^'\\])*'/g, "''");

// Scopes follow braces: a variable declared with a Kenshi class type counts until its block ends;
// declarations on a line that opens a parameter list and does not end with ";" (a function head)
// belong to the block opened next.
export function check(text, { names, classes }) {
  const lines = text.split(/\r?\n/);
  const scopes = [new Set()];
  let pending = new Set();
  const isGameVar = (v) => scopes.some((s) => s.has(v));
  const out = [];
  lines.forEach((line, i) => {
    if (/^\s*#/.test(line)) return;
    const code = strip(line);
    const decls = [];
    for (const m of code.matchAll(DECL_RE)) if (classes.has(m[2]) && m[3] !== m[2]) decls.push(m[3]);
    const head = code.includes("(") && !/;\s*$/.test(code);
    if (head) decls.forEach((d) => pending.add(d)); else decls.forEach((d) => scopes[scopes.length - 1].add(d));
    if (!/\/\/\s*kp-not-game:/.test(line)) {
      for (const m of code.matchAll(ACCESS_RE)) {
        const [, root, , member] = m;
        if (!names.has(member)) continue;
        const chained = (root === ")" || root === "]") && /KP_FIELD\s*\(/.test(code);
        if (chained || isGameVar(root) || pending.has(root)) out.push({ line: i + 1, member, text: line.trim() });
      }
    }
    for (const ch of code) {
      if (ch === "{") { scopes.push(pending); pending = new Set(); }
      else if (ch === "}" && scopes.length > 1) scopes.pop();
    }
    if (/;\s*$/.test(code)) pending = new Set();
  });
  return out;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const files = process.argv.slice(2).length ? process.argv.slice(2)
    : readdirSync(join(REPO, "client")).filter((f) => /\.(cpp|h)$/.test(f)).map((f) => join(REPO, "client", f));
  const members = gameMembers();
  let bad = 0;
  for (const f of files) {
    for (const h of check(readFileSync(f, "utf8"), members)) {
      console.log(`${f}:${h.line}: raw access to game field "${h.member}" - use KP_FIELD (or mark // kp-not-game: <why>)\n    ${h.text}`);
      bad++;
    }
  }
  if (bad) { console.log(`check-fields: ${bad} raw game-field access(es)`); process.exit(1); }
  console.log(`check-fields: ok (${files.length} files, ${members.names.size} game field names, ${members.classes.size} classes)`);
}
