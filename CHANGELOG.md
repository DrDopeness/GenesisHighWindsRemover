# Changelog

## 1.0.0 - 2026-08-01

- Block `Gen_AreaBuff_Arctic_C` before the Genesis Arctic blizzard controller
  can attach to a character.
- Include a status-only profile that blocks only `ArcticBlizzard_High_C`.
- Scope operation to `Genesis_WP` by default.
- Remove already-active controller and residual tier buffs from online survivors
  once on load or reload.
- Add RCON status, config reload, and active-buff diagnostic commands.
- Pin the source and import library preparation flow to AsaApi 2.03.
