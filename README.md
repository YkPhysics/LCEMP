# LCEMP

LCEMP is a Minecraft Legacy Console Edition source fork focused on LAN and custom multiplayer features.

## Notes
- If you reuse this fork in other LCE-based projects, credit **notpies**.
- Please know most of this stuff is currently half baked and just POC.

## Current Features
- Fully working LAN multiplayer hosting/joining.
- Block breaking/placing sync.
- Kick system.
- Up to 8 players by default (modifiable in source).
- Keyboard and mouse support.
- Gamma/fullscreen fixes.

### Minigames / Bedwars Foundation
- Main menu **LCE Minigames** flow.
- Minigames hub flow for create/join style gameplay.
- Bedwars-oriented queue foundation:
  - queue types: solo, doubles, squads, practice
  - party-aware queueing
  - host queue-mode switching
- Server transfer payload channel (`LCE|Xfer`) for cross-server handoff.
- Bungee-like named route transfer via `proxy-worlds.properties`.

### Multiplayer Commands
- Player commands:
  - `/help`
  - `/tps`
  - `/list`
  - `/party invite|accept|leave|list`
  - `/queue <solo|doubles|squads|practice|leave>`
  - `/queuehost <hub|solo|doubles|squads|practice>`
  - `/hub`
  - `/server <list|name|reload>`
- Admin commands:
  - `/send <player> <server>`
  - `/kick`, `/ban`, `/pardon`
  - `/op`, `/deop`
  - `/tp`
  - `/gamemode`
  - `/save-on`, `/save-off`, `/save-all`
  - `/whitelist`

### Dedicated Server (Windows64)
- Dedicated mode launch support (`-dedicated`).
- `dedicated-server.properties` configuration support.
- Runtime dedicated GUI with status/logging and controls:
  - stop server
  - refresh
  - copy connect info
  - toggle saving
  - toggle whitelist
  - kick all players
- Bind address, port, max players, whitelist, world name, flat world options.

## Launch Arguments
- `-name <username>`: Set local in-game username.
- `-ip <targetip>`: Connect directly to an IP.
- `-port <targetport>`: Override target port.
- `-dedicated`: Run as dedicated server mode.
- `-bind <address>`: Bind dedicated server network address.
- `-maxplayers <count>`: Set public slot count.
- `-world <name>`: Set world/save title.
- `-servername <name>`: Set advertised server name.
- `-flat` / `-normal`: Force world type.
- `-nosave` / `-save`: Toggle save behavior.
- `-whitelist`: Start with whitelist enabled.

### Example
`Minecraft.Client.exe -dedicated -bind 0.0.0.0 -port 25565 -maxplayers 8 -world "LCE Dedicated Server" -flat -nosave -whitelist`

## Transfer Route Config
Create `proxy-worlds.properties` in the repo root (or executable-relative fallback paths):

```properties
# format: name=host:port|Display Name
hub=127.0.0.1:25565|Hub
bedwars=127.0.0.1:25566|Bedwars Match
practice=127.0.0.1:25567|Practice
```

## Required Assets
See previous README list and ensure all media/resource directories are present before building.

## Install
1. Get required assets.
2. Replace your `Minecraft.Client` and `Minecraft.World` source folders with this fork.
3. Build.
4. Run with launch arguments as needed.

## Contributing
- Open a PR for fixes/features.
- Valid changes will be reviewed and merged.

## Author
notpies
