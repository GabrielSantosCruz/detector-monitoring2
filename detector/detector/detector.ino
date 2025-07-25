#include <Wire.h>
#include "pcf8563.h"     // Biblioteca Lewis He
#include "esp_sleep.h"   // Deep sleep
#include "driver/gpio.h" // Para gpio_wakeup_enable()

PCF8563_Class rtc;

#define RTC_INT_PIN 10  // GPIO conectada ao INT do PCF8563
#define RTC_SDA_PIN 8
#define RTC_SCL_PIN 9

volatile bool alarmeDisparado = false;

void IRAM_ATTR handleRtcInterrupt();

void setup() {
  Serial.begin(115200);
  delay(2000);  // Dá tempo de abrir o monitor serial

  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN); // Ajuste para os seus pinos SDA e SCL

  rtc.begin(Wire);

  // Configura data/hora inicial (ajuste para hora atual)
  rtc.setDateTime(2025, 7, 25, 10, 0, 0);

  // Limpa alarmes anteriores e flags
  rtc.disableAlarm();

  // Programa alarme para 1 minuto após o minuto atual
  RTC_Date now = rtc.getDateTime();
  uint8_t alarmMinute = (now.minute + 1) % 60;
  rtc.setAlarmByMinutes(alarmMinute);
  rtc.enableAlarm();

  pinMode(RTC_INT_PIN, INPUT_PULLUP);

  // Configura interrupção no pino INT para borda de descida (ativo LOW)
  attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), handleRtcInterrupt, FALLING);

  Serial.printf("Alarme programado para %02d:%02d\n", now.hour, alarmMinute);
  Serial.println("Aguardando alarme...");
}

void IRAM_ATTR handleRtcInterrupt() {
  alarmeDisparado = true;
  Serial.println(">> Alarme do RTC disparado! <<");
}

void loop() {
  RTC_Date now = rtc.getDateTime();

  Serial.printf("[%02d:%02d:%02d]\n", now.hour, now.minute, now.second);

  if (alarmeDisparado) {
    // Serial.println(">> Alarme do RTC disparado! <<");
    alarmeDisparado = false;

    // Aqui você pode limpar o alarme no RTC, reprogramar, etc.
    rtc.disableAlarm();
    // Reprograma próximo alarme, se quiser...
  }

  delay(1000);
}
