/*
 * PIN TEST — прозвонка пинов nice!nano v2 (nRF52840)
 *
 * Замкните любой пин на GND — в Serial Monitor (115200) появится строка
 * с названием пина. Служит для проверки проводки и новых пинов.
 * USB HID и Bluetooth не используются.
 *
 * Список ниже — пины, которые работают через pinMode() (см. docs/PINOUT.md).
 * Нерабочие (D6, D8, D9, D10, D14, D15) намеренно не включены.
 */

struct TestPin {
  int pin;            // номер для pinMode()
  const char* label;  // что написано на плате / Arduino D
};

const TestPin testPins[] = {
  {11, "006 (D1)"},
  {12, "008 (D0)"},
  {29, "017 (D2)"},
  {28, "020 (D3)"},
  {30, "022 (D4)"},
  {1,  "024 (D5)"},
  {23, "011 (D7)"},
  {2,  "010 (D16)"},
  {3,  "115 (D18)"},
  {18, "002 (D19)"},
  {20, "029 (D20)"},
  {21, "031 (D21)"},
};
const int numPins = sizeof(testPins) / sizeof(testPins[0]);

bool lastState[numPins];

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000) delay(10);

  Serial.println("\n=== PIN TEST: замыкайте пины на GND ===");
  for (int i = 0; i < numPins; i++) {
    pinMode(testPins[i].pin, INPUT_PULLUP);
    lastState[i] = digitalRead(testPins[i].pin);
  }
}

void loop() {
  for (int i = 0; i < numPins; i++) {
    bool state = digitalRead(testPins[i].pin);
    if (state != lastState[i]) {
      delay(20);  // антидребезг
      state = digitalRead(testPins[i].pin);
      if (state != lastState[i]) {
        lastState[i] = state;
        Serial.print("Пин ");
        Serial.print(testPins[i].label);
        Serial.print(" [pinMode ");
        Serial.print(testPins[i].pin);
        Serial.print("]: ");
        Serial.println(state == LOW ? "ЗАМКНУТ на GND" : "разомкнут");
      }
    }
  }
  delay(5);
}
