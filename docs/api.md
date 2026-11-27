# Native loopback control

Base URL: `http://127.0.0.1:8093`. This server is owned by the plugin in SDRangel. It enumerates all plugin channels. The ordinary SDRangel API still handles adding/removing channels, devices and workspaces.

`GET /channels` returns `{plugin, version, receiveOnly, channels}`. Each channel entry has `settings` and `report`; channel UIDs are decimal strings. `GET /channels/{uid}` reads one.

`PATCH /channels/{uid}/settings` accepts a partial object:

| Setting | Type / range |
| --- | --- |
| `inputFrequencyOffset` | Hz relative to hardware center; must fit inside the current receive span |
| `mode` | `pi4-dqpsk`, `dqpsk`, `h-dqpsk` |
| `symbolRate` | 1,000–12,000 symbols/s |
| `pi4` | Phase reference; true for pi/4 and H-DQPSK, false for DQPSK |
| `rrcEnabled` | Receive RRC on/off |
| `rolloff` | 0.05–1; applies when RRC is enabled |
| `carrierOffsetHz` | Coarse carrier correction, −8,000–8,000 Hz; occupied spectrum must remain inside the channel filter |
| `squelchDB` | Minimum matched-filter relative power, −160–0 dB |
| `dibitMap` | A permutation of integers 0,1,2,3 |
| `title` | Native channel title, at most 128 characters |
| `outputDirectory` | Default directory for captures |

Selecting `h-dqpsk` supplies 6,000 symbols/s, pi/4 reference and channel-filter-only reception unless the patch explicitly supplies rate/filter overrides. Switching back to an investigation model should explicitly set its rate and receive filter. This preserves experimental settings instead of silently imposing a protocol interpretation.

`POST /channels/{uid}/capture/start` accepts:

```json
{"seconds":30,"maxBytes":67108864,"directory":"C:/Radio/Symbols"}
```

Duration must be 1–1,200 seconds; maximum file size 16 KiB–256 MiB. The directory must be absolute. The receive device must be running at at least 50 kS/s baseband. `POST /channels/{uid}/capture/stop` takes `{}`. Reports expose running/recording state, capture path/bytes/stop reason, quality values, a bounded constellation preview, queued samples and cumulative queue-drop count.

Settings changes are rejected during capture. Unknown keys/types, invalid ranges or invalid mappings return an error. The endpoint binds IPv4 localhost, limits pending connections, bounds request bodies and times out incomplete requests. It has no transmit or arbitrary-command endpoint. If port 8093 is occupied, native GUI operation remains available; this version has no configurable control port.

`GET /channels/{uid}/panel.png` returns a PNG of that channel's own native Qt panel. It renders the registered widget on the GUI thread; it does not inspect or capture the desktop or other applications. This endpoint is available only while a GUI panel exists. Capture the panel during reception to preserve the live constellation and lock statistics.
