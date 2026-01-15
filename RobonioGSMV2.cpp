#include "RobonioGSMV2.h"

RobonioGSMV2::RobonioGSMV2(uint8_t rxPin, uint8_t txPin)
: _soft(rxPin, txPin) {
  _io = &_soft;
  _caller[0] = '\0';
}

void RobonioGSMV2::attachStream(Stream& io) {
  _io = &io;
}

bool RobonioGSMV2::begin(long baud) {
  _soft.begin(baud);
  _drainInput(100);
  return true;
}

bool RobonioGSMV2::init(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (!isResponsive(1200)) {
    if (millis() - start > timeoutMs) return false;
    delay(200);
  }

  // Stability settings (SIM800 AT manual):
  // - ATE0: echo off
  // - AT+CSCLK=0: disable sleep
  // - AT&D0: ignore DTR
  // - AT+CLIP=1: caller ID
  sendCommand("ATE0", "OK", 2000);
  sendCommand("AT+CSCLK=0", "OK", 2000);
  sendCommand("AT&D0", "OK", 2000);
  (void)enableCallerID(_callerIdEnabled, 2000);
  
  // SMS config
  sendCommand("AT+CMGF=1", "OK", 3000);                    // text mode
  sendCommand("AT+CSCS=\"GSM\"", "OK", 3000);            // charset
  sendCommand("AT+CPMS=\"SM\",\"SM\",\"SM\"", "OK", 4000); // prefer SIM storage
  sendCommand("AT+CNMI=2,1,0,0,0", "OK", 3000);            // new SMS indication (+CMTI)

  // Apply default audio
  setRingerVolume(_audio.ringerVolume, 2000);
  setSpeakerVolume(_audio.speakerVolume, 2000);
  setMicGain(_audio.micGain, 2000);
  setSidetone(_audio.sidetone, _audio.sidetoneGain, 2000);

  // Reset runtime state
  _ringFlag = false;
  _callActive = false;
  _callEnded = false;
  _caller[0] = '\0';
  _hasPendingSms = false;
  _pendingSmsIndex = 0;
  strncpy(_pendingSmsMem, "SM", sizeof(_pendingSmsMem));

  // Re-apply persistent user settings after any recovery init
  if (_autoAnswerRings > 0) (void)setAutoAnswerRings(_autoAnswerRings, 2000);

  _lastKeepAliveMs = millis();
  _keepAliveFails = 0;
  _lastPurgeCheckMs = millis();

  _drainInput(120);
  return true;
}

void RobonioGSMV2::update() {
  _processIncomingBytes();

  const uint32_t now = millis();

  // Keepalive: ping with "AT"
  if (_ka.intervalMs > 0 && (uint32_t)(now - _lastKeepAliveMs) >= _ka.intervalMs) {
    _lastKeepAliveMs = now;

    if (!_callActive) {
      bool ok = sendCommand("AT", "OK", _ka.atTimeoutMs);
      if (!ok) {
        _keepAliveFails++;
        if (_ka.autoRecover && _keepAliveFails >= _ka.maxFails) {
          (void)init(6000);
          _keepAliveFails = 0;
        }
      } else {
        _keepAliveFails = 0;
      }
    }
  }

  // Auto purge: prevent SMS storage from filling up
  if (_ap.enabled && !_callActive && (uint32_t)(now - _lastPurgeCheckMs) >= _ap.checkIntervalMs) {
    _lastPurgeCheckMs = now;
    uint16_t used = 0, total = 0;
    if (getSmsStorage(used, total, "SM", 4000) && total > 0) {
      uint32_t pct = (uint32_t)used * 100UL / (uint32_t)total;
      if (pct >= _ap.thresholdPercent) {
        (void)deleteAllSMS(15000);
        _hasPendingSms = false;
        _pendingSmsIndex = 0;
      }
    }
  }
}

bool RobonioGSMV2::sendCommand(const char* cmd,
                              const char* expectToken,
                              uint32_t timeoutMs,
                              bool appendCRLF) {
  if (!_io || !cmd) return false;

  _drainInput(30);

  _io->print(cmd);
  if (appendCRLF) _io->print("\r\n");

  if (!expectToken || expectToken[0] == '\0') return true;

  bool matchedA = false;
  return _waitForToken(expectToken, "ERROR", timeoutMs, matchedA) && matchedA;
}

bool RobonioGSMV2::isResponsive(uint32_t timeoutMs) {
  return sendCommand("AT", "OK", timeoutMs);
}

