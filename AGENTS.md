# AGENTS.md

Working notes for AI agents (and humans) modifying this repo. This is a
starting point — extend it as patterns and decisions accumulate.

## What this repo is

ZMK *user config* for the Ergohaven **OP36** split keyboard. No firmware
source lives here: `config/west.yml` pulls `ergohaven/ergohaven-zmk` (boards +
firmware). All changes are Kconfig (`*.conf`) and keymap (`*.keymap`,
devicetree source with C preprocessing) edits plus `build.yaml`. See
README.md for the full picture.

## Scope

- **OP36 + K:04 Mini.** Other keyboards' files (velvet, k03, imperial44,
  trackball) were deliberately deleted. Do not resurrect them or add new
  keyboards without being asked.
- The main workhorse is **`config/op36_ruen.keymap`** (EN/RU dual-language
  keymap with a mouse layer). `config/op36.keymap` is the stock QWERTY
  fallback keymap; treat it as reference, not a target.
- **K:04 Mini** support (`config/k04_mini.*`, `boards/shields/k04_mini/`,
  `zephyr/module.yml`, `CROSS_OS.md`) was imported verbatim from
  `fustopoke/ergohaven-zmk-config` at its commit `1afd537`. It is the friend's
  design: one keymap for Linux/Windows/macOS, language switching via a single
  "next input source" shortcut (`&os_lang_tog`), NOT the OP36
  `Meta+Ctrl+1/2` scheme below — golden rules 1–2 are OP36-specific. When
  touching k04 files, re-read `CROSS_OS.md` first; upstream `ergohaven-zmk`
  has no k04_mini shield, so `boards/` here is the only source.
- **K:04 hardware invariants** (see the diagnosis section in `CROSS_OS.md`):
  the onboard WS2812 is on **SPI3**, and that is the only instance the
  board leaves free — SPI0/SPI1 share their physical peripherals with the
  console UART and with TWIM1 (which the right half's touchpad owns;
  Zephyr build-asserts on the collision), and SPI2 does not drive the
  strip at all on this hardware (every SPI2 build came out with a dark
  LED). nRF52840 anomaly 198 (SPIM3 EasyDMA TX corrupted by concurrent CPU
  RAM access, i.e. by the BLE stack) is therefore mitigated **in the
  widget**, not in the devicetree: `zmk-smart-rgbled-widget` writes one
  frame per colour change straight through the Zephyr LED-strip API and
  ZMK's 20 Hz underglow refresh (`CONFIG_ZMK_RGB_UNDERGLOW_ON_START=n`,
  `zmk_rgb_underglow_off()` at init) is never used. Do not restore the
  periodic refresh, and do not "simplify" the widget back onto
  `zmk_rgb_underglow_*`. Also invariant: `EXT_POWER` (gpio1 9) feeds the
  touchpad module rail and must stay **enabled** — the widget enables it at
  init and never disables it; cutting it in sleep is what made the first
  touch after wake take seconds. `config/west.yml` pins the three external
  modules to exact SHAs (branch name in a trailing comment); the two
  fustopoke k04 modules are pinned through `arazyan` forks that carry
  `patches/0001`/`0002` applied via `git am` (fustopoke repos are not
  pushable) — bumping a revision means rebasing the matching patch in the
  fork and moving the SHA in the same commit. Treat
  `config/k04_mini.keymap` as the friend's frozen design unless asked
  to change it.
- `config/keys_ru.h` is generated data (Unicode license header) — do not
  reformat or hand-edit definitions; add nothing.

## Golden rules

1. **Layout invariant:** ZMK `ru` base layer ON ⟺ OS keyboard layout Russian.
   Any change to language switching (`to_en`, `to_ru`, `layer_en`, `layer_ru`,
   `cycle_*`, the Z/P keys in `nav`/`nav_ru`) must preserve this. The design
   rationale is the comment block above the `macros` section in
   `op36_ruen.keymap` — read it first, and update it in the same commit when
   the design changes.
2. Host layout shortcuts are **`Meta+Ctrl+1` (EN) / `Meta+Ctrl+2` (RU)**
   (OP36 keymaps). Digits, never letters — the RU layout remaps letters, which
   breaks keysym-matched host shortcuts.
3. The design comments in `op36_ruen.keymap` are the spec. When in doubt, the
   comments + recent git history (`git log -- config/op36_ruen.keymap`) are
   more authoritative than intuition. Several recent commits are by
   `keymap-editor[bot]` (web editor) and reflect deliberate user decisions.

## Conventions

- **Layer numbers are positional**: the index of a node in the `keymap` tree
  (0-based). Current: `0 en, 1 ru, 2 sym_en, 3 sym_ru, 4 nav, 5 nav_ru,
  6 adj, 7 mouse, 8 mouse_precision`. Renumbering means fixing every
  `&mo N` / `&lt N` / `&to N` / `layers = <N>` in the file.
- **Key positions**: 18 keys per half, row-major, split gap unnumbered.
  Left: `0–4, 10–14, 20–24, 30–32`; right: `5–9, 15–19, 25–29, 33–35`.
  Thumb row: 30=tab, 31=thumb L, 32=space, 33=enter (R), 34=thumb R,
  35=backspace. Combos and `hold-trigger-key-positions` use these indices.
- **Mouse speed**: base mouse layer uses `ZMK_POINTING_DEFAULT_MOVE_VAL`
  (1500). Precision mode is pinned at `PREC_MOVE_VAL` (650) — deliberately
  decoupled, so tuning the base speed doesn't change the fine-tuning layer.
- RU keycodes: use `keys_ru.h` symbols (`RU_CYRILLIC_*`, `RU_*`), never raw
  HID usage numbers.
- Keep the `nav` and `nav_ru` copies consistent unless a difference is
  intentional (they are per-base-layer copies so each can know its current
  language).
- Commits: short imperative subject, prefix `op36 ruen:` for keymap changes
  (existing history style). One logical change per commit.

## Builds & checks

- CI is the build: push → `.github/workflows/build.yml` → artifacts in the
  run's `firmware` archive (see README table). There is no local lint; a
  green CI build is the compile check.
- Local build (mirrors CI):

  ```sh
  west init -l config && west update && west zephyr-export
  west build -s zmk/app -b ergohaven -S "studio-rpc-usb-uart" \
    -- -DZMK_CONFIG="$PWD/config" -DKEYMAP_FILE="$PWD/config/op36_ruen.keymap" \
       -DSHIELD="op36_left"
  ```

  Errors from `west build` name the offending devicetree node; a typo in a
  binding usually fails the whole build.
- When changing `op36.json` (geometry), keep the `op36_ruen.json` symlink
  valid and consider updating the web-editor rendering.

## Things to be careful about

- `build.yaml` matrix order and `artifact-name`s: the *left* and *right*
  halves must run matching keymaps (split over BLE); a left `ruen` with a
  right stock keymap will misbehave. Keep both sides in sync.
- `op36_qube.conf` exists because Qube builds override `op36.conf` settings;
  don't merge its settings into `op36.conf` (sleep must stay off on Qube).
- Macro gotcha: `&to`/`&tog` also clears held momentary layers off the stack —
  that's why switch macros are used from held nav layers. Don't "simplify"
  them away.
- The E+R language-switch combo is intentionally disabled (commented out in
  `combos`). Leave it commented unless asked.
