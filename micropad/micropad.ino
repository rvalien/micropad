/*
 * ═══════════════════════════════════════════════════════════
 *  MICROPAD 5K — 5 кнопок + потенциометр громкости
 *  nice!nano v2 (nRF52840) · USB HID + Bluetooth LE HID
 * ═══════════════════════════════════════════════════════════
 *
 * РЕЖИМЫ:
 *  - USB:        автоматически, когда хост смонтировал устройство
 *  - Bluetooth:  автоматически, когда USB не подключён (имя "Micropad-5K")
 *  - Сон:        System OFF после SLEEP_TIMEOUT без активности (только вне USB),
 *                пробуждение нажатием любой кнопки (плата перезагружается,
 *                само «будящее» нажатие не передаётся)
 *  - Батарея:    уровень отдаётся хосту через BLE Battery Service
 *
 * ПОДКЛЮЧЕНИЕ (номера — для pinMode(), проверено pin_scan, см. docs/PINOUT.md):
 *  ┌────────────┬───────────────┬────────┬───────────┐
 *  │ Функция    │ Пин на плате  │ Порт   │ pinMode() │
 *  ├────────────┼───────────────┼────────┼───────────┤
 *  │ ВВЕРХ ↑    │ 115           │ P1.15  │ 3         │
 *  │ ВНИЗ ↓     │ 017           │ P0.17  │ 29        │
 *  │ ВЛЕВО ←    │ 002           │ P0.02  │ 18        │
 *  │ ВПРАВО →   │ 031           │ P0.31  │ 21        │
 *  │ ПАУЗА      │ 011           │ P0.11  │ 23        │
 *  │ ГРОМКОСТЬ  │ 029 (AIN5)    │ P0.29  │ 20        │
 *  └────────────┴───────────────┴────────┴───────────┘
 *  Кнопки: между пином и GND (INPUT_PULLUP).
 *  Потенциометр 10 кОм: крайние выводы на VCC и GND, средний — на пин 029.
 *
 * ADC:
 *  Для потенциометра используется ADC по умолчанию (10 бит, референс 3,6 В).
 *  При питании 3,3 В максимум ≈ 938, а не 1023 (3,3 / 3,6 × 1023).
 *  Для батареи ADC на время измерения переключается на 12 бит / 3,0 В
 *  и затем возвращается обратно (см. readBatteryMilliVolts()).
 */

#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>
#include "nrf_gpio.h"

#define FW_VERSION "2.1"

// ═══════════════════════════════════════════════════════════
// РЕЖИМ ПОДКЛЮЧЕНИЯ
// ═══════════════════════════════════════════════════════════

enum ConnectionMode {
  MODE_USB,            // хост смонтировал USB
  MODE_BLE_CONNECTED,  // USB нет, есть BLE-соединение
  MODE_BLE_WAITING     // USB нет, идёт реклама BLE
};

ConnectionMode currentMode = MODE_BLE_WAITING;

// ═══════════════════════════════════════════════════════════
// USB / BLE HID
// ═══════════════════════════════════════════════════════════

Adafruit_USBD_HID usb_hid;

// Report ID 1 — клавиатура, Report ID 2 — медиа-клавиши (Consumer Control)
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};

BLEDis bledis;          // Device Information Service
BLEHidAdafruit blehid;  // HID Service
BLEBas blebas;          // Battery Service

typedef struct {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
} KeyReport;

// ═══════════════════════════════════════════════════════════
// РАСПИНОВКА (номера для pinMode(), проверено, см. docs/PINOUT.md)
// ═══════════════════════════════════════════════════════════

const int PIN_UP     = 3;   // 115, P1.15
const int PIN_DOWN   = 29;  // 017, P0.17
const int PIN_LEFT   = 18;  // 002, P0.02
const int PIN_RIGHT  = 21;  // 031, P0.31
const int PIN_PAUSE  = 23;  // 011, P0.11
const int PIN_VOLUME = 20;  // 029, P0.29 (AIN5)

