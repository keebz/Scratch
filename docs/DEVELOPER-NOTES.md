# Scratch (Keebz) - developer notes

Working notes written while building the plugin (behaviour details, measurements, test commands). Some early lines are historic; the code and `tests/` are the reference. The project front page is [`../README.md`](../README.md).

DJ scratch instrument for the MPC. Target: MPC Live III (arm64), also builds for the 32-bit units.

## How it works
- **Samples**: `Content/Samples/*.wav` (mono/stereo, any rate, first 6 s used, up to 32 files). Early note: one demo
  sample is included: 01 "Ahh" (the test voice sample, on the deck; the cut pad is note 39).
- **Deck**: `Deck Sample` puts a sample on the record; it loops at 33 1/3 / 45 / Stop.
- **Strip scratching**: grab the record with the touch strip. Up = forward, down = reverse, still = held
  and silent. `Throw` = how much of the record one full strip pull covers. `Strip Src` picks how the strip
  reaches the plugin (the strip mode on the MPC must match): `Bend` (Pitch Bend mode, best: 14-bit),
  `ModWheel` (CC1, 7-bit) or `Scrub` (Q-Link mode, drives the Scrub knob).
  - **Lead** (%, default 100, up to 300) and **Smooth** (default 80, up to 300) trade latency against smoothness.
    Measured on a real Live III scratch log (build 1): at Lead 60 / Smooth 20 the record speed carried 12-16% ripple
    above 20 Hz (the strip's own step noise); at Lead 100 / Smooth 80 it is ~3% with ~25-30 ms of delay.
    Lower Smooth for less delay, raise it if you hear warble. Lead above 100% looks further ahead and makes up for
    the delay of a high Smooth: on the same log, Lead 200 / Smooth 160 had ~1% ripple at ~32 ms, against ~3% at
    ~29 ms for the defaults (it overshoots 1.2% of a pull when the finger stops dead; Lead 300 overshoots 3.7%).
  - **Strip Zone** (default Bottom): which part of the strip scrubs. The MPC strip is a pitch wheel: it reports a fixed
    rest value (-1) in the middle, both when your finger is on the middle and when you lift off anywhere, so on the
    full strip the middle cannot be told from a lift. `Bottom` / `Top` use one half only, with the middle at the end
    of the zone: the rest value then always means "lifted" (released within ~30 ms) and the middle is never a
    position. Touching the other half just holds the record at the end of the zone. `Whole` uses the whole strip
    (middle = position, lift from the middle found by `Strip Rel` / 3). Throw is scaled so a zone feels the same per
    millimetre as the full strip.
  - **Lift detection** (measured, Pitch Bend mode): lifting sends one rest value (-1) and then nothing. Off the
    middle (or in a zone) that is detected within ~30 ms; a finger held still only reports now and then (gaps up to
    0.6 s), so the record stays held for 1 s of silence.
  - How and why it works, measurements and open questions: `docs/scratch-tracking.md`.
- **Cut pad** (`Cut Note`, default 39): gates the deck like a crossfader. `Cut Curve` 0 = instant full
  volume; higher = velocity-sensitive level and slower fade. `Hamster` reverses it. `Cut Learn` on, then hit
  a pad to set the cut pad.
- **Shots**: pads `Shot Base` + 0..15 (except the cut pad) fire samples 1..16 through channel B.
- **Mixer**: Q-Link rows A and B are Gain / High / Mid / Low (DJ EQ, bottom of the range kills the band).
- **Stickers**: toggle `Sticker` to add a marker at the current angle (or remove the nearest). Saved per
  sample in the project. `Go Sticker` 1..8 jumps the platter to that marker.


## Touch-screen UI (skins/)
Hand-written skin, drawn procedurally by `tools/skin_art.py` / `tools/make_skin.py` (needs numpy to regenerate;
the generated files are committed). Preview: `docs/mockup.png`.
- **BATTLE tab**: cement wall, turntable in battle position (tonearm across the top, 33 1/3 record label that
  turns with the sample, cue **stickers** that ride the record), two-channel mixer with LED meters, crossfader
  (shows the cut gate live) and the **Hamster** switch, 8 sample pads, **LOAD** switch, platter speed buttons.
  Q-Link columns: the first column is channel A (Gain, Hi, Mid, Low), the second is channel B.
- **SETUP tab**: strip feel (Strip Src, Throw, Smooth, Lead, Strip Rel, Scrub), cut settings, deck sample.
- The plugin writes display-only parameters for the skin (Rec Angle, Cut Pos, Level A/B, Stk 1-8). Pads are
  momentary parameters (Pad 1-8). Tapping the record toggles `Sticker` (adds a sticker at the needle, or removes
  the nearest one within 2% of a turn of the needle: bring it under the needle and tap).
- If the custom skin ever fails to load on the MPC, use the "generic UI" zip (same plugin, `ui.json` pages).

## Not done yet
- Drag and drop of pads onto the record (the skin system has no drag): use **LOAD** + tap a pad instead.
- Tapping a specific sticker to remove it (taps carry no position): see the sticker note above.
- Not tested on hardware. Unverified: which strip mode reaches plugins, the real note numbers of pad A04
  (set with Cut Learn), and whether the strip recentres to exactly 0 on release.

## Tests
`tests/tracker_lab.cpp` compares tracking algorithms on emulated strip data; `tests/scratch_test.cpp` checks the plugin itself (warble, latency, centre, pure-tone purity, real recorded pull).
`plugins/Scratch/tests/scratch_test.cpp` drives the engine with strip and pad MIDI (see header comment).
Host simulator: `.sdk-tools/host_sim plugins/Scratch/build/native/scratch.so test --notes 39,36,37`.


## Touch screen (BATTLE tab) - current behaviour
- **Sample list** (right): every `.wav` in `Content/Samples` (name order, a leading "01 " only sets the order, up to 24).
  Turn the data encoder (or drag on the list) to scroll. **PREVIEW** on: the selected sample plays (channel B) as you
  scroll; off: silent.
- **LOAD** (next to PREVIEW): the eight pad slots flash on the screen (the MPC does not let a plugin light its own
  pads); hit hardware pad A01-A08 (notes 36-43, set by `Shot Base`) and the selected sample is stored on that pad.
  Press LOAD again to cancel. Assignments are saved with the project.
- **Pads A01-A08 are samples and faders in one**: hit a pad and its sample goes on the record (starting at its cue
  sticker if it has one, otherwise the start) and the cut opens for as long as the pad is held, with the velocity curve
  (`Cut Curve`). Hitting the pad of the sample already on the record just works the cut; it does not restart it.
  Hamster reverses the cut.
- **STOP B**: stops the channel B loop.
- **TO B**: loops the selected sample on channel B, seamlessly (the wrap is read across the join), at its original
  speed, independent of the record and the cut. Press again to clear. Channel B has its own gain and EQ knobs.
- **Hamster**: the on-screen switch, or MIDI pad 12 (note 47, `Ham Note` on the SETUP tab) toggles it.
- **Mixer knobs** sit at 12 o'clock = 0 dB. Left end: -24 dB (gain) or kill (EQ); right end: +12 dB.
- Defaults: Throw 120, Smooth 300, Lead 300.
- Switch parameters are declared as 2-choice parameters (the MPC screen did not write plain bool ones).
- Test with `host_sim ... --notes 36` (pad A01 holds sample 1).
- **Platter and Throw no longer depend on the sample length**: the platter turns once per 1.8 s of record travel (33 1/3 rpm;
  45 is faster, STOP stops it) and Throw 100% = 0.8 s of audio under one full strip pull, for a 0.05 s chop or a 2.5 s loop
  alike. A sticker rides with the platter and still cues the record at the sample position it was stuck at.
- **Encoder**: the list moves one line per notch whatever the host's step size (only the direction of each change counts).
- **Gain knobs** go all the way to silence (-60 dB at the left end, OFF); EQ knobs kill at the left end.
- `Content/Samples/Beat_Loop.wav` is a loop meant for channel B (select it, TO B).

- **HELP tab** (third tab): explains the plugin; notes the MPC pads must be Chromatic. Battle page: pad slots are laid out like the MPC (A01-A04 bottom row), taller buttons.
- **Loop Snap** (SETUP, default On): the sequencer records strip and pad events but not the record's position, so each loop pass
  used to start the sample somewhere else. The plugin now keeps one anchor (record position, platter turn, deck sample, speed) taken
  when the transport starts (or recording starts) and puts the record back whenever the song position reaches that point again
  (loop wrap, restart from the same spot). The anchor is saved with the project. A new record chosen while stopped, or Loop Snap
  Off/On, starts a new take. Uses the host transport (play state + song position); the DEBUG log shows `N` lines for each capture (0) / snap (1).
- **Quick Start PDF** (`docs/Scratch-QuickStart.pdf`, built by `tools/make_quickstart.py`, also placed in each zip next to `VST2 Plugins`): install (clean and merge into an existing VST2 Plugins folder), MPC setup (Chromatic, Pitch Bend, Bank B), why the strip is split, own samples, disclaimer. HELP tab carries the author links.
