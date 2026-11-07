# SpectraShell

A harmonic resonator effect (VST3), made for FL Studio 20+ and any other VST3 host.

SpectraShell tunes a bank of resonators to the notes you pick. Run drums, noise,
vocals or pads through it and they start to ring in key. You can choose the notes
by hand or play them live over MIDI.

## How it works

Each enabled note drives one feedback comb resonator per stacked octave, so up
to 12 notes × 4 octaves = 48 voices per channel. Each resonator has a damping filter
inside its loop that sets the brightness. The tuning compensates for that filter's
phase delay, so notes stay within about 0.5 cent of pitch at any brightness.

Efficiency:
- A voice uses no CPU once its note is off and its tail has died away.
- Coefficient math runs once per 64-sample chunk. The per-sample work is a few
  multiply-adds plus a 4-point cubic interpolation.
- Measured (x86-64, 48 kHz, stereo): typical triad **≈0.3 %** of one core;
  worst case with all 48 voices **≈3.7 %**.
- Zero latency.

## Controls

| Control | What it does |
|---|---|
| **Keyboard** | Click keys to turn notes on or off. Keys light up with each note's resonance. A violet dot marks a note being held over MIDI. |
| **Root + Chord / Scale** | Sets the keyboard to a chord or scale in one step (triads, 7ths, modes, pentatonics, and more). |
| **Note Source** | `Manual` = the keyboard. `MIDI` = the notes you play. `Manual + MIDI` = both. MIDI notes are folded to pitch classes, so the Octave/Stack knobs set the register. |
| **Harmonics** | `Full` = every harmonic (bright, string-like). `Hollow` = odd harmonics only (woody, clarinet-like). |
| **Octave** | Register of the lowest resonance (3 = C3). |
| **Stack** | How many octaves are stacked above it (1–4). |
| **Tune** | Fine tuning, ±100 cents. |
| **Width** | Detunes left against right for stereo width. |
| **Decay** | Ring time while a note is on. |
| **Release** | Ring time after a note is switched off or released (never longer than Decay). |
| **Brightness** | Damping inside the resonators: dark/muted to glassy. |
| **Low Cut** | High-pass on the signal feeding the resonators, so low end doesn't muddy them. |
| **Mix / Output** | Dry/wet balance and output gain. A soft limiter guards the wet signal. |

## Getting the plugin

### Easiest: let GitHub build it
Every push to `main` runs `.github/workflows/build.yml`, which builds Windows and
macOS VST3s. Open the repo on GitHub → **Actions** → the latest **Build** run →
download **SpectraShell-Windows-VST3**.

### Build it yourself
You need CMake ≥ 3.22, Git, and a C++17 compiler (Visual Studio 2022 or newer on Windows,
Xcode on macOS). JUCE is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugin is created at `build/SpectraShell_artefacts/Release/VST3/SpectraShell.vst3`.

## Installing in FL Studio 20

1. Copy `SpectraShell.vst3` to `C:\Program Files\Common Files\VST3\`
   (macOS: `/Library/Audio/Plug-Ins/VST3/`).
2. In FL Studio: **Options → Manage plugins → Find plugins**.
3. Load **SpectraShell** on a mixer insert.

**Driving it with MIDI in FL Studio:** set *Note Source* to `MIDI`. Add a
**MIDI Out** channel and set its port number (e.g. 1). Then on SpectraShell, open
the wrapper settings (gear icon → *Processing*) and set *Input port* to the same
number. Notes you play or sequence on the MIDI Out channel now set which notes
resonate.

### Linux (Wine / Proton / Lutris)

Use the Windows VST3 and install it into the Wine prefix's
`drive_c/Program Files/Common Files/VST3/`. The build includes these Wine fixes:

- The plugin window uses JUCE's software renderer instead of Direct2D, which
  Wine can't display (the window would stay black). Real Windows still uses
  Direct2D.
- The UI uses its own embedded font (DejaVu Sans, `Resources/Fonts/`). Under
  Wine, JUCE 8 otherwise asks for "Bitstream Vera Sans", which most prefixes
  don't have, so no text renders.
- A JUCE 8.0.4 bug that crashed the host when text couldn't be shaped
  (access violation reading `FFFFFFFFFFFFFFE9`) is patched.

The JUCE patches are applied automatically (`cmake/PatchJuceWine.cmake`).

## Tests

The DSP engine (`Source/ResonatorEngine.h`) does not depend on JUCE. Its tests cover
tuning accuracy, stability under worst-case input, level, voice cleanup and CPU cost:

```bash
cmake -S . -B build-test -DSPECTRASHELL_PLUGIN=OFF -DSPECTRASHELL_BUILD_TESTS=ON
cmake --build build-test && ./build-test/engine_test
```

## License

Copyright (C) 2026 Spectra

SpectraShell is licensed under the GNU Affero General Public License, version 3
(AGPLv3). See [LICENSE](LICENSE) for the full text.

## License note

SpectraShell is built on [JUCE](https://juce.com) 8. JUCE is free for personal use
and small businesses under its Starter license, or available under AGPLv3. Check
its terms before you distribute the plugin.
