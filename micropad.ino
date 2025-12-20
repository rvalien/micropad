/*
 * ═══════════════════════════════════════════════════════════
 * МИКРОПАД НА 5 КНОПОК + ПОТЕНЦИОМЕТР ГРОМКОСТИ
 * для nice!nano v2 (nRF52840)
 * ПОДДЕРЖКА: USB (проводное) + BLUETOOTH (беспроводное)
 * ═══════════════════════════════════════════════════════════
 *
 * РЕЖИМЫ РАБОТЫ:
 * - USB: Автоматически при подключении кабеля
 * - Bluetooth: Автоматически при отключенном кабеле
 * - Имя устройства BLE: "Micropad-5K"
 *
 * СХЕМА ПОДКЛЮЧЕНИЯ:
 * ┌──────────────┬────────────┬───────────────────────────┐
 * │ Функция      │ Arduino D  │ Физический пин на плате   │
 * ├──────────────┼────────────┼───────────────────────────┤
 * │ ВВЕРХ ↑      │ D18        │ P115 (P1.15)              │
 * │ ВНИЗ ↓       │ D2         │ P017 (P0.17)              │
 * │ ВЛЕВО ←      │ D19        │ P002 (P0.02)              │
 * │ ВПРАВО →     │ D21        │ P031 (P0.31)              │
 * │ ПАУЗА        │ D7         │ P011 (P0.11)              │
 * │ ГРОМКОСТЬ    │ D20        │ P029 (P0.29) / A5         │
 * └──────────────┴────────────┴───────────────────────────┘
 *
 * Кнопки: между пином и GND (с подтяжкой INPUT_PULLUP)
 * Потенциометр 10кОм: VCC → средний пин → D20 (A5) → GND
 */

#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>

// ═══════════════════════════════════════════════════════════
// РЕЖИМ ПОДКЛЮЧЕНИЯ
// ═══════════════════════════════════════════════════════════

enum ConnectionMode {
  MODE_USB,
  MODE_BLUETOOTH,
  MODE_NONE
};

ConnectionMode currentMode = MODE_NONE;
bool wasUSBConnected = false;

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
// BLUETOOTH HID НАСТРОЙКИ
// ═══════════════════════════════════════════════════════════

BLEDis bledis;    // Device Information Service
BLEHidAdafruit blehid;  // HID Service

// ═══════════════════════════════════════════════════════════
// РАСПИНОВКА
// ═══════════════════════════════════════════════════════════

// Кнопки
const int PIN_UP    = 3;   // D18 (пин P115 на плате)
const int PIN_DOWN  = 29;  // D2  (пин P017 на плате)
const int PIN_LEFT  = 18;  // D19 (пин P002 на плате)
const int PIN_RIGHT = 21;  // D21 (пин P031 на плате)
const int PIN_PAUSE = 23;  // D7  (пин P011 на плате)

// Потенциометр
const int PIN_VOLUME = 20; // D20 (пин P029 на плате) - ADC/A5

const int buttonPins[] = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_PAUSE};
const char* buttonNames[] = {"ВВЕРХ ↑", "ВНИЗ ↓", "ВЛЕВО ←", "ВПРАВО →", "ПАУЗА ⏸"};
const int numButtons = 5;

// ═══════════════════════════════════════════════════════════
// СОСТОЯНИЯ КНОПОК
// ═══════════════════════════════════════════════════════════

bool buttonStates[numButtons] = {false};
bool lastButtonStates[numButtons] = {false};
unsigned long lastDebounceTime[numButtons] = {0};
const unsigned long debounceDelay = 50;

// ═══════════════════════════════════════════════════════════
// СОСТОЯНИЯ ГРОМКОСТИ
// ═══════════════════════════════════════════════════════════

// Экспоненциальное сглаживание
float smoothedVolume = 0;
const float ALPHA = 0.15; // Коэффициент сглаживания (0-1), меньше = плавнее и стабильнее

// Текущий уровень громкости
int currentVolumeLevel = 50; // 0-100 (уровни Windows)

// Неблокирующая отправка команд громкости
int pendingVolumeSteps = 0; // Количество оставшихся команд для отправки
bool volumeIncreasing = true; // Направление изменения
unsigned long lastVolumeCommandTime = 0;
const unsigned long volumeCommandDelay = 20; // Задержка между командами громкости (мс)

// Проверка изменения громкости
unsigned long lastVolumeCheckTime = 0;
const unsigned long volumeCheckDelay = 100; // Частота проверки потенциометра (увеличено для стабильности)

