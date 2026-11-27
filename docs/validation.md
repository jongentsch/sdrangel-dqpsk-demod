# Validation

Development target: SDRangel 8.2.0, Windows x64 MSVC, Qt 6.11.3 runtime, 24-bit receive samples. Plugin compiled with a Qt 6.8.3 SDK. All synthetic signals are generated offline; no transmitter is used.

| Check | Observed result |
| --- | --- |
| Random pi/4-DQPSK with carrier offset and noise | 38,399 recovered symbols; 100% agreement with generated information |
| Same waveform fed in 4,096- versus 137-sample chunks | Identical decisions |
| Noise and CW controls | No locked output blocks |
| H-DQPSK physical-symbol reference: 6,000/s, pi/4 increments, raised-cosine pulse | 29,999 symbols; 100% agreement |
| Saved two-minute SDRangel/HackRF upper-band I/Q extraction | 1,200,000 symbols; zero disagreements with the independently established periodic template |
| Native DLL, raw SDRangel recording, DownChannelizer and rational resampler | 47,999 compared symbols; zero disagreements |
| Five-second native symbol recording | About 99 KB; stopped automatically |
| 32 KiB capture ceiling | About 20 KB written; stopped before exceeding the limit |
| Manual stop and stale-start regression | Stopped; no automatic reopening |
| Invalid dibit mapping | Rejected |
| Actual DeviceAPI ownership and repeated binding to the same owner | Registration must be idempotent; regression added after full-application testing exposed recursion |
| Native GUI | Created, rendered to an image without reading the desktop, and destroyed |
| Full SDRangel application, two simultaneous live receive channels | Twenty minutes; 11,520,002 phase decisions per channel; no lock gaps or observed input-queue drops |
| Saved configuration restored in the full application | Both channels and their settings restored; receiver idle and both recorders off |

The H-DQPSK reference validates symbol recovery for the documented phase family and rate. It does not certify the complete transmitter pulse shape, multipath performance, P25 TDMA synchronization, FEC, payload or speech decoding. Live signals still need protocol validation.

## Reproduce

The Python test harnesses require NumPy and SciPy; the channel/API harness also uses Requests. The installed native plugin needs none of those Python packages.

```powershell
python tests/validate_core.py
```

Optionally supply local development recordings (not distributed):

```powershell
python tests/validate_core.py --recorded-npz channel.npz --template template.npy
python tests/validate_channel.py simultaneous.sdriq template.npy
```

The `.npz` contains complex64 `iq` at 50 kS/s. The native `.sdriq` test expects 24-bit samples in interleaved int32 containers, and a signal at +62.5 kHz from the recorded center. The template is a one-dimensional uint8 array of phase labels. The native test uses port 8093; close other plugin test instances first. Its helper runs for 16 seconds and opens no physical SDR device.

`dqpsk_gui_preview` can render the channel panel without showing its window or inspecting the user's desktop. It tests normal destruction while the GUI and channel are still valid.

Detailed local test reports and RF recordings are excluded from Git. The public implementation contains generated test logic, not intercepted payloads or receiver configuration.

## v0.1.0 binary release

The Windows x64 release DLL was rebuilt in Release mode, then checked with the
native plugin loader and the channel GUI creation/render/destruction harness.
The synthetic pi/4-DQPSK and 6,000-symbol/s reference tests, chunk-boundary test,
and noise/CW rejection checks passed. These release checks used no physical SDR.

`tools/audit_binary.py` checks printable ASCII and UTF-16 strings, private
builder-supplied denylist values, PE architecture/imports, CodeView debug records,
PDB references and COFF symbol tables. The release audit found no personal paths,
email addresses, machine names, credential patterns or supplied private values.
The public project URL remains intentional plugin metadata. The ZIP includes the
exact DLL hash and audit report; PDBs and private development files are omitted.

Six synthetic privacy-gate tests cover a clean PE, an allowed public URL, an ASCII
home path, a UTF-16 email address, a UTF-16BE denylist value, and CodeView metadata:

```powershell
python tests/test_binary_audit.py
```
