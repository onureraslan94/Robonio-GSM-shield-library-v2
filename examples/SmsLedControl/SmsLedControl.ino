#include <RobonioGSMV2.h>

/*
  SmsLedControl example:
  - Any incoming SMS with text "LEDOPEN" turns D13 LED ON.
  - "LEDCLOSE" turns D13 LED OFF.
*/

RobonioGSMV2 gsm;
const uint8_t LED_PIN = 13;

static void normalize(char* s) {
  int i = 0;
  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
  if (i > 0) { int j = 0; while (s[i]) s[j++] = s[i++]; s[j] = 0; }

  int n = (int)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[n - 1] = 0; n--;
  }

  for (int k = 0; s[k]; k++) {
    if (s[k] >= 'a' && s[k] <= 'z') s[k] = (char)(s[k] - 32);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  gsm.begin(9600);

  if (!gsm.init()) {
    Serial.println("GSM init failed!");
  } else {
    Serial.println("GSM ready.");
  }

  Serial.println("Send SMS: LEDOPEN / LEDCLOSE");
}

void loop() {
  gsm.update();

  RobonioSMS sms;
  if (gsm.fetchPendingSMS(sms, true)) {
    normalize(sms.text);

    Serial.print("FROM: ");
    Serial.println(sms.sender);
    Serial.print("CMD:  ");
    Serial.println(sms.text);

    if (strcmp(sms.text, "LEDOPEN") == 0) digitalWrite(LED_PIN, HIGH);
    else if (strcmp(sms.text, "LEDCLOSE") == 0) digitalWrite(LED_PIN, LOW);
  }

  delay(50);
}