// Пин измерения батареи: P0.04 (AIN2), внутренний, на колодки не выведен.
// В используемом ядре это pinMode(14) (проверено pin_scan: 14 = P0.04).
// Макрос ядра PIN_VBAT НЕ используется: в раскладке Feather он указывает
// на P0.29, то есть на пин потенциометра.
const int PIN_BATTERY = 14;

// HID-коды клавиш
const uint8_t KEY_ARROW_UP    = 0x52;
const uint8_t KEY_ARROW_DOWN  = 0x51;
const uint8_t KEY_ARROW_LEFT  = 0x50;
const uint8_t KEY_ARROW_RIGHT = 0x4F;
const uint8_t KEY_SPACEBAR    = 0x2C;

const int numButtons = 5;
const int buttonPins[numButtons]    = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_PAUSE};
const uint8_t buttonKeys[numButtons] = {KEY_ARROW_UP, KEY_ARROW_DOWN, KEY_ARROW_LEFT, KEY_ARROW_RIGHT, KEY_SPACEBAR};
const char* buttonNames[numButtons] = {"ВВЕРХ", "ВНИЗ", "ВЛЕВО", "ВПРАВО", "ПАУЗА"};

// ═══════════════════════════════════════════════════════════
// СОСТОЯНИЕ КНОПОК
// ═══════════════════════════════════════════════════════════

bool buttonStates[numButtons];      // отфильтрованное состояние (LOW = нажата)
bool lastButtonStates[numButtons];  // последнее сырое чтение
unsigned long lastDebounceTime[numButtons] = {0};
const unsigned long debounceDelay = 50;

// ═══════════════════════════════════════════════════════════
// РЕЖИМ СНА
// ═══════════════════════════════════════════════════════════

const unsigned long SLEEP_TIMEOUT = 5UL * 60UL * 1000UL;  // 5 минут
unsigned long lastActivityTime = 0;

// ═══════════════════════════════════════════════════════════
// БАТАРЕЯ
// ═══════════════════════════════════════════════════════════

// ADC батареи: 12 бит, референс 3,0 В → 3000 / 4096 мВ на шаг.
// VBAT_DIVIDER — коэффициент делителя. ТРЕБУЕТ КАЛИБРОВКИ по мультиметру:
//   VBAT_DIVIDER = (напряжение батареи по мультиметру, В) / (В, которое показывает
//   прошивка при VBAT_DIVIDER = 1.0)
// Известные варианты: 2.0 (делитель 1:1), ≈1.403 (клоны с делителем 2 МОм / 806 кОм).
const float VBAT_MV_PER_LSB = 3000.0F / 4096.0F;
const float VBAT_DIVIDER    = 2.0F;

const float VBAT_FULL  = 4200.0F;  // 100 %
const float VBAT_EMPTY = 3300.0F;  // 0 % (с запасом, критический минимум ~3,0 В)
const float VBAT_NO_BATTERY_MV = 2500.0F;  // ниже — считаем, что батареи нет

int batteryLevel = -1;             // -1 = ещё не измеряли
float batteryVoltage = 0.0F;
unsigned long lastBatteryCheck = 0;
const unsigned long batteryCheckInterval = 60000;

// ═══════════════════════════════════════════════════════════
// ГРОМКОСТЬ
// ═══════════════════════════════════════════════════════════

const int ADC_MIN = 0;
const int ADC_MAX = 938;             // 10 бит, референс 3,6 В, питание 3,3 В
const int VOLUME_STEPS = 100;        // уровни 0–100 (как в Windows)
const int STEP_THRESHOLD = 3;        // минимальное изменение в шагах
const int MAX_STEPS_PER_UPDATE = 10; // максимум шагов за одно обновление
const float ALPHA = 0.15F;           // сглаживание (меньше = плавнее)

float smoothedVolume = 0;
int currentVolumeLevel = 50;

int pendingVolumeSteps = 0;
bool volumeIncreasing = true;
unsigned long lastVolumeCommandTime = 0;
const unsigned long volumeCommandDelay = 20;
unsigned long lastVolumeCheckTime = 0;
const unsigned long volumeCheckDelay = 100;

