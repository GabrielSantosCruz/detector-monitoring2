#include <Wire.h>
#include <WiFi.h>
#include "pcf8563.h"  // Biblioteca Lewis He (SensorLib)
#include "esp_sleep.h"
#include "driver/gpio.h"  // Para gpio_wakeup_enable()
#include "esp_task_wdt.h"
#include <SPI.h>
#include <SD.h>

PCF8563_Class rtc;

#define RTC_INT_PIN 3  // GPIO conectada ao INT do PCF8563
#define RTC_SDA_PIN 8
#define RTC_SCL_PIN 9
#define GEIGER_PIN 0
#define SD_CS 2  // Chip Select do SD
#define SD_MISO 5
#define SD_MOSI 6
#define SD_SCK 4
#define LED_HEARTBEAT 10

#define ARRAY_SIZE 24

volatile bool alarmeDisparado = false;
int pulsosDesdeUltimoWakeup = 0;
const char* ssid = "";
const char* password = "";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;  // Fuso horário (Brasil: -3h)
const int daylightOffset_sec = 0;      // Horário de verão (0 atualmente)
volatile unsigned long contadorHoras[ARRAY_SIZE] = { 0 };
bool rtcSincronizado = false;

unsigned long ultimoPiscaLed = 0;
const int intervaloPisca = 1000;
bool estadoLed = false;

void IRAM_ATTR handleRtcInterrupt();
void clearRtcAlarmFlag();
void IRAM_ATTR contarPulsoISR();
void syncRTCWithNTP();
void print_array();
void wifiConnect();
String getFileName();
void writeHeader(File dataFile);
void writeDataToSD();
void atualizarHeartbeat();

esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 20000,
  .idle_core_mask = (1 << 0),  // Monitora core 0
  .trigger_panic = true        // Reinicia em caso de travamento
};

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

  Serial.println("ESP acabou de ligar!");

  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);

  if (!rtc.begin(Wire)) {
    Serial.println("\nErro ao iniciar o RTC!");
    while (1) {
      Serial.print(".");
      delay(1000);
    }
  }
  Serial.println("RTC pronto.");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 1000000)) {  // 4 MHz
    Serial.println("Falha ao inicializar SD!");
    esp_task_wdt_delete(NULL);  // evita reboot enquanto depura
    while (1) {
      Serial.print(".");
      delay(1000);
    }
  }
  Serial.println("Cartão SD pronto.");

  esp_task_wdt_add(NULL);  // Registra a tarefa principal (loop)

  // Verifica se o RTC já tem um horário válido (evita sincronização desnecessária)
  RTC_Date now = rtc.getDateTime();
  if (now.year < 2024) {    // Se o RTC não estiver configurado (ano inválido)
    wifiConnect();          // Conecta ao Wi-Fi apenas para sincronizar o RTC
    syncRTCWithNTP();       // Sincroniza o RTC com NTP
    WiFi.disconnect(true);  // Desconecta o Wi-Fi
    WiFi.mode(WIFI_OFF);    // Desativa completamente o Wi-Fi
    rtcSincronizado = true;

    now = rtc.getDateTime();
  }

  uint8_t alarmHourReg = getRtcAlarmHour();
  bool alarmIsEnabled = !(alarmHourReg & 0x80);
  uint8_t alarmHourBcd = alarmHourReg & 0x7F;
  
  // Converte o valor da hora lido do RTC (que está em BCD) para decimal
  uint8_t alarmHourValue = bcdToDec(alarmHourBcd);
  
  uint8_t nextHour = (now.hour + 1) % 24;

  if (!alarmIsEnabled || alarmHourValue != nextHour) {
    Serial.println("Alarme do RTC não configurado ou incorreto. Reconfigurando...");
    
    rtc.disableAlarm();
    clearRtcAlarmFlag();
    
    rtc.setAlarmByHours(nextHour);
    rtc.enableAlarm();
    
    Serial.printf("Novo alarme programado para aproximadamente %02d:00\n", nextHour);
  } else {
    Serial.printf("Alarme do RTC já está configurado para aproximadamente %02d:00. Aguardando...\n", alarmHourValue);
  }
  
  esp_task_wdt_reset();

  pinMode(RTC_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), handleRtcInterrupt, FALLING);

  pinMode(GEIGER_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GEIGER_PIN), contarPulsoISR, FALLING);

  pinMode(LED_HEARTBEAT, OUTPUT);
  digitalWrite(LED_HEARTBEAT, LOW);
  Serial.println("LED heartbeat configurado");

  esp_task_wdt_reset();
}

