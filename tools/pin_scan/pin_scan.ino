/*
 * PIN SCAN — полная диагностика пинов nice!nano v2 (nRF52840)
 *
 * Перебирает ВСЕ номера пинов, известные ядру, и показывает для каждого
 * реальный порт nRF52840 (P0.xx / P1.xx) из g_ADigitalPinMap.
 * Цель: получить точное соответствие «надпись на плате -> номер для pinMode()»
 * и проверить, какие пины реально работают.
 *
 * Serial Monitor: 115200, перевод строки не обязателен.
 *
 * ДЕЙСТВИЯ
 *  1. Загрузите скетч, откройте Serial Monitor. Появится таблица всех пинов.
 *  2. Пины, читающиеся как LOW без касания, помечаются IGNORED
 *     (заняты платой/внутренними цепями) и в тесте не участвуют.
 *  3. Проводом замыкайте физические пины на GND по одному.
 *     Скетч напишет: номер pinMode(), порт и новое состояние.
 *     Сопоставьте с надписью на плате (006, 115, ...) и запишите.
 *
 * КОМАНДЫ (отправьте символ в Serial Monitor)
 *  m — вывести таблицу пинов заново
 *  r — заново определить «базовое» состояние (сбросить IGNORED)
 *  a — аналоговый тест: прочитать analogRead() со всех пинов
 *
 * АНАЛОГОВЫЙ ТЕСТ (команда a)
 *  Подключите ОДИН проверяемый пин к выводу 3V3/VCC платы (строго 3,3 В!)
 *  и отправьте 'a'. Пин с аналоговым входом покажет ≈ 938,
 *  при подключении к GND — ≈ 0.
 *
 * ВНИМАНИЕ: никогда не подключайте к пинам GPIO вывод RAW/VIN —
 * на нём до 5 В, это убьёт чип.
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>  // нужен для Serial через USB в ядре Adafruit nRF52

#if defined(PINS_COUNT)
  const int NUM_PINS = PINS_COUNT;
#elif defined(NUM_DIGITAL_PINS)
  const int NUM_PINS = NUM_DIGITAL_PINS;
#else
  #error "Не удалось определить число пинов (PINS_COUNT): вероятно, не ядро Adafruit nRF52"
#endif

const int MAX_PINS = 64;
bool lastState[MAX_PINS];
bool ignored[MAX_PINS];

void portName(int i, char* buf, size_t len) {
  uint32_t hw = g_ADigitalPinMap[i];
  snprintf(buf, len, "P%lu.%02lu", (unsigned long)(hw >> 5), (unsigned long)(hw & 31));
}

void baseline() {
  for (int i = 0; i < NUM_PINS && i < MAX_PINS; i++) {
    pinMode(i, INPUT_PULLUP);
  }
  delay(20);
  for (int i = 0; i < NUM_PINS && i < MAX_PINS; i++) {
    lastState[i] = digitalRead(i);
    ignored[i] = (lastState[i] == LOW);  // подтянут к земле внутри — не трогаем
  }
}

void printTable() {
  Serial.println();
  Serial.println("pinMode | порт  | состояние");
  Serial.println("--------+-------+----------------------------");
  for (int i = 0; i < NUM_PINS && i < MAX_PINS; i++) {
    char port[12];
    portName(i, port, sizeof(port));
    char line[64];
    snprintf(line, sizeof(line), "%7d | %-5s | %s", i, port,
             ignored[i] ? "IGNORED (LOW без касания)" : (lastState[i] ? "HIGH (ок)" : "LOW"));
    Serial.println(line);
  }
  Serial.println();
  Serial.println("Замыкайте пины на GND по одному. Команды: m, r, a");
}

void analogScan() {
  Serial.println();
  Serial.println("=== Аналоговый тест (10 бит, референс по умолчанию) ===");
  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  for (int i = 0; i < NUM_PINS && i < MAX_PINS; i++) {
    if (ignored[i]) continue;
    pinMode(i, INPUT);
    delay(2);
    analogRead(i);  // холостое чтение
    int v = analogRead(i);
    char port[12];
    portName(i, port, sizeof(port));
    char line[64];
    snprintf(line, sizeof(line), "pinMode %2d (%s): %d", i, port, v);
    Serial.println(line);
    pinMode(i, INPUT_PULLUP);
  }
  Serial.println("Значения у неподключённых пинов плавают — смотрите на пин, который вы подключили.");
  delay(20);
  for (int i = 0; i < NUM_PINS && i < MAX_PINS; i++) lastState[i] = digitalRead(i);
}

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000) delay(10);

  Serial.println("\n=== PIN SCAN nice!nano v2 ===");
  char info[64];
  snprintf(info, sizeof(info), "Пинов в ядре: %d", NUM_PINS);
  Serial.println(info);

  baseline();
  printTable();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'm') printTable();
    else if (c == 'r') { baseline(); printTable(); }
    else if (c == 'a') analogScan();
  }

  for (int i = 0; i < NUM_PINS && i < MAX_PINS; i++) {
    if (ignored[i]) continue;
    bool s = digitalRead(i);
    if (s != lastState[i]) {
      delay(20);  // антидребезг
      s = digitalRead(i);
      if (s != lastState[i]) {
        lastState[i] = s;
        char port[12];
        portName(i, port, sizeof(port));
        char line[64];
        snprintf(line, sizeof(line), "pinMode %2d -> %s : %s", i, port, s ? "HIGH (разомкнут)" : "LOW (замкнут на GND)");
        Serial.println(line);
      }
    }
  }
  delay(5);
}
