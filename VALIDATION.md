# Validation record

Validated on 2026-08-02 against AsaApi tag 2.03. The target was corrected from
the Genesis Arctic area controller to the live in-game TEK Hover Skiff debuff
class identified by the server owner:

```text
Buff_SkiffBuffetedByWind_C
```

Completed checks:

- Confirmed map scope remains `Genesis_WP`.
- Confirmed the exact matcher accepts `Buff_SkiffBuffetedByWind_C` and rejects
  the misspelled `Buff_Skiff_BuffetedByWind_C` variant.
- Confirmed `Gen_AreaBuff_Arctic_C` and the `ArcticBlizzard_*` tiers are no
  longer default blocked targets.
- Added same-character cleanup of an existing exact-class instance when a new
  application is rejected.
- Kept `PlayersOnly` disabled so the hook can act on the skiff actor.
- Checked the `APrimalBuff.AddBuff(APrimalCharacter*,AActor*)` and
  `AShooterGameMode.BeginPlay()` hook keys/signatures against AsaApi 2.03.
- Passed the pure matching tests in C++20 mode using
  `-Wall -Wextra -Werror -pedantic`.
- Parsed `config.json`, `PluginInfo.json`, and `vcpkg.json` as JSON.
- Parsed the Visual Studio project/filter files as XML and the GitHub Actions
  workflow as YAML.
- Verified there are no remaining references to the obsolete status-only
  profile in the deployable project.

Environment limitation:

- This Linux workspace cannot produce or execute an MSVC Windows DLL. The
  included workflow installs the pinned MSVC 14.39 toolset and performs the
  Release/x64 build.
- A live Genesis 1 test is still required to confirm the server's runtime class
  name and that the skiff immediately recovers when the wind system retries the
  debuff.
