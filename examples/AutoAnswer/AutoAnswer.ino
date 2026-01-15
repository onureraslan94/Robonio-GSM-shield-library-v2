#include <RobonioGSMV2.h>

/*
  AutoAnswer example:
  - Answers any incoming call after 1 ring.
  - Uses modem-level auto-answer (ATS0=1) + Arduino fallback answerCall().
*/

RobonioGSMV2 gsm;

void setup() {
  Serial.begin(115200);

  gsm.begin(9600);

  if (!gsm.init()) {
    Serial.println("GSM init failed!");
  } else {
    Serial.println("GSM ready.");
  }

  gsm.setAutoAnswerRings(1);

  gsm.setSpeakerVolume(90);
  gsm.setMicGain(12);
  gsm.setSidetone(true, 6);
}

void loop() {
  gsm.update();

  if (gsm.takeRing()) {
    Serial.print("RING. Caller: ");
    Serial.println(gsm.lastCallerNumber());
    gsm.answerCall(); // fallback
  }

  if (gsm.callEnded()) {
    Serial.println("Call ended. Ready for next call.");
  }

  delay(20);
}