void IRAM_ATTR handleRtcInterrupt() {
  alarmeDisparado = true;
}

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

// Função para converter de BCD (Binary-Coded Decimal) para Decimal
uint8_t bcdToDec(uint8_t val) {
  return ((val >> 4) * 10) + (val & 0x0F);
}

uint8_t getRtcAlarmHour() {
  const uint8_t PCF8563_ADDR = 0x51;
  const uint8_t REG_ALRM_HOUR = 0x0A;

  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(REG_ALRM_HOUR);
  Wire.endTransmission(false);

  Wire.requestFrom(PCF8563_ADDR, (uint8_t)1);
  uint8_t alarmHour = Wire.read();
  return alarmHour;
}

void IRAM_ATTR contarPulsoISR() {
  pulsosDesdeUltimoWakeup++;
}

void syncRTCWithNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi não conectado. Não foi possível sincronizar NTP.");
    return;
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  unsigned long start = millis();

  while (!getLocalTime(&timeinfo)) {
    if (millis() - start > 10000) {  // Timeout de 10 segundos
      Serial.println("Falha ao obter tempo NTP");
      return;
    }
    delay(500);
  }

  // Configura o RTC com o horário obtido do NTP
  rtc.setDateTime(
    1900 + timeinfo.tm_year,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec);
  Serial.println("RTC sincronizado com NTP!");
}

void print_array() {
  Serial.print("[");
  for (int i = 0; i < ARRAY_SIZE; i++) {
    Serial.print(contadorHoras[i]);
    if (i < ARRAY_SIZE - 1) Serial.print(", ");
  }
  Serial.println("]");
}

void wifiConnect() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi ");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) {  // Timeout de 15 segundos
      Serial.println("\nFalha ao conectar ao Wi-Fi!");
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
}

String getFileName() {
  RTC_Date now = rtc.getDateTime();
  char filename[20];
  snprintf(filename, sizeof(filename), "/dados_%04d%02d%02d.csv",
           now.year, now.month, now.day);
  return String(filename);
}

void writeHeader(File dataFile) {
  dataFile.print("hora > [contagem ultima hora] =>");
  dataFile.printf("{");
  for (int i = 0; i < ARRAY_SIZE; i++) {
    if (i == 0) {
      dataFile.printf("H%d", i);
    } else {
      dataFile.printf(",H%d", i);
    }
  }
  dataFile.printf("}");
  dataFile.println();
}

void writeDataToSD() {
  // Abre o arquivo (cria se não existir)
  String filename = getFileName();
  File dataFile = SD.open(filename, FILE_APPEND);

  if (!dataFile) {
    Serial.println("Erro ao abrir arquivo!");
    return;
  }

  if (dataFile.size() == 0) {
    writeHeader(dataFile);
  }

  RTC_Date now = rtc.getDateTime();

  dataFile.printf("%02d:%02d:%02d > [%d] => ",
                  now.hour, now.minute, now.second,
                  pulsosDesdeUltimoWakeup);

  dataFile.printf("{");
  for (int i = 0; i < ARRAY_SIZE; i++) {
    if (i == 0) {
      dataFile.printf("%d", contadorHoras[i]);
    } else {
      dataFile.printf(",%d", contadorHoras[i]);
    }
  }
  dataFile.printf("}");
  dataFile.println();

  dataFile.close();
  Serial.println("Dados gravados no SD!");
}

void atualizarHeartbeat() {
  unsigned long tempoAtual = millis();

  if (tempoAtual - ultimoPiscaLed >= intervaloPisca) {
    ultimoPiscaLed = tempoAtual;
    estadoLed = !estadoLed;
    digitalWrite(LED_HEARTBEAT, estadoLed);
    Serial.println(".");
  }
}

void loop() {
  atualizarHeartbeat();

  RTC_Date now = rtc.getDateTime();
  esp_task_wdt_reset();

  if (alarmeDisparado) {
    Serial.println(">> Alarme do RTC disparado! <<");
    alarmeDisparado = false;

    contadorHoras[now.hour] += pulsosDesdeUltimoWakeup;

    writeDataToSD();

    Serial.print("Pulsos na última hora: ");
    Serial.println(pulsosDesdeUltimoWakeup);
    pulsosDesdeUltimoWakeup = 0;

    uint8_t alarmHour = (now.hour + 1) % 24;
    rtc.setAlarmByHours(alarmHour);
    rtc.enableAlarm();
    clearRtcAlarmFlag();

    Serial.printf("Próximo alarme programado para %02d:%02d:00\n", alarmHour, now.minute);
    Serial.println("Aguardando alarme...");

    print_array();
  }

  delay(1000);
}