void RobonioGSMV2::setAudioDefaults(const RobonioGSMV2Audio& audio) { _audio = audio; }
void RobonioGSMV2::setKeepAlive(const RobonioGSMV2KeepAlive& ka) { _ka = ka; _lastKeepAliveMs = millis(); _keepAliveFails = 0; }
void RobonioGSMV2::setAutoPurge(const RobonioGSMV2AutoPurge& ap) { _ap = ap; _lastPurgeCheckMs = millis(); }

bool RobonioGSMV2::enableCallerID(bool on, uint32_t timeoutMs) {
  _callerIdEnabled = on;
  return sendCommand(on ? "AT+CLIP=1" : "AT+CLIP=0", "OK", timeoutMs);
}

bool RobonioGSMV2::setAutoAnswerRings(uint8_t rings, uint32_t timeoutMs) {
  _autoAnswerRings = rings;
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "ATS0=%u", (unsigned)rings);
  return sendCommand(cmd, "OK", timeoutMs);
}

bool RobonioGSMV2::answerCall(uint32_t timeoutMs) {
  if (!_io) return false;
  _callEnded = false;
  _io->print("ATA\r\n");
  bool matchedOk = false;
  bool ok = _waitForToken("OK", "ERROR", timeoutMs, matchedOk) && matchedOk;
  if (ok) {
    _ringFlag = false;
    _callActive = true;
  }
  return ok;
}

bool RobonioGSMV2::hangup(uint32_t timeoutMs) {
  if (!_io) return false;
  _io->print("ATH\r\n");
  bool matchedOk = false;
  bool ok = _waitForToken("OK", "ERROR", timeoutMs, matchedOk) && matchedOk;
  if (ok) {
    _callActive = false;
    _ringFlag = false;
    _callEnded = true;
  }
  return ok;
}

bool RobonioGSMV2::isRinging() const { return _ringFlag; }

bool RobonioGSMV2::takeRing() {
  if (_ringFlag) { _ringFlag = false; return true; }
  return false;
}

bool RobonioGSMV2::callActive() const { return _callActive; }

bool RobonioGSMV2::callEnded() {
  if (_callEnded) { _callEnded = false; return true; }
  return false;
}

const char* RobonioGSMV2::lastCallerNumber() const { return _caller; }

bool RobonioGSMV2::sendSMS(const char* number, const char* text, uint32_t timeoutMs) {
  if (!_io || !number || !text) return false;
  if (!sendCommand("AT+CMGF=1", "OK", 3000)) return false;

  _drainInput(30);
  _io->print("AT+CMGS=\"");
  _io->print(number);
  _io->print("\"\r\n");

  bool matchedPrompt = false;
  if (!(_waitForToken(">", "ERROR", 8000, matchedPrompt) && matchedPrompt)) return false;

  _io->print(text);
  _io->write('\r');
  _io->write((char)26);

  bool matchedA = false;
  if (!(_waitForToken("+CMGS:", "ERROR", timeoutMs, matchedA) && matchedA)) return false;

  _drainInput(200);
  return true;
}

bool RobonioGSMV2::availableSMS() const { return _hasPendingSms; }
uint16_t RobonioGSMV2::pendingSMSIndex() const { return _pendingSmsIndex; }

bool RobonioGSMV2::readSMS(uint16_t index, RobonioSMS& out, uint32_t timeoutMs) {
  if (!_io) return false;

  out.index = index;
  out.sender[0] = '\0';
  out.text[0] = '\0';

  char cmd[20];
  snprintf(cmd, sizeof(cmd), "AT+CMGR=%u", (unsigned)index);
  _drainInput(30);
  _io->print(cmd);
  _io->print("\r\n");

  const uint32_t start = millis();
  bool gotHeader = false;
  bool gotBody = false;

  char line[180];
  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (!_readLineBlocking(line, sizeof(line), 1200)) continue;
    _handleLine(line);

    if (strstr(line, "ERROR")) return false;

    if (!gotHeader && _startsWith(line, "+CMGR:")) {
      char num[24] = {0};
      if (_extractNthQuoted(line, 1, num, sizeof(num))) {
        strncpy(out.sender, num, sizeof(out.sender) - 1);
        out.sender[sizeof(out.sender) - 1] = '\0';
      }
      gotHeader = true;
      continue;
    }

    if (gotHeader && !gotBody) {
      if (line[0] == '\0') continue;
      strncpy(out.text, line, sizeof(out.text) - 1);
      out.text[sizeof(out.text) - 1] = '\0';
      _trim(out.text);
      gotBody = true;
      continue;
    }

    if (strcmp(line, "OK") == 0) break;
  }

  return gotHeader;
}

