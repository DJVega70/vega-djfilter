# Vega Productions DJ Filter (VP-13)

One-knob DJ filter plus performance FX for builds, drops and transitions.

![screenshot](docs/screenshot.png)

## Download
- **Windows (FL Studio):** https://github.com/DJVega70/vega-djfilter/releases/latest/download/VegaProductions-DJFilter-Windows-VST3.zip
- **macOS:** https://github.com/DJVega70/vega-djfilter/releases/latest/download/VegaProductions-DJFilter-macOS-VST3-AU.zip

## Features
- **The big knob:** turn left for low-pass, right for high-pass, centre is a perfect bypass. Resonance, 12/24 dB slope, Drive, tempo-synced LFO wobble and a Riser Noise layer for build-ups.
- **Beat Repeat** — loops the last 1/32 to 1 bar instantly (seamless crossfaded loop).
- **Gate** — tempo-synced trance chop with depth.
- **Echo Out** — the dry fades out and tempo-synced echoes ring out: the classic DJ exit.
- **Stop** — Vinyl Stop, Spinback or Tape Stop with adjustable time.
- Pads latch (or switch to **HOLD TO PLAY**), and every pad is a parameter so you can automate it in FL Studio's playlist or map it to a controller.

## Building
```
cmake -B build -DVEGA_TESTS=ON
cmake --build build --config Release --target VegaDJFilter_VST3 VegaDJFilterTest
```
