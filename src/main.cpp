#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// =========================================
// CONFIGURAÇÕES DO WIFI
// =========================================
const char* WIFI_SSID = "GHEYSON";
const char* WIFI_PASSWORD = "22OXEmm19";

// =========================================
// CONFIGURAÇÕES DO MQTT
// =========================================
const char* MQTT_BROKER = "172.26.66.22";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "cisterna/status";

// =========================================
// PINOS DO SENSOR ULTRASSÔNICO
// =========================================
const int PINO_TRIG = 4;
const int PINO_ECHO = 2;

// =========================================
// PINO DO POTENCIÔMETRO
// =========================================
const int PINO_POT = 32;

// =========================================
// PINOS DO LED RGB
// =========================================
const int PINO_BLUE = 21;
const int PINO_GREEN = 22;
const int PINO_RED = 23;

// =========================================
// LIMITES DE TURBIDEZ
// =========================================
const int TURBIDEZ_LIMPA = 30;
const int TURBIDEZ_ATENCAO = 60;

// =========================================
// OBJETOS DE REDE
// =========================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =========================================
// TELEMETRIA
// =========================================
struct Amostra {
  int id;
  unsigned long timestamp;

  float nivel_cm;
  int nivel_percentual;

  int turbidez_simulada;

  String status;

  int buffer_index;
  int buffer_ocupacao;
};

// =========================================
// TAMANHO DOS TESTES
// ALTERAR PARA:
// 100
// 5000
// 20000
// =========================================
#define TAM_BUFFER 100

// =========================================
// FUNÇÕES DO LED RGB
// =========================================
void apagarTodas() {
  digitalWrite(PINO_RED, LOW);
  digitalWrite(PINO_GREEN, LOW);
  digitalWrite(PINO_BLUE, LOW);
}

void acenderVermelho() {
  apagarTodas();
  digitalWrite(PINO_RED, HIGH);
}

void acenderVerde() {
  apagarTodas();
  digitalWrite(PINO_GREEN, HIGH);
}

void acenderAzul() {
  apagarTodas();
  digitalWrite(PINO_BLUE, HIGH);
}

// =========================================
// CONECTAR AO WIFI
// =========================================
void conectarWiFi() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("Conectando ao Wi-Fi...");
  Serial.println("====================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi conectado com sucesso!");
  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());
  Serial.println("====================================");
}

// =========================================
// CONECTAR AO MQTT
// =========================================
void conectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando ao MQTT...");

    // Cliente simples com ID único
    String clientId = "ESP32-CISTERNA-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" conectado!");
    } else {
      Serial.print(" falhou, estado=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando novamente em 2s");
      delay(2000);
    }
  }
}

// =========================================
// LEITURA DO SENSOR ULTRASSÔNICO
// =========================================
float medirDistancia() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracao = pulseIn(PINO_ECHO, HIGH, 30000);

  if (duracao == 0) {
    return -1;
  }

  float distancia = (duracao * 0.0343) / 2.0;
  return distancia;
}

// =========================================
// LEITURA DA TURBIDEZ SIMULADA
// =========================================
int lerTurbidez() {
  int valorPot = analogRead(PINO_POT);
  int turbidez = map(valorPot, 0, 4095, 0, 100);
  return turbidez;
}

// =========================================
// DEFINE O STATUS DA ÁGUA
// =========================================
String obterStatusAgua(int turbidez) {
  if (turbidez <= TURBIDEZ_LIMPA) {
    acenderVerde();
    return "AGUA LIMPA";
  }

  if (turbidez <= TURBIDEZ_ATENCAO) {
    acenderAzul();
    return "ATENCAO";
  }

  acenderVermelho();
  return "AGUA TURVA / LIMPEZA NECESSARIA";
}

// =========================================
// PUBLICA OS DADOS NO MQTT
// =========================================
void publicarMQTT(float distancia, int turbidez, const String& statusAgua) {
  char payload[200];

  // Se a leitura do ultrassônico falhar, envia -1
  snprintf(
    payload,
    sizeof(payload),
    "{\"distancia_cm\":%.2f,\"turbidez\":%d,\"status\":\"%s\"}",
    distancia,
    turbidez,
    statusAgua.c_str()
  );

  mqttClient.publish(MQTT_TOPIC, payload);

  Serial.print("MQTT enviado em ");
  Serial.print(MQTT_TOPIC);
  Serial.print(": ");
  Serial.println(payload);
}

void setup() {
  // Inicializa serial
  Serial.begin(9600);

  // Configura ultrassônico
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);

  // Configura potenciômetro
  pinMode(PINO_POT, INPUT);

  // Configura LED RGB
  pinMode(PINO_RED, OUTPUT);
  pinMode(PINO_GREEN, OUTPUT);
  pinMode(PINO_BLUE, OUTPUT);

  apagarTodas();

  // Conecta no Wi-Fi
  conectarWiFi();

  // Configura broker MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
}

void loop() {
  // Garante que o MQTT esteja conectado
  if (!mqttClient.connected()) {
    conectarMQTT();
  }
  mqttClient.loop();

  // =========================================
  // MEDIÇÃO DE DISTÂNCIA
  // =========================================
  float distancia = medirDistancia();

  if (distancia >= 0) {
    Serial.print("Distancia: ");
    Serial.print(distancia);
    Serial.println(" cm");
  } else {
    Serial.println("Sem leitura do ultrassonico");
  }

  // =========================================
  // LEITURA DA TURBIDEZ
  // =========================================
  int turbidez = lerTurbidez();

  Serial.print("Turbidez simulada: ");
  Serial.println(turbidez);

  // =========================================
  // STATUS DA ÁGUA
  // =========================================
  String statusAgua = obterStatusAgua(turbidez);

  Serial.print("Status: ");
  Serial.println(statusAgua);

  // =========================================
  // PUBLICA NO MQTT
  // =========================================
  publicarMQTT(distancia, turbidez, statusAgua);

  Serial.println("------------------------------------");

  delay(1000);
}