// Настройки калибровки громкости
const int VOLUME_STEPS = 100;       // Количество уровней громкости 0-100% (Windows использует 0-100)
const int ADC_MIN = 0;              // Минимальное значение АЦП (откалибровано)
const int ADC_MAX = 1023;           // Максимальное значение АЦП (полный диапазон 10-бит АЦП)
const int NOISE_THRESHOLD = 25;     // Порог шума АЦП (увеличено для стабильности)
const int STEP_THRESHOLD = 3;       // Минимальное изменение в уровнях для отправки команды (увеличено!)
const int MAX_STEPS_PER_UPDATE = 10; // Максимум шагов за одно обновление

// ═══════════════════════════════════════════════════════════
// HID КОДЫ
// ═══════════════════════════════════════════════════════════

// Клавиатурные коды (одинаковые для USB и Bluetooth)
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

  // Ждём подключения Serial (но не дольше 2 секунд)
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 2000)) {
    delay(10);
  }

  Serial.println("\n╔═══════════════════════════════════════════════════════════╗");
  Serial.println("║    МИКРОПАД 5K + ПОТЕНЦИОМЕТР (USB + BLUETOOTH)          ║");
  Serial.println("╚═══════════════════════════════════════════════════════════╝\n");

  // ═══════════════════════════════════════════════════════════
  // ИНИЦИАЛИЗАЦИЯ USB HID
  // ═══════════════════════════════════════════════════════════

  Serial.println("🔌 Инициализация USB...");

  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  delay(500);

  if (TinyUSBDevice.mounted()) {
    Serial.println("✓ USB устройство готово");
    currentMode = MODE_USB;
    wasUSBConnected = true;
  } else {
    Serial.println("⚠ USB не подключен");
  }

  // ═══════════════════════════════════════════════════════════
  // ИНИЦИАЛИЗАЦИЯ BLUETOOTH
  // ═══════════════════════════════════════════════════════════

  Serial.println("\n📡 Инициализация Bluetooth...");

  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // Мощность передатчика: -40, -20, -16, -12, -8, -4, 0, 4 dBm
  Bluefruit.setName("Micropad-5K");

  // Настройка Device Information Service
  bledis.setManufacturer("Custom Hardware");
  bledis.setModel("Micropad 5K");
  bledis.begin();

  // Настройка HID Service
  blehid.begin();

  // Настройка рекламы (advertising)
  startAdv();

  Serial.println("✓ Bluetooth готов к подключению");
  Serial.println("  Имя устройства: Micropad-5K");

  // ═══════════════════════════════════════════════════════════
  // ИНИЦИАЛИЗАЦИЯ КНОПОК И ПОТЕНЦИОМЕТРА
  // ═══════════════════════════════════════════════════════════

  Serial.println("\n┌───────────────────────────────────────────────────────────┐");
  Serial.println("│ Инициализация кнопок:                                     │");
  Serial.println("├───────────────────────────────────────────────────────────┤");
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    Serial.print("│  ✓ D");
    if (buttonPins[i] < 10) Serial.print(" ");
    Serial.print(buttonPins[i]);
    Serial.print(" → ");
    Serial.print(buttonNames[i]);
    // Добавляем пробелы для выравнивания
    int spaces = 42 - strlen(buttonNames[i]);
    for (int j = 0; j < spaces; j++) Serial.print(" ");
    Serial.println("│");
  }
  Serial.println("└───────────────────────────────────────────────────────────┘\n");

  // Инициализация потенциометра
  Serial.println("┌───────────────────────────────────────────────────────────┐");
  Serial.println("│ Инициализация потенциометра:                              │");
  Serial.println("├───────────────────────────────────────────────────────────┤");
  pinMode(PIN_VOLUME, INPUT);
  Serial.print("│  ✓ D");
  Serial.print(PIN_VOLUME);
  Serial.println(" → ГРОМКОСТЬ (ADC)                                │");

  // Читаем начальное значение несколько раз для стабилизации
  int sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(PIN_VOLUME);
    delay(5);
  }
  int initialValue = sum / 20;
  smoothedVolume = initialValue; // Инициализация сглаживания
  currentVolumeLevel = map(initialValue, ADC_MIN, ADC_MAX, 0, VOLUME_STEPS);
  currentVolumeLevel = constrain(currentVolumeLevel, 0, VOLUME_STEPS);

  Serial.print("│  Начальное значение АЦП: ");
  Serial.print(initialValue);
  Serial.print("                                ");
  Serial.println("│");
  Serial.print("│  Начальный уровень: ");
  Serial.print(currentVolumeLevel);
  Serial.print(" / ");
  Serial.print(VOLUME_STEPS);
  Serial.print("                              ");
  Serial.println("│");
  Serial.println("└───────────────────────────────────────────────────────────┘\n");

  // Определяем текущий режим
  updateConnectionMode();
  printCurrentMode();

  Serial.println("🚀 Микропад готов к работе!\n");

  delay(100);
}

