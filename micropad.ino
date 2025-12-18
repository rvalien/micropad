/*
 * ═══════════════════════════════════════════════════════════
 * МИКРОПАД НА 5 КНОПОК + ПОТЕНЦИОМЕТР ГРОМКОСТИ
 * для nice!nano v2 (nRF52840)
 * ═══════════════════════════════════════════════════════════
 * 
 * ФИНАЛЬНАЯ ВЕРСИЯ с проверенной распиновкой
 * 
 * СХЕМА ПОДКЛЮЧЕНИЯ:
 * ┌──────────────┬────────────┬──────────────────────┐
 * │ Функция      │ Пин на     │ Arduino D │ Порт     │
 * │              │ плате      │           │          │
 * ├──────────────┼────────────┼──────────────────────┤
 * │ ВВЕРХ ↑      │ 115        │ D18       │ P1.15    │
 * │ ВНИЗ ↓       │ 009        │ D10       │ P0.09    │
 * │ ВЛЕВО ←      │ 002        │ D19       │ P0.02    │
 * │ ВПРАВО →     │ 031        │ D21       │ P0.31    │
 * │ ПАУЗА        │ 113        │ D15       │ P1.13    │
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
const int PIN_UP    = 19;  // D18 (пин 115 на плате)
const int PIN_DOWN  = 16;  // D10 (пин 009 на плате)
const int PIN_LEFT  = 18;  // D19 (пин 002 на плате)
const int PIN_RIGHT = 21;  // D21 (пин 031 на плате)
const int PIN_PAUSE = 15;  // D15 (пин 113 на плате)

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
int currentVolumeLevel = 50; // 0-100
unsigned long lastVolumeChange = 0;
const unsigned long volumeChangeDelay = 100; // Задержка между изменениями громкости

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
  
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 3000)) {
    delay(10);
  }
  
  Serial.println("\n\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║  🎮 МИКРОПАД + РЕГУЛЯТОР ГРОМКОСТИ 🔊                    ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  // Инициализация кнопок
  Serial.println("📌 Инициализация кнопок:");
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
  Serial.println("🎚️  Инициализация потенциометра:");
  Serial.println("─────────────────────────────────────────────────────────");
  pinMode(PIN_VOLUME, INPUT);
  Serial.print("  ✓ D");
  Serial.print(PIN_VOLUME);
  Serial.println(" → ГРОМКОСТЬ (ADC)");
  
  // Читаем начальное значение
  lastVolumeValue = analogRead(PIN_VOLUME);
  currentVolumeLevel = map(lastVolumeValue, 0, 1023, 0, 100);
  Serial.print("  Начальная громкость: ");
  Serial.print(currentVolumeLevel);
  Serial.println("%");
  Serial.println("─────────────────────────────────────────────────────────\n");
  
  // Инициализация USB HID
  Serial.println("🔌 Инициализация USB HID...");
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
  
  while (!TinyUSBDevice.mounted()) {
    delay(1);
  }
  Serial.println("   ✅ USB HID готов!\n");
  
  Serial.println("╔════════════════════════════════════════════════════════════╗");
  Serial.println("║  ✅ МИКРОПАД ГОТОВ К РАБОТЕ!                               ║");
  Serial.println("║                                                            ║");
  Serial.println("║  🎮 Управление: стрелки + пробел                           ║");
  Serial.println("║  🔊 Громкость: вращайте потенциометр                       ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  delay(100);
}

// ═══════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
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
  Serial.print("🔴 ");
  Serial.print(buttonNames[buttonIndex]);
  Serial.print(" → ");
  
  uint8_t keycode = 0;
  
  switch(buttonIndex) {
    case 0: keycode = KEY_ARROW_UP; Serial.println("↑"); break;
    case 1: keycode = KEY_ARROW_DOWN; Serial.println("↓"); break;
    case 2: keycode = KEY_ARROW_LEFT; Serial.println("←"); break;
    case 3: keycode = KEY_ARROW_RIGHT; Serial.println("→"); break;
    case 4: keycode = KEY_SPACEBAR; Serial.println("SPACE"); break;
  }
  
  if (TinyUSBDevice.mounted()) {
    keyReport.keys[0] = keycode;
    usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
  }
}

void handleButtonRelease(int buttonIndex) {
  Serial.print("⚪ ");
  Serial.print(buttonNames[buttonIndex]);
  Serial.println(" - отпущена");
  
  if (TinyUSBDevice.mounted()) {
    keyReport.keys[0] = 0;
    usb_hid.sendReport(1, &keyReport, sizeof(keyReport));
  }
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
  if (diff < 10) {
    return; // Слишком маленькое изменение, игнорируем
  }
  
  // Преобразуем в уровень 0-100
  int newLevel = map(rawValue, 0, 1023, 0, 100);
  
  // Определяем направление изменения
  if (newLevel > currentVolumeLevel + 2) {
    // Увеличение громкости
    sendVolumeUp();
    currentVolumeLevel = newLevel;
    
    Serial.print("🔊 Громкость ВВЕРХ → ");
    Serial.print(currentVolumeLevel);
    Serial.println("%");
    
  } else if (newLevel < currentVolumeLevel - 2) {
    // Уменьшение громкости
    sendVolumeDown();
    currentVolumeLevel = newLevel;
    
    Serial.print("🔉 Громкость ВНИЗ → ");
    Serial.print(currentVolumeLevel);
    Serial.println("%");
  }
  
  lastVolumeValue = rawValue;
  lastVolumeChange = millis();
}

void sendVolumeUp() {
  if (!TinyUSBDevice.mounted()) return;
  
  uint16_t volumeCmd = VOLUME_UP;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
  delay(10);
  
  // Отправляем "отпускание"
  volumeCmd = 0;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
}

void sendVolumeDown() {
  if (!TinyUSBDevice.mounted()) return;
  
  uint16_t volumeCmd = VOLUME_DOWN;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
  delay(10);
  
  // Отправляем "отпускание"
  volumeCmd = 0;
  usb_hid.sendReport(2, &volumeCmd, sizeof(volumeCmd));
}

/*
 * ═══════════════════════════════════════════════════════════
 * СХЕМА ПОДКЛЮЧЕНИЯ
 * ═══════════════════════════════════════════════════════════
 * 
 * КНОПКИ (между пином и GND):
 * ┌────────────┬────────────┬──────────┐
 * │ Функция    │ Пин на     │ Arduino  │
 * │            │ плате      │ D        │
 * ├────────────┼────────────┼──────────┤
 * │ ВВЕРХ ↑    │ 115        │ D18      │
 * │ ВНИЗ ↓     │ 009        │ D10      │
 * │ ВЛЕВО ←    │ 002        │ D19      │
 * │ ВПРАВО →   │ 031        │ D21      │
 * │ ПАУЗА      │ 113        │ D15      │
 * └────────────┴────────────┴──────────┘
 * 
 * ПОТЕНЦИОМЕТР 10кОм:
 * ┌──────────────┬──────────────────────┐
 * │ Пин потенц.  │ Подключение          │
 * ├──────────────┼──────────────────────┤
 * │ Крайний 1    │ VCC (3.3V)           │
 * │ Средний      │ D20 (пин 029)        │
 * │ Крайний 2    │ GND                  │
 * └──────────────┴──────────────────────┘
 * 
 *     Потенциометр 10кОм
 *     ┌─────────────┐
 *     │             │
 *     │      ┌──┐   │
 *  1──┤─────►│  │   │
 *     │      └──┘   │
 *  2──┤─────────────┤
 *     │             │
 *  3──┤─────────────┤
 *     │             │
 *     └─────────────┘
 *     
 *  Пин 1 → VCC (3.3V)
 *  Пин 2 → D20 (029) на плате
 *  Пин 3 → GND
 * 
 * ═══════════════════════════════════════════════════════════
 * ТЕСТИРОВАНИЕ
 * ═══════════════════════════════════════════════════════════
 * 
 * 1. ПРОВЕРКА КНОПОК:
 *    - Откройте Serial Monitor (115200 baud)
 *    - Нажимайте кнопки - должны появляться сообщения
 *    - В текстовом редакторе проверьте стрелки и пробел
 * 
 * 2. ПРОВЕРКА ГРОМКОСТИ:
 *    - Откройте любой плеер/браузер с видео
 *    - Вращайте потенциометр
 *    - Громкость системы должна меняться
 *    - В Serial Monitor смотрите процент громкости
 * 
 * ═══════════════════════════════════════════════════════════
 * НАСТРОЙКИ
 * ═══════════════════════════════════════════════════════════
 * 
 * ЧУВСТВИТЕЛЬНОСТЬ ПОТЕНЦИОМЕТРА:
 * - Если громкость меняется слишком быстро:
 *   увеличьте volumeChangeDelay со 100 до 200
 * 
 * - Если громкость меняется слишком медленно:
 *   уменьшите volumeChangeDelay со 100 до 50
 * 
 * - Если много ложных срабатываний:
 *   увеличьте порог в handleVolume() с 10 до 20
 * 
 * ИЗМЕНИТЬ НАЗНАЧЕНИЕ КНОПОК:
 * - Измените константы KEY_* на нужные HID коды
 * 
 * ДОБАВИТЬ ДРУГИЕ МЕДИА-КЛАВИШИ:
 * - HID_USAGE_CONSUMER_PLAY_PAUSE (воспроизведение/пауза)
 * - HID_USAGE_CONSUMER_SCAN_NEXT (следующий трек)
 * - HID_USAGE_CONSUMER_SCAN_PREVIOUS (предыдущий трек)
 * - HID_USAGE_CONSUMER_MUTE (выключить звук)
 * 
 * ═══════════════════════════════════════════════════════════
 */