bool RobonioGSMV2::fetchPendingSMS(RobonioSMS& out, bool deleteAfter, uint32_t timeoutMs) {
  if (!_hasPendingSms || _pendingSmsIndex == 0) return false;

  uint16_t idx = _pendingSmsIndex;
    (void)_selectSmsMemory(_pendingSmsMem, 3000);
  bool ok = readSMS(idx, out, timeoutMs);

  _hasPendingSms = false;
  _pendingSmsIndex = 0;
  strncpy(_pendingSmsMem, "SM", sizeof(_pendingSmsMem));

  if (ok && deleteAfter) (void)deleteSMS(idx, 6000);
  return ok;
}

bool RobonioGSMV2::deleteSMS(uint16_t index, uint32_t timeoutMs) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "AT+CMGD=%u", (unsigned)index);
  return sendCommand(cmd, "OK", timeoutMs);
}

bool RobonioGSMV2::deleteAllSMS(uint32_t timeoutMs) {
  if (sendCommand("AT+CMGDA=\"DEL ALL\"", "OK", timeoutMs)) return true;
  return sendCommand("AT+CMGD=1,4", "OK", timeoutMs);
}

bool RobonioGSMV2::getSmsStorage(uint16_t& used, uint16_t& total, const char* mem, uint32_t timeoutMs) {
  used = 0; total = 0;
  if (!_io) return false;

  char cmd[32];
  if (mem && mem[0]) {
    snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"", mem, mem, mem);
    (void)sendCommand(cmd, "OK", timeoutMs);
  }

  _drainInput(20);
  _io->print("AT+CPMS?\r\n");

  const uint32_t start = millis();
  char line[180];
  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (!_readLineBlocking(line, sizeof(line), 1200)) continue;
    _handleLine(line);

    if (strstr(line, "ERROR")) return false;

    if (_startsWith(line, "+CPMS:")) {
      const char* p = strchr(line, ',');
      if (!p) continue;
      p++;
      used = (uint16_t)atoi(p);
      const char* p2 = strchr(p, ',');
      if (!p2) continue;
      p2++;
      total = (uint16_t)atoi(p2);
    }

    if (strcmp(line, "OK") == 0) break;
  }

  return total > 0;
}

bool RobonioGSMV2::setSpeakerVolume(uint8_t v, uint32_t timeoutMs) {
  if (v > 100) v = 100;
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "AT+CLVL=%u", (unsigned)v);
  return sendCommand(cmd, "OK", timeoutMs);
}

bool RobonioGSMV2::setRingerVolume(uint8_t v, uint32_t timeoutMs) {
  if (v > 100) v = 100;
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "AT+CRSL=%u", (unsigned)v);
  return sendCommand(cmd, "OK", timeoutMs);
}

bool RobonioGSMV2::setMicGain(uint8_t g, uint32_t timeoutMs) {
  if (g > 15) g = 15;
  char cmd[22];
  snprintf(cmd, sizeof(cmd), "AT+CMIC=0,%u", (unsigned)g);
  return sendCommand(cmd, "OK", timeoutMs);
}

bool RobonioGSMV2::setSidetone(bool enable, uint8_t g, uint32_t timeoutMs) {
  if (g > 15) g = 15;
  char cmd[26];
  snprintf(cmd, sizeof(cmd), "AT+SIDET=%u,%u", enable ? 1u : 0u, (unsigned)g);
  return sendCommand(cmd, "OK", timeoutMs);
}

void RobonioGSMV2::attachPowerKey(uint8_t pin, bool activeLow) {
  _hasPwrKey = true;
  _pwrKeyPin = pin;
  _pwrKeyActiveLow = activeLow;
  pinMode(_pwrKeyPin, OUTPUT);
  digitalWrite(_pwrKeyPin, _pwrKeyActiveLow ? HIGH : LOW);
}

bool RobonioGSMV2::powerToggle(uint16_t pressMs) {
  if (!_hasPwrKey || _pwrKeyPin == 255) return false;
  const uint8_t active = _pwrKeyActiveLow ? LOW : HIGH;
  const uint8_t idle   = _pwrKeyActiveLow ? HIGH : LOW;
  digitalWrite(_pwrKeyPin, active);
  delay(pressMs);
  digitalWrite(_pwrKeyPin, idle);
  delay(200);
  return true;
}

// ---------- Private ----------
void RobonioGSMV2::_processIncomingBytes() {
  if (!_io) return;
  while (_io->available()) {
    char c = (char)_io->read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (_lineLen == 0) continue;
      _lineBuf[_lineLen] = '\0';
      _trim(_lineBuf);
      _handleLine(_lineBuf);
      _lineLen = 0;
      continue;
    }
    _appendCharToLine(c);
  }
}

void RobonioGSMV2::_appendCharToLine(char c) {
  if (_lineLen < sizeof(_lineBuf) - 1) _lineBuf[_lineLen++] = c;
  else _lineLen = 0;
}

