# Genesis High Winds Remover

An open-source, server-side AsaApi plugin for ARK: Survival Ascended. Its
default performance profile disables the Genesis 1 Arctic blizzard controller
before the buff actor is created. Clients do not need a CurseForge mod.

The default blocked generated class is:

```text
Gen_AreaBuff_Arctic_C
/Game/Genesis/CoreBlueprints/Buffs/AreaBuffs/Gen_AreaBuff_Arctic.Gen_AreaBuff_Arctic_C
```

This distinction matters: `ArcticBlizzard_High_C` is only the short-lived
high-tier status presentation. The `Gen_AreaBuff_Arctic_C` controller owns the
server/client weather ticks, wind impulses, hail damage, storm freezing checks,
and requests to attach the High/Med/Low status tiers. Blocking only the High
status does not stop that work.

The included `configs/config.status-only.json` profile is available if the goal
really is to hide only `ArcticBlizzard_High_C` while retaining the entire storm
system.

## What the plugin does

- Caches the map identity before game-mode `BeginPlay` and activates only on
  `Genesis_WP` by default.
- Hooks `APrimalBuff.AddBuff(APrimalCharacter*,AActor*)` and returns `nullptr`
  only for configured generated-class names.
- Uses prebuilt `FName` values in the hot path, avoiding string allocation for
  the default exact match.
- Performs one safe, player-only sweep at load/reload to deactivate a controller
  that was active before the hook was installed, plus residual High/Med/Low
  tier buffs. It does not run a recurring scan.
- Provides RCON commands for status, reload, and live buff discovery.

The default profile disables the complete Arctic blizzard mechanic for
characters on Genesis 1, including its wind, hail, storm-specific freezing,
audio, particles, and post-processing. It does not target other buff classes;
missions or mods that depend on the Arctic controller still need testing.

## Build

The server may run under Docker/Wine on Ubuntu, but AsaApi plugins are Windows
x64 DLLs. Build this project on Windows or use the included GitHub Actions
workflow.

The workflow installs the legacy MSVC 14.39 component explicitly because
GitHub's current Windows image does not keep that optional toolset preinstalled.
The supplied vcpkg overlay triplet also forces dependencies to use that exact
compiler version instead of the runner's newer default v143 toolset.

Requirements:

- Visual Studio 2022 with **Desktop development with C++**
- v143 toolset with MSVC 14.39.33519 (pinned to match AsaApi 2.03)
- vcpkg available on `PATH`, followed once by `vcpkg integrate install`
- PowerShell 7 or Windows PowerShell 5.1

From PowerShell in this directory:

```powershell
./scripts/Prepare-AsaApi.ps1
msbuild GenesisHighWindsRemover.sln /m /p:Configuration=Release /p:Platform=x64
```