// ═══════════════════════════════════════════════════════════
// ОТПРАВКА HID
// ═══════════════════════════════════════════════════════════

// Отправка USB-репорта с коротким ожиданием готовности endpoint'а
bool usbSend(uint8_t reportId, void* data, uint8_t len) {
  unsigned long t = millis();
  while (!usb_hid.ready() && millis() - t < 10) delay(1);
  return usb_hid.sendReport(reportId, data, len);
}

// Отправляет текущий набор нажатых кнопок (поддерживает несколько клавиш сразу)
void sendKeyboardState() {
  uint8_t keys[6] = {0};
  int n = 0;
  for (int i = 0; i < numButtons && n < 6; i++) {
    if (buttonStates[i] == LOW) keys[n++] = buttonKeys[i];
  }

  if (currentMode == MODE_USB) {
    KeyReport report = {0, 0, {0}};
    memcpy(report.keys, keys, 6);
    usbSend(1, &report, sizeof(report));
  } else if (currentMode == MODE_BLE_CONNECTED) {
    blehid.keyboardReport(0, keys);
  }
}

void sendVolumeStep(bool up) {
  uint16_t usage = up ? HID_USAGE_CONSUMER_VOLUME_INCREMENT
                      : HID_USAGE_CONSUMER_VOLUME_DECREMENT;

  if (currentMode == MODE_USB) {
    usbSend(2, &usage, sizeof(usage));
    delay(5);
    uint16_t release = 0;
    usbSend(2, &release, sizeof(release));
  } else if (currentMode == MODE_BLE_CONNECTED) {
    blehid.consumerKeyPress(usage);
    delay(5);
    blehid.consumerKeyRelease();
  }
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);

  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 2000)) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== MICROPAD 5K v" FW_VERSION " (USB + BLE + Sleep + Battery) ===");

  // ADC по умолчанию: 10 бит, референс 3,6 В
  analogReference(AR_DEFAULT);
  analogReadResolution(10);

  // ─── USB ────────────────────────────────────────────────
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
  delay(500);  // время на энумерацию

  // ─── Bluetooth ──────────────────────────────────────────
  Bluefruit.begin();
  Bluefruit.setTxPower(4);  // -40, -20, -16, -12, -8, -4, 0, 4 dBm
  Bluefruit.setName("Micropad-5K");

  bledis.setManufacturer("Custom Hardware");
  bledis.setModel("Micropad 5K v2");
  bledis.begin();

  blebas.begin();
  blebas.write(100);

  blehid.begin();
  startAdv();

  // ─── Кнопки ─────────────────────────────────────────────
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  delay(5);
  for (int i = 0; i < numButtons; i++) {
    buttonStates[i] = lastButtonStates[i] = digitalRead(buttonPins[i]);
  }

  // ─── Потенциометр ───────────────────────────────────────
  pinMode(PIN_VOLUME, INPUT);
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(PIN_VOLUME);
    delay(5);
  }
  int initialValue = sum / 20;
  smoothedVolume = initialValue;
  currentVolumeLevel = constrain(map(initialValue, ADC_MIN, ADC_MAX, 0, VOLUME_STEPS), 0, VOLUME_STEPS);
  Serial.print("Громкость: ADC=");
  Serial.print(initialValue);
  Serial.print(" -> ");
  Serial.print(currentVolumeLevel);
  Serial.println("/100");

  // ─── Батарея ────────────────────────────────────────────
  pinMode(PIN_BATTERY, INPUT);
  updateBatteryLevel();

  // ─── Режим ──────────────────────────────────────────────
  updateConnectionMode();
  printCurrentMode();

  lastActivityTime = millis();
  Serial.println("Микропад готов к работе");
}

// ═══════════════════════════════════════════════════════════
// BLE ADVERTISING
// ═══════════════════════════════════════════════════════════

void startAdv(void) {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addService(blebas);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);  // в единицах 0,625 мс
  Bluefruit.Advertising.setFastTimeout(30);    // секунд в быстром режиме
  Bluefruit.Advertising.start(0);              // 0 = рекламировать бесконечно
}

