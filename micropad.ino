/*
 * ═══════════════════════════════════════════════════════════
 * МИКРОПАД НА 5 КНОПОК + ПОТЕНЦИОМЕТР ГРОМКОСТИ
 * для nice!nano v2 (nRF52840)
 * ═══════════════════════════════════════════════════════════
 *
 *
 * СХЕМА ПОДКЛЮЧЕНИЯ:
 * ┌──────────────┬────────────┬──────────────────────┐
 * │ Функция      │ Пин на     │ Arduino D │ Порт     │
 * │              │ плате      │           │          │
 * ├──────────────┼────────────┼──────────────────────┤
 * │ ВВЕРХ ↑      │ 115        │ D18       │ P1.15    │
 * │ ВНИЗ ↓       │ 017        │ D2        │ P0.17    │
 * │ ВЛЕВО ←      │ 002        │ D19       │ P0.02    │
 * │ ВПРАВО →     │ 031        │ D21       │ P0.31    │
 * │ ПАУЗА        │ 011        │ D7        │ P0.11    │
 * │ ГРОМКОСТЬ    │ 029        │ D20       │ P0.29    │
 * └──────────────┴────────────┴──────────────────────┘
 *
 * Кнопки: между пином и GND
 * Потенциометр 10кОм: VCC → средний пин → D20 (029) → GND
 */

#include <Adafruit_TinyUSB.h>

// ═══════════════════════════════════════════════════════════
// USB HID НАСТРОЙКИ
// ═══════════════════════════════════════════════════════════

Adafruit_USBD_HID usb_hid;

// HID Report Descriptor: Клавиатура + Consumer Control (медиа-клавиши)
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};

// ═══════════════════════════════════════════════════════════
// РАСПИНОВКА
// ═══════════════════════════════════════════════════════════

// Кнопки
const int PIN_UP    = 3;   // D18 (пин 115 на плате)
const int PIN_DOWN  = 29;  // D2  (пин 017 на плате)
const int PIN_LEFT  = 18;  // D19 (пин 002 на плате)
const int PIN_RIGHT = 21;  // D21 (пин 031 на плате)
const int PIN_PAUSE = 23;  // D7  (пин 011 на плате)

// Потенциометр
const int PIN_VOLUME = 20; // D20 (пин 029 на плате) - ADC

const int buttonPins[] = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_PAUSE};
const char* buttonNames[] = {"ВВЕРХ ↑", "ВНИЗ ↓", "ВЛЕВО ←", "ВПРАВО →", "ПАУЗА ⏸"};
const int numButtons = 5;

// ═══════════════════════════════════════════════════════════
// СОСТОЯНИЯ
// ═══════════════════════════════════════════════════════════

// Кнопки
bool buttonStates[numButtons] = {false};
bool lastButtonStates[numButtons] = {false};
unsigned long lastDebounceTime[numButtons] = {0};
const unsigned long debounceDelay = 50;

// Потенциометр
int lastVolumeValue = 0;
int currentVolumeLevel = 50; // 0-100 (шаги Windows)
unsigned long lastVolumeChange = 0;
const unsigned long volumeChangeDelay = 50; // Уменьшена задержка для более быстрого отклика

// Настройки калибровки громкости
const int VOLUME_STEPS = 50;        // Количество шагов громкости в Windows (обычно 50)
const int ADC_MIN = 0;              // Минимальное значение АЦП (откалибровано)
const int ADC_MAX = 954;            // Максимальное значение АЦП (откалибровано)
const int NOISE_THRESHOLD = 15;     // Порог шума АЦП
const int STEP_THRESHOLD = 2;       // Минимальное изменение в шагах для отправки команды

// ═══════════════════════════════════════════════════════════
// HID КОДЫ
// ═══════════════════════════════════════════════════════════

// Клавиатурные коды
const uint8_t KEY_ARROW_UP    = 0x52;
const uint8_t KEY_ARROW_DOWN  = 0x51;
const uint8_t KEY_ARROW_LEFT  = 0x50;
const uint8_t KEY_ARROW_RIGHT = 0x4F;
const uint8_t KEY_SPACEBAR    = 0x2C;

// Consumer Control коды (медиа-клавиши)
const uint16_t VOLUME_UP   = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
const uint16_t VOLUME_DOWN = HID_USAGE_CONSUMER_VOLUME_DECREMENT;

// HID Report структуры
typedef struct {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
} KeyReport;

KeyReport keyReport = {0};

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);

  // ═══════════════════════════════════════════════════════════
  // КРИТИЧЕСКИ ВАЖНО: Инициализация TinyUSB ПЕРЕД всем остальным
  // ═══════════════════════════════════════════════════════════

  // Проверяем, инициализирован ли TinyUSB Device
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  // Настраиваем USB HID
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  // ВАЖНО: Если устройство уже смонтировано, переподключаем для перезагрузки HID
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  // Ждём подключения Serial (но не дольше 3 секунд)
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 3000)) {
    delay(10);
  }

  // Ждём монтирования USB устройства
  Serial.println("\nОжидание подключения USB...");
  startTime = millis();
  while (!TinyUSBDevice.mounted() && (millis() - startTime < 5000)) {
    delay(100);
    Serial.print(".");
  }
  Serial.println();

  if (TinyUSBDevice.mounted()) {
    Serial.println("USB устройство смонтировано!");
  } else {
    Serial.println("USB не смонтирован, но продолжаем...");
  }

  Serial.println("Инициализация кнопок:");
  Serial.println("─────────────────────────────────────────────────────────");
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    Serial.print("  ✓ D");
    if (buttonPins[i] < 10) Serial.print(" ");
    Serial.print(buttonPins[i]);
    Serial.print(" → ");
    Serial.println(buttonNames[i]);
  }
  Serial.println("─────────────────────────────────────────────────────────\n");

  // Инициализация потенциометра
  Serial.println("Инициализация потенциометра:");
  Serial.println("─────────────────────────────────────────────────────────");
  pinMode(PIN_VOLUME, INPUT);
  Serial.print("  ✓ D");
  Serial.print(PIN_VOLUME);
  Serial.println(" → ГРОМКОСТЬ (ADC)");

  // Читаем начальное значение несколько раз для стабилизации
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PIN_VOLUME);
    delay(10);
  }
  lastVolumeValue = sum / 10;
  currentVolumeLevel = map(lastVolumeValue, ADC_MIN, ADC_MAX, 0, VOLUME_STEPS);
  Serial.print("Начальное значение АЦП:");
  Serial.println(lastVolumeValue);
  Serial.print("Начальный уровень:");
  Serial.print(currentVolumeLevel);
  Serial.print(" / ");
  Serial.println(VOLUME_STEPS);

  delay(100);
}