void RobonioGSMV2::_handleLine(const char* line) {
  if (!line || line[0] == '\0') return;

  if (strcmp(line, "RING") == 0) { _ringFlag = true; return; }

  if (_startsWith(line, "+CLIP:")) {
    char num[24] = {0};
    if (_extractFirstQuoted(line, num, sizeof(num))) {
      strncpy(_caller, num, sizeof(_caller) - 1);
      _caller[sizeof(_caller) - 1] = '\0';
    }
    return;
  }

  if (_startsWith(line, "+CMTI:")) {
    // +CMTI: "SM",3  or  +CMTI: "ME",12
    char mem[4] = {0};
    if (_extractFirstQuoted(line, mem, sizeof(mem))) {
      strncpy(_pendingSmsMem, mem, sizeof(_pendingSmsMem) - 1);
      _pendingSmsMem[sizeof(_pendingSmsMem) - 1] = ' ';
    }
    const char* comma = strchr(line, ',');
    if (comma) {
      uint16_t idx = (uint16_t)atoi(comma + 1);
      if (idx > 0) { _pendingSmsIndex = idx; _hasPendingSms = true; }
    }
    return;
  }

  if (strstr(line, "NO CARRIER") || strstr(line, "BUSY") || strstr(line, "NO ANSWER")) {
    _callActive = false;
    _ringFlag = false;
    _callEnded = true;
    return;
  }

  if (strcmp(line, "CONNECT") == 0) { _callActive = true; return; }
}

bool RobonioGSMV2::_readLineBlocking(char* out, size_t outSize, uint32_t timeoutMs) {
  if (!_io || !out || outSize < 2) return false;

  size_t len = 0;
  const uint32_t start = millis();

  while ((uint32_t)(millis() - start) < timeoutMs) {
    while (_io->available()) {
      char c = (char)_io->read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (len == 0) continue;
        out[len] = '\0';
        _trim(out);
        return true;
      }
      if (len < outSize - 1) out[len++] = c;
    }
    delay(2);
  }

  if (len > 0) {
    out[len] = '\0';
    _trim(out);
    return true;
  }
  return false;
}

void RobonioGSMV2::_drainInput(uint32_t ms) {
  if (!_io) return;
  const uint32_t start = millis();
  char line[180];
  while ((uint32_t)(millis() - start) < ms) {
    if (_readLineBlocking(line, sizeof(line), 20)) _handleLine(line);
    else delay(2);
  }
}

bool RobonioGSMV2::_waitForToken(const char* tokenA,
                                const char* tokenB,
                                uint32_t timeoutMs,
                                bool& matchedA) {
  matchedA = false;
  const uint32_t start = millis();
  char line[180];

  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (!_readLineBlocking(line, sizeof(line), 1200)) continue;
    _handleLine(line);
    if (tokenA && strstr(line, tokenA)) { matchedA = true; return true; }
    if (tokenB && strstr(line, tokenB)) { matchedA = false; return true; }
  }
  return false;
}

void RobonioGSMV2::_trim(char* s) {
  if (!s) return;
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
  if (i > 0) { size_t j = 0; while (s[i]) s[j++] = s[i++]; s[j] = '\0'; }

  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[n - 1] = '\0'; n--;
  }
}

bool RobonioGSMV2::_startsWith(const char* s, const char* prefix) {
  if (!s || !prefix) return false;
  while (*prefix) if (*s++ != *prefix++) return false;
  return true;
}

bool RobonioGSMV2::_extractFirstQuoted(const char* s, char* out, size_t outSize) {
  return _extractNthQuoted(s, 0, out, outSize);
}

bool RobonioGSMV2::_extractNthQuoted(const char* s, uint8_t n, char* out, size_t outSize) {
  if (!s || !out || outSize == 0) return false;
  out[0] = '\0';
  const char* p = s;
  uint8_t idx = 0;
  while (*p) {
    if (*p == '"') {
      const char* start = p + 1;
      const char* end = strchr(start, '"');
      if (!end) break;
      if (idx == n) {
        size_t len = (size_t)(end - start);
        if (len >= outSize) len = outSize - 1;
        memcpy(out, start, len);
        out[len] = '\0';
        return true;
      }
      idx++;
      p = end + 1;
      continue;
    }
    p++;
  }
  return false;
}


bool RobonioGSMV2::_selectSmsMemory(const char* mem, uint32_t timeoutMs) {
  if (!mem || !mem[0]) return true;
  // Only accept short tokens like "SM" / "ME"
  if (strlen(mem) > 2) return false;

  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\",\"%s\",\"%s\"", mem, mem, mem);
  return sendCommand(cmd, "OK", timeoutMs);
}
