# Validation record

Validated on 2026-08-01 against AsaApi tag 2.03 and the current Genesis 1 ASA
dedicated-server asset manifest.

Completed checks:

- Confirmed map ID `Genesis_WP`.
- Confirmed controller class `Gen_AreaBuff_Arctic_C` and status-tier classes
  `ArcticBlizzard_High_C`, `ArcticBlizzard_Med_C`, and
  `ArcticBlizzard_Low_C`.
- Checked the `APrimalBuff.AddBuff(APrimalCharacter*,AActor*)` and
  `AShooterGameMode.BeginPlay()` hook keys/signatures against AsaApi 2.03.
- Checked the map, player-controller, character-buff, deactivation, RCON, and
  plugin-lifecycle APIs against the same headers.
- Passed the pure matching tests with GCC 12 in C++20 mode using
  `-Wall -Wextra -Werror -pedantic`.
- Parsed both configs, `PluginInfo.json`, and `vcpkg.json` as JSON.
- Parsed the Visual Studio project/filter files as XML and the GitHub Actions
  workflow as YAML.
- Verified the AsaApi release-archive SHA-256 embedded in the dependency script.
- Mirrored AsaApi 2.03's MSVC 14.39 vcpkg overlay triplet so `fmt`, the plugin,
  and `AsaApi.lib` use the same compiler minor version and CRT linkage.

Environment limitation:

- This Linux workspace cannot produce or execute an MSVC Windows DLL. The
  included workflow installs the pinned MSVC 14.39 toolset and performs the
  actual Release/x64 build. A live Genesis 1 dedicated-server test remains
  required before cluster-wide rollout.
