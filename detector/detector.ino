#include <Wire.h>
#include "pcf8563.h"      // Biblioteca Lewis He
#include "esp_sleep.h"    
#include "driver/gpio.h"  // Para gpio_wakeup_enable()

PCF8563_Class rtc;

#define RTC_INT_PIN 10  // GPIO conectada ao INT do PCF8563
#define RTC_SDA_PIN 8
#define RTC_SCL_PIN 9
#define GEIGER_PIN  0

volatile bool alarmeDisparado = false;
int pulsosDesdeUltimoWakeup;

void IRAM_ATTR handleRtcInterrupt();
void clearRtcAlarmFlag();
void IRAM_ATTR contarPulsoISR();

void setup() {
  Serial.begin(115200);
  while(!Serial);

  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);  

  if(!rtc.begin(Wire)){
    Serial.println("\nErro ao iniciar o RTC!");
    while(1){
      Serial.print(".");
      delay(1000);
    }
  }

  rtc.setDateTime(2025, 7, 25, 10, 0, 0);

  // Limpa alarmes anteriores e flags
  rtc.disableAlarm();

  RTC_Date now = rtc.getDateTime();
  uint8_t alarmMinute = (now.minute + 1) % 60;
  rtc.setAlarmByMinutes(alarmMinute);
  rtc.enableAlarm();

  pinMode(RTC_INT_PIN, INPUT_PULLUP);

  // Configura interrupção no pino INT para borda de descida (ativo LOW)
  attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), handleRtcInterrupt, FALLING);

  Serial.printf("Alarme programado para %02d:%02d\n", now.hour, alarmMinute);
  Serial.println("Aguardando alarme...");

  pinMode(GEIGER_PIN, INPUT);  // Configura o pino como entrada

  // Configura interrupção na borda de descida
  attachInterrupt(digitalPinToInterrupt(GEIGER_PIN), contarPulsoISR, FALLING);
}

void IRAM_ATTR handleRtcInterrupt() {
  alarmeDisparado = true;
}

void clearRtcAlarmFlag() {
  const uint8_t PCF8563_ADDR = 0x51;  // Endereço I2C padrão do PCF8563
  const uint8_t REG_CS2 = 0x01;       // Control/Status2

  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(REG_CS2);
  Wire.endTransmission(false);

  Wire.requestFrom(PCF8563_ADDR, (uint8_t)1);
  uint8_t cs2 = Wire.read();

  // Limpa o bit AF (bit 3)
  cs2 &= ~(1 << 3);

  // Escreve de volta no registrador
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(REG_CS2);
  Wire.write(cs2);
  Wire.endTransmission();
}

void IRAM_ATTR contarPulsoISR() {
  pulsosDesdeUltimoWakeup++;
}

void loop() {
  RTC_Date now = rtc.getDateTime();

  Serial.printf("[%02d:%02d:%02d]\n", now.hour, now.minute, now.second);

  if (alarmeDisparado) {
    Serial.println(">> Alarme do RTC disparado! <<");
    alarmeDisparado = false;

    clearRtcAlarmFlag();

    RTC_Date now = rtc.getDateTime();
    uint8_t alarmMinute = (now.minute + 1) % 60;
    rtc.setAlarmByMinutes(alarmMinute);
    rtc.enableAlarm();
  }

  delay(1000);
}