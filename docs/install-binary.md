# Install the Windows DLL

Download the ZIP or standalone `demoddqpsk.dll` from the
[v0.1.0 release](https://github.com/jongentsch/sdrangel-dqpsk-demod/releases/tag/v0.1.0).
The ZIP includes the DLL, license, install instructions, build information and
binary audit report. Neither compilation nor Python is needed to use the plugin.

## Compatibility

- Windows x64; **SDRangel 8.2.0**, MSVC build, **24-bit receive samples**.
- Built with Qt 6.8.3; load and GUI checks used the SDRangel Qt 6.11.3 runtime.
- The plugin uses the Qt, SDRangel and Visual C++ runtime DLLs supplied with the
  matching SDRangel installation. Those dependencies are not bundled here.
- Other SDRangel versions, Qt major versions, 16-bit receive builds, Linux and
  macOS require a matching source build. This is not a universal plugin binary.

## Install

1. Close SDRangel.
2. Extract `demoddqpsk.dll` from the ZIP, or use the standalone download.
3. Copy it into the `plugins` folder beside your matching SDRangel executable.
   A standard installation uses `C:\Program Files\SDRangel\plugins`; Windows may
   request administrator permission for that folder. Preserve a previous plugin
   DLL separately if replacing one. No Qt or SDRangel core DLLs need replacement.
4. Launch SDRangel and add **Differential QPSK Symbol Demodulator** to a receive
   device set. Its channel ID is `DQPSKDemod`.

If the channel does not appear, check the application log, plugin/core version,
architecture, receive-sample representation and runtime dependencies. Restart
after copying the DLL. Remove this one DLL with SDRangel closed to uninstall it.

The project also has an optional [user-local installation helper](../tools/install-user.ps1)
for users who prefer a separate runtime copied/linked from an existing SDRangel
installation. The manual DLL installation above does not require that script.

## Verify the download

`SHA256SUMS.txt` on the release lists hashes for both downloads. On Windows:

```powershell
Get-FileHash .\demoddqpsk.dll -Algorithm SHA256
```

The binary is unsigned. The audit report records its exact SHA-256, PE architecture,
imports, debug-metadata checks and embedded-string inspection. PDBs, build paths,
private settings, hardware identifiers and recordings are not release assets.
The public project URL is intentionally embedded in the plugin metadata.

For another inspection using Python's standard library:

```powershell
python tools/audit_binary.py demoddqpsk.dll
```

Builders can additionally pass `--denylist private-values.json` with a private
JSON list of names, serials or other strings to reject. Do not commit that file.

## Use and scope

Keep the device's decimated baseband at least 50 kS/s, place the channel over the
signal, then select the modulation model, symbol rate and pulse filter. The
H-DQPSK preset is physical-symbol recovery at 6,000 symbols/s; P25 TDMA framing,
FEC, application interpretation and speech decoding are not implemented here.
Symbol recording is off until explicitly started and is bounded by duration and
size. The plugin's control API binds only to `127.0.0.1:8093`.

See the [README](../README.md) and [API documentation](api.md) for details.
Source corresponding to this binary is in the `v0.1.0` tag under GPL-3.0-or-later.
