# FIZZFUEL V2 — The Effects Gearbox (Design Spec)

**Date:** 2026-07-05
**Status:** Design — awaiting review
**Author:** Soda + Claude
**Supersedes:** FIZZFUEL v1 (5-flavor saturation gearbox)

---

## 1. Concept

V1 is a saturation gearbox: 5 gears = 5 flavors of the *same* distortion. V2 keeps the H-gate shifter as the hero, but **each gear becomes a different effect.** You throw the stick to pick one effect; the clutch drives that effect's intensity. One effect active at a time (selector model, not a parallel rack).

The distortion is kept as **one** gear (warmer, less fizzy, multi-style), and the other gears become brand-new effects.

### Gate layout

| Gear | Effect | Internal styles | Neon |
|------|--------|-----------------|------|
| **1** | **DRIVE** — saturation (warmer) | Tape · Tube · Console · Clip · Fuzz | cyan |
| **2** | **REVERB** | Room · Hall · Plate · Canyon | orange |
| **3** | **DELAY** — tempo-synced | Slap · 1/8 · Dotted · Long | yellow |
| **4** | **PITCH** | −Oct · +5th · Detune · Shimmer | pink |
| **5** | **FILTER/EQ** | Telephone · LP Sweep · Tilt · Reso | magenta |
| **R** | **CLEAN** | dry bypass / instant A-B | white |

---

## 2. Control model (reuses the v1 UI)

The physical layout is unchanged — title bar, tach, preset browser, 5-knob row, H-gate shifter. What changes is that the controls become **context-aware** to the selected effect.

- **Shifter** — picks the effect (unchanged interaction; gate physics kept).
- **5-knob row** — labels + functions **remap per effect**. Same 5 knobs, new meaning:
  | Effect | K1 | K2 | K3 | K4 | K5 |
  |---|---|---|---|---|---|
  | Drive | DRIVE | STYLE-blend | TONE | MIX | OUT |
  | Reverb | SIZE | DECAY | TONE | MIX | OUT |
  | Delay | TIME | FEEDBACK | TONE | MIX | OUT |
  | Pitch | INTERVAL | DETUNE | TONE | MIX | OUT |
  | Filter | FREQ | RESO | TONE | MIX | OUT |
- **Clutch** — **redefined** as the current effect's *intensity macro* (its single big "more" control). Morphing *between* gears no longer makes sense (they're different effects), so the clutch now morphs within the active effect.
- **Style selector** — a new compact 4-way segmented control in the display picks the active style for the current gear.
- **Tach** — shows a live per-effect readout: drive level / reverb decay / delay time / pitch interval / filter freq.
- **Per-gear neon** — kept; each effect lights its own color.

### Parameter model (APVTS)
- Global (all effects): `gear` (choice), `mix`, `output`, `clutch` (intensity), `tone`.
- Per-effect params are stored as their own APVTS params but only the active gear's are surfaced to the UI knob row (host automation still sees all). Naming: `drive_amt`, `rev_size`, `rev_decay`, `dly_time`, `dly_fb`, `pitch_interval`, `pitch_detune`, `filt_freq`, `filt_reso`, plus a per-gear `*_style` choice.
- **State compatibility:** keep the APVTS valueTreeType id `"OCTANE"` (unchanged) so v1 sessions load; v1 had `gear/clutch/drive/tone/mix/output` — these are preserved; new params default to no-op so a v1 session opens sounding like v1 Drive.

---

## 3. Architecture

Introduce an **`Effect` interface** so each engine is isolated, testable, and swappable:

```cpp
struct Effect {
    virtual void prepare (const juce::dsp::ProcessSpec&) = 0;
    virtual void reset() = 0;
    virtual int  latencySamples() const { return 0; }
    // params: style index, clutch/intensity 0..1, k1..k3 (effect-specific), tone
    virtual void process (juce::AudioBuffer<float>&, const EffectParams&) = 0;
    virtual ~Effect() = default;
};
```

Implementations (each its own `.h/.cpp`, one clear purpose):
- `DriveEffect`   — wraps the existing `GearEngine` (already built, DC-fixed)
- `ReverbEffect`
- `DelayEffect`
- `PitchEffect`
- `FilterEffect`

**Processor** owns all five (all `prepare`d), routes audio to the selected one, and manages:
- **Latency reporting** — pitch (and any block-based effect) has latency; report the active effect's latency via `setLatencySamples()` on gear change (hosts re-sync PDC). Document that switching gears can cause a PDC change.
- **Click-free gear switching** — on gear change, do a short (~10–20 ms) **equal-power crossfade** between the outgoing and incoming effect's output. Time-based effects (reverb/delay) keep processing during the fade so their tails ring out naturally instead of cutting.
- **Global dry/wet mix + output** applied after the active effect.
- **License gate** unchanged (dry-bypass until activated); **demo gate** unchanged.

