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
const char* WIFI_SSID = "uaifai-brum";
const char* WIFI_PASSWORD = "bemvindoaocesar";

// =========================================
// CONFIGURAÇÕES DO MQTT
// =========================================
const char* MQTT_BROKER = "172.26.125.207";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "cisterna/status";
const char* MQTT_LOTE_TOPIC = "cisterna/lote";

// Overhead do frame MQTT (cabeçalho + length do tópico + margem)
const size_t MQTT_FRAME_OVERHEAD = 20;

uint16_t mqttBufferSize = 256;

uint16_t estimarBufferMqtt(size_t payloadLen, const char* topic);

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
  int turbidez;
};

// =========================================
// TAMANHO DOS TESTES (alterar entre compilações)
// 100 | 5000 | 1000 | 20000
// =========================================
#define TAM_BUFFER 100
#define MAX_LOTES 3

#define MODO_IOT 0
#define MODO_ON 1
#define MODO_O1 2

#define MODO_EXECUCAO MODO_IOT

uint16_t estimarBufferMqtt(size_t payloadLen, const char* topic) {
  size_t total = payloadLen + strlen(topic) + MQTT_FRAME_OVERHEAD;
  if (total > 65535) {
    return 65535;
  }
  return (uint16_t)(((total + 255) / 256) * 256);
}

