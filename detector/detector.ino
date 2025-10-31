#include <Wire.h>
#include <WiFi.h>
#include "pcf8563.h"  // Biblioteca Lewis He (SensorLib)
#include "esp_sleep.h"
#include "driver/gpio.h"  // Para gpio_wakeup_enable()
#include "esp_task_wdt.h"
#include <SPI.h>
#include <SD.h>

PCF8563_Class rtc;

#define RTC_INT_PIN 3  // GPIO conectada ao INT do PCF8563 (precisa mudar para um abaixo de 4)
#define RTC_SDA_PIN 8
#define RTC_SCL_PIN 9
#define GEIGER_PIN 0
#define SD_CS 2  // Chip Select do SD
#define SD_MISO 5
#define SD_MOSI 6
#define SD_SCK 4

#define ARRAY_SIZE 24

volatile bool alarmeDisparado = false;
int pulsosDesdeUltimoWakeup = 0;
const char* ssid = "UEFS_VISITANTES";
const char* password = "";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;  // Fuso horário (Brasil: -3h)
const int daylightOffset_sec = 0;      // Horário de verão (0 atualmente)
volatile unsigned long contadorHoras[ARRAY_SIZE] = { 0 };
bool rtcSincronizado = false;

unsigned long ultimoPiscaLed = 0;
const int intervaloPisca = 1000;
bool estadoLed = false;

uint8_t getRtcAlarmHour();
uint8_t bcdToDec(uint8_t val);
void IRAM_ATTR handleRtcInterrupt();
void clearRtcAlarmFlag();
void IRAM_ATTR contarPulsoISR();
void syncRTCWithNTP();
void print_array();
void wifiConnect();
String getFileName();
void writeHeader(File dataFile);
void writeDataToSD();
void logToFile(String logMessage);
void atualizarHeartbeat();
void printSerial(char text[]);

esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 20000,
  .idle_core_mask = (1 << 0),  // Monitora core 0
  .trigger_panic = true        // Reinicia em caso de travamento
};

