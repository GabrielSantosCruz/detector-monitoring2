#include <Wire.h>
#include "pcf8563.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

// Pinagem para ESP32-C3 Mini
#define RTC_INT_PIN 3   // GPIO3 para interrupção (bom para wakeup)
#define RTC_SDA_PIN 8   // GPIO4 para SDA
#define RTC_SCL_PIN 9   // GPIO5 para SCL

PCF8563_Class rtc;

// Função para limpar o flag de alarme (como você forneceu)
void clearRtcAlarmFlag() {
  const uint8_t PCF8563_ADDR = 0x51;
  const uint8_t REG_CS2 = 0x01;

  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(REG_CS2);
  Wire.endTransmission(false);

  Wire.requestFrom(PCF8563_ADDR, (uint8_t)1);
  uint8_t cs2 = Wire.read();

  cs2 &= ~(1 << 3);

  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(REG_CS2);
  Wire.write(cs2);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  while(!Serial); // Aguarda conexão serial
  Serial.println("ESP32-C3 iniciado");
  
  // Inicializa I2C
  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
  
  // Configuração do pino de interrupção
  pinMode(RTC_INT_PIN, INPUT_PULLUP);

  // Inicializa RTC
  if(!rtc.begin(Wire)) {
    Serial.println("Falha ao iniciar RTC!");
    while(1);
  }
  Serial.println("RTC iniciado corretamente");

  // Configura alarme para 1 minuto no futuro
  RTC_Date now = rtc.getDateTime();
  uint8_t alarmMinute = (now.minute + 1) % 60;
  
  rtc.disableAlarm();          // Desativa alarmes anteriores
  clearRtcAlarmFlag();         // Limpa flags pendentes
  rtc.setAlarmByMinutes(alarmMinute);  // Configura novo alarme
  rtc.enableAlarm();           // Habilita o alarme

  Serial.printf("Alarme programado para %02d:%02d:%02d\n", 
               now.hour, alarmMinute, now.second);

  // Configura wakeup para ESP32-C3
  esp_deep_sleep_enable_gpio_wakeup(1 << RTC_INT_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  
  Serial.println("Entrando em deep sleep...");
  delay(100); // Pequeno delay para garantir envio serial
  esp_deep_sleep_start();
}

void loop() {}