uint16_t estimarBufferMqttLoteMax() {
  // Cabeçalho JSON + pior caso por amostra (~40 chars) + fechamento "]}")
  const size_t payloadMax = 96UL + (size_t)TAM_BUFFER * 40UL + 2UL;
  return estimarBufferMqtt(payloadMax, MQTT_LOTE_TOPIC);
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
// MANTÉM WIFI E MQTT ATIVOS (MODOS AA)
// =========================================
void manterRedeMQTT() {
  if (!MQTT_ATIVO) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  if (!mqttClient.connected()) {
    conectarMQTT();
  }

  mqttClient.loop();
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
// LEITURA RÁPIDA DA TURBIDEZ (MODOS AA)
// 1 leitura real, sem média — para lotes grandes
// =========================================
int lerTurbidezRapida() {
  int valorBruto = analogRead(PINO_TURBIDEZ);
  int turbidez = map(valorBruto, 0, 4095, 100, 0);
  return constrain(turbidez, 0, 100);
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

bool publicarMQTT(float distancia, float nivelPercentual, int turbidez);

// =========================================
// VERTENTE 1 — REALOCAÇÃO + DESLOCAMENTO O(n)
// =========================================
Amostra* bufferIneficiente = nullptr;
int quantidadeIneficiente = 0;
int numeroLote = 0;
int contadorAmostras = 0;

bool inserirBufferIneficiente(const Amostra& amostra, unsigned long& latenciaUs) {
  unsigned long inicio = micros();

  if (quantidadeIneficiente < TAM_BUFFER) {
    quantidadeIneficiente++;
    bufferIneficiente = (Amostra*)realloc(
      bufferIneficiente,
      quantidadeIneficiente * sizeof(Amostra)
    );
    bufferIneficiente[quantidadeIneficiente - 1] = amostra;
  } else {
    bufferIneficiente = (Amostra*)realloc(
      bufferIneficiente,
      TAM_BUFFER * sizeof(Amostra)
    );

    for (int i = 0; i < TAM_BUFFER - 1; i++) {
      bufferIneficiente[i] = bufferIneficiente[i + 1];
    }

    bufferIneficiente[TAM_BUFFER - 1] = amostra;
  }

  latenciaUs = micros() - inicio;
  return quantidadeIneficiente >= TAM_BUFFER;
}

// =========================================
// BUFFER CIRCULAR — VERTENTE 2 (O(1))
// =========================================
class BufferCircular {
  private:
    Amostra* buffer;
    int capacidade;
    int head;
    int tail;
    int quantidade;
  
  public:
    BufferCircular() : buffer(nullptr), capacidade(0), head(0), tail(0), quantidade(0) {}
  
    void iniciar(int tam) {
      liberar();
      capacidade = tam;
      buffer = (Amostra*)malloc(capacidade * sizeof(Amostra));
      head = 0;
      tail = 0;
      quantidade = 0;
    }
  
    void liberar() {
      if (buffer != nullptr) {
        free(buffer);
        buffer = nullptr;
      }
      capacidade = 0;
      head = 0;
      tail = 0;
      quantidade = 0;
    }
  
    bool inserir(const Amostra& amostra, unsigned long& latenciaUs) {
      unsigned long inicio = micros();
  
      buffer[head] = amostra;
      head = (head + 1) % capacidade;
  
      if (quantidade < capacidade) {
        quantidade++;
      } else {
        tail = (tail + 1) % capacidade;
      }
  
      latenciaUs = micros() - inicio;
      return quantidade >= capacidade;
    }
  
    void copiarLote(Amostra* destino) const {
      int indice = tail;
      for (int i = 0; i < capacidade; i++) {
        destino[i] = buffer[indice];
        indice = (indice + 1) % capacidade;
      }
    }
  };

// =========================================
// GERAÇÃO DE AMOSTRAS REAIS (MODOS AA)
// =========================================
Amostra gerarAmostraReal() {
  contadorAmostras++;

  Amostra amostra;
  amostra.id = contadorAmostras;
  amostra.timestamp = millis();
  amostra.turbidez = lerTurbidezRapida();

  return amostra;
}

// =========================================
// PUBLICA LOTE VIA MQTT (MODOS AA)
// =========================================
void publicarLoteMQTT(const char* modo, const Amostra* amostras, int tamanhoLote, int numeroDoLote, unsigned long latenciaUs) {
  if (!MQTT_ATIVO || !mqttClient.connected()) {
    return;
  }

  String payload;
  payload.reserve((size_t)tamanhoLote * 32 + 96);
  payload = "{\"modo\":\"";
  payload += modo;
  payload += "\",\"lote\":";
  payload += numeroDoLote;
  payload += ",\"tam\":";
  payload += tamanhoLote;
  payload += ",\"latencia_us\":";
  payload += latenciaUs;
  payload += ",\"heap_livre\":";
  payload += ESP.getFreeHeap();
  payload += ",\"amostras\":[";

  for (int i = 0; i < tamanhoLote; i++) {
    if (i > 0) {
      payload += ",";
    }

    char item[48];
    snprintf(
      item,
      sizeof(item),
      "{\"id\":%d,\"ts\":%lu,\"tz\":%d}",
      amostras[i].id,
      amostras[i].timestamp,
      amostras[i].turbidez
    );
    payload += item;
  }

  payload += "]}";

  const size_t pacoteNecessario = payload.length() + strlen(MQTT_LOTE_TOPIC) + MQTT_FRAME_OVERHEAD;
  if (pacoteNecessario > mqttBufferSize) {
    Serial.print("Payload (");
    Serial.print(payload.length());
    Serial.print(" bytes) excede buffer MQTT (");
    Serial.print(mqttBufferSize);
    Serial.println(" bytes).");
    return;
  }

  bool ok = mqttClient.publish(MQTT_LOTE_TOPIC, payload.c_str());
  mqttClient.loop();

  if (!ok) {
    Serial.print("MQTT publish FALHOU (lote ");
    Serial.print(numeroDoLote);
    Serial.print(", payload=");
    Serial.print(payload.length());
    Serial.print(" bytes, buffer=");
    Serial.print(mqttBufferSize);
    Serial.print(") | estado=");
    Serial.println(mqttClient.state());
    return;
  }

  Serial.print("Lote ");
  Serial.print(numeroDoLote);
  Serial.print(" enviado (");
  Serial.print(modo);
  Serial.print(", tam=");
  Serial.print(tamanhoLote);
  Serial.print(") | Latencia: ");
  Serial.print(latenciaUs);
  Serial.print(" us | Heap Livre: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
}

// =========================================
// LOOPS DOS MODOS AA
// =========================================
void loopVertenteIneficiente() {
  static bool experimentoConcluido = false;

  if (experimentoConcluido) {
    delay(1000);
    return;
  }

  manterRedeMQTT();

  Amostra amostra = gerarAmostraReal();
  unsigned long latenciaUs = 0;

  if (inserirBufferIneficiente(amostra, latenciaUs)) {
    if (numeroLote < MAX_LOTES) {
      numeroLote++;
      publicarLoteMQTT("ON", bufferIneficiente, TAM_BUFFER, numeroLote, latenciaUs);
    }

    if (numeroLote >= MAX_LOTES) {
      Serial.println("Experimento concluido: 5 lotes enviados.");
      experimentoConcluido = true;
    }
  }

  Serial.printf("ON | Latencia: %lu us | Heap Livre: %u bytes\n", latenciaUs, ESP.getFreeHeap());
}

void loopVertenteCircular() {
  static BufferCircular bufferCircular;
  static Amostra* loteEnvio = nullptr;
  static bool inicializado = false;
  static bool experimentoConcluido = false;

  if (experimentoConcluido) {
    delay(1000);
    return;
  }

  if (!inicializado) {
    bufferCircular.iniciar(TAM_BUFFER);
    loteEnvio = (Amostra*)malloc(TAM_BUFFER * sizeof(Amostra));
    inicializado = true;

    Serial.print("Buffer Circular iniciado com capacidade ");
    Serial.println(TAM_BUFFER);
  }

  manterRedeMQTT();

  Amostra amostra = gerarAmostraReal();
  unsigned long latenciaUs = 0;

  if (bufferCircular.inserir(amostra, latenciaUs)) {
    if (numeroLote < MAX_LOTES) {
      bufferCircular.copiarLote(loteEnvio);
      numeroLote++;
      publicarLoteMQTT("O1", loteEnvio, TAM_BUFFER, numeroLote, latenciaUs);
    }

    if (numeroLote >= MAX_LOTES) {
      Serial.println("Experimento concluido: 5 lotes enviados.");
      experimentoConcluido = true;
    }
  }

  Serial.printf("O1 | Latencia: %lu us | Heap Livre: %u bytes\n", latenciaUs, ESP.getFreeHeap());
}

// =========================================
// LOOP DO PROJETO IoT
// =========================================
void loopIoT() {
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
          bool ok = mqttClient.publish(
            "cisterna/alerta",
            "{\"alerta\":\"turbidez_prolongada\", \"mensagem\":\"A água permanece turva. Limpeza necessária!\"}"
          );
          mqttClient.loop();
          if (!ok) {
            Serial.println("MQTT publish FALHOU em cisterna/alerta");
          }
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

// =========================================
// PUBLICA OS DADOS NO MQTT (Modo IoT)
// =========================================
bool publicarMQTT(float distancia, float nivelPercentual, int turbidez) {
  if (!mqttClient.connected()) {
    Serial.println("MQTT publish ignorado: cliente desconectado.");
    return false;
  }

  char payload[220];

  snprintf(
    payload,
    sizeof(payload),
    "{\"distancia_cm\":%.2f,\"nivel_percentual\":%.2f,\"turbidez\":%d}",
    distancia,
    nivelPercentual,
    turbidez
  );

  bool ok = mqttClient.publish(MQTT_TOPIC, payload);
  mqttClient.loop();

  if (ok) {
    Serial.print("MQTT enviado em ");
    Serial.print(MQTT_TOPIC);
    Serial.print(": ");
    Serial.println(payload);
  } else {
    Serial.print("MQTT publish FALHOU em ");
    Serial.print(MQTT_TOPIC);
    Serial.print(" | estado=");
    Serial.print(mqttClient.state());
    Serial.print(" | payload=");
    Serial.println(payload);
  }

  return ok;
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

#if MODO_EXECUCAO == MODO_IOT
    mqttBufferSize = estimarBufferMqtt(128, MQTT_TOPIC);
#else
    mqttBufferSize = estimarBufferMqttLoteMax();
#endif

    if (!mqttClient.setBufferSize(mqttBufferSize)) {
      Serial.print("AVISO: setBufferSize falhou (");
      Serial.print(mqttBufferSize);
      Serial.println(" bytes). Tentando 512...");
      mqttBufferSize = 512;
      mqttClient.setBufferSize(mqttBufferSize);
    }

    Serial.print("Buffer MQTT alocado: ");
    Serial.print(mqttBufferSize);
    Serial.println(" bytes");

    mqttClient.setKeepAlive(30);
    conectarMQTT();
  }

  Serial.println();
  Serial.println("Sistema iniciado.");

#if MODO_EXECUCAO == MODO_IOT
  Serial.println("Modo: IoT (cisterna)");
  Serial.println("Leitura local dos sensores pronta.");
#elif MODO_EXECUCAO == MODO_ON
  Serial.println("Modo: Vertente 1 - O(n) com realloc/deslocamento");
  Serial.println("Envio: janela deslizante (ex: 1-100, 2-101, 3-102...)");
  Serial.print("Tamanho da janela (TAM_BUFFER): ");
  Serial.println(TAM_BUFFER);
#elif MODO_EXECUCAO == MODO_O1
  Serial.println("Modo: Vertente 2 - O(1) Buffer Circular ");
  Serial.println("Envio: janela deslizante (ex: 1-100, 2-101, 3-102...)");
  Serial.print("Tamanho da janela (TAM_BUFFER): ");
  Serial.println(TAM_BUFFER);
#endif

  Serial.println("------------------------------------");
}

void loop() {
#if MODO_EXECUCAO == MODO_IOT
  loopIoT();
#elif MODO_EXECUCAO == MODO_ON
  loopVertenteIneficiente();
  delay(1);
#elif MODO_EXECUCAO == MODO_O1
  loopVertenteCircular();
  delay(1);
#endif
}