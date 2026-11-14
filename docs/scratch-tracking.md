# Scratch: strip tracking and scrubbing smoothness

Notes from the overnight work on making the touch strip feel like a turntable (CDJ-1000 style): no warble,
no dead centre, minimum latency. Everything here is reproducible with the tools in `tests/`.

## 1. What the Live III strip actually does (measured, Pitch Bend mode)

From `scratch_log.txt` recorded with the debug build on a real Live III:

| Fact | Evidence | Consequence |
|---|---|---|
| Position is clean 14-bit data, no noise | a pull is a smooth run: -142, -428, -849, -1270, -1675 ... | smoothing is not needed to remove noise |
| The strip samples evenly (~13.06 ms) | step size vs delivery gap: correlation 0.06 | speed is smooth *at the source* |
| The MPC delivers messages at 128-frame block starts, so gaps alternate 11.6 / 14.5 ms | 20 consecutive moving gaps | arrival-time jitter must not become speed jitter |
| Rest value is **-1**, not 0 | finger resting at the centre sent -1 repeatedly | "exact 0" rules never matched |
| **Nothing is sent when the finger lifts** | after the last pull message the next one came 961 ms later | a lift can only be detected by silence (Strip Rel) |
| A finger resting on the centre sends irregular messages, gaps 12-190 ms | log | release timeout must stay above ~190 ms; near-centre "lift" rules misfire |

Mod Wheel (CC1) mode behaves similarly (messages ~12 ms apart, centre 62/63) but only has 128 steps.

## 2. Why earlier versions failed

* **One-pole smoothing (the "smooth" one):** 60-90 ms of lag, tracks a 3 Hz scratch 16% off, and takes
  576-664 ms to settle after the finger stops (the "wind-down").
* **g-h / velocity prediction:** overshoots when messages arrive unevenly -> garbled.
* **Straight-line jitter buffer:** a speed *step* at every message (5.8% / 12.5% ripple on 3 / 6 Hz scratches)
  and, worse, the head ran out of queued points for a few ms when the interval estimate drifted; it then reset
  its speed to zero, so every other message pulsed the speed (38 Hz wobble, up to 60% ripple). My first lab
  metric (speed minus a low-pass) hid this: it counted anything below 40 Hz as real motion.
* **Volume fade with speed (x^2 below 0.1x)** made the start of every slow move silent - perceived as a dead zone.

## 3. What other systems do (research notes)

* DVS systems (Serato, Traktor) decode position from a 1-2 kHz timecode carrier and recompute velocity every
  ~32 samples, i.e. hundreds to thousands of position updates per second. Our strip gives ~77 per second, so we
  must reconstruct motion between updates rather than react to each one.
* Mixxx's position-based scratch controller runs jittery position through a filter chain that was tuned from
  recorded data (reported in a Launchpad blueprint comment; source `src/engine/positionscratchcontroller.cpp`,
  GPL - read for ideas only, nothing copied).
* Sources are thin; the algorithm below was chosen by measurement, not by copying anyone.

## 4. The design (src/strip_tracker.h)

1. **Jitter buffer**: positions queue up and are played back one per *average* interval (running average, locks
   in a few messages), so uneven arrival times do not matter. Costs about one interval (13 ms).
2. **Cubic Hermite moves that start from the head's actual position and speed** (never from stored points), so
   speed is continuous and a late message costs nothing: the head just carries on at its speed. Silence longer
   than ~5 ms makes the speed die out (~25 ms to rest).
3. **Lead**: add `lead x interval x speed` to the output. The speed is smooth, so this stays smooth; it buys
   back latency at the price of some ripple. This is the user-facing "Lead" knob.
4. **3-pole smoother** of `Smooth x 0.2` ms removes what is left.
5. A repeated strip value ends a move at zero speed (finger stopped).
6. Deck sample is read with a 4-point cubic (Catmull-Rom) interpolator instead of linear.

## 5. Results (tests/tracker_lab.cpp, tests/scratch_test.cpp; emulated Live III strip timing)

Ripple = share of head-speed energy above 20 Hz. Latency = lag of the head behind the *true* finger position.

| Algorithm | 3 Hz scratch ripple / latency | 6 Hz scratch | settle after stop |
|---|---|---|---|
| one-pole 80 ms (old favourite) | 4.3% / 64 ms | 7.7% / 44 ms | 576-664 ms |
| **tracker, Lead 60, Smooth 20 (default)** | 2.1% / 15.6 ms | 7.9% / 15.8 ms | ~25-40 ms |
| tracker, Lead 100, Smooth 2 | 6.6% / 7.0 ms | 22.6% / 7.6 ms | ~25-40 ms |
| tracker, Lead 0, Smooth 20 | 0.5% / 23 ms | 2.1% / 23 ms | ~25-40 ms |

Other checks (all in `tests/scratch_test.cpp`): steady pull ripple <= 4% for strip intervals 11.9-14.2 ms
(the previous design reached 59%); the first move away from the centre is audible 5.8 ms after it reaches the
plugin, in both directions; a pure 1 kHz tone pulled at 0.5x / 1.2x / 1.7x keeps its pitch within 2.1% / 1.0% /
0.5%; the recorded real pull replays with 3.4% roughness.

**Tuning**: *Lead* up = less latency, more ripple. *Smooth* up = less ripple, more latency. Floor: the strip's
14-bit steps give ~3% ripple on very slow pulls whatever the algorithm does.

## 6. What is NOT verified

* Nothing here has been run on hardware since the rewrite. The numbers come from emulating the strip with the
  timing measured in the log.
* The "dead zone at the centre": the log shows the strip sends -1 at the centre and moves off smoothly (no
  jump), so no hardware dead zone was found in the downward direction; **upward pulls were never logged**. The
  likely software causes (centre release rules, volume fade at slow speed) were removed; if the centre still
  feels dead, record a debug log with an upward pull from the centre and a downward pull, for Pitch Bend AND
  Mod Wheel.
* Lift detection is silence-based (Strip Rel, default 250 ms). Whether a finger held still away from the
  centre keeps sending messages is unknown.

## 7. Next ideas (not done)

* Shorten the lift timeout if held-still turns out to send heartbeats.
* A "hold" gesture (second finger / pad) to keep the record stopped without the strip.
* Pre-compute a higher-quality (sinc) interpolation for extreme speeds.
