#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// =========================================
// TAMANHO DOS TESTES
// ALTERAR PARA:
// 100
// 5000
// 20000
// =========================================
#define TAM_BUFFER 100

// =========================================
// MODOS DE EXECUÇÃO
// =========================================
#define MODO_IOT 0
#define MODO_ON 1
#define MODO_O1 2
#define MODO_BENCHMARK 3

#define MODO_EXECUCAO MODO_IOT

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
const char* MQTT_TOPIC_BENCHMARK = "cisterna/benchmark";

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
// CONTROLE DO BENCHMARK
// =========================================
unsigned long ultimoConsumo = 0;
bool benchmarkExecutado = false;

// =========================================
// TELEMETRIA
// =========================================
struct Amostra {

  int id;

  unsigned long timestamp;

  float nivel_cm;

  int nivel_percentual;

  int turbidez_simulada;

  int buffer_index;

  int buffer_ocupacao;
};
// =========================================
// OBJETOS DE REDE
// =========================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =========================================
// TELEMETRIA
// =========================================
int contadorAmostras = 0;

Amostra criarAmostra(
    float distancia,
    float nivelPercentual,
    int turbidez
) {

  Amostra dado;

  dado.id = contadorAmostras++;

  dado.timestamp = millis();

  dado.nivel_cm = distancia;

  dado.nivel_percentual = (int)nivelPercentual;

  dado.turbidez_simulada = turbidez;

  dado.buffer_index = 0;
  dado.buffer_ocupacao = 0;

  return dado;
}

struct EstatisticasBenchmark {

  unsigned long somaLatencias = 0;

  unsigned long totalInsercoes = 0;

  unsigned long maiorLatencia = 0;

} benchmark;

Amostra historicoON[TAM_BUFFER];

int quantidadeON = 0;

void inserirON(Amostra dado) {

  if (quantidadeON < TAM_BUFFER) {
    historicoON[quantidadeON++] = dado;
    return;
  }

  for (int i = 0; i < TAM_BUFFER - 1; i++) {
    historicoON[i] = historicoON[i + 1];
  }

  historicoON[TAM_BUFFER - 1] = dado;
}

class RingBuffer {

private:

  Amostra buffer[TAM_BUFFER];

  int head = 0;
  int tail = 0;
  int quantidade = 0;

  unsigned long descartados = 0;

public:

  void inserir(Amostra dado) {

    buffer[head] = dado;

    head = (head + 1) % TAM_BUFFER;

    if (quantidade < TAM_BUFFER) {
      quantidade++;
    } else {
      descartados++;
      tail = (tail + 1) % TAM_BUFFER;
    }
  }

  int getHead() {
    return head;
  }

  bool vazio() {
    return quantidade == 0;
  }

  unsigned long getDescartados() {
    return descartados;
  }

  Amostra remover() {

    Amostra dado = buffer[tail];

    tail = (tail + 1) % TAM_BUFFER;

    quantidade--;

    return dado;
  }

  int ocupacao() {
    return quantidade;
  }
};

RingBuffer ringBuffer;

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

void publicarBenchmark(
    const char* vertente,
    unsigned long latenciaMedia,
    unsigned long latenciaMaxima,
    uint32_t heapLivre,
    int ocupacaoBuffer,
    unsigned long descartados
) {

  if (!MQTT_ATIVO || !mqttClient.connected()) {
    return;
  }

  char payload[256];

  snprintf(
    payload,
    sizeof(payload),
    "{\"vertente\":\"%s\","
    "\"n\":%d,"
    "\"latencia_media_us\":%lu,"
    "\"latencia_maxima_us\":%lu,"
    "\"heap_livre\":%u,"
    "\"ocupacao_buffer\":%d,"
    "\"descartados\":%lu}",
    vertente,
    TAM_BUFFER,
    latenciaMedia,
    latenciaMaxima,
    heapLivre,
    ocupacaoBuffer,
    descartados
);

  mqttClient.publish(
      MQTT_TOPIC_BENCHMARK,
      payload
  );

  Serial.println(payload);
}

void processarON(Amostra dado) {

  unsigned long inicio = micros();

  inserirON(dado);

  unsigned long latencia = micros() - inicio;

  if (latencia > benchmark.maiorLatencia)
    benchmark.maiorLatencia = latencia;

  benchmark.somaLatencias += latencia;
  benchmark.totalInsercoes++;

  if (benchmark.totalInsercoes % 100 == 0) {

    unsigned long media =
        benchmark.somaLatencias /
        benchmark.totalInsercoes;

    publicarBenchmark(
        "ON",
        media,
        benchmark.maiorLatencia,
        ESP.getFreeHeap(),
        quantidadeON,
        0
    );
  }

  if (MQTT_ATIVO) {

    publicarMQTT(
        dado.nivel_cm,
        dado.nivel_percentual,
        dado.turbidez_simulada
    );

    delay(200);
  }
}

