# SDRangel Differential QPSK Symbol Demodulator

A native, receive-only SDRangel channel plugin for recovering differential-QPSK phase symbols, recording provisional channel bits, and investigating signals without retaining wideband I/Q.

The DSP, constellation display, bounded recorder and control endpoint all run inside SDRangel. The plugin does not identify a protocol, validate application messages or decode speech.

This repository contains source, documentation, and synthetic test logic. Captures, decoded research data, machine-specific build files, and personal configuration are kept separately. See [development practices](CONTRIBUTING.md).

## Modes

| Mode | Initial settings | Output |
| --- | --- | --- |
| pi/4-DQPSK | 9,600 symbols/s; RRC roll-off 1 | Four differential phase states at ±45° and ±135° |
| DQPSK | Adjustable 1,000–12,000 symbols/s | Four differential states with a zero-degree reference |
| H-DQPSK symbol mode | 6,000 symbols/s; pi/4 states; channel filter only | P25 Phase II physical-symbol model and Gray dibit map |

H-DQPSK is a pi/4-DQPSK-family modulation used on the P25 Phase II base-station downlink. The mode includes the 6,000-symbol/s preset and phase-to-dibit assignment; **TDMA synchronization, descrambling, FEC, MAC messages and voice decoding are separate work**. Its receiver pulse filter can be switched between channel-filter-only and adjustable RRC. This is an investigation receiver, not a transmitter-conformance implementation of the complete H-DQPSK pulse specification.

The symbol rate, receive pulse filter, carrier correction and dibit map are editable. Carrier-reference ambiguity remains possible until framing validates the assignment. Exported bits are therefore explicitly labelled provisional, including in H-DQPSK mode.

## Use

1. Install the matching DLL and restart SDRangel.
2. Add **Differential QPSK Symbol Demodulator** to a receive device set. Its channel ID is `DQPSKDemod`.
3. Keep the signal in the receive span. The device's decimated baseband must be at least **50 kS/s**. Set the channel offset, symbol rate, pulse filter and coarse carrier correction.
4. Watch the differential-phase display and quality values. Lock requires three populated states, a concentrated fourth moment, low phase error, an open symbol eye and sufficient relative power. Silence, a CW carrier and noise should not produce locked data.
5. Choose a directory and a finite duration, then select **Capture symbols**. The default duration is 30 seconds and the default byte ceiling is 64 MiB. The API permits at most 20 minutes and 256 MiB per capture. Limits stop recording; they do not start another file automatically.

Capture files are JSONL. At 9,600 symbols/s, a typical locked recording with phase-quality samples occupies about **20 KB/s**, compared with approximately 4 MB/s for the 500 kS/s native I/Q used during development. Actual size varies with JSON metadata and lock state. There is no automatic I/Q recording.

Unlocked windows become gap records. Input-queue overruns reset the DSP and start a new segment; missing samples are not silently joined. The processing queue is bounded. Recording is off after adding/restoring a channel and stops when the receiver stops.

## Smaller files for further analysis

Export a symbol capture with Python's standard library:

```powershell
python tools/export_symbols.py capture.jsonl exported-new-directory
```

Each continuous part produces:

- `part-NNN.symbols.u8`: one physical phase-state label (0–3) per byte.
- `part-NNN.bits.bin`: provisional mapped bits, most-significant bit first. At 9,600 symbols/s this is about **2.4 KB/s**.
- `manifest.json`: settings, timing, valid bit count, padding and SHA-256 hashes.

The exporter splits at gaps/discontinuities and removes per-record packing padding before assembling a continuous part. It never overwrites an existing exported part. See [the file-format documentation](docs/file-format.md).

## Native control API

SDRangel's normal REST API can list and create this channel:

```http
POST http://127.0.0.1:8091/sdrangel/deviceset/0/channel
Content-Type: application/json

{"direction":0,"channelType":"DQPSKDemod"}
```

The installed SDRangel core's generated Swagger models do not contain this third-party channel's settings/report schema. Consequently, the plugin supplies its own **loopback-only endpoint inside SDRangel** at `127.0.0.1:8093`:

| Request | Purpose |
| --- | --- |
| `GET /channels` | Enumerate channels, UIDs, settings and reports |
| `GET /channels/{uid}` | Read one channel |
| `PATCH /channels/{uid}/settings` | Change tuning/model/rate/filter/mapping |
| `POST /channels/{uid}/capture/start` | Start a bounded symbol recording |
| `POST /channels/{uid}/capture/stop` | Stop recording |
| `GET /channels/{uid}/panel.png` | Image of this channel's own native GUI panel |

For example, select H-DQPSK using `{"mode":"h-dqpsk"}`. This selects pi/4 states and, unless explicitly supplied, 6,000 symbols/s and channel-filter-only reception. To return to the development signal model, explicitly set `{"mode":"pi4-dqpsk","symbolRate":9600,"rrcEnabled":true,"rolloff":1}`.

