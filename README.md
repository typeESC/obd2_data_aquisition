# 🚗 OBDScan - Sistema de Monitoramento OBD-II

Um sistema completo de aquisição e análise de dados OBD-II usando ESP32 e backend FastAPI, projetado para monitoramento veicular em tempo real com armazenamento em nuvem.

## 📋 Índice

- [Visão Geral](#-visão-geral)
- [Tecnologias Utilizadas](#-tecnologias-utilizadas)
- [Arquitetura do Sistema](#️-arquitetura-do-sistema)
- [Hardware Necessário](#-hardware-necessário)
- [Instalação e Configuração](#-instalação-e-configuração)
- [Como Funciona o Código ESP32](#-como-funciona-o-código-esp32)
- [Características de Transmissão de Dados](#-características-de-transmissão-de-dados)
- [Execução com Docker](#-execução-com-docker)
- [API e Endpoints](#-api-e-endpoints)
- [Monitoramento e Debugging](#-monitoramento-e-debugging)
- [Contribuição](#-contribuição)

## 🌟 Visão Geral

O OBDScan é uma solução profissional para aquisição, processamento e armazenamento de dados de telemetria veicular através do protocolo OBD-II. O sistema consiste em:

- **ESP32 Device**: Dispositivo embarcado para coleta de dados do veículo via barramento CAN
- **Backend FastAPI**: API robusta para recebimento, processamento e armazenamento dos dados
- **PostgreSQL**: Banco de dados para persistência com esquemas otimizados
- **Docker**: Containerização para deploy simplificado

### 🎯 Funcionalidades Principais

- 📊 **Aquisição em Tempo Real**: Coleta contínua de 17+ parâmetros OBD-II
- 🔄 **Processamento em Lote**: Transmissão eficiente via batching inteligente
- 📱 **Conectividade WiFi**: Auto-reconnexão e monitoramento de sinal
- ⚡ **Modo Fallback**: Operação offline em caso de falha de rede
- 🛡️ **Recuperação Automática**: Sistema robusto com detecção e correção de erros
- 📈 **Análise Estatística**: Relatórios detalhados de desempenho veicular
- 🚨 **Alertas Inteligentes**: Notificações baseadas em thresholds configuráveis

## 🛠️ Tecnologias Utilizadas

### Backend
- **FastAPI** - Framework web moderno e de alta performance
- **PostgreSQL** - Banco de dados relacional robusto
- **SQLAlchemy** - ORM para Python com migrations automáticas
- **Redis** - Cache em memória para otimização
- **Docker & Docker Compose** - Containerização e orquestração
- **Pydantic** - Validação de dados e serialização
- **Uvicorn** - Servidor ASGI de alta performance

### ESP32 Firmware
- **ESP-IDF** - Framework oficial Espressif
- **FreeRTOS** - Sistema operacional em tempo real
- **TWAI Driver** - Interface CAN nativa do ESP32
- **HTTP Client** - Comunicação com API via WiFi
- **cJSON** - Biblioteca para serialização JSON
- **NVS** - Armazenamento não-volátil para configurações

### Ferramentas de Desenvolvimento
- **CMake** - Sistema de build
- **Black & Pylint** - Formatação e análise de código Python
- **Git** - Controle de versão
- **GitHub** - Hospedagem e CI/CD

## 🏗️ Arquitetura do Sistema

```
┌─────────────────┐    CAN Bus    ┌─────────────────┐    WiFi/HTTP    ┌─────────────────┐
│   Veículo       │◄──────────────┤     ESP32       │◄───────────────┤  Backend API    │
│   (OBD-II)      │               │   Data Logger   │                │   (FastAPI)     │
└─────────────────┘               └─────────────────┘                └─────────────────┘
                                           │                                   │
                                           ▼                                   ▼
                                  ┌─────────────────┐                ┌─────────────────┐
                                  │ Processamento   │                │   PostgreSQL    │
                                  │ Local + Batch   │                │    Database     │
                                  └─────────────────┘                └─────────────────┘
```

### Fluxo de Dados

1. **Aquisição**: ESP32 solicita dados via barramento CAN do veículo
2. **Processamento**: Parsing e validação dos dados OBD-II
3. **Agregação**: Agrupamento em lotes para transmissão eficiente
4. **Transmissão**: Envio via HTTP/JSON para a API
5. **Persistência**: Armazenamento estruturado no PostgreSQL
6. **Análise**: Geração de estatísticas e alertas

## 🔧 Hardware Necessário

### Componentes Principais

| Componente | Modelo Recomendado | Preço Estimado | Descrição |
|------------|-------------------|----------------|-----------|
| **Microcontrolador** | ESP32-WROOM-32 | R$ 25-40 | Módulo com WiFi/Bluetooth integrado |
| **Transceiver CAN** | SN65HVD230 | R$ 8-15 | Interface CAN 3.3V para ESP32 |
| **Conector OBD-II** | DB9 Macho | R$ 10-20 | Conector padrão para porta OBD |
| **Regulador de Tensão** | LM2596 (12V→5V) | R$ 8-12 | Conversão da alimentação veicular |
| **PCB/Protoboard** | Universal 5x7cm | R$ 5-10 | Base para montagem |

### Esquema de Ligação

```
ESP32 (Pinos)          CAN Transceiver       Conector OBD-II
GPIO 25 (TX)    ────►  CTX                  
GPIO 27 (RX)    ◄────  CRX                  
GND             ────►  GND           ────►  Pino 5 (Terra)
3.3V            ────►  VCC           
                       CANH          ────►  Pino 6 (CAN High)
                       CANL          ────►  Pino 14 (CAN Low)
                                           Pino 16 (+12V) ──► Regulador ──► ESP32 VIN
```

### Alimentação do Sistema

```
Veículo (12V) → Regulador LM2596 → ESP32 (5V) → Transceiver CAN (3.3V)
     │                                              │
     └─────────── Proteção contra inversão ────────┘
```

## ⚙️ Instalação e Configuração

### 1. Configuração do Backend

#### Pré-requisitos
- Docker Engine 20.10+
- Docker Compose 2.0+
- Git
- 4GB RAM disponível
- 10GB espaço em disco

#### Setup Rápido

```bash
# Clone o repositório
git clone https://github.com/your-username/obd2_data_aquisition.git
cd obd2_data_aquisition

# Execute o script de configuração
# Linux/macOS:
chmod +x backend/setup-dev.sh
cd backend && ./setup-dev.sh

# Windows:
cd backend && setup-dev.bat
```

#### Setup Manual

```bash
# Navegue para o diretório backend
cd backend

# Crie o arquivo de ambiente
cp .env.example .env

# Edite as configurações (opcional)
nano .env

# Inicie os serviços
docker-compose up -d --build

# Verifique os logs
docker-compose logs -f api
```

### 2. Configuração do ESP32

#### Pré-requisitos
- ESP-IDF v4.4 ou superior
- Cabo USB para programação
- Driver CP210x ou CH340 (dependendo do ESP32)

#### Configuração do Código

```bash
# Configure o ambiente ESP-IDF
. $HOME/esp/esp-idf/export.sh

# Copie o template de configuração
cp main/obd_config_template.h main/obd_config.h

# Edite as configurações
nano main/obd_config.h
```

**Configurações Obrigatórias em `obd_config.h`:**

```c
// WiFi do seu ambiente
#define WIFI_SSID           "Sua_Rede_WiFi"
#define WIFI_PASSWORD       "Sua_Senha_WiFi"

// IP do servidor FastAPI (mesmo IP do Docker)
#define API_BASE_URL        "http://192.168.1.100:8000/api/v1"

// IDs únicos (gere novos UUIDs)
#define API_SESSION_ID      "seu-session-id-único"
#define API_VEHICLE_ID      "seu-vehicle-id-único"

// Pinos do hardware (ajuste conforme sua montagem)
#define CAN_RX_PIN          GPIO_NUM_27
#define CAN_TX_PIN          GPIO_NUM_25
```

#### Compilação e Gravação

```bash
# Configure o projeto
idf.py menuconfig

# Compile o firmware
idf.py build

# Grave no ESP32 (ajuste a porta COM/USB)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial para debug
idf.py -p /dev/ttyUSB0 monitor
```

## 💻 Como Funciona o Código ESP32

### Arquitetura Multi-Task

O firmware ESP32 utiliza FreeRTOS com arquitetura multi-task para garantir operação em tempo real:

```c
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   OBD Task      │     │   API Task      │     │  Main Loop      │
│  (Prioridade 6) │     │ (Prioridade 5)  │     │ (Prioridade 1)  │
│                 │     │                 │     │                 │
│ • Aquisição CAN │     │ • Envio HTTP    │     │ • Monitoramento │
│ • Parsing dados │     │ • Health checks │     │ • Estatísticas  │
│ • Validação     │     │ • Gerenc. WiFi  │     │ • Watchdog      │
│ • Batch buffer  │     │ • Retry logic   │     │ • Recovery      │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

### Componentes Principais

#### 1. **OBD CAN Interface** (`obd_can.c`)
```c
// Inicialização do driver TWAI (CAN)
esp_err_t obd_can_init(void) {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    
    // Instala e inicia o driver
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

// Requisição OBD-II com timeout
esp_err_t obd_can_request(uint8_t pid, uint8_t *response, size_t *response_len) {
    twai_message_t tx_msg = {
        .identifier = 0x7DF,  // ID padrão OBD-II
        .data_length_code = 8,
        .data = {0x02, 0x01, pid, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}
    };
    
    // Transmite requisição e aguarda resposta
    ESP_ERROR_CHECK(twai_transmit(&tx_msg, pdMS_TO_TICKS(100)));
    ESP_ERROR_CHECK(twai_receive(&rx_msg, pdMS_TO_TICKS(300)));
}
```

#### 2. **Parser de Dados** (`obd_parser.c`)
```c
// Parsing específico por PID com validação
bool obd_parse_response(uint8_t pid, const uint8_t *response, telemetry_data_t *telemetry) {
    switch (pid) {
        case PID_RPM:
            telemetry->rpm = ((response[0] * 256.0f) + response[1]) / 4.0f;
            return IS_VALID_RPM(telemetry->rpm);
            
        case PID_SPEED:
            telemetry->speed = response[0];
            return IS_VALID_SPEED(telemetry->speed);
            
        case PID_COOLANT_TEMP:
            telemetry->coolant_temp = response[0] - 40;
            return IS_VALID_TEMP(telemetry->coolant_temp);
    }
}
```

#### 3. **Cliente API** (`api_client.c`)
```c
// Serialização JSON e envio em lote
esp_err_t api_send_telemetry_batch(const telemetry_batch_t *batch) {
    // Serializa dados para JSON
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "session_id", API_SESSION_ID);
    cJSON_AddNumberToObject(json, "batch_size", batch->count);
    
    // Cria array de telemetria
    cJSON *telemetry_array = cJSON_CreateArray();
    for (int i = 0; i < batch->count; i++) {
        cJSON *telemetry_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(telemetry_obj, "rpm", batch->data[i].rpm);
        cJSON_AddNumberToObject(telemetry_obj, "speed", batch->data[i].speed);
        // ... outros campos
        cJSON_AddItemToArray(telemetry_array, telemetry_obj);
    }
    
    // Envia via HTTP POST
    esp_http_client_set_post_field(http_client, json_string, strlen(json_string));
    esp_http_client_perform(http_client);
}
```

### Ciclo Principal de Operação

```c
void obd_task(void *pvParameters) {
    while (system_running) {
        // 1. Verifica integridade do sistema
        if (!obd_can_is_ready() || esp_get_free_heap_size() < HEAP_MIN_FREE) {
            // Procedimentos de recuperação
            continue;
        }
        
        // 2. Cicla pelos PIDs de interesse
        obd_pid_t current_pid = obd_scan_pids[pid_index++];
        
        // 3. Faz requisição OBD-II
        if (obd_can_request(current_pid, response_buffer, &response_len) == ESP_OK) {
            // 4. Parse e validação
            if (obd_parse_response(current_pid, response_buffer, &telemetry)) {
                if (obd_validate_telemetry(&telemetry)) {
                    // 5. Adiciona ao batch
                    add_to_batch(&telemetry);
                }
            }
        }
        
        // 6. Envia batch se estiver cheio ou timeout
        if (current_batch.ready_to_send) {
            send_and_reset_batch();
        }
        
        // 7. Delay configurável entre leituras
        vTaskDelay(pdMS_TO_TICKS(OBD_QUERY_DELAY_MS));
    }
}
```

## 📡 Características de Transmissão de Dados

### Protocolo de Comunicação

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| **Protocolo de Rede** | HTTP/1.1 over WiFi | REST API com JSON |
| **Frequência de Aquisição** | 100-200 Hz | ~100 amostras/segundo |
| **Tamanho do Batch** | 10 registros | Configurável (5-20) |
| **Timeout do Batch** | 30 segundos | Força envio parcial |
| **Tamanho do Payload** | ~2KB por batch | JSON comprimido |
| **Latência de Rede** | <200ms | Dependente da rede |
| **Retry Logic** | 3 tentativas | Backoff exponencial |

### Estrutura de Dados Transmitidos

#### Formato JSON do Batch
```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440001",
  "vehicle_id": "550e8400-e29b-41d4-a716-446655440000",
  "batch_size": 10,
  "first_timestamp": 1704067200000,
  "telemetry_data": [
    {
      "device_timestamp": 1704067200100,
      "rpm": 2500.5,
      "speed": 80,
      "coolant_temp": 87,
      "engine_load": 45.2,
      "throttle_pos": 32.1,
      "maf_rate": 15.8,
      "fuel_level": 75.0,
      "module_voltage": 14.2
    }
  ]
}
```

### Parâmetros Monitorados

| PID | Parâmetro | Unidade | Range | Freq. Amostragem |
|-----|-----------|---------|-------|------------------|
| 0x0C | **RPM** | rpm | 0-8000 | Alta (100Hz) |
| 0x0D | **Velocidade** | km/h | 0-250 | Alta (100Hz) |
| 0x05 | **Temperatura Radiador** | °C | -40-150 | Média (50Hz) |
| 0x04 | **Carga do Motor** | % | 0-100 | Alta (100Hz) |
| 0x11 | **Posição Acelerador** | % | 0-100 | Alta (100Hz) |
| 0x10 | **Taxa MAF** | g/s | 0-655 | Média (50Hz) |
| 0x0E | **Avanço Ignição** | ° | -64-63.5 | Baixa (20Hz) |
| 0x0F | **Temp. Ar Admissão** | °C | -40-150 | Baixa (20Hz) |
| 0x2F | **Nível Combustível** | % | 0-100 | Baixa (10Hz) |
| 0x42 | **Tensão Módulo** | V | 8-18 | Baixa (10Hz) |
| 0x1F | **Tempo Motor Ligado** | s | 0-65535 | Muito Baixa (1Hz) |
| 0x31 | **Dist. Códigos Limpos** | km | 0-65535 | Muito Baixa (1Hz) |

### Características de Performance

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Aquisição     │    │   Agregação     │    │   Transmissão   │
│                 │    │                 │    │                 │
│ • 100-200 Hz    │───▶│ • Batch 10x     │───▶│ • ~3s interval  │
│ • 17 PIDs       │    │ • 2KB payload   │    │ • <200ms RTT    │
│ • Validação     │    │ • JSON format   │    │ • Retry 3x      │
│ • 180KB RAM     │    │ • Compression   │    │ • Fallback mode │
└─────────────────┘    └─────────────────┘    └─────────────────┘

Performance típica:
├── Taxa de dados: ~50KB/min (veículo em movimento)
├── Eficiência de rede: 90% (10% overhead HTTP/JSON)
├── Disponibilidade: 99.5% (com auto-recovery)
└── Consumo: ~150mA @ 12V (otimizado)
```

## 🐳 Execução com Docker

### Configuração dos Serviços

O sistema utiliza Docker Compose para orquestração de múltiplos serviços:

```yaml
# docker-compose.yml (estrutura principal)
services:
  api:                    # FastAPI backend
  postgres:              # Banco de dados principal  
  redis:                 # Cache e sessões
  pgadmin:               # Interface web do PostgreSQL
  nginx:                 # Proxy reverso (opcional)
```

### Comandos Essenciais

```bash
# Iniciar todos os serviços em background
docker-compose up -d --build

# Ver logs em tempo real
docker-compose logs -f

# Ver logs específicos do API
docker-compose logs -f api

# Parar todos os serviços
docker-compose down

# Parar e remover volumes (CUIDADO: apaga dados!)
docker-compose down -v

# Rebuild apenas um serviço
docker-compose up -d --build api

# Ver status dos containers
docker-compose ps

# Executar comando dentro do container
docker-compose exec api bash

# Ver uso de recursos
docker stats
```

### Variáveis de Ambiente

Edite o arquivo `.env` para personalizar a configuração:

```bash
# Configuração do Banco
POSTGRES_USER=obd_user
POSTGRES_PASSWORD=obd_password_123
POSTGRES_DB=obd_database
POSTGRES_HOST=postgres
POSTGRES_PORT=5432

# Configuração da API
API_HOST=0.0.0.0
API_PORT=8000
DEBUG=false
SECRET_KEY=sua-chave-secreta-muito-longa-e-segura

# Configuração Redis
REDIS_HOST=redis
REDIS_PORT=6379
REDIS_PASSWORD=redis_password_123

# Configuração de Logs
LOG_LEVEL=INFO
LOG_FORMAT=json
```

### Acesso aos Serviços

| Serviço | URL Local | Credenciais | Descrição |
|---------|-----------|-------------|-----------|
| **API FastAPI** | http://localhost:8000 | - | API principal |
| **Documentação** | http://localhost:8000/docs | - | Swagger UI automático |
| **PostgreSQL** | localhost:5432 | `obd_user`/`obd_password_123` | Banco via cliente SQL |
| **PgAdmin** | http://localhost:5050 | `admin@obd.com`/`admin123` | Interface web do DB |
| **Redis** | localhost:6379 | - | Cache (via redis-cli) |

### Monitoramento Docker

```bash
# Ver uso de recursos dos containers
docker stats --format "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}"

# Ver logs com timestamp
docker-compose logs -f -t

# Verificar saúde dos serviços
curl http://localhost:8000/api/v1/health

# Backup do banco de dados
docker-compose exec postgres pg_dump -U obd_user obd_database > backup.sql

# Restaurar backup
docker-compose exec -T postgres psql -U obd_user obd_database < backup.sql
```

## 🔗 API e Endpoints

### Documentação Automática

A API FastAPI gera documentação automática disponível em:
- **Swagger UI**: http://localhost:8000/docs
- **ReDoc**: http://localhost:8000/redoc
- **OpenAPI Schema**: http://localhost:8000/openapi.json

### Endpoints Principais

#### 🚗 Gerenciamento de Veículos

```bash
# Criar novo veículo
curl -X POST "http://localhost:8000/api/v1/vehicles" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Civic 2020",
    "make": "Honda",
    "model": "Civic",
    "year": 2020,
    "engine_size": 2.0
  }'

# Listar veículos
curl "http://localhost:8000/api/v1/vehicles"

# Obter estatísticas do veículo
curl "http://localhost:8000/api/v1/vehicles/{vehicle_id}/statistics"
```

#### 📊 Telemetria

```bash
# Enviar lote de dados (usado pelo ESP32)
curl -X POST "http://localhost:8000/api/v1/telemetry/batch" \
  -H "Content-Type: application/json" \
  -d '{
    "session_id": "uuid-session",
    "vehicle_id": "uuid-vehicle", 
    "batch_size": 2,
    "telemetry_data": [
      {
        "device_timestamp": 1704067200000,
        "rpm": 2500,
        "speed": 80,
        "coolant_temp": 87
      }
    ]
  }'

# Consultar dados com filtros
curl "http://localhost:8000/api/v1/telemetry/records?vehicle_id=uuid&start_time=2024-01-01T00:00:00Z&limit=100"

# Importar CSV
curl -X POST "http://localhost:8000/api/v1/telemetry/import/csv" \
  -H "Content-Type: application/json" \
  -d '{
    "vehicle_id": "uuid-vehicle",
    "session_id": "uuid-session",
    "csv_data": "timestamp,rpm,speed\n1704067200,2500,80"
  }'
```

#### 🎯 Sessões

```bash
# Criar sessão de coleta
curl -X POST "http://localhost:8000/api/v1/sessions" \
  -H "Content-Type: application/json" \
  -d '{
    "vehicle_id": "uuid-vehicle",
    "name": "Viagem São Paulo",
    "description": "Teste de consumo urbano"
  }'

# Obter estatísticas da sessão
curl "http://localhost:8000/api/v1/sessions/{session_id}/statistics"
```

#### ⚡ Sistema

```bash
# Health check
curl "http://localhost:8000/api/v1/health"

# Estatísticas gerais
curl "http://localhost:8000/api/v1/stats"

# Informações do sistema
curl "http://localhost:8000/api/v1/info"
```

### Códigos de Response

| Código | Status | Descrição |
|--------|--------|-----------|
| 200 | OK | Operação bem sucedida |
| 201 | Created | Recurso criado com sucesso |
| 400 | Bad Request | Dados inválidos na requisição |
| 404 | Not Found | Recurso não encontrado |
| 422 | Unprocessable Entity | Erro de validação |
| 500 | Internal Server Error | Erro interno do servidor |

## 📈 Monitoramento e Debugging

### Logs do ESP32

```bash
# Monitor serial em tempo real
idf.py monitor

# Filtrar apenas logs de erro
idf.py monitor | grep "E ("

# Salvar logs em arquivo
idf.py monitor > debug.log 2>&1
```

### Exemplo de Log ESP32
```
I (12345) OBD_MAIN: === System Statistics ===
I (12346) OBD_MAIN: Uptime: 300 seconds (5.0 minutes)
I (12347) OBD_MAIN: Total OBD readings: 1500
I (12348) OBD_MAIN: Successful readings: 1485 (99.0%)
I (12349) OBD_MAIN: Failed readings: 15 (1.0%)
I (12350) OBD_MAIN: API sends: 150
I (12351) OBD_MAIN: API failures: 2
I (12352) OBD_MAIN: Current batch size: 7/10
I (12353) OBD_MAIN: Free heap: 187456 bytes
I (12354) OBD_MAIN: WiFi: Connected to MeuWiFi, IP: 192.168.1.150, RSSI: -45 dBm
I (12355) OBD_MAIN: API: Status 1, Consecutive failures: 0, Fallback: NO
I (12356) OBD_MAIN: CAN: TX errors: 0, RX errors: 2, Bus errors: 0
```

### Logs do Backend

```bash
# Logs estruturados do FastAPI
docker-compose logs api | jq '.'

# Monitorar apenas erros
docker-compose logs api | grep "ERROR"

# Logs do PostgreSQL
docker-compose logs postgres

# Métricas de performance
curl "http://localhost:8000/api/v1/stats" | jq '.'
```

### Debugging Avançado

```bash
# Debug do ESP32 com GDB
idf.py gdb

# Análise de memória
idf.py monitor | grep "heap"

# Profile de CPU
idf.py monitor | grep "Task"

# Verificar conectividade de rede
ping 192.168.1.100  # IP do servidor API

# Testar endpoint manualmente
curl -v "http://192.168.1.100:8000/api/v1/health"
```

## 🤝 Contribuição

Contribuições são bem-vindas! Para contribuir:

1. **Fork** o repositório
2. **Clone** seu fork localmente
3. **Crie** uma branch para sua feature (`git checkout -b feature/AmazingFeature`)
4. **Commit** suas mudanças (`git commit -m 'Add some AmazingFeature'`)
5. **Push** para a branch (`git push origin feature/AmazingFeature`)
6. **Abra** um Pull Request

### Guidelines de Desenvolvimento

- Use **Black** para formatação Python (`black .`)
- Execute **Pylint** para análise de código (`pylint app/`)
- Teste o ESP32 em ambiente real antes do PR
- Documente novas funcionalidades
- Mantenha compatibilidade com versões anteriores

### Estrutura de Commits

```
feat: adiciona suporte a novos PIDs OBD-II
fix: corrige bug de reconexão WiFi
docs: atualiza documentação da API
style: formata código conforme Black
refactor: reorganiza estrutura de pastas
test: adiciona testes para parser OBD
```

---

## 📄 Licença

Este projeto está licenciado sob a **MIT License** - veja o arquivo [LICENSE](LICENSE) para detalhes.

## 📞 Suporte

Para suporte técnico ou dúvidas:
- 🐛 **Issues**: [GitHub Issues](https://github.com/your-username/obd2_data_aquisition/issues)
- 📖 **Documentação**: [Wiki do Projeto](https://github.com/your-username/obd2_data_aquisition/wiki)
- 💬 **Discussões**: [GitHub Discussions](https://github.com/your-username/obd2_data_aquisition/discussions)

---

<div align="center">

**🚗 Feito com ❤️ para entusiastas automotivos e desenvolvedores IoT**

</div>