void produzirO1(Amostra dado) {

  unsigned long inicio = micros();

  dado.buffer_index = ringBuffer.getHead();

  dado.buffer_ocupacao = ringBuffer.ocupacao();

  ringBuffer.inserir(dado);

  unsigned long latencia = micros() - inicio;

  if (latencia > benchmark.maiorLatencia)
    benchmark.maiorLatencia = latencia;

  benchmark.somaLatencias += latencia;
  benchmark.totalInsercoes++;

  if (benchmark.totalInsercoes % 100 == 0) {

    unsigned long media =
        benchmark.somaLatencias /
        benchmark.totalInsercoes;

    publicarBenchmark(
        "O1",
        media,
        benchmark.maiorLatencia,
        ESP.getFreeHeap(),
        ringBuffer.ocupacao(),
        ringBuffer.getDescartados()
    );
  }
}

void consumirBuffer() {

  if (ringBuffer.vazio()) {
    return;
  }

  Amostra dado =
      ringBuffer.remover();

  if (MQTT_ATIVO) {

    publicarMQTT(
        dado.nivel_cm,
        dado.nivel_percentual,
        dado.turbidez_simulada
    );

    delay(200);
  }
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

Amostra dado =
    criarAmostra(
        distancia,
        nivelPercentual,
        turbidez
    );

#if MODO_EXECUCAO == MODO_IOT
Serial.println("Modo IoT");
#elif MODO_EXECUCAO == MODO_ON
Serial.println("Modo O(n)");
#elif MODO_EXECUCAO == MODO_O1
Serial.println("Modo O(1)");
#elif MODO_EXECUCAO == MODO_BENCHMARK
Serial.println("Modo Benchmark");
#endif
    
#if MODO_EXECUCAO == MODO_IOT

  if (MQTT_ATIVO) {

    publicarMQTT(
        distancia,
        nivelPercentual,
        turbidez
    );
  }

#elif MODO_EXECUCAO == MODO_ON

  processarON(dado);

#elif MODO_EXECUCAO == MODO_O1

  produzirO1(dado);

if (millis() - ultimoConsumo >= 1000) {

    consumirBuffer();

    ultimoConsumo = millis();
}

#elif MODO_EXECUCAO == MODO_BENCHMARK

if (benchmarkExecutado)
    return;

  benchmarkExecutado = true;

benchmark.somaLatencias = 0;
benchmark.maiorLatencia = 0;

for (int i = 0; i < TAM_BUFFER; i++) {

    Amostra dado;

    dado.id = i;

    unsigned long inicio = micros();

    ringBuffer.inserir(dado);

    unsigned long latencia = micros() - inicio;

    benchmark.somaLatencias += latencia;

    if (latencia > benchmark.maiorLatencia)
        benchmark.maiorLatencia = latencia;
}

unsigned long media =
    benchmark.somaLatencias / TAM_BUFFER;

Serial.printf(
    "N=%d | Media=%lu us | Max=%lu us | Heap=%u\n",
    TAM_BUFFER,
    media,
    benchmark.maiorLatencia,
    ESP.getFreeHeap()
);

publicarBenchmark(
    "O1",
    media,
    benchmark.maiorLatencia,
    ESP.getFreeHeap(),
    ringBuffer.ocupacao(),
    ringBuffer.getDescartados()
);

benchmark.somaLatencias = 0;
benchmark.maiorLatencia = 0;

for (int i = 0; i < TAM_BUFFER; i++) {

    Amostra dado;
    dado.id = i;

    unsigned long inicio = micros();

    inserirON(dado);

    unsigned long latencia = micros() - inicio;


    benchmark.somaLatencias += latencia;

    if (latencia > benchmark.maiorLatencia)
        benchmark.maiorLatencia = latencia;

    delay(200);
}

unsigned long mediaON =
    benchmark.somaLatencias / TAM_BUFFER;

Serial.printf(
    "[ON] N=%d | Media=%lu us | Max=%lu us | Heap=%u\n",
    TAM_BUFFER,
    mediaON,
    benchmark.maiorLatencia,
    ESP.getFreeHeap()
);

publicarBenchmark(
    "ON",
    mediaON,
    benchmark.maiorLatencia,
    ESP.getFreeHeap(),
    quantidadeON,
    0
);

while (true);

#endif

  Serial.println("------------------------------------");
  delay(3000);
}