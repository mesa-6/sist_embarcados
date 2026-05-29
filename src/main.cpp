#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// =========================================
// CONFIGURAÇÕES GERAIS
// =========================================
const bool MQTT_ATIVO = true; // depois muda para true

// =========================================
// CONFIGURAÇÕES DO WIFI
// =========================================
const char* WIFI_SSID = "GHEYSON";
const char* WIFI_PASSWORD = "22OXEmm19";

// =========================================
// CONFIGURAÇÕES DO MQTT
// =========================================
const char* MQTT_BROKER = "10.0.0.156";
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
// CONFIGURAÇÕES DE ALERTA DE TURBIDEZ
// =========================================
const int LIMITE_TURBIDEZ_CRITICA = 60; // Acima disso é considerado crítico
const unsigned long TEMPO_TOLERANCIA_MS = 60000; // Tempo em milissegundos (ex: 60000 = 1 minuto)
const unsigned long INTERVALO_REENVIO_MS = 3600000; // Intervalo em milissegundos (ex: 3600000 = 1 hora)

unsigned long inicioTurbidezAlta = 0;
bool alertaEmailEnviado = false;
unsigned long ultimoAlertaEnviado = 0;

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

  for (int i = 0; i < 10; i++) {
    digitalWrite(PINO_TRIG, LOW);
    delayMicroseconds(2);

    digitalWrite(PINO_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PINO_TRIG, LOW);

    long duracao = pulseIn(PINO_ECHO, HIGH, 30000);

    if (duracao > 0) {
      float distancia = (duracao * 0.0343) / 2.0;
      somaDistancias += distancia;
      leiturasValidas++;
    }

    delay(5);
  }

  if (leiturasValidas == 0) {
    return -1;
  }

  return somaDistancias / leiturasValidas;
}

// =========================================
// LEITURA DA TURBIDEZ COM MÉDIA DE 10 AMOSTRAS
// =========================================
int lerTurbidez() {
  long somaLeituras = 0;

  for (int i = 0; i < 10; i++) {
    somaLeituras += analogRead(PINO_TURBIDEZ);
    delay(5);
  }

  int valorBrutoMedio = somaLeituras / 10;

  // 0 = água muito suja (100%)
  // 4095 = água muito limpa (0%)
  int turbidez = map(valorBrutoMedio, 0, 4095, 100, 0);

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

  // Conecta ao Wi-Fi apenas se o MQTT estiver ativo
  if (MQTT_ATIVO) {
    conectarWiFi();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setBufferSize(256);
    mqttClient.setKeepAlive(30);
  }

  Serial.println();
  Serial.println("Sistema iniciado.");
  Serial.println("Leitura local dos sensores pronta.");
  Serial.println("------------------------------------");
}

void loop() {
  // Parte de rede só funciona quando ativar MQTT_ATIVO = true
  if (MQTT_ATIVO) {
    if (WiFi.status() != WL_CONNECTED) {
      conectarWiFi();
    }

    if (!mqttClient.connected()) {
      conectarMQTT();
    }

    mqttClient.loop();
  }

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

// =========================================
  // LÓGICA DE ALERTA PROLONGADO E RECORRENTE
  // =========================================
  if (turbidez > LIMITE_TURBIDEZ_CRITICA) {
    
    // Se a turbidez acabou de ficar alta, marca o tempo inicial
    if (inicioTurbidezAlta == 0) {
      inicioTurbidezAlta = millis();
      Serial.println("Alerta: Turbidez alta detectada. Iniciando contagem de tempo...");
    } 
    // Se já passou do tempo de tolerância inicial
    else if (millis() - inicioTurbidezAlta >= TEMPO_TOLERANCIA_MS) {
      
      // Verifica se é o primeiro envio OU se já passou o tempo de intervalo desde o último envio
      if (ultimoAlertaEnviado == 0 || (millis() - ultimoAlertaEnviado >= INTERVALO_REENVIO_MS)) {
        Serial.println("Alerta CRÍTICO: Água continua turva! Solicitando envio de e-mail...");
        
        // Publica no MQTT para o Node-RED enviar o e-mail
        if (MQTT_ATIVO && mqttClient.connected()) {
          mqttClient.publish("cisterna/alerta", "{\"alerta\":\"turbidez_prolongada\", \"mensagem\":\"A água permanece turva. Limpeza necessária!\"}");
        }
        
        // Registra o momento em que este alerta foi enviado para iniciar o "cooldown"
        ultimoAlertaEnviado = millis(); 
      }
    }
  } else {
    // Se a água voltar a ficar limpa, zera tudo imediatamente
    if (inicioTurbidezAlta != 0 || ultimoAlertaEnviado != 0) {
      Serial.println("Resetando alertas de turbidez.");
      inicioTurbidezAlta = 0;
      ultimoAlertaEnviado = 0;
    }
  }

  // Publicação MQTT só quando o broker estiver pronto
  if (MQTT_ATIVO) {
    publicarMQTT(distancia, nivelPercentual, turbidez);
  }

  Serial.println("------------------------------------");
  delay(3000);
}