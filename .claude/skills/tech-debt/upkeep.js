// Measurements for the tech-debt review: what the project keeps paying for, and the map scanners split and
// check coverage by. Read-only. Adapted from the user's Roblox project.
//   node .claude/skills/tech-debt/upkeep.js [--days 90] [--top 20]
// Prints:
//   - the area map: every second-level folder and every root file (tracked, plus untracked files git does not
//     ignore), with file count, text lines and size; the scan is split by it and every row must be reported read
//   - code and notes changed most often, and files changed together across folders again and again (kept in
//     step by hand)
//   - the biggest notes and the biggest code files of our own
// The original mod folder (evidence, never edited) and REA's own skill are mapped but left out of change
// counts and size lists.
// WORK.md changes by design and is left out of the change counts. Commits touching more than 15 counted files
// are sweeps and count for "changed" only.
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const arg = (name, fallback) => {
  const i = process.argv.indexOf(name);
  return i > 0 ? Number(process.argv[i + 1]) : fallback;
};
const DAYS = arg('--days', 90);
const TOP = arg('--top', 20);
const ROOT = execFileSync('git', ['rev-parse', '--show-toplevel'], { encoding: 'utf8' }).trim();
const git = (...args) =>
  execFileSync('git', ['-c', 'core.quotepath=off', ...args], { cwd: ROOT, encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
const OUTSIDE = /(^|\/)(\.claude\/worktrees|node_modules|\.venv|\.cache|tools)(\/|$)/;
const THIRD_PARTY = /^KenshiMultiplayer[^/]*\/|^\.claude\/skills\/reverse-engineer-anything\/|(^|\/)(build|downloads|third_party|vendor)(\/|$)/;
const BY_DESIGN = /(^|\/)WORK\.md$/;
const CODE = /\.(c|cc|cpp|h|hpp|rs|py|js|mjs|ts|java|ps1|sh)$/;
const TEXT = /\.(c|cc|cpp|h|hpp|rs|py|js|mjs|ts|java|md|json|toml|txt|yml|yaml|ps1|cmd|bat|sh|cfg|ini|html|css)$/;
const SWEEP = 15;
const exists = (f) => fs.existsSync(path.join(ROOT, f));
const kb = (bytes) => `${Math.round(bytes / 1024)} KB`;

// Area map.
const files = [...git('ls-files').split('\n'), ...git('ls-files', '--others', '--exclude-standard').split('\n')].filter(
  (f) => f && !OUTSIDE.test(f) && exists(f),
);
const areas = new Map();
for (const f of [...new Set(files)]) {
  const parts = f.split('/');
  const key = parts.length === 1 ? f : parts.length === 2 ? `${parts[0]}/ (files)` : `${parts[0]}/${parts[1]}`;
  const row = areas.get(key) || { files: 0, lines: 0, bytes: 0 };
  const full = path.join(ROOT, f);
  row.files++;
  row.bytes += fs.statSync(full).size;
  if (TEXT.test(f)) row.lines += fs.readFileSync(full, 'utf8').split('\n').length;
  areas.set(key, row);
}

// Change history.
const changed = new Map();
const together = new Map();
const add = (map, key) => map.set(key, (map.get(key) || 0) + 1);
let commits = 0;
for (const block of git('log', `--since=${DAYS}.days`, '--name-only', '--format=@@%h').split('@@').slice(1)) {
  const touched = block.split('\n').slice(1).filter((f) => f && !OUTSIDE.test(f) && !THIRD_PARTY.test(f) && !BY_DESIGN.test(f) && exists(f));
  commits++;
  for (const f of touched) add(changed, f);
  if (touched.length > SWEEP) continue;
  for (let i = 0; i < touched.length; i++)
    for (let j = i + 1; j < touched.length; j++)
      if (path.dirname(touched[i]) !== path.dirname(touched[j])) add(together, [touched[i], touched[j]].sort().join('  +  '));
}

const bigNotes = new Map();
const bigCode = new Map();
for (const f of new Set(files)) {
  if (THIRD_PARTY.test(f)) continue;
  const full = path.join(ROOT, f);
  if (f.endsWith('.md')) bigNotes.set(f, fs.statSync(full).size);
  else if (CODE.test(f)) bigCode.set(f, fs.readFileSync(full, 'utf8').split('\n').length);
}

const top = (map, min = 1) => [...map].filter(([, n]) => n >= min).sort((a, b) => b[1] - a[1]).slice(0, TOP);
const show = (title, rows) => {
  console.log(`\n${title}`);
  for (const row of rows) console.log(`  ${row}`);
  if (rows.length === 0) console.log('  (none)');
};

show(
  'Area map (files, text lines, size):',
  [...areas]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([k, r]) => `${String(r.files).padStart(5)} ${String(r.lines).padStart(7)} ${kb(r.bytes).padStart(10)}  ${k}`),
);
console.log(`\nLast ${DAYS} days: ${commits} commits.`);
show('Code and notes changed most often (times):', top(changed).map(([f, n]) => `${n}  ${f}`));
show('Changed together across folders (times):', top(together, 3).map(([p, n]) => `${n}  ${p}`));
show('Biggest notes (.md):', top(bigNotes).map(([f, n]) => `${kb(n)}  ${f}`));
show('Biggest code files (lines):', top(bigCode).map(([f, n]) => `${n}  ${f}`));