// ═══════════════════════════════════════════════════════════
// РЕЖИМ ПОДКЛЮЧЕНИЯ
// ═══════════════════════════════════════════════════════════

void updateConnectionMode() {
  ConnectionMode newMode;
  if (TinyUSBDevice.mounted()) {
    newMode = MODE_USB;
  } else if (Bluefruit.connected()) {
    newMode = MODE_BLE_CONNECTED;
  } else {
    newMode = MODE_BLE_WAITING;
  }

  if (newMode != currentMode) {
    currentMode = newMode;
    printCurrentMode();
  }
}

void printCurrentMode() {
  Serial.print("Режим: ");
  switch (currentMode) {
    case MODE_USB:           Serial.println("USB"); break;
    case MODE_BLE_CONNECTED: Serial.println("Bluetooth (подключено)"); break;
    case MODE_BLE_WAITING:   Serial.println("Bluetooth (ожидание подключения)"); break;
  }
}

// ═══════════════════════════════════════════════════════════
// РЕЖИМ СНА
// ═══════════════════════════════════════════════════════════

void updateActivity() {
  lastActivityTime = millis();
}

bool anyButtonPressed() {
  for (int i = 0; i < numButtons; i++) {
    if (digitalRead(buttonPins[i]) == LOW) return true;
  }
  return false;
}

void checkSleepMode() {
  // На USB не спим (питание постоянное) и сбрасываем таймер
  if (currentMode == MODE_USB) {
    lastActivityTime = millis();
    return;
  }
  if (millis() - lastActivityTime > SLEEP_TIMEOUT) {
    enterSleepMode();
  }
}

void enterSleepMode() {
  Serial.println("Неактивность: переход в System OFF. Для пробуждения нажмите любую кнопку.");
  Serial.flush();

  // Если кнопка зажата, плата проснётся мгновенно — ждём отпускания (до 2 с)
  unsigned long t = millis();
  while (anyButtonPressed() && millis() - t < 2000) delay(10);

  if (Bluefruit.Advertising.isRunning()) {
    Bluefruit.Advertising.stop();
  }

  // Пробуждение по LOW на любой кнопке.
  // nrf_gpio_cfg_sense_input корректно работает и с портом P1 (кнопка ВВЕРХ = P1.15).
  for (int i = 0; i < numButtons; i++) {
    nrf_gpio_cfg_sense_input(g_ADigitalPinMap[buttonPins[i]],
                             NRF_GPIO_PIN_PULLUP,
                             NRF_GPIO_PIN_SENSE_LOW);
  }

  delay(100);
  sd_power_system_off();  // после пробуждения плата перезагрузится и выполнит setup()

  // Сюда попадаем только если System OFF не удался
  Serial.println("System OFF не выполнен, продолжаем работу");
  lastActivityTime = millis();
  if (!Bluefruit.Advertising.isRunning() && !Bluefruit.connected()) {
    startAdv();
  }
}

// ═══════════════════════════════════════════════════════════
// БАТАРЕЯ
// ═══════════════════════════════════════════════════════════

// Измеряет напряжение батареи в мВ.
// На время измерения ADC переключается на 12 бит / 3,0 В, затем настройки
// по умолчанию возвращаются — иначе ломается чтение потенциометра.
float readBatteryMilliVolts() {
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  analogRead(PIN_BATTERY);  // холостое чтение после смены референса

  long sum = 0;
  const int samples = 8;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(PIN_BATTERY);
    delay(2);
  }

  analogReference(AR_DEFAULT);
  analogReadResolution(10);

  return (sum / (float)samples) * VBAT_MV_PER_LSB * VBAT_DIVIDER;
}

