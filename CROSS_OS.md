# K:04 Mini on Linux, Windows and macOS

One keymap, one firmware build, all three hosts. This file explains what the
keyboard can and cannot know about the machine it is talking to, and where the
two switches are that adapt it.

## What changed, and why the previous build only worked on macOS

The RuEn layout keeps two pieces of state in step:

* the **keyboard layer** (`en` = 0 / `ru` = 1) — decides which scancodes the
  keys send,
* the **OS input source** (English / Russian) — decides what those scancodes
  become.

Letters ride the OS translit, so the OS input source is what actually produces
`й` instead of `q`. The keyboard has to tell the host to switch, and it can
only do that with a keyboard shortcut. ZMK cannot detect the host OS over
BLE or USB — there is no such signal — so the shortcut has to be declared.

The earlier version tried to cover every OS at once by firing **both**
`Ctrl+Space` (macOS) and `LShift+RShift` (a common X11 convention) on every
language change. That does not work:

* `Ctrl+Space` is the *completion* shortcut in essentially every editor and
  IDE on Linux and Windows, so instead of switching the layout it popped up
  autocomplete.
* GNOME, KDE and Windows switch layouts with `Super+Space`, which was never
  sent — so the OS layout stayed put while the keyboard layer flipped. This is
  the "A12 does nothing / types Latin while on the Russian layer / `{` comes
  out as `Х`" symptom.
* An extra shortcut that *is* accepted flips the layout an odd number of
  times, which permanently desynchronises the two states.

There is no shortcut that is safe on all three hosts, so the config declares
the shortcuts it uses in one place instead of firing several.

## Primary mechanism — absolute anchors (2026-11 redesign)

Every normal language action now sets a **definite target**: "select layout
1 (English)" / "select layout 2 (Russian)" via `&os_sel_en` / `&os_sel_ru`
in `config/k04_mini.keymap`, sending `Meta+Ctrl+1` / `Meta+Ctrl+2` — the
same anchors the OP36 RuEn keymap uses. The relative "next source"
shortcut survives only in the Adjust layer's SYNC/LANG.

Why: a relative toggle loses parity the moment the host coalesces rapid
key chords (repeated symbol-layer entries, A12 immediately followed by
T7 — the "and now the language keeps switching" symptom). Absolute
commands are idempotent; a lost or doubled intermediate cannot matter,
the last command always states the truth.

One-time host setup:

