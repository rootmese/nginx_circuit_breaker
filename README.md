# Traction Control Module for NGINX

Módulo dinâmico para NGINX que implementa **controle adaptativo de tráfego baseado em taxa de erros**, protegendo serviços upstream contra sobrecarga, degradação progressiva e falhas em cascata.

---

## Visão Geral

O módulo monitora a saúde operacional do tráfego HTTP observando a taxa de erros em uma **janela temporal deslizante** (*sliding window*), com métricas **isoladas por zone** (serviço/upstream).

Conforme a degradação aumenta, o módulo aplica ações progressivas:

| Estado | Comportamento |
|--------|---------------|
| **Normal** | Tráfego liberado |
| **Warning** | Configurável: headers, rate limit parcial ou off (ver `traction_warning_action`) |
| **Critical** | HTTP 429 (Too Many Requests) |
| **Emergency** | HTTP 503 (Service Unavailable) |

---

## Principais Características

- Operações atômicas (*lock-free*) nos contadores
- Memória compartilhada entre workers do NGINX
- **Zones nomeadas** com métricas independentes por serviço
- Janela temporal configurável de **1 a 3600 segundos**
- Sliding window com buckets rotativos e reset por epoch
- Thresholds configuráveis por `http`, `server` ou `location`
- Endpoint de monitoramento (`traction_status`)
- Ação configurável em **warning** (`headers`, `rate_limit`, `off`)
- Baixo overhead computacional
- Integração nativa com o ciclo de fases do NGINX

---

## Como Funciona

### Coleta de Métricas

Métricas são registradas na fase **LOG**, somente para requisições que passaram pelo upstream (`proxy_pass`, etc.):

- **Request** — incrementado para cada resposta upstream
- **Error** — incrementado em respostas 5xx, 502 e 504

Requisições bloqueadas pelo próprio módulo (429/503) **não** entram nas métricas.

### Sliding Window

Cada zone aloca buckets proporcionais à janela configurada:

```text
bucket_index = current_second % window
```

Buckets expirados são ignorados no cálculo. Quando o segundo muda, o bucket correspondente é resetado automaticamente.

### Cálculo do Score

```text
Score = 100 - ((Errors / Requests) * 100)
```

Se não houver requests na janela, o score assume **100** (saudável).

### Motor de Decisão

| Score | Estado | Ação |
|-------|--------|------|
| ≥ warning (padrão: 80) | Normal | Continua o fluxo |
| ≥ critical (padrão: 50) | Warning | Depende de `traction_warning_action` (ver abaixo) |
| ≥ emergency (padrão: 20) | Critical | HTTP 429 + `Retry-After` |
| < emergency (padrão: 20) | Emergency | HTTP 503 + `Retry-After` |

Thresholds devem satisfazer: `emergency < critical < warning`.

### Ação em Warning (`traction_warning_action`)

| Valor | Comportamento |
|-------|---------------|
| `headers` (padrão) | Tráfego liberado + headers `X-Traction-*` + log WARN |
| `rate_limit=N%` | Rejeita **N%** das requisições com HTTP 429; as demais passam com headers `X-Traction-*` |
| `off` | Nenhuma ação — tráfego 100% liberado até atingir critical |

O rate limit usa contador atômico na shared memory da zone, distribuindo o shed entre workers de forma uniforme.

```nginx
location /api {
    traction_control zone=backend;
    traction_warning_action rate_limit=30%;   # derruba 30% em warning
    proxy_pass http://upstream;
}
```

---

## Instalação

Compilar como módulo dinâmico:

```bash
./configure --add-dynamic-module=/caminho/para/nginx_circuit_breaker
make modules
```

Carregar no `nginx.conf`:

```nginx
load_module modules/ngx_http_traction_control_module.so;
```

---

## Configuração

### Exemplo completo

```nginx
http {
    traction_zone api_backend   1m window=60;
    traction_zone pay_backend   1m window=120;

    server {
        location /api {
            traction_control zone=api_backend;
            traction_warning_threshold 80;
            traction_critical_threshold 50;
            traction_emergency_threshold 20;
            traction_warning_action rate_limit=30%;
            proxy_pass http://api_upstream;
        }

        location /payments {
            traction_control zone=pay_backend;
            proxy_pass http://pay_upstream;
        }

        location = /traction/status {
            traction_status zone=api_backend;
            traction_warning_threshold 80;
            traction_critical_threshold 50;
            traction_emergency_threshold 20;
            allow 127.0.0.1;
            deny all;
        }
    }
}
```

### Diretivas

| Diretiva | Contexto | Descrição |
|----------|----------|-----------|
| `traction_zone name size [window=N]` | `http` | Declara uma zone de métricas (janela 1–3600 s, padrão 60) |
| `traction_control on \| off \| zone=name` | `http`, `server`, `location` | Ativa o controle e associa uma zone |
| `traction_status zone=name` | `server`, `location` | Habilita endpoint de status para a zone |
| `traction_warning_threshold N` | `http`, `server`, `location` | Limite de warning (padrão: 80) |
| `traction_critical_threshold N` | `http`, `server`, `location` | Limite de critical / 429 (padrão: 50) |
| `traction_emergency_threshold N` | `http`, `server`, `location` | Limite de emergency / 503 (padrão: 20) |
| `traction_warning_action headers \| off \| rate_limit=N%` | `http`, `server`, `location` | Ação no estado warning (padrão: `headers`) |

### Endpoint de status

Com `traction_status zone=name` em um `location`, uma requisição GET retorna:

```text
Traction Status
===============
zone: api_backend
window: 60 s
score: 85.50
requests: 1000
errors: 145
error_rate: 14.50%
state: normal
thresholds: warning=80 critical=50 emergency=20
warning_action: rate_limit
warning_reject_rate: 30%
```

Proteja o endpoint com `allow`/`deny` ou autenticação — ele expõe métricas operacionais.

---

## Arquitetura

```text
NGINX Worker
 ├── traction_zone (shared memory por serviço)
 │    └── buckets[window]  (requests, errors, epoch)
 ├── PREACCESS phase  → traction_handler   (decisão: permitir/bloquear)
 ├── LOG phase        → traction_log_handler (coleta de métricas)
 ├── HEADER filter    → traction_header_filter (headers em warning)
 └── CONTENT phase    → traction_status (monitoramento)
```

---

## Estrutura do Projeto

```text
nginx_circuit_breaker/
 ├── config                              # Script de build do módulo NGINX
 ├── ngx_http_traction_control_module.c  # Módulo principal + diretivas
 ├── traction_handler.c                  # Handlers PREACCESS e LOG
 ├── traction_header_filter.c            # Headers X-Traction-* em warning
 ├── traction_status.c                   # Endpoint de monitoramento
 ├── traction_shared_memory.c/h          # Zones e shared memory
 ├── traction_metrics.c/h                # Registro de requests/erros
 ├── traction_score.c/h                  # Cálculo de score e stats
 ├── traction_decision.c/h               # Decisão 429/503
 ├── traction_state.c/h                  # Máquina de estados
 ├── traction_warning.c/h                # Ação configurável em warning
 └── traction_config.c/h                 # Configuração por location
```

---

## Status do Projeto

**Alpha**

Funcional para validação de arquitetura e testes em ambientes controlados. Não recomendado para produção sem testes de carga e observabilidade adequados.

---

## Detalhamento Técnico

Consulte [README.txt](README.txt) para referência completa de diretivas, fluxo interno, memória compartilhada e notas de implementação.
