# Rivals x DayZ

Spider-Man and Rogue from **your own Marvel Rivals install**, inside **DayZ**.

You drop onto a Chernogorsk rooftop, pick a hero, and the other one is already hunting you.

- **Spider-Man's web-shooters**
  - Web-swing between buildings: aim and press **X**. Hold sprint to climb the web, jump to let go.
  - Crawl walls: jump at any tall wall.
  - *Get Over Here!* (**T**): zip to a surface, or yank a zombie or player to you.
  - *Web-Cluster* (**B**): web a target in place.
- **Rogue's gloves**
  - *Power Absorption*: your melee hits drain health into you.
  - Area drain (**X**): damages everything around you, then pulls it in.
  - *Mimicry*: hit Spider-Man (a player or the AI one) and his powers are yours for a while. He loses them until it wears off.
- **Spawn menu (J)**: give yourself either hero's kit, spawn an AI Spider-Man or AI Rogue, or clear them.
- **Play alone or with friends**: up to 8 players on a game you host, and friends join from your Melty join link or an invite code.

All keys can be changed in DayZ's Controls menu.

## How Marvel Rivals is in it

Marvel Rivals is never started or changed, so your Rivals account is never involved. When you press Play, the launcher **reads** your Rivals install and converts what the mashup uses into a folder next to DayZ (`RivalsDayZ_Rivals`):

- hero and ability names and descriptions, in your language
- voice lines and ability sounds
- portraits and ability icons

None of Rivals' files are shipped with this mod. If something can't be read, the game still runs with built-in names, and `%LOCALAPPDATA%\RivalsDayZ\rivals-report.txt` says what was found.

## What you need

- DayZ, and Marvel Rivals installed.
- Hosting a game for friends also needs the free **DayZ Server** tool from Steam. The first time you host, the launcher asks Steam to install it, and you click Install once.

## Playing

- **Solo**: press Play. DayZ starts offline in Chernogorsk, with no server and no BattlEye.
- **Host**: choose Host in Melty. The launcher:
  1. starts your own DayZ server with the mashup;
  2. opens its port on your router automatically (UPnP or NAT-PMP);
  3. copies an invite code like `RDZ-2H09-2T02-7Y` to your clipboard;
  4. puts you in the game.
- **Join**: use the host's Melty join link, or run `RivalsDayZ.exe --join RDZ-XXXX-XXXX-XX`.

If your router won't open the port automatically, friends on your home network can still join, but friends elsewhere can't yet. Opening the port without help from the router is planned (see `design/sheets/multiplayer.json`).

## Status

This is a **test build that hasn't been run in the game yet**. It was built in a cloud container that has neither game installed. The design sheets list every cell that still needs checking in the running game (animation poses, class names, default keys and so on):

```
python3 tools/preflight.py
```

## For developers

- **Design sheets** (`design/sheets/*.json`) are the source of truth. Each row is one thing (an ability, item, AI hero, key, game hook...) and each column one of its properties. `tools/gen.py` turns every row into code: Enforce Script data, `config.cpp` classes, `inputs.xml`, and launcher constants.
- **Checks**:
  - `tools/preflight.py` lists unfilled or unverified cells and broken references between sheets.
  - `tools/escheck.py` statically checks the scripts.
  - `tools/test_pbo.py` checks the PBO signer against a known-good signature.
  - `launcher/Tests` tests invite codes, the PAA writer, Ogg encoding and Wwise bank parsing.
- **Build**: `python3 tools/build.py` produces `build/release/RivalsDayZ-<version>.zip`. It needs .NET 10 and Python 3 with `cryptography`.

| Folder | What it holds |
|---|---|
| `mod/RivalsDayZ` | DayZ mod: `scripts/3_Game`, `4_World` and `5_Mission`, plus GUI layouts and the `config.cpp` template |
| `mission/` | Offline solo mission |
| `server/` | `init.c` for hosted games |
| `launcher/` | `RivalsDayZ.exe`: Rivals reader, solo/host/join, router port opening, invite codes |
| `tools/` | Preflight, generator, script checks, PBO packer and signer, build |

See [THIRD_PARTY.md](THIRD_PARTY.md) for credits.
