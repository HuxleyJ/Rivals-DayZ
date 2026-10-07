# MODLOG - Rivals x DayZ

Journal of the build (route, facts, what is verified, what is not). Newest at the bottom.

## 2026-10-07 - first build (cloud container, no games installed)

**Idea:** you're in DayZ (Chernogorsk rooftops). Spider-Man and Rogue come from the player's own Marvel Rivals install. Spider-Man web-swings, crawls walls, uses Get Over Here! and Web-Cluster. Rogue drains on melee, has an area drain, and her Mimicry steals Spider-Man's powers. A spawn-menu key gives kits and spawns AI heroes. It plays solo and with friends (hosted by a player).

**Route**
- **DayZ (host game):** a script mod loaded with `-mod`. DayZ needs no loader, and Melty installs none for it.
  - Solo runs as an offline `-mission` with `-filePatching`, with no server and no BattlEye. The pattern comes from Arkensor's DayZCommunityOfflineMode .bat.
  - Hosted games use the free DayZ Server tool (Steam app 223350), which needs a Steam login that owns DayZ, so the launcher asks Steam to install it once.
- **Marvel Rivals (companion):** read only, never launched or changed, because of the NEAC kernel anti-cheat and the 2025-26 mod bans. The launcher reads it with CUE4Parse (`GAME_MarvelRivals`) at Play.

**Facts checked in Bohemia's published scripts** (DayZ-Script-Diff, build 1.29.163709)
- `HumanCommandScript` gives full control of a player command:
  - `PrePhys_SetTranslation` and `PostPhys_SetPosition`/`SetRotation`;
  - `SetHeading` (PreAnim only);
  - `PreAnim_CallCommand` with ids bound through `HumanAnimInterface.BindCommand`.
- `StartCommand_Script` is started from `ModCommandHandlerInside`. Finished commands are handled *before* that hook, so the web command keeps its states internal instead of chaining commands.
- In Bohemia's DayZ-Samples Test_ScriptCmdSwim, `CMD_Swim` + `MovementSpeed` are bound from a script command with gravity off. This is the pose used for swinging and crawling.
- Client to server: `ScriptInputUserData` (`INPUT_UDT_*`) → `PlayerBase.OnInputUserDataProcess`. Server to both sides in lockstep: `SendSyncJuncture` → `OnSyncJuncture`. The same pattern works offline (as in the emote manager).
- Infected can run `DayZInfectedCommandScript` via `StartCommand_Script` from `ZombieBase.ModCommandHandlerBefore`.
- Heading is the negative of yaw: direction = (-sin h, 0, cos h).
- Player creation offline: `CreatePlayer(null, CreateRandomPlayer(), pos, 0, "NONE")` + `SelectPlayer(null, p)`.

**Verified here**
- Preflight: 0 blocking. All cross-sheet references resolve, and every hook's file contains its modded class and override, also checked against the vanilla scripts.
- `tools/escheck.py`: 0 problems. It catches planted errors (layering, unknown methods, duplicate locals).
- PBO signer: byte-identical to a known-good v3 `.bisign` (HEMTT fixture). Fresh-key v2/v3 sign+verify round trips pass, and a tampered PBO fails.
- Launcher: compiles for win-x64.
  - Tests pass for invite codes, PAA DXT5 (decoded back, colour error 2.6/255), Wwise `.bnk` extraction, and Ogg Vorbis (`ogginfo` reads it).
  - A Linux smoke run with an empty or corrupt Rivals folder fails soft and writes the report.

**Not verified (needs the PC with both games)** - see `python3 tools/preflight.py`
- Whether the scripts compile in DayZ: there's no Enforce compiler on Linux.
- Swing/crawl feel and the crawl tilt.
- Infected and clothing class names, default keys, `DZ_Characters_Gloves`.
- Whether loose files load through `-filePatching` (Rivals sounds and images).
- Whether `$CurrentDir:` file reads work.
- Server `BattlEye = 0`, and whether DayZ accepts the v3 signature.
- UPnP/NAT-PMP on a real router.
- Rivals: whether its files are encrypted (AES key), whether an Oodle DLL ships with the game, whether textures need `.usmap` mappings, and the paths of voice lines and icons. `rivals-report.txt` answers all of these on the first Play.

**Gotchas**
1. `Environment.GetFolderPath` returns "" for a folder that doesn't exist. Use `SpecialFolderOption.Create`.
2. In the PAA SFFO tag, data starts 12 bytes after the tag start (GGAT + name + u32 length), not 8. The first build corrupted files until a test caught it.
3. CUE4Parse ≥ 1.2.2 targets net10.0. Ubuntu 24.04 has `dotnet-sdk-10.0` in apt, and cross-publishing to win-x64 works from Linux.
4. Transitive `Microsoft.Bcl.Memory` 9.0.0 has GHSA-73j8-2gch-69rq. It's pinned to 10.0.12.
