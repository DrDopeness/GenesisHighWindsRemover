# Changelog

## 1.0.2 - 2026-10-01

- Support ASA v94.7 (server build 25636863), where `APrimalBuff::AddBuff` gained a
  `TFunction<void(APrimalBuff*)>` callback parameter. The plugin now tries the new
  3-argument hook first and falls back to the old 2-argument one, so one DLL runs on
  both builds. AsaApi logs one harmless "Failed to get the offset" line for the
  signature that does not exist on the running build.
- Log and `GWH.Status` (`addBuffHook=`) report which AddBuff signature is hooked.

## 1.0.1 - 2026-08-02

- Correct the default target to `Buff_SkiffBuffetedByWind_C`.
- Stop blocking `Gen_AreaBuff_Arctic_C` and all `ArcticBlizzard_*` tiers.
- Deactivate an already-attached exact-class instance when the wind system next
  attempts to reapply it to the same target.
- Remove the obsolete Arctic status-only configuration profile.
- Update documentation, package metadata, tests, and the CI artifact version.

## 1.0.0 - 2026-08-01

- Block `Gen_AreaBuff_Arctic_C` before the Genesis Arctic blizzard controller
  can attach to a character.
- Include a status-only profile that blocks only `ArcticBlizzard_High_C`.
- Scope operation to `Genesis_WP` by default.
- Remove already-active controller and residual tier buffs from online survivors
  once on load or reload.
- Add RCON status, config reload, and active-buff diagnostic commands.
- Pin the source and import library preparation flow to AsaApi 2.03.
