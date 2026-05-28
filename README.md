# 💧 Sistema de Telemetria para Cisterna com Buffer Circular

> Monitoramento em tempo real de nível e turbidez da água via ESP32, MQTT e dashboard — com buffer circular como núcleo embarcado da solução.

---

## 📌 Descrição do Projeto

O sistema monitora continuamente a água de uma cisterna, mede nível e turbidez, publica os dados via MQTT, exibe em dashboard em tempo real e utiliza **buffer circular** para evitar perda de amostras e reduzir latência em cenários de rede instável.

Este projeto integra dois trabalhos acadêmicos:

| Trabalho | Foco |
|---|---|
| **Projeto de Telemetria** | Monitoramento de cisterna com ESP32 + MQTT + Dashboard |
| **Análise de Algoritmos** | Comparação entre array com deslocamento O(n) vs. buffer circular O(1) |

---

## 🧰 Hardware Utilizado

| Componente | Função |
|---|---|
| ESP32 | Microcontrolador principal |
| Sensor Ultrassônico (HC-SR04) | Mede o nível da água |
| Potenciômetro | Simula turbidez (fase 1) |
| Sensor de Turbidez | Leitura real (fase 2 — aguardando chegada) |
| LED RGB | Indicador visual de estado local |
| Resistores + Jumpers | Divisor de tensão e conexões gerais |
| 2x Protoboard | Bancada de prototipagem |

---

## 🗺️ Arquitetura do Sistema

```
┌─────────────────────────────────────────┐
│               ESP32                     │
│                                         │
│  Ultrassônico → Nível da água           │
│  Potenciômetro → Turbidez simulada      │
│  LED RGB → Status visual local          │
│                                         │
│  ┌─────────────────────┐                │
│  │   Buffer Circular   │ ← núcleo       │
│  │   (fila de amostras)│                │
│  └────────┬────────────┘                │
│           │                             │
│      MQTT Client                        │
└───────────┼─────────────────────────────┘
            │
     ┌──────▼──────┐
     │  Mosquitto  │  (broker — PC ou WSL)
     └──────┬──────┘
            │
     ┌──────▼──────┐
     │  Node-RED   │  (dashboard em tempo real)
     └─────────────┘
```

---

## 🔌 Mapeamento de Pinos

| Componente | Pino ESP32 | Observação |
|---|---|---|
| Potenciômetro (centro) | GPIO34 | ADC1 — obrigatório com Wi-Fi ativo |
| Potenciômetro (extremos) | 3.3V / GND | — |
| Ultrassônico TRIG | GPIO5 | Saída digital |
| Ultrassônico ECHO | GPIO18 | Entrada com divisor de tensão (5V → 3.3V) |
| LED RGB — Vermelho | GPIO25 | PWM + resistor em série |
| LED RGB — Verde | GPIO26 | PWM + resistor em série |
| LED RGB — Azul | GPIO27 | PWM + resistor em série |

> ⚠️ **Atenção:** o sinal ECHO do HC-SR04 opera em 5V. Use divisor resistivo (ex: 1kΩ + 2kΩ ou 4.7kΩ + 10kΩ) para proteger o ESP32.

---

## 📐 Lógica de Dados

### Camadas de processamento

```
Dados brutos       →  leitura ADC / leitura ultrassônico
Dados processados  →  turbidez % / nível %
Dados de telemetria →  timestamp + leitura + status + buffer_index
```

### Cálculo do nível da água

```
nivel_cm         = altura_total - distancia_medida
nivel_percentual = (nivel_cm / altura_total) × 100
```

### Mapeamento do potenciômetro para turbidez

| Faixa ADC (0–4095) | Turbidez (%) | Status |
|---|---|---|
| 0 – 1229 | 0 – 30 | ✅ Limpa |
| 1230 – 2457 | 31 – 60 | ⚠️ Atenção |
| 2458 – 4095 | 61 – 100 | 🚨 Crítica |

### LED RGB — indicação de estado

| Cor | Condição |
|---|---|
| 🟢 Verde | Nível e turbidez normais |
| 🟡 Amarelo | Um dos parâmetros em atenção |
| 🔴 Vermelho | Alerta crítico |

---

## 🗃️ Estrutura da Amostra (Telemetria)

```json
{
  "id": 42,
  "timestamp": 123456,
  "nivel_cm": 74.2,
  "nivel_percentual": 74,
  "turbidez_simulada": 58,
  "status": "ATENCAO",
  "buffer_index": 7,
  "buffer_ocupacao": 12
}
```

