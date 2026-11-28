# Playing together: host and friend (M6)

Plain steps for two people: the **host** runs the server on their PC, the **friend** joins over a private
network. Technical background: [client.md](client.md) "Multiplayer package (M6)" and "Game speed (M6)".

## What you get

Each of you sees the other's character, named `[KP] <name>`, walking where the other walks. Everything else
stays each player's own world: NPCs, fights, items, buildings, time of day. From the moment you connect until
you close Kenshi the game always runs at normal speed (x1): the pause key, the faster-speed buttons and keys, talking
to NPCs and the build menu do not pause or speed up. Only loading and a new game's character editor still pause.

Needed: Kenshi on Steam (1.0.68, or 1.0.65 through RE_Kenshi). GOG is not supported yet.

## Once: the private network (Tailscale)

The friend needs to reach the host's PC. Tailscale is the simplest safe way (free for this):

1. Both: install Tailscale from tailscale.com and sign in (any account, e.g. Google).
2. Host: open the Tailscale admin page (login.tailscale.com), **Machines**, the `...` next to your PC, **Share**,
   and send the link to your friend. Friend: open the link and accept.
3. Host: the Tailscale app shows your PC's address, starting with `100.` Send it to your friend.

A forwarded router port (UDP 27850) also works, but then anyone on the internet who finds it can join: the
server has no password yet.

## Once: the package

The host (or Claude for them) builds it: `scripts\make-package.ps1` writes `build\package\KenshiPure-<version>.zip`.
The host sends the zip to the friend (e-mail, chat, USB stick).

## Installing (both)

1. Close Kenshi.
2. Unzip the whole folder, e.g. to the Desktop.
3. Double-click **Install KenshiPure.cmd**. If Windows asks, choose "Run" (or "More info", then "Run anyway").
   - Kenshi folder: it finds Steam's Kenshi by itself; press Enter.
   - Server address: the friend types the host's `100.` address; the host types `127.0.0.1`.
   - Player name: what the other player sees (plain letters, no umlauts).
   - AnarchyKenshi, if installed, is switched off while KenshiPure is installed.
4. Removing later: **Uninstall KenshiPure.cmd** (AnarchyKenshi comes back on if the install switched it off).

## Each time you play

1. Host: double-click `server\Start server.cmd` in the unzipped folder and leave the window open. The first time
   Windows may ask about the firewall: tick **both** boxes (private and public networks) and allow.
2. Both: Tailscale on, then start Kenshi through Steam. The very first start can take a few minutes.
3. Both: start a new game with the **same start** (e.g. The Wanderer) and meet in the town it puts you in. The other
   player only shows up while you are near each other.
4. The host's server window shows `player ... joined: <name>` when someone connects.

## After playing

Tell Claude what you saw. Claude reads the logs: `<Kenshi folder>\mods\KenshiPure\KenshiPure.log` on each PC
(the friend sends theirs, it has names and positions, no passwords) and the host's `server\kp-server.log`.