* **KDE Plasma** (the user's primary host): System Settings → Shortcuts →
  *Keyboard Layout* → assign **Meta+Ctrl+1** to "Select Keyboard Layout"
  slot 1 and **Meta+Ctrl+2** to slot 2. The tray's layout list must be
  English first, Russian second.
* **macOS**: System Settings → Keyboard → Keyboard Shortcuts → Input →
  "Select the Input Source 1/2" → Ctrl+Shift+1/2; change the `os_sel_*`
  bindings to `LS(LC(N1))` / `LS(LC(N2))` (this is what upstream
  `k03_ruen.keymap` does).
* **Windows**: no built-in select-layout-N chord — either add one via
  PowerToys Keyboard Manager, or stay on the relative scheme below for
  that machine.
* **GNOME**: also has no absolute selection; on GNOME the relative
  "Switch 1" below remains the mechanism (and its two-sources-only rule).

## Switch 1 — the relative shortcut (SYNC/LANG fallback, one line)

`config/k04_mini.keymap`, in the `macros` block:

```c
os_lang_tog: os_lang_tog {
    bindings = <&os_next_linux_win>; /* ← ACTIVE: the one line to change */
};
```

Three presets are already defined right above it:

| Preset              | Sends        | Default on                 | Host setup needed |
| ------------------- | ------------ | -------------------------- | ----------------- |
| `os_next_linux_win` | `Super+Space`| GNOME, Windows 10/11       | none              |
| `os_next_mac`       | `Ctrl+Space` | macOS                      | none              |
| `os_next_x11`       | `Alt+Shift`  | X11/XFCE/LMDE              | `setxkbmap -option grp:alt_shift_toggle` |

KDE Plasma is the one desktop that varies by version — its layout switch may
ship bound to `Alt+Shift` rather than `Super+Space`. Check
*System Settings → Shortcuts → Keyboard Layout* (or the input-method shortcut
in Fcitx5/Kimpanel) and make the preset match whatever the desktop actually
shows. The rule is simple: **the preset must send the shortcut your host uses,
nothing else.**

Pick one, rebuild, flash **both** halves.

## Switch 2 — the spacebar chord (one line)

Holding the OS key and tapping space switches layout *and* layer together.
Since the 2026-11 redesign there are two per-base mod-morphs —
`&lg_space_en` on the en base (its morph target is `&layer_ru`) and
`&lg_space_ru` on the ru base (target `&layer_en`) — so even the chord
sets a definite target instead of blind-toggling. The modifier they watch
has to be **exactly** the one the anchor shortcut carries (Super for the
KDE `Meta+Ctrl+1/2` anchors; the morph adds its own copy of that modifier
on top of the key you are holding):

```c
mods = <(MOD_LGUI)>;   /* Linux / Windows — Super */
mods = <(MOD_LCTL)>;   /* macOS — Ctrl */
```

If you hold Ctrl while the macro emits `Super+Space`, the host sees a
`Ctrl+Super+Space` chord and matches no binding at all — the key appears dead
and nothing reports an error. This is the failure mode to check first if a
language key "does nothing" after you change presets.

## Host-side requirements

* **Two input sources only: English + Russian** (for the relative
  Switch-1/LANG/SYNC fallbacks). The shortcut means "next source"; with two
  sources "next" and "previous" are the same cycle, which is what makes a
  single shortcut deterministic. A third source (a third layout, an emoji
  picker, an IBus engine) breaks the cycle — remove it, or rely on the
  absolute anchors above: they are immune to extra sources as long as
  slots 1 and 2 of the host's list are English and Russian.
* **Use the standard "Russian" layout (JCUKEN), not a phonetic one.**
  `config/keys_ru.h` maps the Cyrillic keys by the positions of the stock
  Russian layout; a phonetic or "Russian (typing)" variant will produce the
  wrong letters.
* **Linux:** GNOME/KDE need nothing. On X11 without a desktop shortcut, enable
  a group switch (`grp:alt_shift_toggle` or `grp:shifts_toggle`) and use
  `os_next_x11`. Wayland sessions take their shortcut from the DE's keyboard
  settings, not from `setxkbmap`.
* **Windows:** `Win+Space` is built in; make sure both English (US) and
  Russian are in the language list.
* **macOS:** `Ctrl+Space` is "Select the previous input source" by default
  (System Settings → Keyboard → Keyboard Shortcuts → Input Sources). Add the
  Russian source to the menu bar list.

### Optional, macOS only: absolute selection

Superseded: absolute anchors are now the primary mechanism for every host
that supports them — see "Primary mechanism — absolute anchors" above.
On macOS that means Ctrl+Shift+1/2 and `LS(LC(N1))/LS(LC(N2))` inside the
`os_sel_*` macros; the layout list order (English first) is the anchor.

## Keeping the two states paired

* **A12** (top-right, both base layers) — `&layer_en` / `&layer_ru`: one
  action, two absolute sets — keyboard layer via `&to`, host layout via the
  anchor shortcut. Nothing to drift.
* **Adjust → LANG** (`&lg_tog`) — layer-blind fallback: `&tog 1` + one
  relative host flip; reach for it only when you don't know which base is
  active.
* **Adjust → SYNC** (`&os_lang_tog`) — flips *only* the OS input source
  (relative). Use it when a mouse/system menu/second keyboard changed the
  host layout behind the keyboard's back. The base en/ru layers share the
  white LED color by design (language is not LED-encoded); the momentary
  layers are colored (nav green, sym cyan, adj yellow), so drift is
  spotted by typing one letter: it comes out `й` instead of `q` or
  vice versa.
* Symbols that do not exist in the Russian layout (`{ } [ ] @ # $ ^ & ~`)
  live on the single **sym** layer (index 3). T7 momentaries it from
  either base and does NOT touch the host by itself. While T7 is held, the
  C-row end keys on the sym layer set the language explicitly: **T7+C1**
  (left, the LSHFT position) = `&layer_ru` (host SET to Russian, base ru),
  **T7+C12** (right, the RSHFT position) = `&layer_en` (host SET to
  English, base en). So to type symbols from the Russian base: hold T7,
  tap C12 once (context→EN), type the symbols, and when done hold T7 and
  tap C1 to go back. Every command is absolute, so bursts cannot desync
  keyboard and host. (The old schemes — per-symbol `&en` toggles, then a
  per-hold toggle pair, then a sticky session — each raced or fought the
  user's key layout; this is the agreed one.)

## Pairing the second / third Bluetooth profile

A BT profile is a bond slot on the *keyboard* side; one slot holds exactly
one host. Slots 0–4 are the `BT_SEL` keys in the Adjust layer (top row,
right side), `BT_CLR_ALL` (last key of that row) empties every slot.
Selecting a slot shows its colour for 1 s: BT0 red, BT1 blue, BT2 yellow,
BT3 green, BT4 magenta.

1. From the Adjust layer press the `BT_SEL` of the slot you want to add.
   If the slot has no bond, the keyboard drops the current host and
   becomes discoverable (blue blink).
2. Pair "K:04 Mini" from the new host. Blue blink → green 1 s = bonded
   and connected.
3. Later, `BT_SEL n` reconnects that slot's saved host (white flash while
   reconnecting).

Failure modes worth knowing about:

* A slot already holding a bond **replaces** it if you pair a different
  host into it — pair the new host into the *next empty slot*, and use
  `BT_CLR_ALL` + full re-pairing only when the slot memory is suspect
  (there is no per-slot clear key in the current keymap).
* A host that paired against **older firmware** may keep the cached HID
  report descriptor (the classic "touchpad sends nothing" cause on
  Linux). Symptom: pairing succeeds but mouse reports are ignored by
  that host. Fix: forget the device on the host, `BT_CLR_ALL` the slot,
  re-pair.
* The keyboard can usually serve two or three hosts concurrently per
  radio, but each slot still remembers only one; the split link itself
  occupies one radio connection, so leave at least one slot unused
  before reporting "profile N will not connect at all".

## Touchpad

`boards/shields/k04_mini/k04_mini_right.overlay` holds the device node.

* **After any change that touches pointing (enabling `CONFIG_ZMK_POINTING`,
  changing `CONFIG_ZMK_POINTING_SMOOTH_SCROLLING`, or first flashing a build
  where the touchpad started working), remove and re-pair the keyboard on every
  host.** The HID report descriptor changes and hosts — Linux especially —
  keep the cached descriptor over BLE and drop the new mouse reports.
* **Scrolling direction** is deliberately *not* set in firmware. The device
  emits "content tracks the fingers", which is the touchpad convention on all
  three hosts, and each OS has a "natural scrolling" setting that inverts it.
  Use the OS setting; that is what makes one firmware correct everywhere.
  `natural-scroll-y` / `natural-scroll-x` exist as commented-out escape
  hatches in the node for hosts with no such setting.
* **Polling rate**: `CONFIG_INPUT_AZOTEQ_IQS5XX_POLL_INTERVAL_MS=16` (~60/s).
  Set `8` if the cursor feels steppy — motion deltas accumulate between reads,
  so a shorter interval ships smaller, more frequent reports that host pointer
  acceleration renders much more smoothly. It costs a little battery on the
  right half, because the poll is what keeps that half waking up.
* **Jitter / crawl on Linux and Windows** (both apply pointer acceleration on
  top): lower `bottom-beta` in the device node (0 = most filtering,
  255 = rawest). Raise it if the cursor feels mushy when starting a stroke.
* **Dead zone at the start of a drag**: lower `stationary-threshold`
  (default 5 px, 3 is snappier).
* **Cursor mirrored on diagonals** (finger moves down-right, cursor drifts
  down-left): the orientation triple `switch-xy` + `flip-x` + `flip-y` is
  over-constrained — a swap plus two flips is a reflection, while rotating the
  module's axes needs a swap plus *one* flip. Remove `flip-y` and re-test.
* **Scroll resistance**: the driver emits one notch per 32 px of two-finger
  travel. That divisor is compiled into the driver, not configurable here; it
  is a one-line change in the `k04-polling` branch of the IQS5xx fork if you
  want it lighter.
* **Mouse buttons**: the `pointing` layer was previously unreachable — no key
  activated layer 5. Adjust → MOUSE (`&mo 5`) now brings it up, giving middle
  and side buttons on the home-row fingers while held. The touchpad's own
  taps/scroll work on every layer regardless.

## The other RuEn keymaps in this folder

`k03_ruen.keymap`, `op36_ruen.keymap`, `imperial44_ruen.keymap` and
`velvet_v3_ui_ruen.keymap` are Ergo Haven's upstream RuEn references. They
switch language with `Ctrl+Shift+1` / `Ctrl+Shift+2`, i.e. macOS absolute
layout-slot selection, and they only work if you assign those two shortcuts in
macOS. `build.yaml` does not build any of them — this repo only builds the
`k04_mini_*` shields — so treat them as reading material, and copy the
`os_lang_tog` structure from `k04_mini.keymap` if you ever want one of them to
be host-neutral too.

## Status LED and random half dropouts — diagnosis (2026-11)

Symptoms: the onboard WS2812 indicators behaved erratically (wrong colors,
flicker), and either half could silently fall off the BLE link at random
times. Two independent root causes, both fixed here:

1. **The strip has to live on SPIM3, and that is where anomaly 198 is.**
   nRF52840 silicon anomaly 198: while SPIM3 transfers via EasyDMA, any
   CPU RAM access in the same cycle corrupts the outgoing bitstream — and
   the BLE link layer is constantly touching RAM. There is nowhere else to
   move it: SPI0 and SPI1 are the same physical peripherals as the board's
   console UART and as TWIM1 (owned by the right half's touchpad — Zephyr
   build-asserts on that), and SPI2, although documented as free and
   errata-free, **never drove the strip on this hardware** — every build
   that put the WS2812 on `&spi2` came out with a dark LED. So the strip
   stays on `&spi3` (data line P0.30, unchanged) and the mitigation is in
   software: `zmk-smart-rgbled-widget` writes frames straight through the
   Zephyr LED-strip API, one per colour change (plus a 10 s self-healing
   re-push), and ZMK's own underglow — which refreshes the strip every
   50 ms, i.e. 20 SPIM3 DMA transfers a second — is switched off at init
   and never re-enabled (`CONFIG_ZMK_RGB_UNDERGLOW_ON_START=n`). One DMA
   lottery ticket per colour change instead of twenty per second. *Do not
   restore the periodic refresh, and do not try spi2 again.*
2. **The halves never slept, so the battery sagged and the radio
   browned out.** Three loops ran continuously on each half: the LED
   widget's 30 ms poll re-writing the strip, the IQS5xx touchpad polled
   every 16 ms through ZMK sleep, and the always-lit white base LED at
   brightness 60. Fix = `patches/0001` + `patches/0002` (event-driven
   sleep-gated polling + dead-chip backoff — fork-and-apply, see
   `patches/README.md`), `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT` 10 min → 3 min
   → 1 h (the flashed 3-min firmware felt too eager in desk use; with
   the polls now parked, the longer window costs little),
   `CONFIG_RGBLED_WIDGET_PERSISTENT_BRIGHTNESS` 60 → 40. A side effect of
   the fixes: after a split reconnect the right-half LED now re-lights on
   its own within ~5 s (the central re-pushes its color; the peripheral
   clears it on link loss).
3. **The touchpad died until reboot, then woke slowly.** Two separate
   faults in the IQS5xx driver. (a) When the chip self-resets (watchdog or
   EMI), `SHOW_RESET` was ACKed but `iqs5xx_setup_device()` was never
   re-run, so the chip sat at factory defaults and the touchpad was dead
   until the half was rebooted — the "works for 5–10 minutes, then stops"
   symptom. Fixed in `patches/0002`: replay the full configuration on
   reset and after a long parked poll. (b) Slow first touch after wake:
   ZMK's underglow `off()` used to call `ext_power_disable()`, and
   `EXT_POWER` (gpio1 9) is the **module rail** that powers the touchpad;
   every sleep power-cycled the IQS5xx, so the first touch had to wait for
   its power-up. Fixed in `patches/0001`: the widget enables `EXT_POWER`
   at init and never disables it. The driver also stopped backing off to
   its 1 s interval on the first failed poll — the backoff now only kicks
   in after a burst of failures and resets on wake.

Flash order after pulling this: no `settings_reset` is needed — just
left, then right. Host re-pairing is *not* required (the HID descriptor
did not change), but if a host still misbehaves, un-pair/re-pair it.

## Building and flashing

`build.yaml` produces three artifacts from the GitHub Actions run:
`k04_mini_left` (central, ZMK Studio enabled over USB), `k04_mini_right`
(peripheral, owns the touchpad) and `settings_reset`. Flash `settings_reset`
once per half when moving between major config revisions, then the two half
images.

`config/k04_mini.conf` is applied to **both** halves: ZMK also tries the
shield's *directory* name (`k04_mini`) as a config candidate, not just the
instance names (`k04_mini_left`, `k04_mini_right`). Keep shared settings
there. For something that should only affect one half, either add
`config/k04_mini_left.conf` / `config/k04_mini_right.conf` (applied in
addition to the shared one) or, better, express it in devicetree — the two
halves already have separate overlays, and a pointing-device or LED setting
that differs between them is usually a wiring fact, not a build flag.
