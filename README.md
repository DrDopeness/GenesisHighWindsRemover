# Genesis High Winds Remover

An open-source, server-side AsaApi plugin for ARK: Survival Ascended. It blocks
the TEK Hover Skiff's **Buffeted by Wind** debuff on Genesis 1 without disabling
the Arctic storm controller or unrelated weather effects. Clients do not need a
CurseForge mod.

The exact blocked generated class is:

```text
Buff_SkiffBuffetedByWind_C
```

The spelling matters: there is no underscore between `Skiff` and `Buffeted`.
The earlier `Gen_AreaBuff_Arctic_C` target was the Genesis Arctic area
controller and is intentionally no longer blocked.

## What the plugin does

- Activates only on `Genesis_WP` by default.
- Hooks `APrimalBuff.AddBuff(APrimalCharacter*,AActor*)`.
- Returns `nullptr` only when the incoming generated class is exactly
  `Buff_SkiffBuffetedByWind_C`.
- If the target already has that debuff, safely deactivates the existing
  instance when the wind system next tries to apply it.
- Leaves `Gen_AreaBuff_Arctic_C`, the Arctic blizzard tiers, hail, freezing,
  particles, audio, and other Genesis weather behavior untouched.
- Uses prebuilt `FName` values in the hot path to avoid string allocation for
  the default exact match.
- Provides RCON commands for status, reload, and player-buff discovery.

`PlayersOnly` must remain `false` for the default behavior because the target is
the skiff rather than the survivor piloting it.

## Build

The server may run under Docker/Wine on Ubuntu, but AsaApi plugins are Windows
x64 DLLs. Build this project on Windows or use the included GitHub Actions
workflow.

The workflow installs MSVC 14.39 explicitly because the project is pinned to
AsaApi 2.03 and its matching import library. The supplied vcpkg overlay triplet
forces dependencies to use the same compiler minor version and CRT linkage.

Requirements:

- Visual Studio 2022 with **Desktop development with C++**
- v143 toolset with MSVC 14.39.33519
- vcpkg available on `PATH`, followed once by `vcpkg integrate install`
- PowerShell 7 or Windows PowerShell 5.1

From PowerShell in this directory:

```powershell
./scripts/Prepare-AsaApi.ps1
msbuild GenesisHighWindsRemover.sln /m /p:Configuration=Release /p:Platform=x64
```

The preparation script pins the headers and import library to
[AsaApi 2.03](https://github.com/ArkServerApi/AsaApi/tree/2.03), verifies the
release archive checksum, and does not overwrite an existing checkout at a
different tag.

The deployable folder is written to:

```text
out/GenesisHighWindsRemover/
```

It contains the DLL, hot-reload `.dll.ArkApi` copy, PDB, `config.json`, and
`PluginInfo.json`.

### GitHub Actions build

Open **Actions**, choose **Build plugin**, and run the workflow. Download the
`GenesisHighWindsRemover-1.0.1` artifact when the job finishes.

## Install

Copy the deployable folder into the Genesis server's mounted game directory so
the final layout is:

```text
ShooterGame/Binaries/Win64/ArkApi/Plugins/GenesisHighWindsRemover/
  GenesisHighWindsRemover.dll
  PluginInfo.json
  config.json
```

Restart the Genesis 1 server. For a configured AsaApi hot-reload workflow, copy
`GenesisHighWindsRemover.dll.ArkApi` into the same directory instead.

Check the ArkApi process log under `ShooterGame/Binaries/Win64/logs/` or the
server console for:

```text
[GenesisHighWindsRemover] Map 'Genesis_WP' is enabled for this plugin
[GenesisHighWindsRemover] Blocked TEK Hover Skiff wind debuff: class='Buff_SkiffBuffetedByWind_C' ...
```

## RCON commands

| Command | Purpose |
| --- | --- |
| `GWH.Status` | Show map scope, block/deactivation counters, target counts, and AsaApi version. |
| `GWH.Reload` | Validate and reload `config.json`. |
| `GWH.DumpBuffs` | Log unique active player buff classes and paths to the ArkApi log/server console. |

## Configuration

The supplied default uses a conservative exact match:

```json
{
  "Enabled": true,
  "AllowedMaps": ["Genesis_WP"],
  "PlayersOnly": false,
  "ExactBlockedClassNamesOrPaths": [
    "Buff_SkiffBuffetedByWind_C"
  ],
  "ExactCleanupClassNamesOrPaths": [
    "Buff_SkiffBuffetedByWind_C"
  ],
  "NormalizedBlockedClassNameContains": [],
  "SweepExistingPlayerBuffs": true,
  "LogFirstBlockedClass": true,
  "LogUnmatchedDiscoveryCandidates": false,
  "DiscoveryNormalizedNameContains": [
    "skiffbuffetedbywind",
    "buffetedbywind",
    "wind"
  ]
}
```

`ExactBlockedClassNamesOrPaths` controls future applications.
`ExactCleanupClassNamesOrPaths` controls the same-target cleanup performed when
an application is rejected. Full paths can also be supplied and are reduced to
their generated-class leaf for the fast exact match.

`SweepExistingPlayerBuffs` retains the load/reload sweep for any copy attached
to an online survivor. A copy attached to a skiff is cleared when the wind
system next attempts to reapply the blocked class.

`NormalizedBlockedClassNameContains` is available for diagnosis after a game
update, but broad matchers such as `wind` are not enabled by default because
they could catch unrelated buffs.

## Verification and rollback

1. Start Genesis 1 and run `GWH.Status`; `mapActive` should be `true`.
2. Fly a TEK Hover Skiff into the location that previously produced the
   Buffeted by Wind debuff.
3. Confirm `blockedSinceLoad` increases and the skiff does not retain
   `Buff_SkiffBuffetedByWind_C`.
4. Run `admincheat ListMyBuffs` while piloting, or dismount and use
   `admincheat ListMyTargetBuffs` while aiming at the skiff.
5. Confirm the Genesis Arctic storm and unrelated weather effects still work.

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
g++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  src/Matching.cpp tests/MatchingTests.cpp -o matching-tests
./matching-tests
```
