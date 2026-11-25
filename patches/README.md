# patches/ — fixes for the k04_mini external modules

The two ZMK modules `config/west.yml` pulls for the K:04 Mini originate
under `fustopoke` (no push access there). Each patch below is a `git am`
diff against the exact upstream commit they are based on. They are
**already applied**: `config/west.yml` pins the patched forks under
`arazyan/` (created with `gh repo fork`, patches applied with `git am`,
pushed). Together with the in-repo fixes (WS2812 moved to SPI2 — see
`boards/shields/k04_mini/k04_mini.dtsi` — sleep timeout, brightness)
they address three failures: erratic status LED, random half
disconnects, and the touchpad dying permanently after a few minutes.

## What each patch fixes

### `0001-widget-sleep-gating-frame-dedupe-spec-led-language.patch`
`zmk-smart-rgbled-widget` @ `5fd554d` (branch `v0.3-compat`)

- The 30 ms poll thread hammered the WS2812 through the underglow
  subsystem with the *same* frame forever, and each pass re-armed the
  underglow 50 ms refresh timer — the strip was never quiet while lit,
  and the thread kept waking the SoC even during ZMK sleep. Now frames
  are deduped (refreshed every 2 s so a corrupted write self-heals),
  the thread blocks event-driven while the half sleeps, and the widget
  calls `zmk_rgb_underglow_off()` on SLEEP so the refresh timer stops
  too.
- The central now re-pushes the right-half color every ~5 s: the
  peripheral clears its received color on link loss, and ZMK v0.3 has
  no "peripheral reconnected" event — previously the right LED stayed
  dark after a dropout until the next layer switch.
- Low-battery red blink threshold is now Kconfig
  `RGBLED_WIDGET_LOW_BATTERY_LEVEL` (default 20) to match the official
  Ergo Haven K:04 status language ("red blink every 2 s at 20% or
  below, not charging"); the pinned module hard-coded 10%. The 2 s
  intervals themselves are set in `config/k04_mini.conf`.

### `0002-iqs5xx-chip-reset-self-heal-and-sleep-gated-polling.patch`
`zmk-driver-azoteq-iqs5xx` @ `2af5a3e` (branch `k04-polling`)

- **Touchpad died after 5–10 minutes of use, permanently**: whenever
  the chip self-resets (watchdog/EMI — visible to the driver as the
  SHOW_RESET flag), the pinned driver only *acknowledges* the reset and
  leaves the chip at factory defaults — event mode off, no gestures,
  dead until reboot. The patch replays `iqs5xx_setup_device()` on
  reset detection (and after resuming a parked poll), so the touchpad
  recovers within one polling cycle (~16 ms).
- The I2C polling loop (the K:04 module socket does not route RDY) ran
  at 16 ms forever — including through ZMK sleep, which defeats the
  point of sleeping, and against an absent module, which just burns
  power on NACKs. The patch parks the loop on SLEEP/STANDBY, resumes it
  on wake (replaying `setup_device` after a long park, because the
  chip's watchdog resets its registers meanwhile), and backs off to a
  1 s interval while reads fail. Gated on `CONFIG_ZMK_SLEEP`.

## Current fork state (already wired into west.yml)

- `arazyan/zmk-smart-rgbled-widget`, branch `k04-led-fixes` @ `a435896`
  (= `5fd554d` + `0001`) — pinned in `west.yml`.
- `arazyan/zmk-driver-azoteq-iqs5xx`, branch `k04-polling` @ `478a726`
  (= upstream `2af5a3e` + `0002`) — pinned in `west.yml`.

To reproduce from scratch or to recover a fork:

```sh
# 1. Fork both repos under your GitHub account (no-op if they exist)
gh repo fork fustopoke/zmk-smart-rgbled-widget --clone=false
gh repo fork fustopoke/zmk-driver-azoteq-iqs5xx  --clone=false

# 2. Clone, apply, push
git clone https://github.com/<you>/zmk-smart-rgbled-widget
cd zmk-smart-rgbled-widget
git checkout -b k04-led-fixes 5fd554d
git am /path/to/this/repo/patches/0001-widget-sleep-gating-frame-dedupe-spec-led-language.patch
git push -u origin k04-led-fixes
cd ..

git clone https://github.com/<you>/zmk-driver-azoteq-iqs5xx
cd zmk-driver-azoteq-iqs5xx
git checkout k04-polling
git am /path/to/this/repo/patches/0002-iqs5xx-chip-reset-self-heal-and-sleep-gated-polling.patch
git push origin k04-polling
```

(`git am` will complain about author identity — use yours, e.g.
`git config user.name/user.email` in each clone before applying.)

After any fork change: update `revision:` in `config/west.yml` to the
new SHA (branch name in the trailing comment), push the config, and CI
must go green — that build is the compile check for the module code.

## If the patches conflict

They apply to exact pinned SHAs; if a module branch moved, rebase the
patch interactively (`git checkout <new-sha> && git am -3 …`) or redo
the hunks against the new code — the "what each patch fixes" notes
above describe each hunk's intent.
