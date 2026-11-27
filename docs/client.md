# Client plugin: live game probe

Source: `client/Probe.cpp`, `client/Probe.h`; MCP side: `mcp/kenshi-probe.mjs`. Setup: `docs/setup.md`.
Measured in game on Kenshi 1.0.65 Steam (KenshiLib 0.5.0): all commands answered with plausible
values; `paused` followed the in-game pause, the position stayed fixed while paused and changed by
tens of units per 2 s while the character walked, and a newly started game showed its new character.
`nearby` listed the Trade Ninja guards and barman of a nearby bar, Garru (faction Herbivore) and
outlaws with matching faction names; `unconscious` turned true when the user's character was knocked out.

## How it works

- The plugin starts a TCP server on **127.0.0.1 only** (default port 47800, `probe_port=N` in
  `mods\KenshiPure\KenshiPure.cfg`, 0 = off) on its own thread (Winsock, `ws2_32.lib`).
- One JSON object per line in, one JSON line out, one client at a time, lines up to 4 KB.
  Requests that are not `{"cmd":...}` are refused (a web page cannot do anything useful with it).
- `status`, `characters` and `nearby` touch the game: they go into a one-slot queue and the frame hook
  (`MainLoopHook`, game thread) answers them after the game's own frame. No game call runs on the
  server thread. If no frame comes within 3 s (menu, loading) the reply is an error.
- Game reads are wrapped in `__try` and copy only plain data (as `SampleAbi` does), so a bad pointer
  gives an error reply instead of a crash in Kenshi.
- `log_tail` is answered on the server thread from an in-memory ring of the last 500 log lines
  (no game access).

Second run (same game, character Gecko), all measured:

- `health`: `percent` = (flesh - stun) / maxHealth on all 7 parts (Chest: (12.17 - 25.24) / 100 = -0.1307);
  undamaged parts have stun 0. So `stun` is stun damage that recovers, and `heal` with `stun: zero`
  is a full heal: afterwards every part read flesh 100, stun 0, percent 1, blood 75.9 of 75.9.
- `inventory` listed equipped sections (hip, back, head, shirt, armour, legs) and the main bag; `find_items`
  "bread" gave `Bread` / `1946-gamedata.base` (309 item templates, mods included).
- `spawn_item` Bread x1 landed in the bag. With a full bag, Samurai Armour x1 and Bread x5 came back
  "refused" but lay on the ground next to the character (`addItem` drops on fail). Bread x5 produced a stack
  of 5 **and** 4 single breads (9 in all; cause unknown). Katana (category weapon) failed: the factory
  returned nothing, likely because `createItem` needs the weapon's manufacturer/material data (inferred).
- `teleport` while paused replied ok but the character stayed in place; unpaused, `dx 200` moved x by
  exactly 200 and the game put y back on the ground; absolute x/y/z also worked.
- `knockoutTimer` is non-zero (0.7, 10.8) on some knocked-out NPCs and 0 on others; `money` was 1000 for the
  player and a Vagrants thug, 0 for Holy Nation Outlaws (still not clear whether per faction).

## Commands

| Request | Reply |
|---|---|
| `{"cmd":"status"}` | build, kenshiVersion, frames, frameThread, sameWorld, paused, frameSpeed, timeStamp, playerInterface, playerCharacters |
| `{"cmd":"characters"}` | `characters` (player-owned, max 64): name, faction, x, y, z, player, dead, unconscious, money, blood, hunger, knockoutTimer |
| `{"cmd":"nearby","radius":3000,"limit":30}` | `loaded` (all characters the game updates), `inRadius`, `characters` with the same fields plus `distance` from our first character, nearest first |
| `{"cmd":"health","who":"all"}` | per body part: flesh, stun, maxHealth, hpMult, percent; blood, maxBlood, hunger |
| `{"cmd":"inventory","who":"first"}` | items per inventory section with quantity |
| `{"cmd":"find_items","query":"bread","category":"item"}` | item templates: name and string ID |
| `{"cmd":"heal","who":"all","stun":"keep"}` | **write**: parts to max health, blood to max, bleeding 0; `stun` keep, max or zero |
| `{"cmd":"teleport","who":"selected","dx":200}` | **write**: absolute x/y/z or relative dx/dy/dz via `CharMovement::_setPositionAndTeleport`; lost while the game is paused |
| `{"cmd":"spawn_item","name":"...","quantity":1}` | **write**: `RootObjectFactory::createItem` + `Inventory::addItem`; weapons fail, see below |
| `{"cmd":"log_tail","lines":50}` | `text`: last lines (max 500) |
| `{"cmd":"ping"}` | `pong` |

Sources (KenshiLib 0.5.0 headers; names and layouts inferred from them, values confirmed by the run above
unless noted):

- own characters: `GameWorld::player->playerCharacters` (member read); all loaded characters:
  `GameWorld::getCharacterUpdateList()` (Boost `unordered_set`, iterated in `__try`);
- name: virtual `getName()`; position: virtual `getPosition()`; faction: `getFaction()->getName()`;
  `isPlayerCharacter()`, `isDead()`, virtual `getMoney()`;
- `getMedical()`: `isUnconcious()`, fields `blood` (+0x70), `hunger` (+0x60), `knockoutTimer` (+0xA0).
- Open: `money` was the same (8377) for every Trade Ninja, so it is likely the faction's money, not the
  person's (inferred). `knockoutTimer` stayed 0 on the knocked-out player but was non-zero on some knocked-out NPCs (meaning unclear). The units
  of `blood` and `hunger` are not known (Gecko 46-48 after a fight, NPCs 80-130; hunger about 1.6-3).

`Probe.cpp` sets `BOOST_ALL_NO_LIB` and `BOOST_SYSTEM_NO_DEPRECATED`: `Faction.h` pulls in Boost.Thread
headers, which otherwise want Boost libraries we do not build.

## Safety

Read-only by default. `heal`, `teleport` and `spawn_item` change the single-player game and answer only
when `mods\KenshiPure\KenshiPure.cfg` holds `probe_write=1` (written by `install-client.ps1 -ProbeWrite`,
removed by a plain install or `-Uninstall`). Port bound to loopback; other programs on the same PC can
still connect (no authentication), so keep writes off when not testing.

# Client plugin: display fix

Source: `client/Display.cpp`. Kenshi can start full screen in a smaller, stretched mode until Alt+Enter
(seen by the user: first frame 1680x1050 on a 1920x1080 screen, `kenshi.cfg` asking for 1920x1080 full
screen). Cause (**M**, AnarchyKenshi's log): the game creates its swap chain with
`DXGI_MODE_SCALING_STRETCHED`, and DXGI then picks a smaller mode; AK sets it to `UNSPECIFIED` in a
`CreateSwapChain` hook ([findings](findings/AnarchyKenshi.dll.md), section 5).

Our plugin is loaded by RE_Kenshi after the swap chain exists, so for now it only repairs afterwards:
when `kenshi.cfg` says `Full Screen=Yes` and `KenshiPure.cfg` has no `display_fix=0`, a worker thread
waits until the game window is in front and answers messages (3 checks, 0.5 s apart). If the client area
differs from the desktop mode saved in Windows, it minimizes the window and restores it after 0.7 s.
Log lines `display: ...` show window, client, monitor and display-mode sizes and DPI. Not yet run in
game. The early fix needs our own loader: [plan-own-loader.md](plan-own-loader.md).