Capture-start JSON accepts `seconds`, `maxBytes`, and an absolute `directory`. Ordinary settings cannot change during recording. Invalid settings are rejected rather than silently clamped. Browser-origin requests and oversized/chunked HTTP bodies are rejected. No transmit channel or transmitter control is exposed.

The native settings and GUI workspace survive normal SDRangel configuration/preset serialization. The stock MCP's generic settings/report tools cannot access an unregistered Swagger model; use this local endpoint for the extra channel controls. See [API settings](docs/api.md).

## Build on Windows

The initial DLL targets **SDRangel 8.2.0, 64-bit MSVC, Qt 6, 24-bit receive samples**. C++ plugin interfaces are version-sensitive: use the matching SDRangel source and sample representation. A Qt 6.8 SDK-built plugin was tested against the installed Qt 6.11.3 runtime.

Requirements: Visual Studio C++ build tools, CMake, Ninja or NMake, Qt 6.8+ development files (`qtbase`), and the matching [SDRangel source](https://github.com/f4exb/sdrangel/tree/v8.2.0).

In a Visual Studio x64 developer shell:

```powershell
powershell -File tools/make-imports.ps1 -SDRangel 'C:\Program Files\SDRangel' -Output "$PWD\build\imports"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='C:/Qt/6.8.3/msvc2022_64' `
  -DSDRANGEL_SOURCE_DIR='C:/src/sdrangel' `
  -DSDRANGEL_IMPORT_DIR="$PWD/build/imports"
cmake --build build
```

The import-library helper reads the installed SDRangel DLL exports. It does not modify SDRangel. For a DSP-only portable build, pass `-DBUILD_SDRANGEL_PLUGIN=OFF`; that target does not require Qt or SDRangel.

## Install

For **Windows x64 / SDRangel 8.2.0 / 24-bit receive builds**, download the compiled DLL from the [v0.1.0 release](https://github.com/jongentsch/sdrangel-dqpsk-demod/releases/tag/v0.1.0). See [binary installation and compatibility](docs/install-binary.md). Python and build tools are not required for the installed plugin.

With SDRangel closed, copy `build/demoddqpsk.dll` into the matching installation's `plugins` directory. Writing to `Program Files` normally requires administrator rights.

For a separate user-local runtime, run:

```powershell
powershell -File tools/install-user.ps1 -CreateShortcut
```

This creates `%LOCALAPPDATA%/SDRangel-DQPSK` and a **SDRangel with Symbol Demodulator** Start-menu entry. It links or copies runtime files from the existing installation and adds the plugin. Directory junctions keep shared resource directories available; copies are used if Windows denies hard links. Development PDBs and the original uninstaller are omitted. The original installation is preserved. Both runtimes use the same SDRangel user settings, so run one at a time. Recreate/update the user-local runtime coherently when upgrading SDRangel; do not mix plugin/core versions.

To remove the extension from this runtime, close SDRangel and remove only `plugins/demoddqpsk.dll`, or return to the original executable/shortcut. Preserve desired configurations first.

## Validation and scope

See [validation results](docs/validation.md). Tests cover synthetic pi/4-DQPSK, a 6,000-symbol/s H-DQPSK phase/raised-cosine reference, noise/CW rejection, chunk-boundary invariance, saved-I/Q agreement, native channelization, capture limits and GUI creation/destruction.

Acquisition currently uses one-second buffered timing/carrier estimates. It is intended for persistent signals with modest clock/carrier error. Short bursts, large frequency errors, strong multipath and standards-specific equalization need further work. The matched-filter power value is relative, not calibrated dBm. Symbols near the end of a stopped stream can remain in the bounded processing buffer and are not fabricated as a partial block.

A lock is evidence that the selected sampling model produces concentrated differential states. It does **not** independently prove the symbol rate: subharmonic sampling can also produce a lock on some waveforms. Compare the spectral clock, alternative rates and validated protocol framing before identifying a signal.

References:

- [VIAVI: Understanding and Testing P25 Phase 2 TDMA](https://www.viavisolutions.com/en-us/literature/understanding-and-testing-p25-phase-2-tdma-discontinued-application-notes-en.pdf): H-DQPSK phase mapping and downlink role.
- [Keysight custom I/Q modulation presets](https://helpfiles.keysight.com/csg/n7608/Content/Main/Custom_IQ_ASK_PSK_QAM.htm): pi/4-DQPSK at 6 ksps for H-DQPSK and its distinct pulse-filter model.
- [SDRangel Windows build guide](https://github.com/f4exb/sdrangel/wiki/Compile-in-Windows).

Licensed GPL-3.0-or-later. SDRangel remains a separate upstream project. No captured RF payloads, user configuration, runtime dependencies or credentials are included in this repository.