// ═══════════════════════════════════════════════════════════
// BLUETOOTH ADVERTISING
// ═══════════════════════════════════════════════════════════

void startAdv(void) {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);

  Bluefruit.Advertising.addService(blehid);

  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

// ═══════════════════════════════════════════════════════════
// ОПРЕДЕЛЕНИЕ РЕЖИМА ПОДКЛЮЧЕНИЯ
// ═══════════════════════════════════════════════════════════

void updateConnectionMode() {
  bool usbMounted = TinyUSBDevice.mounted();

  // USB подключен
  if (usbMounted) {
    if (currentMode != MODE_USB) {
      currentMode = MODE_USB;
      Serial.println("\n🔌 Переключение на USB режим");

      // Отключаем Bluetooth для экономии энергии (опционально)
      // Bluefruit.Advertising.stop();
    }
    wasUSBConnected = true;
  }
  // USB отключен, но Bluetooth подключен
  else if (Bluefruit.connected()) {
    if (currentMode != MODE_BLUETOOTH) {
      currentMode = MODE_BLUETOOTH;
      Serial.println("\n📡 Переключение на Bluetooth режим");
    }
    wasUSBConnected = false;
  }
  // Ничего не подключено, но раньше был USB
  else if (wasUSBConnected && !usbMounted) {
    currentMode = MODE_BLUETOOTH;
    Serial.println("\n📡 USB отключен, ожидание Bluetooth соединения...");
    wasUSBConnected = false;
  }
  // Полностью не подключено
  else if (!Bluefruit.connected() && !usbMounted) {
    currentMode = MODE_NONE;
  }
}

void printCurrentMode() {
  Serial.println("┌───────────────────────────────────────────────────────────┐");
  Serial.print("│ Текущий режим: ");

  switch(currentMode) {
    case MODE_USB:
      Serial.println("🔌 USB (проводное подключение)            │");
      break;
    case MODE_BLUETOOTH:
      if (Bluefruit.connected()) {
        Serial.println("📡 BLUETOOTH (беспроводное, подключено)   │");
      } else {
        Serial.println("📡 BLUETOOTH (ожидание подключения)       │");
      }
      break;
    case MODE_NONE:
      Serial.println("⚠️  НЕ ПОДКЛЮЧЕНО                          │");
      break;
  }

  Serial.println("└───────────────────────────────────────────────────────────┘");
}

// ═══════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
  // Периодически обновляем режим подключения
  static unsigned long lastModeCheck = 0;
  if (millis() - lastModeCheck > 1000) {
    ConnectionMode oldMode = currentMode;
    updateConnectionMode();
    if (oldMode != currentMode) {
      printCurrentMode();
    }
    lastModeCheck = millis();
  }

  // Проверяем наличие подключения
  if (currentMode == MODE_NONE) {
    delay(100);
    return;
  }

  // Обработка кнопок (всегда работает без блокировки)
  handleButtons();

  // Проверка изменения громкости (периодически)
  if (millis() - lastVolumeCheckTime >= volumeCheckDelay) {
    checkVolumeChange();
    lastVolumeCheckTime = millis();
  }

  // Отправка очереди команд громкости (неблокирующая)
  sendPendingVolumeCommands();

  delay(1); // Минимальная задержка для стабильности
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
  Serial.print("[");
  Serial.print(millis());
  Serial.print("ms] 🔴 ");
  Serial.print(buttonNames[buttonIndex]);
  Serial.print(" → ");

  uint8_t keycode = 0;

  switch(buttonIndex) {
    case 0:
      keycode = KEY_ARROW_UP;
      Serial.println("↑");
      break;
    case 1:
      keycode = KEY_ARROW_DOWN;
      Serial.println("↓");
      break;
    case 2:
      keycode = KEY_ARROW_LEFT;
      Serial.println("←");
      break;
    case 3:
      keycode = KEY_ARROW_RIGHT;
      Serial.println("→");
      break;
    case 4:
      keycode = KEY_SPACEBAR;
      Serial.println("SPACE");
      break;
  }

  // Отправляем в зависимости от режима
  if (currentMode == MODE_USB) {
    keyReport.keys[0] = keycode;
    usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
  }
  else if (currentMode == MODE_BLUETOOTH && Bluefruit.connected()) {
    // Используем keyboardReport с массивом клавиш
    uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
    blehid.keyboardReport(0, keycodes); // 0 = no modifiers
  }
}

