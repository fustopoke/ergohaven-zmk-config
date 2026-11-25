# patches/ — fixes for the k04_mini external modules

The two ZMK modules `config/west.yml` pulls for the K:04 Mini are forks
under `fustopoke` (no push access from here). Each patch below is a
ready `git am` diff against the exact commit pinned in `west.yml`.
Together with the in-repo fixes (WS2812 moved to SPI2 — see
`boards/shields/k04_mini/k04_mini.dtsi` — sleep timeout, brightness)
they address the two failures: status LED behaving erratically and
halves disconnecting at random times.

## What each patch fixes

### `0001-widget-stop-LED-poll-from-defeating-sleep.patch`
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

### `0002-iqs5xx-park-polling-while-ZMK-sleeps-back-off-on-dead-chip.patch`
`zmk-driver-azoteq-iqs5xx` @ `2af5a3e` (branch `k04-polling`)

- The I2C polling loop (the K:04 module socket does not route RDY) ran
  at 16 ms forever — including through ZMK sleep, which defeats the
  point of sleeping, and against an absent module, which just burns
  power on NACKs. The patch parks the loop on SLEEP/STANDBY, resumes it
  on wake (replaying `setup_device` after a long park, because the
  chip's watchdog resets its registers meanwhile), and backs off to a
  1 s interval while reads fail. Gated on `CONFIG_ZMK_SLEEP`.

## How to activate

Run from anywhere with `gh` authenticated as an account that can fork:

```sh
# 1. Fork both repos under your GitHub account
gh repo fork fustopoke/zmk-smart-rgbled-widget --clone=false
gh repo fork fustopoke/zmk-driver-azoteq-iqs5xx  --clone=false

# 2. Clone, apply, push
git clone https://github.com/<you>/zmk-smart-rgbled-widget
cd zmk-smart-rgbled-widget
git checkout v0.3-compat
git am /path/to/this/repo/patches/0001-widget-stop-LED-poll-from-defeating-sleep.patch
git push origin v0.3-compat
cd ..

git clone https://github.com/<you>/zmk-driver-azoteq-iqs5xx
cd zmk-driver-azoteq-iqs5xx
git checkout k04-polling
git am /path/to/this/repo/patches/0002-iqs5xx-park-polling-while-ZMK-sleeps-back-off-on-dead-chip.patch
git push origin k04-polling
```

(`git am` will complain about author identity — use yours, e.g.
`git config user.name/user.email` in each clone before applying.)

3. Point `config/west.yml` at your forks: change `remote: fustopoke` to
   a new remote entry for your account (or keep `fustopoke` as the
   `url-base` only for what stays there), and replace the pinned
   `revision:` with the new commit SHAs you just pushed.

4. Verify with CI: push the config — the build must go green; flash and
   watch the K:04 status language (it should now also *go dark* a few
   minutes after you stop using the keyboard, and the right half's LED
   should come back on its own after a split reconnect).

## If the patches conflict

They apply to exact pinned SHAs; if a module branch moved, rebase the
patch interactively (`git checkout <new-sha> && git am -3 …`) or redo
the hunks against the new code — the "what each patch fixes" notes
above describe each hunk's intent.
