#ifndef ROBONIO_GSMV2_H
#define ROBONIO_GSMV2_H

#include <Arduino.h>
#include <SoftwareSerial.h>

/**
 * Default pins for Arduino UNO + SoftwareSerial.
 * RX_PIN is Arduino's RX (from modem TX).
 * TX_PIN is Arduino's TX (to modem RX).
 */
#ifndef ROBONIO_GSMV2_DEFAULT_RX_PIN
#define ROBONIO_GSMV2_DEFAULT_RX_PIN 10
#endif

#ifndef ROBONIO_GSMV2_DEFAULT_TX_PIN
#define ROBONIO_GSMV2_DEFAULT_TX_PIN 11
#endif

struct RobonioSMS {
  uint16_t index = 0;
  char sender[24] = {0};   // E.164 + quotes removed
  char text[161] = {0};    // 160 chars + null
};

struct RobonioGSMV2Audio {
  uint8_t speakerVolume = 90;   // 0..100 (AT+CLVL)
  uint8_t ringerVolume  = 90;   // 0..100 (AT+CRSL)
  uint8_t micGain       = 12;   // 0..15  (AT+CMIC)
  bool    sidetone      = true; // AT+SIDET
  uint8_t sidetoneGain  = 6;    // 0..15
};

struct RobonioGSMV2KeepAlive {
  uint32_t intervalMs   = 8000; // how often to ping with "AT" (0 disables)
  uint16_t atTimeoutMs  = 800;  // timeout for each ping
  uint8_t  maxFails     = 2;    // consecutive fails before auto-recover
  bool     autoRecover  = true; // call init() on repeated fails
};

struct RobonioGSMV2AutoPurge {
  bool     enabled            = false;
  uint8_t  thresholdPercent   = 70;       // purge when used >= total * thresholdPercent/100
  uint32_t checkIntervalMs    = 600000UL; // 10 minutes
};

class RobonioGSMV2 {
public:
  /**
   * Create an instance with internal SoftwareSerial.
   * This is the default for Arduino UNO.
   */
  explicit RobonioGSMV2(uint8_t rxPin = ROBONIO_GSMV2_DEFAULT_RX_PIN,
                        uint8_t txPin = ROBONIO_GSMV2_DEFAULT_TX_PIN);

  /**
   * Attach an external Stream (e.g. Serial1 on Mega).
   * You must call begin() on that serial yourself.
   */
  void attachStream(Stream& io);

  /**
   * Start internal SoftwareSerial with given baud rate.
   */
  bool begin(long baud = 9600);

  /**
   * Initialize modem for stable operation:
   * - Disable echo (ATE0)
   * - Disable sleep (AT+CSCLK=0)
   * - Ignore DTR (AT&D0)
   * - Enable caller ID (AT+CLIP=1)
   * - Configure SMS (text mode, storage, CNMI)
   */
  bool init(uint32_t timeoutMs = 10000);

  /**
   * Main service function. Call frequently in loop().
   * - Parses URCs (incoming call, SMS notifications, call end)
   * - Runs keepalive + optional auto-purge
   */
  void update();

  // ---------- Low-level ----------
  bool sendCommand(const char* cmd,
                   const char* expectToken = "OK",
                   uint32_t timeoutMs = 2000,
                   bool appendCRLF = true);

  bool isResponsive(uint32_t timeoutMs = 1500);

  // ---------- Configuration ----------
  void setAudioDefaults(const RobonioGSMV2Audio& audio);
  void setKeepAlive(const RobonioGSMV2KeepAlive& ka);
  void setAutoPurge(const RobonioGSMV2AutoPurge& ap);

  // ---------- Calls ----------
  bool enableCallerID(bool on = true, uint32_t timeoutMs = 2000);
  bool setAutoAnswerRings(uint8_t rings, uint32_t timeoutMs = 2000); // ATS0=<rings>
  bool answerCall(uint32_t timeoutMs = 8000); // ATA
  bool hangup(uint32_t timeoutMs = 5000);     // ATH

  bool isRinging() const;
  bool takeRing();           // returns true once per ring event and clears the flag
  bool callActive() const;
  bool callEnded();          // one-shot flag (clears after read)
  const char* lastCallerNumber() const;

  // ---------- SMS ----------
  bool sendSMS(const char* number, const char* text, uint32_t timeoutMs = 25000);

  bool availableSMS() const;
  uint16_t pendingSMSIndex() const;

  bool readSMS(uint16_t index, RobonioSMS& out, uint32_t timeoutMs = 8000);
  bool fetchPendingSMS(RobonioSMS& out, bool deleteAfter = true, uint32_t timeoutMs = 8000);

  bool deleteSMS(uint16_t index, uint32_t timeoutMs = 6000);
  bool deleteAllSMS(uint32_t timeoutMs = 15000);

  bool getSmsStorage(uint16_t& used, uint16_t& total, const char* mem = "SM", uint32_t timeoutMs = 4000);

  // ---------- Audio ----------
  bool setSpeakerVolume(uint8_t percent0to100, uint32_t timeoutMs = 2000);
  bool setRingerVolume(uint8_t percent0to100, uint32_t timeoutMs = 2000);
  bool setMicGain(uint8_t gain0to15, uint32_t timeoutMs = 2000);
  bool setSidetone(bool enable, uint8_t gain0to15, uint32_t timeoutMs = 2000);

  // ---------- Optional hardware power key ----------
  void attachPowerKey(uint8_t pin, bool activeLow = true);
  bool powerToggle(uint16_t pressMs = 1200);

private:
  // IO
  SoftwareSerial _soft;
  Stream* _io = nullptr;

  // Optional PWRKEY
  bool _hasPwrKey = false;
  uint8_t _pwrKeyPin = 255;
  bool _pwrKeyActiveLow = true;

  // State
  volatile bool _ringFlag = false;
  volatile bool _callActive = false;
  volatile bool _callEnded = false;

  char _caller[24] = {0};
  volatile bool _hasPendingSms = false;
  volatile uint16_t _pendingSmsIndex = 0;
  char _pendingSmsMem[4] = "SM";

  uint8_t _autoAnswerRings = 0;
  bool _callerIdEnabled = true;

  // Config
  RobonioGSMV2Audio _audio;
  RobonioGSMV2KeepAlive _ka;
  RobonioGSMV2AutoPurge _ap;

  // Timers
  uint32_t _lastKeepAliveMs = 0;
  uint8_t  _keepAliveFails = 0;
  uint32_t _lastPurgeCheckMs = 0;

  // Line assembly for non-blocking update()
  char _lineBuf[180] = {0};
  uint16_t _lineLen = 0;

  // Helpers
  void _handleLine(const char* line);
  bool _selectSmsMemory(const char* mem, uint32_t timeoutMs = 3000);
  bool _readLineBlocking(char* out, size_t outSize, uint32_t timeoutMs);
  void _drainInput(uint32_t ms);
  void _appendCharToLine(char c);
  void _processIncomingBytes();

  bool _waitForToken(const char* tokenA,
                     const char* tokenB,
                     uint32_t timeoutMs,
                     bool& matchedA);

  static void _trim(char* s);
  static bool _startsWith(const char* s, const char* prefix);
  static bool _extractFirstQuoted(const char* s, char* out, size_t outSize);
  static bool _extractNthQuoted(const char* s, uint8_t n, char* out, size_t outSize);
};

#endif
