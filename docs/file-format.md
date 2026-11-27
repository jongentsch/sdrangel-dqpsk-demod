# Symbol recording format v1

UTF-8 JSONL; one object per line. This is a channel-symbol format, not an application protocol or a packet/CRC container.

The `header` record has `format: "sdrangel-dqpsk-symbols-v1"`, host UTC, channel UID, hardware center, baseband rate, 50 kS/s demodulation rate, current segment, settings and byte/time ceilings. Settings include the provisional four-entry `dibitMap`. Host UTC describes processing time; it is not calibrated RF arrival time.

Each `symbols` record contains:

| Field | Meaning |
| --- | --- |
| `segment`, `sequence` | DSP segment and block sequence; resets/discontinuities remain explicit |
| `firstSample` | Fractional 50 kS/s position of the first differential decision in this segment; causal filter delay is retained |
| `samplesPerSymbol` | Nominal 50,000 / configured symbol rate; block timing corrections appear in successive `firstSample` values |
| `count` | Valid decisions; packing padding is excluded |
| `phaseLabels2bitBase64` | Physical labels 0–3, four per byte: bits 7:6, 5:4, 3:2, 1:0 |
| `provisionalBitsBase64` | Same packing after applying `dibitMap`; two channel bits per decision, MSB first |
| `phaseErrorsInt8Base64` | One signed int8 per decision; multiply by 45/127 degrees for the quantized residual phase error |
| `carrierHzRelativeTuning` | Configured coarse correction plus local residual; ambiguous modulo symbol rate / 4 |
| `fourthMoment`, `phaseRMSDegrees`, `amplitudeCV` | Model-fit quality, not an RF BER or decoded checksum |

For pi/4 and H-DQPSK, phase label `k` means a differential phase near `pi/4 + k*pi/2`, modulo `2*pi`, after local residual-carrier correction. The default provisional Gray mapping is `[0,1,3,2]`. Ordinary DQPSK uses a zero-degree base. A protocol must resolve carrier-reference, polarity and framing ambiguities before the resulting bits acquire a validated meaning.

The final packed byte of each record can contain up to six zero padding bits. **Do not concatenate packed record bytes directly**: strip that padding first. The included exporter carries unfinished bytes across continuous records and emits exact valid-bit counts.

`gap` records mark unlocked windows or DSP discontinuities; unlocked windows contain no fabricated payload. A `segment` record identifies a reset. `end` gives the stop reason: time limit, byte limit, manual stop, receiver stop, write failure or channel destruction. Configuration changes are rejected during recording. Files are created with exclusive/new-only semantics.

In version 0.1.0, the end record's `symbolsTotal` is the channel's cumulative processed-decision counter, including unlocked decisions and processing before this capture. It is **not** the number of valid symbols saved in the file. Sum the `count` fields of `symbols` records, or use the export manifest, for the actual captured-symbol count.

No claim is made that a record is a radio frame, that two adjacent bits form a protocol dibit after all coding stages, or that eight serialized bits coincide with a protocol byte boundary.