// ═══════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
  // Проверяем, что USB смонтирован
  if (!TinyUSBDevice.mounted()) {
    delay(100);
    return;
  }

  // Обработка кнопок
  handleButtons();

  // Обработка потенциометра
  handleVolume();

  delay(5);
}

// ═══════════════════════════════════════════════════════════
// ОБРАБОТКА КНОПОК
// ═══════════════════════════════════════════════════════════

void handleButtons() {
  for (int i = 0; i < numButtons; i++) {
    int reading = digitalRead(buttonPins[i]);

    if (reading != lastButtonStates[i]) {
      lastDebounceTime[i] = millis();
    }

    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (reading != buttonStates[i]) {
        buttonStates[i] = reading;

        if (buttonStates[i] == LOW) {
          handleButtonPress(i);
        } else {
          handleButtonRelease(i);
        }
      }
    }

    lastButtonStates[i] = reading;
  }
}

void handleButtonPress(int buttonIndex) {
  Serial.print("🔴");
  Serial.print(buttonNames[buttonIndex]);
  Serial.print("→");

  uint8_t keycode = 0;

  switch(buttonIndex) {
    case 0: keycode = KEY_ARROW_UP; Serial.println("↑"); break;
    case 1: keycode = KEY_ARROW_DOWN; Serial.println("↓"); break;
    case 2: keycode = KEY_ARROW_LEFT; Serial.println("←"); break;
    case 3: keycode = KEY_ARROW_RIGHT; Serial.println("→"); break;
    case 4: keycode = KEY_SPACEBAR; Serial.println("SPACE"); break;
  }

  // Отправляем нажатие клавиши
  keyReport.keys[0] = keycode;
  usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
}

void handleButtonRelease(int buttonIndex) {
  Serial.print("⚪");
  Serial.print(buttonNames[buttonIndex]);
  Serial.println(" - отпущена");

  // Отправляем отпускание клавиши
  keyReport.keys[0] = 0;
  usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
}

// ═══════════════════════════════════════════════════════════
// ОБРАБОТКА ГРОМКОСТИ
// ═══════════════════════════════════════════════════════════

void handleVolume() {
  // Ограничиваем частоту проверки
  if (millis() - lastVolumeChange < volumeChangeDelay) {
    return;
  }

  // Читаем значение потенциометра
  int rawValue = analogRead(PIN_VOLUME);

  // Сглаживание: игнорируем маленькие изменения (шум)
  int diff = abs(rawValue - lastVolumeValue);
  if (diff < NOISE_THRESHOLD) {
    return; // Слишком маленькое изменение, игнорируем
  }

  // Преобразуем текущее и новое значение в шаги Windows
  int oldLevel = currentVolumeLevel;
  int newLevel = map(rawValue, ADC_MIN, ADC_MAX, 0, VOLUME_STEPS);

  // Ограничиваем диапазон
  newLevel = constrain(newLevel, 0, VOLUME_STEPS);

  // Вычисляем разницу в шагах
  int stepDiff = newLevel - oldLevel;

  // Если изменение слишком маленькое, игнорируем
  if (abs(stepDiff) < STEP_THRESHOLD) {
    return;
  }

  // Определяем направление и количество шагов
  if (stepDiff > 0) {
    // Увеличение громкости - отправляем несколько команд VOLUME_UP
    Serial.print("🔊 Громкость ВВЕРХ: ");
    Serial.print(oldLevel);
    Serial.print(" → ");
    Serial.print(newLevel);
    Serial.print(" (+");
    Serial.print(stepDiff);
    Serial.println(" шагов)");

    for (int i = 0; i < stepDiff; i++) {
      sendVolumeUp();
      delay(15); // Небольшая задержка между командами
    }

  } else if (stepDiff < 0) {
    // Уменьшение громкости - отправляем несколько команд VOLUME_DOWN
    int absStepDiff = abs(stepDiff);
    Serial.print("🔉 Громкость ВНИЗ: ");
    Serial.print(oldLevel);
    Serial.print(" → ");
    Serial.print(newLevel);
    Serial.print(" (-");
    Serial.print(absStepDiff);
    Serial.println(" шагов)");

    for (int i = 0; i < absStepDiff; i++) {
      sendVolumeDown();
      delay(15); // Небольшая задержка между командами
    }
  }

  // Обновляем текущий уровень и последнее значение
  currentVolumeLevel = newLevel;
  lastVolumeValue = rawValue;
  lastVolumeChange = millis();
}

void sendVolumeUp() {
  uint16_t volumeCmd = VOLUME_UP;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
  delay(5);

  // Отправляем "отпускание"
  volumeCmd = 0;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
}

void sendVolumeDown() {
  uint16_t volumeCmd = VOLUME_DOWN;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
  delay(5);

  // Отправляем "отпускание"
  volumeCmd = 0;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
}
