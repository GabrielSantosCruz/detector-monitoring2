#include <Wire.h>
#include <WiFi.h>
#include "pcf8563.h"      // Biblioteca Lewis He (SensorLib)
#include "esp_sleep.h"    
#include "driver/gpio.h"  // Para gpio_wakeup_enable()
#include "esp_task_wdt.h"

PCF8563_Class rtc;

#define RTC_INT_PIN 10  // GPIO conectada ao INT do PCF8563
#define RTC_SDA_PIN 8
#define RTC_SCL_PIN 9
#define GEIGER_PIN  0

#define ARRAY_SIZE 24

volatile bool alarmeDisparado = false;
int pulsosDesdeUltimoWakeup;
const char* ssid     = "ssid";
const char* password = "password";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600;  // Fuso horário (Brasil: -3h)
const int   daylightOffset_sec = 0;     // Horário de verão (0 atualmente)
volatile unsigned long contadorHoras[ARRAY_SIZE] = { 0 };

void IRAM_ATTR handleRtcInterrupt();
void clearRtcAlarmFlag();
void IRAM_ATTR contarPulsoISR();
void syncRTCWithNTP();
void print_array();
void wifiConnect();

esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 10000,       // 10 segundos
    .idle_core_mask = (1 << 0),// Monitora core 0
    .trigger_panic = true      // Reinicia em caso de travamento
};

void setup() {
  Serial.begin(115200);
  while(!Serial);

  Serial.println("ESP acabou de ligar!");

  // Inicializa o watchdog com a configuração
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);  // Adiciona a task principal (loop)

  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);  

  if(!rtc.begin(Wire)){
    Serial.println("\nErro ao iniciar o RTC!");
    while(1){
      Serial.print(".");
      delay(1000);
    }
  }

  wifiConnect(); // ver posteriormente como desconectar/desligar o wifi para economizar energia

  // Verificar isso aqui posteriormente para que a a configuração seja feita apenas uma vez
  // Sincroniza com NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  syncRTCWithNTP();
  esp_task_wdt_reset();

  // apenas desativa o alarme sem limpar as flags do anterior
  rtc.disableAlarm();
  clearRtcAlarmFlag();

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

void syncRTCWithNTP() {
  // Obtém a hora atual do sistema (sincronizada via NTP)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Falha ao obter tempo NTP");
    return;
  }

  // Configura o RTC com a hora obtida
  rtc.setDateTime(
    1900 + timeinfo.tm_year, // tm_year é "anos desde 1900"
    timeinfo.tm_mon + 1,     // tm_mon começa em 0
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );

  Serial.println("RTC sincronizado com NTP!");
}

void print_array(){
  Serial.print("[");
  for(int i = 0; i < ARRAY_SIZE; i++){
    Serial.print(contadorHoras[i]);
    if(i < ARRAY_SIZE-1){
      Serial.print(", ");
    }
  }
  Serial.println("]");
}

void wifiConnect(){
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
}

void loop() {
  RTC_Date now = rtc.getDateTime();
  esp_task_wdt_reset();

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