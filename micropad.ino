/*
 * ═══════════════════════════════════════════════════════════
 * МИКРОПАД НА 5 КНОПОК + ПОТЕНЦИОМЕТР ГРОМКОСТИ
 * для nice!nano v2 (nRF52840)
 * ═══════════════════════════════════════════════════════════
 *
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
const float ALPHA = 0.25; // Коэффициент сглаживания (0-1), меньше = плавнее

// Текущий уровень громкости
int currentVolumeLevel = 50; // 0-100 (уровни Windows)

// Неблокирующая отправка команд громкости
int pendingVolumeSteps = 0; // Количество оставшихся команд для отправки
bool volumeIncreasing = true; // Направление изменения
unsigned long lastVolumeCommandTime = 0;
const unsigned long volumeCommandDelay = 20; // Задержка между командами громкости (мс)

// Проверка изменения громкости
unsigned long lastVolumeCheckTime = 0;
const unsigned long volumeCheckDelay = 30; // Частота проверки потенциометра (мс)

// Настройки калибровки громкости
const int VOLUME_STEPS = 100;       // Количество уровней громкости 0-100% (Windows использует 0-100)
const int ADC_MIN = 0;              // Минимальное значение АЦП (откалибровано)
const int ADC_MAX = 1023;           // Максимальное значение АЦП (полный диапазон 10-бит АЦП)
const int NOISE_THRESHOLD = 15;     // Порог шума АЦП
const int STEP_THRESHOLD = 1;       // Минимальное изменение в уровнях для отправки команды
const int MAX_STEPS_PER_UPDATE = 10; // Максимум шагов за одно обновление

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
  Serial.println("\n╔═══════════════════════════════════════════════════════════╗");
  Serial.println("║       МИКРОПАД НА 5 КНОПОК + ПОТЕНЦИОМЕТР ГРОМКОСТИ      ║");
  Serial.println("╚═══════════════════════════════════════════════════════════╝\n");

  Serial.println("Ожидание подключения USB...");
  startTime = millis();
  while (!TinyUSBDevice.mounted() && (millis() - startTime < 5000)) {
    delay(100);
    Serial.print(".");
  }
  Serial.println();

  if (TinyUSBDevice.mounted()) {
    Serial.println("✓ USB устройство смонтировано!");
  } else {
    Serial.println("⚠ USB не смонтирован, но продолжаем...");
  }

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

  Serial.println("🚀 Микропад готов к работе!\n");

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
    case 0: keycode = KEY_ARROW_UP; Serial.println("↑");     break;
    case 1: keycode = KEY_ARROW_DOWN; Serial.println("↓");   break;
    case 2: keycode = KEY_ARROW_LEFT; Serial.println("←");   break;
    case 3: keycode = KEY_ARROW_RIGHT; Serial.println("→");  break;
    case 4: keycode = KEY_SPACEBAR; Serial.println("SPACE"); break;
  }

  // Отправляем нажатие клавиши
  keyReport.keys[0] = keycode;
  usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
}

void handleButtonRelease(int buttonIndex) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("ms] ⚪ ");
  Serial.print(buttonNames[buttonIndex]);
  Serial.println(" - отпущена");

  // Отправляем отпускание клавиши
  keyReport.keys[0] = 0;
  usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
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
