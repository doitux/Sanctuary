# Sanctuary

**A computed worship pad and drone – no samples.**
Free and open source (GPLv3), for Linux, Windows and macOS. VST3 · CLAP · LV2 · standalone (AU on macOS).

Sanctuary is made for the keyboarder in a worship band: hold a drone in the song's key while you play
piano, or play warm, wide pads with the sustain pedal and swell them with the expression pedal.
Every sound is computed in real time from band-limited oscillators, tuned noise and a large hall –
the plugin is a few megabytes and loads instantly.

> Status: **0.2.0** – softer, rounder top (tuned to sit under a piano), bell-like *Sparkle*; engine, plugin, 8 factory presets. Sister project of
> [OpenSection](https://github.com/doitux/OpenSection) (computed string section).

## Two modes

**Drone.** Any key selects the drone *key* by its pitch class – the octave you press does not matter,
so a C anywhere on the keyboard gives a C drone. Sanctuary plays root, fifth and octave (plus an
optional *Colour*: **Sus2** adds the 9th in the middle of the chord, **Add9** adds it an octave
higher, softly) in a fixed comfortable register between E2 and D#3. Pressing another key crossfades
to the new key over *Crossfade* seconds (1–10 s, default 4) – equal power, no gap, no click. Pressing
the current key again fades the drone out. The 12 key buttons in the window do the same with the
mouse and show the active key. All-notes-off / panic fades everything out within 0.3 s.

**Play.** A polyphonic pad: voices follow your notes, the sustain pedal holds them, attack and
release are long and soft. Re-striking a held note starts a new voice while the old one releases
underneath (no cut-off).

The **expression pedal** (CC11 by default; CC1, CC2 or off selectable) swells the whole instrument:
pedal fully up is silent. It acts before the hall, so the room keeps ringing naturally when you pull
the pedal back. With the source set to *Off*, the *Swell* knob does the same job.

## How it works

Each note (a *voice*) has four layers, each with its own level knob:

| layer | what it is |
|---|---|
| **Warm** | five wavetable oscillators (triangle ↔ saw, *Shape*), detuned by up to 25 cents (*Detune*) and drifting slowly at random (*Motion*). The middle oscillator stays in tune and carries the fundamental; the four outer ones have a weakened fundamental and spread across the stereo field. Equal detuned oscillators beat like random phasors and make a pad swell and dip by itself (≈ 10 dB); with this "anchored ensemble" the level stays within ± 0.6 dB while the upper harmonics still chorus. A 24 dB/oct low-pass (*Brightness*, 300 Hz – 6 kHz) moves slowly with *Motion*. |
| **Air** | white noise through two comb filters tuned to the note (left and right with independent noise): breath that sits exactly on the harmonics. The noise is darkened before the combs and the band is rounded off with a 24 dB/oct low-pass at 1–2.6 kHz: air without hiss. |
| **Shimmer** | sine partials an octave, octave + fifth, two octaves and two octaves + fifth above each note. Each one blooms and fades on its own slow cycle (4–17 s). Only the amplitude moves – never the pitch – so nothing wobbles or chirps. Partials fade out between 1.2 and 3.6 kHz (glassy, never hissy). With *Sparkle* the partials also light up one by one like small bells – a quick 35 ms rise, a 0.5–2 s fade – at a rate that follows *Motion*. |
| **Foundation** | an organ-like fundamental (harmonics 1–4) plus a sine an octave below for low notes (never below 30 Hz), centred, phase-locked to the Warm layer so the two add and never beat. |

After the layers: expression → dry signal + a **16-line feedback delay network hall** (decay 1.5 to 14 s,
slowly modulated so it never rings metallically) with an optional **octave-up feedback** inside the
hall (*Shimmer* in the HALL group: a two-tap pitch shifter with 100 ms constant-power windows,
band-limited before shifting) → volume → DC blocker → a soft safety limiter that only acts above
−2 dBFS (it never engaged in any of the tests below).

All oscillators are band-limited: wavetables are built per octave by additive synthesis
(harmonics up to 15 kHz, Lanczos-smoothed), so there is no aliasing. Control values are updated
every ≤ 32 samples with linear ramps; parameters are smoothed. **No added latency.**

### Measured (48 kHz, 128-sample buffer, default preset "Warm Worship", volume −6 dB)

Measured with `sanctuary-render` and `tools/analyze/analyze.py` on a shared 2-core VM.

| check | result |
|---|---|
| CPU, drone D → G (crossfade) → off | 1.7–2 % of one core |
| CPU, play mode, 6-note chords with sustain pedal | 2.7–3.3 % |
| CPU, play mode stress (up to 18 overlapping voices) | 2.7–3.8 % |
| CPU when silent | 0 (idle bypass once the tail has died away) |
| peak level, drone / 6-note chords with pedal | −5.5 dBFS / −3.1 dBFS (all presets: ≤ −1.9 dBFS) |
| DC offset | below −160 dBFS |
| aliasing (pure Warm layer, saw, brightness fully open, C5–C8) | non-harmonic energy ≤ −108 dB |
| energy above 12 kHz (full mix incl. air and hall) | −43 … −46 dB relative to the total |
| crossfade D → G, 4 s | no dip (0.0 dB), ≤ +1.9 dB bump; with Sus2/Add9 ≤ 2.1 dB dip |
| clicks (linear-prediction residual detector, calibrated: clean 6–9.5, a −40 dBFS step reads 20–27) | 6–10 in all scenarios and presets |
| expression pedal fully up | exact silence |
| stereo correlation L/R (default / Air & Light) | 0.8 / 0.2 |

## Controls

| group | knob | range |
|---|---|---|
| top | Preset · Mode (Drone / Play) · Colour (Open / Sus2 / Add9) · Expression pedal (CC11 / CC1 / CC2 / Off) | |
| LAYERS | Warm · Air · Shimmer · Foundation | 0–100 % |
| TONE | Shape (triangle → saw) · Detune (0–25 cents) · Brightness (350 Hz – 9 kHz) · Motion · Width | 0–100 % |
| ENVELOPE | Attack (20 ms – 8 s) · Release (0.1 – 12 s) · Crossfade (1 – 10 s, drone) | |
| HALL | Size (1.5 – 14 s) · Mix · Shimmer (octave-up feedback) | 0–100 % |
| OUTPUT | Swell (expression when the pedal source is Off) · Volume (−30 … +6 dB, default −6 dB) | |

The header always shows the version, the mode and active drone key, the number of voices, the CPU
load and the pedal position.

## Presets

Factory presets set the sound (layers, tone, envelope, hall) and leave the performance settings
alone – mode, colour, crossfade, pedal source and volume stay as you set them. They are also exposed
as host programs.

| preset | character |
|---|---|
| **Warm Worship** | the all-rounder: warm, round ensemble, a breath of air, a few glassy lights, medium hall |
| **Shimmer Heaven** | octaves and fifths bloom and light up like small bells above a soft bed, octave-up hall, very wide |
| **Air & Light** | breath tuned to the chord, very little body, widest |
| **Deep Drone** | dark, slow and grounded: organ fundamental and sub, little top |
| **Felt Ambient** | soft triangle ensemble, intimate room, gentle air |
| **Soft Strings Pad** | rounded ensemble with wide detune, like a quiet synth string section |
| **Dark Prayer** | low and closed, long cathedral tail |
| **Bright Morning** | open and present, still round on top: lighter ensemble, airy, glittering lights |

## Building

```bash
git clone --recursive https://github.com/doitux/Sanctuary
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Sanctuary_VST3 Sanctuary_CLAP Sanctuary_LV2 Sanctuary_Standalone sanctuary-render
```

Linux needs `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev
libxext-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`. JUCE 8 and clap-juce-extensions are
git submodules. `-DSANCTUARY_PLUGIN=OFF` builds only the engine and the command-line tools (seconds,
no JUCE).

### Command-line renderer and benchmark

```bash
./build/sanctuary-render notes.txt out.wav --mode drone --preset "Deep Drone"
./build/sanctuary-render --bench tools/testdata/bench-play.txt --mode play
./build/sanctuary-render --list-presets
python3 tools/analyze/analyze.py stats out.wav --xfade 12.5,16.5     # peak, DC, HF, width, clicks, crossfade dip
bash tools/demo/make-demos.sh                                          # one MP3 per preset + Sanctuary-Demo.mp3
```

`notes.txt`: one line per note `start_s end_s midi velocity(0..1)`, plus `cc time_s num value(0..1)`
(64 = sustain, 123/120 = all notes off, the expression CC set with `--expr-cc`, default 11) and
`key time_s pitch-class` (a click on a drone key button). Options: `--mode drone|play`,
`--preset name|index`, `--colour open|sus2|add9`, `--crossfade s`, `--set name=value` for any
parameter, `--sr`, `--block`, `--seed`, `--tail`.

`bash packaging/linux/package.sh` builds `dist/Sanctuary-<version>-linux-x86_64.zip` from a finished build.

`sanctuary-claphost` (Linux, `--target sanctuary-claphost`) loads the built `.clap` like a host and
renders a notes file through it.

## Installing (Linux)

Download `Sanctuary-<version>-linux-x86_64.zip` (or the GitHub Actions artifact), unpack and run
`bash Sanctuary/install.sh`. It installs VST3, CLAP and LV2 into your home folder (`~/.vst3`,
`~/.clap`, `~/.lv2`) plus the standalone app (`~/.local/bin/Sanctuary`) and checks that all libraries
are present. `bash install.sh --remove` uninstalls. In Reaper: Options → Preferences → Plug-ins →
VST/CLAP → Re-scan, then add the FX "Sanctuary".

Windows and macOS builds are produced by CI (VST3, CLAP, standalone; AU on macOS) – testers welcome.

## Credits

The hall (`engine/src/Hall.h`), the FFT used to build the wavetables (`engine/src/Fft.h`) and the CLAP
test host come from OpenSection by the same author (GPL-3.0-or-later). Built with
[JUCE](https://juce.com) and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions).
Details in [CREDITS.md](CREDITS.md).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