### Per-effect DSP approach

**1. DRIVE (warmer)** — evolve `GearEngine`:
- Raise oversampling **4× → 8×** to push aliasing above audibility (the "fizz").
- Soften curves: tape = soft sigmoid + gentle HF roll; tube = asymmetric even-harmonic, gentler knee; console = subtle; clip = band-limited hard clip; fuzz = the aggressive one. Keep the analytic DC-pedestal fix.
- Styles = the 5 flavors, chosen by the style selector (not the gear anymore).
- Clutch = drive amount.

**2. REVERB** — algorithmic. MVP: `juce::dsp::Reverb` (Freeverb) with the 4 styles as tuned parameter sets (roomSize/damping/width/pre-delay). Upgrade path: a hand-built FDN or Dattorro plate for Plate/Canyon quality. Params: Size, Decay, Tone(=damping), Mix. Clutch = size+decay macro. *Phase 2.*

**3. DELAY** — `juce::dsp::DelayLine` + feedback path with a tone filter. Tempo-synced to host (`getPlayHead()` BPM). Styles = sync divisions (Slap≈80 ms free, 1/8, dotted 1/8, 1/4+). Params: Time/Division, Feedback, Tone, Mix. Clutch = feedback (self-oscillation as it approaches 1). *Phase 1.*

**4. PITCH** — reuse **Signalsmith Stretch** (MIT, already vetted in Dispenser) for real-time pitch shift. Styles: −12, +7, ±detune (cents), +12→feedback shimmer. Params: Interval, Detune, Mix. Has inherent latency → report it. *Phase 2 (hardest).*

**5. FILTER/EQ** — `juce::dsp::StateVariableTPTFilter` + a tilt EQ. Styles: Telephone (BP, narrow), LP Sweep (resonant LP), Tilt (shelf pair), Reso (resonant peak). Params: Freq, Reso, Tone, Mix. Clutch = the sweep (freq). *Phase 1.*

---

## 4. UI changes (WebView)

- **Knob row relabels** dynamically from a per-gear label/mapping table (JS already owns the knob row; add a `KNOB_MAPS[gear]` table and re-render on gear change).
- **Style selector** — new 4-segment control in the display module, styled to match; drives the active gear's `*_style` param via a relay.
- **Tach readout** — per-effect value + unit (dB / s / ms / semitones / Hz).
- **Clutch** knob stays; its label can read the effect's intensity name.
- Everything else (gate physics, presets, UI scale, brand neon) unchanged.

---

## 5. Phased build

**Phase 1 — achievable, high-impact (ship as v2.0):**
1. `Effect` interface + processor refactor (route + crossfade + per-effect params)
2. DRIVE warmer + 8× + styles-in-gear
3. FILTER/EQ (easy, high value)
4. DELAY (tempo-synced)
5. UI: knob remap + style selector + tach readouts
6. Idle-noise + null regression harness for each effect

**Phase 2 — the hard DSP (v2.1):**
7. REVERB (Freeverb MVP → FDN/plate)
8. PITCH (Signalsmith Stretch, latency-reported)

Rationale: pitch + reverb are the two hardest engines; shipping Drive/Filter/Delay first delivers a real multi-effect while those bake.

---

## 6. Testing

- Extend the `BUILD_IDLE_TEST` harness: **every effect must output true silence on silence in** (the v1 DC lesson) — assert < −140 dBFS idle for all gears/styles.
- **Null test** per effect at Mix=0 (bypass must be bit-transparent or within dither).
- **auval + pluginval** each build.
- **Latency assertion** — reported latency matches measured (impulse in, find peak).
- Tempo-sync test for Delay (divisions land on the grid at several BPMs).

---

## 7. Risks / open questions

- **PDC on gear switch:** changing to/from Pitch changes plugin latency mid-session. Some hosts handle this gracefully, some glitch. Mitigation: option to report a **fixed max latency** always (padding non-pitch effects) so latency never changes — costs a little delay on all effects but rock-solid. **Decision needed.**
- **Pitch quality vs CPU:** Signalsmith is good but pitch shifting always has artifacts on some material. Accept for v2.1, iterate.
- **Reverb quality:** Freeverb is a fine MVP but won't wow on Plate/Canyon; FDN is the upgrade if reviews ask.
- **Style count:** 4 styles × 5 effects = 20 sub-algorithms + presets — sizable content/tuning load. YAGNI check: could ship 2–3 styles per effect at launch, add more later.
- **Marketing:** V2 is a different product (multi-fx, not just saturation) — repositioning + new demo material.

---

## 8. Non-goals (v2)

- Parallel/simultaneous effects (rack mode) — explicitly a selector; revisit if demand.
- Modulation matrix, LFOs beyond what an effect needs internally.
- Per-effect independent wet chains.
