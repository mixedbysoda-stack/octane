# OCTANE — Saturation Gearbox

Five saturation circuits on a stick shifter. Throw the gear, morph between circuits with the clutch, slam into R for a clean A/B.

**Carbonated Audio** · https://carbonatedaudio.com/octane

| Gear | Circuit |
|------|---------|
| 1 | Tape — soft, rounded, glue |
| 2 | Tube — warm even harmonics |
| 3 | Console — punchy, forward |
| 4 | Clip — biting odd harmonics |
| 5 | Fuzz — redline chaos |
| R | Clean — instant dry A/B |

**CLUTCH** interpolates the actual circuit coefficients between adjacent gears — gear 2.4 is a real tube→console hybrid, not a crossfade.

## Build (macOS)

```bash
# JUCE 8.0.12 expected at ./JUCE (CI clones it; locally copy or clone)
cmake -B build -G Xcode -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release
```

Formats: VST3 / AU / AAX / Standalone, universal (arm64 + x86_64).
AAX needs the SDK at `~/Documents/aax-sdk-2-9-0`.

Demo build (60s play / 10s mute, no activation):

```bash
cmake -B build-demo -DBUILD_DEMO=ON && cmake --build build-demo --config Release
```

Windows builds run in GitHub Actions (`.github/workflows/build-windows.yml`) — tag `v*` for paid, `v*-demo` for demo.

## Ship process

Follow the Carbonated Audio Shipping Playbook (vault). Key steps: sign VST3/AU → notarize → AAX pre-sign → PACE wrap → re-sign → notarize → `installer/build-installer.sh` → productsign + notarize PKG.

Licensing: HMAC keys via `activate-octane` Netlify function, 3 machines/key, offline after first activation.