void handleButtonRelease(int buttonIndex) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("ms] ⚪ ");
  Serial.print(buttonNames[buttonIndex]);
  Serial.println(" - отпущена");

  // Отправляем отпускание в зависимости от режима
  if (currentMode == MODE_USB) {
    keyReport.keys[0] = 0;
    usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
  }
  else if (currentMode == MODE_BLUETOOTH && Bluefruit.connected()) {
    uint8_t keycodes[6] = {0, 0, 0, 0, 0, 0};
    blehid.keyboardReport(0, keycodes); // Отпускаем все клавиши
  }
}

// ═══════════════════════════════════════════════════════════
// ОБРАБОТКА ГРОМКОСТИ (неблокирующая)
// ═══════════════════════════════════════════════════════════

void checkVolumeChange() {
  // Если еще есть очередь команд, не проверяем новые изменения
  if (pendingVolumeSteps > 0) {
    return;
  }

  // Читаем значение потенциометра
  int rawValue = analogRead(PIN_VOLUME);

  // Экспоненциальное сглаживание
  smoothedVolume = ALPHA * rawValue + (1 - ALPHA) * smoothedVolume;
  int smoothedValue = (int)smoothedVolume;

  // Преобразуем в шаги Windows
  int oldLevel = currentVolumeLevel;
  int newLevel = map(smoothedValue, ADC_MIN, ADC_MAX, 0, VOLUME_STEPS);

  // Ограничиваем диапазон
  newLevel = constrain(newLevel, 0, VOLUME_STEPS);

  // Вычисляем разницу в шагах
  int stepDiff = newLevel - oldLevel;

  // Если изменение слишком маленькое, игнорируем
  if (abs(stepDiff) < STEP_THRESHOLD) {
    return;
  }

  // Ограничиваем максимальное изменение за раз
  stepDiff = constrain(stepDiff, -MAX_STEPS_PER_UPDATE, MAX_STEPS_PER_UPDATE);

  // Обновляем целевой уровень
  int targetLevel = oldLevel + stepDiff;
  targetLevel = constrain(targetLevel, 0, VOLUME_STEPS);

  // Сохраняем количество шагов для отправки
  pendingVolumeSteps = abs(stepDiff);
  volumeIncreasing = (stepDiff > 0);

  // Обновляем текущий уровень
  currentVolumeLevel = targetLevel;

  // Выводим информацию
  Serial.print("[");
  Serial.print(millis());
  Serial.print("ms] ");
  if (volumeIncreasing) {
    Serial.print("🔊 Громкость ВВЕРХ: ");
  } else {
    Serial.print("🔉 Громкость ВНИЗ: ");
  }
  Serial.print(oldLevel);
  Serial.print(" → ");
  Serial.print(targetLevel);
  Serial.print(" (");
  if (volumeIncreasing) Serial.print("+");
  else Serial.print("-");
  Serial.print(abs(stepDiff));
  Serial.print(" шагов, ADC=");
  Serial.print(smoothedValue);
  Serial.println(")");
}

void sendPendingVolumeCommands() {
  // Если нет команд в очереди, выходим
  if (pendingVolumeSteps <= 0) {
    return;
  }

  // Проверяем, прошло ли достаточно времени с последней команды
  if (millis() - lastVolumeCommandTime < volumeCommandDelay) {
    return;
  }

  // Отправляем одну команду
  if (volumeIncreasing) {
    sendVolumeUp();
  } else {
    sendVolumeDown();
  }

  // Уменьшаем счетчик
  pendingVolumeSteps--;
  lastVolumeCommandTime = millis();
}

void sendVolumeUp() {
  if (currentMode == MODE_USB) {
    uint16_t volumeCmd = VOLUME_UP;
    usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
    delay(5);
    volumeCmd = 0;
    usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
  }
  else if (currentMode == MODE_BLUETOOTH && Bluefruit.connected()) {
    blehid.consumerKeyPress(HID_USAGE_CONSUMER_VOLUME_INCREMENT);
    delay(5);
    blehid.consumerKeyRelease();
  }
}

void sendVolumeDown() {
  if (currentMode == MODE_USB) {
    uint16_t volumeCmd = VOLUME_DOWN;
    usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
    delay(5);
    volumeCmd = 0;
    usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
  }
  else if (currentMode == MODE_BLUETOOTH && Bluefruit.connected()) {
    blehid.consumerKeyPress(HID_USAGE_CONSUMER_VOLUME_DECREMENT);
    delay(5);
    blehid.consumerKeyRelease();
  }
}