---

## 📡 Tópicos MQTT

| Tópico | Conteúdo |
|---|---|
| `cisterna/telemetria/nivel` | Nível em cm e % |
| `cisterna/telemetria/turbidez` | Turbidez simulada ou real |
| `cisterna/telemetria/status` | Estado atual do sistema |
| `cisterna/telemetria/buffer` | Ocupação do buffer circular |
| `cisterna/benchmark/latencia` | Resultados do benchmark de algoritmos |

---

## 🧠 Buffer Circular — Núcleo Embarcado

O buffer circular resolve o descompasso entre a velocidade de coleta dos sensores e a velocidade de transmissão pela rede.

```
Sensor coleta rápido
Rede transmite devagar
Buffer segura a diferença → sem perda de amostras
```

### Comparação com array de deslocamento

| Operação | Array com deslocamento | Buffer Circular |
|---|---|---|
| Inserção | O(n) — desloca todos os elementos | O(1) — só atualiza head/tail |
| Remoção | O(n) | O(1) |
| Uso de CPU | Alto | Mínimo |

---

## 🧱 Estrutura do Firmware

O firmware é dividido em três blocos modulares:

```
Bloco A — Leitura de Sensores
  ├── lerPotenciometro()
  └── lerUltrassonico()

Bloco B — Buffer Circular
  ├── inserirAmostra()
  ├── removerAmostra()
  ├── verificarCheio()
  ├── verificarVazio()
  └── contarOcupacao()

Bloco C — MQTT
  ├── conectarWiFi()
  ├── conectarBroker()
  └── publicarLeitura()
```

### Modo de operação duplo

| Modo | Descrição |
|---|---|
| `OPERACAO` | Lê sensores reais → buffer circular → publica MQTT |
| `BENCHMARK` | Compara inserção em array deslocado vs. buffer circular, mede tempo em µs |

---

## 🛠️ Stack Tecnológica

| Camada | Tecnologia |
|---|---|
| Firmware | C++ / Arduino (PlatformIO + VSCode) |
| Broker MQTT | Mosquitto (PC ou WSL) |
| Dashboard | Node-RED Dashboard |
| Debug | Serial Monitor |

---

## ✅ Checklist de Implementação — Fase 1

- [ ] **Etapa 1** — Configurar PlatformIO, compilar sketch vazio e fazer upload
- [ ] **Etapa 2** — Testar LED RGB (vermelho, verde, azul individualmente)
- [ ] **Etapa 3** — Ler potenciômetro e exibir valor bruto no Serial Monitor
- [ ] **Etapa 4** — Testar ultrassônico e validar leitura de distância em cm
- [ ] **Etapa 5** — Criar estrutura do buffer circular com amostras fictícias
- [ ] **Etapa 6** — Integrar sensores ao buffer (leitura real → amostra → buffer)
- [ ] **Etapa 7** — Conectar ao broker MQTT e publicar JSON
- [ ] **Etapa 8** — Criar dashboard (indicadores de nível, turbidez e gráfico temporal)
- [ ] **Etapa 9** — Executar benchmark e comparar array deslocado vs. buffer circular

---

## 🔭 Roadmap

| Fase | Status | Descrição |
|---|---|---|
| Fase 1 | 🔄 Em andamento | ESP32 + potenciômetro + ultrassônico + LED + MQTT + dashboard |
| Fase 2 | ⏳ Pendente | Substituição do potenciômetro pelo sensor de turbidez real |
| Fase 3 | ⏳ Pendente | Integração com cisterna física e validação em campo |

---

## 🔎 Observação sobre Turbidez

O sensor de turbidez não mede o fundo do reservatório diretamente. O sistema identifica **aumento de turbidez como sinal de degradação da qualidade visual da água** e possível necessidade de limpeza da cisterna. O acúmulo de resíduos e a perturbação da água provocam elevação mensurável da turbidez, que é usada como indicativo de manutenção.

---

## 👥 Equipe

| Membros | Responsabilidades |
| :--- | :--- |
| Gabriel Tabosa | Teste da Aplicação |
| João Antonio | Dashboard |
| Arthur Capistrano | ML |
| Gheyson Melo | Codido c++ |
| Ericos Chen | Teste da Aplicação |
| Lucas Holanda | ML |

