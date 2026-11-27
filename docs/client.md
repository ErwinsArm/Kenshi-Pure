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

## Commands

| Request | Reply |
|---|---|
| `{"cmd":"status"}` | build, kenshiVersion, frames, frameThread, sameWorld, paused, frameSpeed, timeStamp, playerInterface, playerCharacters |
| `{"cmd":"characters"}` | `characters` (player-owned, max 64): name, faction, x, y, z, player, dead, unconscious, money, blood, hunger, knockoutTimer |
| `{"cmd":"nearby","radius":3000,"limit":30}` | `loaded` (all characters the game updates), `inRadius`, `characters` with the same fields plus `distance` from our first character, nearest first |
| `{"cmd":"health","who":"all"}` | per body part: flesh, stun, maxHealth, hpMult, percent; blood, maxBlood, hunger (not yet run in game) |
| `{"cmd":"inventory","who":"first"}` | items per inventory section with quantity (not yet run in game) |
| `{"cmd":"find_items","query":"bread","category":"item"}` | item templates: name and string ID (not yet run in game) |
| `{"cmd":"heal","who":"all","stun":"keep"}` | **write**: parts to max health, blood to max, bleeding 0 (not yet run in game) |
| `{"cmd":"teleport","who":"selected","dx":200}` | **write**: absolute x/y/z or relative dx/dy/dz via `CharMovement::_setPositionAndTeleport` (not yet run in game) |
| `{"cmd":"spawn_item","name":"...","quantity":1}` | **write**: `RootObjectFactory::createItem` + `Inventory::addItem` (not yet run in game) |
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
  person's (inferred). `knockoutTimer` stayed 0 while knocked out (meaning or offset unclear). The units
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
screen). When `kenshi.cfg` says `Full Screen=Yes` and `KenshiPure.cfg` has no `display_fix=0`, a worker
thread waits until the game window is in front and answers messages (3 checks, 0.5 s apart), then
minimizes it and restores it after 0.7 s, as AnarchyKenshi does after start
([findings](findings/AnarchyKenshi.dll.md), section 5). The log line `display: before` / `after` shows
window, client, monitor and display-mode sizes and DPI. Not yet run in game.

Our plugin is loaded by RE_Kenshi as a post-load plugin (RE_Kenshi log: plugin at ~7.5 s, first shader
compile at ~5.3 s), so the swap chain already exists; AnarchyKenshi's other part, changing the full-screen
request in `IDXGIFactory::CreateSwapChain`, needs RE_Kenshi's `PreloadPlugins` list (loaded at ~1.5 s;
whether that is before the window is created is not measured).
