# Naming Notes (MimiClaw -> AtomClaw)

This repository was forked from a MimiClaw codebase and evolved into AtomClaw.

Some internal symbols still use `MIMI_*` / `mimi_*` prefixes. These are mostly
legacy names in shared modules and do not indicate that MimiClaw firmware is
being built.

Current status:

- AtomClaw firmware entry: `main/atom_main.c`
- AtomClaw config/secrets: `main/atom_config.h`, `main/atom_secrets.h.template`
- AtomClaw build env: `platformio.ini` -> `env:atomclaw`
- Variant selection: `CONFIG_DEVICE_ATOMCLAW=y`

Why some `MIMI_*` names remain:

- They are used by shared components compiled for both variants.
- Renaming all of them at once is high-risk and can cause regressions.
- Functional behavior is controlled by build variant flags, not identifier text.

Recommended incremental cleanup:

1. Introduce neutral aliases (for example `APP_NVS_*`) in shared modules.
2. Migrate call sites module-by-module with tests.
3. Remove legacy `MIMI_*` names only after both variants pass CI.