void setup() {
  Serial.begin(115200);
  // colocar a verificação em uma variavel global aqui
  // while (!Serial);

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
  if (!SD.begin(SD_CS, SPI, 4000000)) {    // 4 MHz
    if (!SD.begin(SD_CS, SPI, 4000000)) {  // 4 MHz
      Serial.println("Falha ao inicializar SD!");
      esp_task_wdt_delete(NULL);  // evita reboot enquanto depura
      while (1) {
        Serial.print(".");
        delay(1000);
      }
    }
    esp_reset_reason_t reset_reason = esp_reset_reason();
    if (reset_reason == ESP_RST_TASK_WDT) {
      logToFile("ALERTA: Sistema reiniciado pelo Watchdog Timer (WDT).");
    }
    Serial.println("Cartão SD pronto.");
    logToFile("Sistema iniciado. RTC e Cartao SD prontos.");

    // Verifica e loga a causa do último reset

    esp_task_wdt_add(NULL);  // Registra a tarefa principal (loop)


    // Verifica se o RTC já tem um horário válido (evita sincronização desnecessária)
    RTC_Date now = rtc.getDateTime();
    if (now.year < 2024) {    // Se o RTC não estiver configurado (ano inválido)
      wifiConnect();          // Conecta ao Wi-Fi apenas para sincronizar o RTC
      syncRTCWithNTP();       // Sincroniza o RTC com NTP
      WiFi.disconnect(true);  // Desconecta o Wi-Fi e desliga o rádio
      WiFi.mode(WIFI_OFF);    // Desativa completamente o Wi-Fi
      rtcSincronizado = true;
      logToFile("Horário ajustado via NTP!");
    }

    // Configura o alarme para a proxima hora cheia
    uint8_t nextHour = (now.hour + 1) % 24;

    logToFile("Configurando alarme do RTC para a proxima hora: " + String(nextHour) + ":00");
    rtc.disableAlarm();
    clearRtcAlarmFlag();
    rtc.setAlarmByHours(nextHour);
    rtc.enableAlarm();

    esp_task_wdt_reset();

    pinMode(LED_HEARTBEAT, OUTPUT);
    pinMode(RTC_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), handleRtcInterrupt, FALLING);

    pinMode(GEIGER_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(GEIGER_PIN), contarPulsoISR, FALLING);

    logToFile("Configuracao inicial finalizada. Aguardando primeiro alarme...");
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
        logToFile("Falha ao obter tempo NTP");
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
        logToFile("nFalha ao conectar ao Wi-Fi!");
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

  void logToFile(String logMessage) {
    String filename = getFileName();
    File dataFile = SD.open(filename, FILE_APPEND);
    if (!dataFile) {
      Serial.println("Erro ao abrir arquivo para log!");
      return;
    }

    // Garante que o cabeçalho exista se o arquivo for novo
    if (dataFile.size() == 0) {
      writeHeader(dataFile);
    }

    RTC_Date now = rtc.getDateTime();
    char entry[256];

    snprintf(entry, sizeof(entry), "LOG [%02d:%02d:%02d]: %s",
             now.hour, now.minute, now.second, logMessage.c_str());

    dataFile.println(entry);
    dataFile.close();

    // Também imprime no Serial para monitoramento em tempo real
    Serial.println(entry);
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

    // Se o arquivo estiver vazio, escreve o cabeçalho
    /*if (dataFile.size() == 0) {
    writeHeader(dataFile);
  }*/

    // Obtém a hora atual
    RTC_Date now = rtc.getDateTime();

    // Escreve os dados
    dataFile.printf("%02d:%02d:%02d > [%d] => ",
                    now.hour, now.minute, now.second,
                    pulsosDesdeUltimoWakeup);

    // Escreve o array completo
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
    logToFile("Dados gravados no SD. Pulsos na ultima hora: " + String(pulsosDesdeUltimoWakeup));
  }

  void atualizarHeartbeat() {
    unsigned long tempoAtual = millis();

    if (tempoAtual - ultimoPiscaLed >= intervaloPisca) {
      ultimoPiscaLed = tempoAtual;
      estadoLed = !estadoLed;
      digitalWrite(LED_HEARTBEAT, estadoLed);
      Serial.print(".");
    }
  }

  void printSerial(char text[]) {
    if (Serial) {
      Serial.printf("%s", text);
    }
  }

  void loop() {
    atualizarHeartbeat();
    RTC_Date now = rtc.getDateTime();
    esp_task_wdt_reset();

    if (alarmeDisparado) {
      logToFile("Alarme do RTC disparado!");
      alarmeDisparado = false;

      // Atualiza o array de contagem
      uint8_t horaAnterior = (now.hour + 23) % 24;
      contadorHoras[horaAnterior] += pulsosDesdeUltimoWakeup;

      // Grava os dados no SD
      writeDataToSD();
      pulsosDesdeUltimoWakeup = 0;  // Zera para a próxima hora

      // Zera o array de contagem diária à meia-noite.
      if (now.hour == 0) {
        for (int i = 0; i < ARRAY_SIZE; i++) {
          contadorHoras[i] = 0;
        }
        logToFile("Novo dia detectado! Zerando o array de contagem diaria.");
        // tem que lembrar que os dados completos estão sendo salvos no 00:00:00 do dia seguinte
      }


      // Configura o alarme para a proxima hora cheia
      uint8_t nextHour = (now.hour + 1) % 24;
      rtc.disableAlarm();
      clearRtcAlarmFlag();
      rtc.setAlarmByHours(nextHour);
      rtc.enableAlarm();

      logToFile("Proximo alarme programado para " + String(nextHour) + ":00. Aguardando...");

      // Mostra o array atualizado no Serial para depuração
      print_array();
    }

    delay(1000);
  }