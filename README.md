# RobonioGSMV2 (Arduino Library)

RobonioGSMV2 is an Arduino library for Robonio GSM Shield (SIM800C-class AT commands).

## Features
- Line-based URC parser (`RING`, `+CLIP`, `+CMTI`, `NO CARRIER`, etc.)
- Keepalive + automatic recovery (re-init on repeated `AT` failures)
- SMS helpers: read by index, send, delete, delete-all (with fallback methods)
- Caller ID parsing (CLIP)
- Auto-answer support via `ATS0` + Arduino fallback answer
- Optional auto-purge to prevent SMS storage from filling up

## Credits / Reference (Upstream)
This project is a clean rewrite inspired by:
- https://github.com/RobonioDev/Robonio-GSM-shield-library

RobonioGSMV2 is not an official RobonioDev release.

## Installation
Arduino IDE:
- Sketch → Include Library → Add .ZIP Library... (select the ZIP)

If ZIP install fails, extract the ZIP and copy the `RobonioGSMV2` folder into your Arduino `libraries` directory.

## Quick Start
```cpp
#include <RobonioGSMV2.h>

RobonioGSMV2 gsm; // default: SoftwareSerial RX=10 TX=11

void setup() {
  Serial.begin(115200);
  gsm.begin(9600);
  gsm.init();
  gsm.setAutoAnswerRings(1);   // answer after 1 ring
}

void loop() {
  gsm.update(); // call frequently
}
```

## API (Minimal)
- `begin(baud)` / `attachStream(Stream&)`
- `init()` (applies stability settings: `ATE0`, `AT+CSCLK=0`, `AT&D0`, `AT+CLIP=1`, SMS config)
- `update()` (parse URCs, keepalive, optional auto-purge)

### Calls
- `isRinging()`, `takeRing()`, `lastCallerNumber()`
- `answerCall()`, `hangup()`
- `callActive()`, `callEnded()`
- `setAutoAnswerRings(rings)`

### SMS
- `availableSMS()`, `pendingSMSIndex()`
- `readSMS(index, out)`
- `fetchPendingSMS(out, deleteAfter=true)`
- `sendSMS(number, text)`
- `deleteSMS(index)`, `deleteAllSMS()`
- `setAutoPurge(enabled, thresholdPercent, checkIntervalMs)`

### Audio
- `setSpeakerVolume(0..100)`
- `setRingerVolume(0..100)`
- `setMicGain(0..15)`
- `setSidetone(enable, 0..15)`

## Notes
- Call `update()` frequently (avoid long `delay()`), especially when using `SoftwareSerial`.
- Audio wiring depends on your shield (external speaker may be required).

## License
MIT (see `LICENSE`).