The preparation script pins both the headers and import library to
[AsaApi 2.03](https://github.com/ArkServerApi/AsaApi/tree/2.03), verifies the
release archive checksum, and does not overwrite an existing checkout at a
different tag.

The deployable folder is written to:

```text
out/GenesisHighWindsRemover/
```

It contains the DLL, hot-reload `.dll.ArkApi` copy, PDB, both configuration
profiles, and `PluginInfo.json`.

### GitHub Actions build

Push the project to a GitHub repository, open **Actions**, choose **Build
plugin**, and run the workflow. Download the
`GenesisHighWindsRemover-1.0.0` artifact when the job finishes.

## Install

Copy the deployable folder into the Genesis server's mounted game directory so
the final layout is:

```text
ShooterGame/Binaries/Win64/ArkApi/Plugins/GenesisHighWindsRemover/
  GenesisHighWindsRemover.dll
  PluginInfo.json
  config.json
  config.status-only.json
```

Restart the Genesis 1 server. A full restart is recommended for the first test
so every client receives a clean weather state. For a configured AsaApi
hot-reload workflow, copy `GenesisHighWindsRemover.dll.ArkApi` into the same
directory instead.

Check the ArkApi process log under `ShooterGame/Binaries/Win64/logs/` or the
server console for:

```text
[GenesisHighWindsRemover] Map 'Genesis_WP' is enabled for this plugin
[GenesisHighWindsRemover] Blocked Genesis high-winds buff: class='Gen_AreaBuff_Arctic_C' ...
```

## RCON commands

| Command | Purpose |
| --- | --- |
| `GWH.Status` | Show map scope, block/deactivation counters, target counts, and AsaApi version. |
| `GWH.Reload` | Validate and reload `config.json`, then sweep online survivors once. |
| `GWH.DumpBuffs` | Log every unique active player buff class/path to the ArkApi log/server console. |

## Configuration

The supplied performance defaults are conservative exact matches:

```json
{
  "Enabled": true,
  "AllowedMaps": ["Genesis_WP"],
  "PlayersOnly": false,
  "ExactBlockedClassNamesOrPaths": [
    "Gen_AreaBuff_Arctic_C",
    "/Game/Genesis/CoreBlueprints/Buffs/AreaBuffs/Gen_AreaBuff_Arctic.Gen_AreaBuff_Arctic_C"
  ],
  "ExactCleanupClassNamesOrPaths": [
    "Gen_AreaBuff_Arctic_C",
    "ArcticBlizzard_High_C",
    "ArcticBlizzard_Med_C",
    "ArcticBlizzard_Low_C"
  ],
  "NormalizedBlockedClassNameContains": [],
  "SweepExistingPlayerBuffs": true,
  "LogFirstBlockedClass": true,
  "LogUnmatchedDiscoveryCandidates": false,
  "DiscoveryNormalizedNameContains": ["wind", "blizzard"]
}
```

`ExactBlockedClassNamesOrPaths` controls future additions. The cleanup list is
used only during the one-time online-player sweep, so residual tier statuses
disappear after a hot load or config reload. Full paths are reduced to their
generated-class leaf once, keeping exact matching cheap.

`NormalizedBlockedClassNameContains` is available for diagnosis after a game
update, but broad matchers such as `wind` are not enabled by default because
they could catch unrelated buffs. The TEK Hover Skiff's
`Buff_SkiffBuffetedByWind_C` indicator is deliberately untouched.

Set `PlayersOnly` to `true` to reject a target only when it is being attached
to a survivor. The default is map-wide for the exact class.

### Status-only profile

To remove only the High status icon/effect, copy
`configs/config.status-only.json` over the deployed `config.json`, then restart
or run `GWH.Reload`. That profile leaves wind impulses, hail, freezing logic,
VFX, audio, and the controller's recurring tick active, so it should not be
expected to materially improve server performance.

## Verification and rollback

1. Start Genesis 1 and run `GWH.Status`; `mapActive` should be `true`.
2. Enter the Arctic during a storm and confirm the controller no longer
   attaches; `blockedSinceLoad` should increase.
3. Confirm there are no wind impulses, hail damage, or Arctic storm overlays.
4. Compare server frame time in the same location and player-count conditions.
5. Test missions or mods that intentionally depend on Arctic storm mechanics.

To roll back, stop the server, move the
`ArkApi/Plugins/GenesisHighWindsRemover` folder out of the plugins directory,
and start the server again.

## Source compatibility

This project is pinned to AsaApi 2.03 and its matching `AsaApi.lib`. The hook
and field signatures were checked against the 2.03
[Buff API](https://github.com/ArkServerApi/AsaApi/blob/2.03/AsaApi/Core/Public/API/ARK/Buff.h),
[Actor API](https://github.com/ArkServerApi/AsaApi/blob/2.03/AsaApi/Core/Public/API/ARK/Actor.h),
and [GameMode API](https://github.com/ArkServerApi/AsaApi/blob/2.03/AsaApi/Core/Public/API/ARK/GameMode.h).

The pure matching helpers have Linux-compatible tests:

```bash
g++ -std=c++20 -Wall -Wextra -Werror \
  src/Matching.cpp tests/MatchingTests.cpp -o matching-tests
./matching-tests
```
