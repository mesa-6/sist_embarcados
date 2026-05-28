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
const char* MQTT_BROKER = "10.0.0.155";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "cisterna/status";

// =========================================
// PINOS DO SENSOR ULTRASSÔNICO
// =========================================
const int PINO_TRIG = 4;
const int PINO_ECHO = 2;

// =========================================
// PINO DO SENSOR DE TURBIDEZ
// =========================================
const int PINO_TURBIDEZ = 32;

// =========================================
// ALTURA ÚTIL DA CISTERNA
// =========================================
const float ALTURA_UTIL_CISTERNA_CM = 100.0;

// =========================================
// OBJETOS DE REDE
// =========================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =========================================
// CONECTAR AO WIFI
// =========================================
void conectarWiFi() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("Conectando ao Wi-Fi...");
  Serial.println("====================================");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
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
// COM MÉDIA DE 10 LEITURAS
// =========================================
float medirDistancia() {

  float somaDistancias = 0;
  int leiturasValidas = 0;

  // Faz 10 leituras
  for (int i = 0; i < 10; i++) {

    // Disparo do TRIG
    digitalWrite(PINO_TRIG, LOW);
    delayMicroseconds(2);

    digitalWrite(PINO_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PINO_TRIG, LOW);

    // Mede o tempo do ECHO
    long duracao = pulseIn(PINO_ECHO, HIGH, 30000);

    // Ignora leituras inválidas
    if (duracao > 0) {

      float distancia = (duracao * 0.0343) / 2.0;

      somaDistancias += distancia;
      leiturasValidas++;
    }

    delay(5);
  }

  // Se nenhuma leitura válida
  if (leiturasValidas == 0) {
    return -1;
  }

  // Retorna média das leituras válidas
  return somaDistancias / leiturasValidas;
}

// =========================================
// LEITURA DA TURBIDEZ COM MÉDIA DE 10 AMOSTRAS
// =========================================
int lerTurbidez() {

  long somaLeituras = 0;

  // Faz 10 leituras do sensor
  for (int i = 0; i < 10; i++) {
    somaLeituras += analogRead(PINO_TURBIDEZ);

    // Pequeno delay para estabilizar leitura
    delay(5);
  }

  // Calcula média das 10 leituras
  int valorBrutoMedio = somaLeituras / 10;

  // Converte para percentual de turbidez
  // 4095 = água limpa (0%)
  // 0 = água muito suja (100%)
  int turbidez = map(valorBrutoMedio, 0, 4095, 100, 0);

  // Garante limites válidos
  turbidez = constrain(turbidez, 0, 100);

  return turbidez;
}

// =========================================
// CONVERTE DISTÂNCIA EM NÍVEL PERCENTUAL
// =========================================
float calcularNivelPercentual(float distanciaCm) {
  if (distanciaCm < 0) {
    return -1;
  }

  float nivel = ((ALTURA_UTIL_CISTERNA_CM - distanciaCm) / ALTURA_UTIL_CISTERNA_CM) * 100.0;

  if (nivel < 0) nivel = 0;
  if (nivel > 100) nivel = 100;

  return nivel;
}

// =========================================
// PUBLICA OS DADOS NO MQTT
// =========================================
void publicarMQTT(float distancia, float nivelPercentual, int turbidez) {
  char payload[220];

  snprintf(
    payload,
    sizeof(payload),
    "{\"distancia_cm\":%.2f,\"nivel_percentual\":%.2f,\"turbidez\":%d}",
    distancia,
    nivelPercentual,
    turbidez
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

  // Configura sensor de turbidez
  pinMode(PINO_TURBIDEZ, INPUT);

  // Ajustes de leitura analógica no ESP32
  analogReadResolution(12);
  analogSetPinAttenuation(PINO_TURBIDEZ, ADC_11db);

  // =====================================================
  // WI-FI / MQTT TEMPORARIAMENTE DESATIVADOS
  // =====================================================
  // Conecta ao Wi-Fi
  // conectarWiFi();

  // Configura broker MQTT
  // mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  // mqttClient.setBufferSize(256);
  // mqttClient.setKeepAlive(30);

  Serial.println();
  Serial.println("Sistema iniciado.");
  Serial.println("Wi-Fi/MQTT mantidos no codigo, mas desativados por ora.");
  Serial.println("------------------------------------");
}

void loop() {
  // =====================================================
  // WI-FI / MQTT TEMPORARIAMENTE DESATIVADOS
  // =====================================================
  // Garante conexão Wi-Fi
  // if (WiFi.status() != WL_CONNECTED) {
  //   conectarWiFi();
  // }

  // Garante conexão MQTT
  // if (!mqttClient.connected()) {
  //   conectarMQTT();
  // }
  // mqttClient.loop();

  // =========================================
  // MEDIÇÃO DE DISTÂNCIA
  // =========================================
  float distancia = medirDistancia();

  if (distancia >= 0) {
    Serial.print("Distancia ate a agua: ");
    Serial.print(distancia);
    Serial.println(" cm");
  } else {
    Serial.println("Sem leitura do ultrassonico");
  }

  // =========================================
  // CÁLCULO DO NÍVEL DA CISTERNA
  // =========================================
  float nivelPercentual = calcularNivelPercentual(distancia);

  if (nivelPercentual >= 0) {
    Serial.print("Nivel da cisterna: ");
    Serial.print(nivelPercentual);
    Serial.println(" %");
  } else {
    Serial.println("Nao foi possivel calcular o nivel");
  }

  // =========================================
  // LEITURA DA TURBIDEZ
  // =========================================
  int turbidez = lerTurbidez();

  Serial.print("Turbidez (media de 10 leituras): ");
  Serial.print(turbidez);
  Serial.println("%");

  // =========================================
  // STATUS SIMPLES DA ÁGUA
  // =========================================
  if (turbidez <= 30) {
    Serial.println("Status: AGUA LIMPA");
  } else if (turbidez <= 60) {
    Serial.println("Status: ATENCAO");
  } else {
    Serial.println("Status: AGUA TURVA / LIMPEZA NECESSARIA");
  }

  // =====================================================
  // PUBLICAÇÃO MQTT TEMPORARIAMENTE DESATIVADA
  // =====================================================
  // publicarMQTT(distancia, nivelPercentual, turbidez);

  Serial.println("------------------------------------");

  delay(3000);
}