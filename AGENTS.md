# AGENTS.md

Working notes for AI agents (and humans) modifying this repo. This is a
starting point — extend it as patterns and decisions accumulate.

## What this repo is

ZMK *user config* for the Ergohaven **K:04 Mini** split keyboard. No
firmware source lives here: `config/west.yml` pulls `ergohaven/ergohaven-zmk`
(boards + firmware) plus the two k04 modules. All changes are Kconfig
(`*.conf`), keymap (`*.keymap`, devicetree source with C preprocessing) and
devicetree (`boards/shields/k04_mini/`) edits plus `build.yaml`.

## Scope

- **K:04 Mini is the target** (`config/k04_mini.*`,
  `boards/shields/k04_mini/`, `zephyr/module.yml`, `CROSS_OS.md`,
  `patches/`). `build.yaml` builds only k04_mini (left, right,
  settings_reset). The other keyboards' files under `config/` (velvet, k03,
  imperial44, op36, trackball) are inherited from the upstream template and
  are NOT built here — do not resurrect builds for them or add new keyboards
  without being asked.
- Upstream `ergohaven-zmk` has no k04_mini shield: `boards/` here is the
  only source of truth for the shield's devicetree.
- The language-switch design spec is the comment block at the top of
  `config/k04_mini.keymap` plus `CROSS_OS.md`. Treat the keymap as a
  deliberate design: change it only when asked, and re-read `CROSS_OS.md`
  first when touching k04 files.
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
  touch after wake take seconds.
- `config/west.yml` pins the three external modules to exact SHAs (branch
  name in a trailing comment). The two k04 modules are currently pinned
  through `arazyan` forks that carry `patches/0001`/`0002` applied via
  `git am`; this repo's own `fustopoke` module repos are the bases of those
  forks, so the patches may also be rebased onto the fustopoke branches and
  the pins moved back — in either case bumping a revision means rebasing
  the matching patch and moving the SHA in the same commit
  (see `patches/README.md`).
- `config/keys_ru.h` is generated data (Unicode license header) — do not
  reformat or hand-edit definitions; add nothing.

## Golden rules

1. **Layout invariant:** ZMK `ru` base layer ON ⟺ OS keyboard layout
   Russian. Every action that changes the layer must change the OS input
   source, and nothing else may change one of the two (`&os_lang_tog`,
   `&layer_en`/`&layer_ru`, the `&lg_space` mod-morph, `&en` macro). The
   keymap header comment documents why the switch keys are per-layer
   bindings rather than a blind toggle.
2. The RU layout remaps letters, so host shortcuts must use digits or
   layout-independent keys; the `&en` macro's waits (150 ms) exist because
   the host applies input-source switches asynchronously — do not trim
   them.

## Conventions

- **Layer numbers are positional**: 0 en, 1 ru, 2 nav, 3 sym_en, 4 sym_ru,
  5 adj, 6 pointing. They are also baked into the LED colors in
  `config/k04_mini.conf` (`RGBLED_WIDGET_LAYER_*`): renumbering a layer
  means fixing every `&mo`/`&lt`/`&to`/`layers = <N>` *and* the conf.
- RU keycodes: use `keys_ru.h` symbols (`RU_CYRILLIC_*`, `RU_*`), never raw
  HID usage numbers.
- Commits: short imperative subject; `k04 mini:` prefix for firmware
  behaviour, `ru:` / `touchpad:` etc. for narrower changes. One logical
  change per commit.

## Builds & checks

- CI is the build: push → `.github/workflows/build.yml` (uses `build.yaml`)
  → left/right/settings_reset UF2s as artifacts. There is no local lint; a
  green CI build is the compile check — it also compiles the pinned module
  forks, so it validates `patches/` against their SHAs.
- Local build (mirrors CI):

  ```sh
  west init -l config && west update && west zephyr-export
  west build -s zmk/app -b ergohaven -S "studio-rpc-usb-uart" \
    -- -DZMK_CONFIG="$PWD/config" -DSHIELD="k04_mini_left"
  ```

  Errors from `west build` name the offending devicetree node; a typo in a
  binding usually fails the whole build.

## Things to be careful about

- `build.yaml` matrix: the *left* (central) and *right* (peripheral) halves
  must run matching keymaps (split over BLE). Keep both sides in sync.
- Flash order after a `patches/` or Kconfig change: no `settings_reset`
  needed — left, then right (see the diagnosis section in `CROSS_OS.md`).
- The status LED language (profile colors, low-battery 20 %/2 s) is pinned
  explicitly in `config/k04_mini.conf` so a module bump cannot silently
  change it — keep those `CONFIG_RGBLED_WIDGET_*` lines when editing.
- Macro gotcha: `&to`/`&tog` also clears held momentary layers off the
  stack — that's why switch macros are used from held layers. Don't
  "simplify" them away.