void updateBatteryLevel() {
  float mv = readBatteryMilliVolts();
  batteryVoltage = mv / 1000.0F;

  if (mv < VBAT_NO_BATTERY_MV) {
    Serial.print("Батарея не обнаружена (на АЦП ");
    Serial.print(batteryVoltage, 2);
    Serial.println(" В)");
    return;  // не портим значение в BLE
  }

  int percent = (int)((mv - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0F);
  percent = constrain(percent, 0, 100);

  bool changed = (percent != batteryLevel);
  batteryLevel = percent;
  if (changed) blebas.write(batteryLevel);

  // Печатаем при каждом замере — это нужно для калибровки VBAT_DIVIDER
  Serial.print("Батарея: ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" В (");
  Serial.print(batteryLevel);
  Serial.println("%)");

  if (changed && batteryLevel <= 10) {
    Serial.println("ВНИМАНИЕ: низкий заряд батареи!");
  }
}

// ═══════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
  static unsigned long lastModeCheck = 0;
  if (millis() - lastModeCheck > 500) {
    updateConnectionMode();
    lastModeCheck = millis();
  }

  handleButtons();

  if (millis() - lastBatteryCheck >= batteryCheckInterval) {
    updateBatteryLevel();
    lastBatteryCheck = millis();
  }

  if (millis() - lastVolumeCheckTime >= volumeCheckDelay) {
    checkVolumeChange();
    lastVolumeCheckTime = millis();
  }
  sendPendingVolumeCommands();

  checkSleepMode();

  delay(1);
}

// ═══════════════════════════════════════════════════════════
// КНОПКИ
// ═══════════════════════════════════════════════════════════

void handleButtons() {
  for (int i = 0; i < numButtons; i++) {
    int reading = digitalRead(buttonPins[i]);

    if (reading != lastButtonStates[i]) {
      lastDebounceTime[i] = millis();
    }

    if ((millis() - lastDebounceTime[i]) > debounceDelay && reading != buttonStates[i]) {
      buttonStates[i] = reading;
      updateActivity();

      Serial.print("[");
      Serial.print(millis());
      Serial.print(" мс] ");
      Serial.print(buttonNames[i]);
      Serial.println(buttonStates[i] == LOW ? " нажата" : " отпущена");

      sendKeyboardState();
    }

    lastButtonStates[i] = reading;
  }
}

// ═══════════════════════════════════════════════════════════
// ГРОМКОСТЬ (неблокирующая)
// ═══════════════════════════════════════════════════════════

void checkVolumeChange() {
  // Пока есть очередь команд, новые не планируем
  if (pendingVolumeSteps > 0) return;

  int rawValue = analogRead(PIN_VOLUME);
  smoothedVolume = ALPHA * rawValue + (1 - ALPHA) * smoothedVolume;
  int smoothedValue = (int)smoothedVolume;

  int newLevel = constrain(map(smoothedValue, ADC_MIN, ADC_MAX, 0, VOLUME_STEPS), 0, VOLUME_STEPS);

  // Нет соединения — просто следим за положением ручки, ничего не отправляя,
  // чтобы после подключения не было скачка громкости
  if (currentMode == MODE_BLE_WAITING) {
    currentVolumeLevel = newLevel;
    return;
  }

  int stepDiff = newLevel - currentVolumeLevel;
  if (abs(stepDiff) < STEP_THRESHOLD) return;

  updateActivity();

  stepDiff = constrain(stepDiff, -MAX_STEPS_PER_UPDATE, MAX_STEPS_PER_UPDATE);
  int oldLevel = currentVolumeLevel;
  currentVolumeLevel = constrain(oldLevel + stepDiff, 0, VOLUME_STEPS);

  pendingVolumeSteps = abs(stepDiff);
  volumeIncreasing = (stepDiff > 0);

  Serial.print("[");
  Serial.print(millis());
  Serial.print(" мс] Громкость ");
  Serial.print(volumeIncreasing ? "+" : "-");
  Serial.print(abs(stepDiff));
  Serial.print(": ");
  Serial.print(oldLevel);
  Serial.print(" -> ");
  Serial.print(currentVolumeLevel);
  Serial.print(" (ADC=");
  Serial.print(smoothedValue);
  Serial.println(")");
}

void sendPendingVolumeCommands() {
  if (pendingVolumeSteps <= 0) return;
  if (millis() - lastVolumeCommandTime < volumeCommandDelay) return;

  sendVolumeStep(volumeIncreasing);
  pendingVolumeSteps--;
  lastVolumeCommandTime = millis();
}
