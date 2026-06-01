# Traction Control Module for Nginx

Um modulo dinamico para Nginx que implementa controle de tracao baseado em taxa de erros, protegendo o upstream contra sobrecarga e falhas em cascata.

## Indice

- [Visao Geral](#visao-geral)
- [Como Funciona](#como-funciona)
- [Arquitetura](#arquitetura)
- [Memoria Compartilhada](#memoria-compartilhada)
- [Compilacao](#compilacao)
- [Configuracao](#configuracao)
- [Exemplos](#exemplos)
- [API para Desenvolvedores](#api-para-desenvolvedores)
- [Limitacoes](#limitacoes)
- [Roadmap](#roadmap)
- [Contribuicao](#contribuicao)
- [Licenca](#licenca)

---

## Visao Geral

O modulo monitora continuamente a taxa de erros das requisicoes e aplica acoes corretivas baseadas em limiares configuraveis. Quando a saude do sistema degrada, o modulo retorna status HTTP apropriados para aliviar a carga.

Principais caracteristicas:
- Operacoes atomicas para thread-safety
- Memoria compartilhada entre workers
- Sliding window temporal (buckets rotativos)
- Baixo overhead (sem locks)
- Configuravel por location

---

## Como Funciona

### Coleta de Metricas

Cada requisicao e registrada em um bucket temporal baseado no timestamp atual:

bucket = time() % bucket_count

Os buckets formam uma sliding window onde os dados mais antigos sao naturalmente sobrescritos.

### Calculo da Pontuacao

score = 100 - (erros / total) * 100

| Score | Significado |
|-------|-------------|
| 100   | Saudavel (0% erro) |
| 50    | Critico (50% erro) |
| 0     | Morto (100% erro) |

### Decisao

| Score | Acao | HTTP Status |
|-------|------|-------------|
| < 20  | Emergency | 503 Service Unavailable |
| < 50  | Critical  | 429 Too Many Requests |
| >= 50 | Normal    | NGX_DECLINED (segue fluxo) |

---

## Arquitetura
+-------------------------------------------------------------+
| Nginx Master Process |
| +---------------------------------------------------------+ |
| | Shared Memory Zone (traction_shm) | |
| | +-------+ +-------+ +-------+ +-------+ | |
| | |bucket0| |bucket1| |bucket2| ... |bucketN| | |
| | |req/err| |req/err| |req/err| |req/err| | |
| | +-------+ +-------+ +-------+ +-------+ | |
| +---------------------------------------------------------+ |
+-------------------------------------------------------------+
|
+-----------------+-----------------+
| | |
v v v
+--------------+ +--------------+ +--------------+
| Worker 1 | | Worker 2 | | Worker N |
| | | | | |
| traction_ | | traction_ | | traction_ |
| handler() | | handler() | | handler() |
+--------------+ +--------------+ +--------------+

text

### Estrutura de Arquivos
traction-control/
├── ngx_http_traction_control_module.c # Modulo principal
├── traction_handler.c # Handler HTTP
├── traction_metrics.c # Registro de metricas
├── traction_metrics.h
├── traction_score.c # Calculo da pontuacao
├── traction_score.h
├── traction_decision.c # Logica de decisao
├── traction_decision.h
├── traction_shared_memory.c # Memoria compartilhada
├── traction_shared_memory.h
├── traction_config.c # Configuracao
├── traction_config.h
└── README.md

text

---

## Memoria Compartilhada

### Como Funciona

A memoria compartilhada e implementada usando ngx_shm_zone_t, que:

1. Aloca um segmento de memoria compartilhada durante a inicializacao
2. Mapeia o mesmo segmento em todos os workers
3. Persiste durante reloads (dados sao preservados)
4. Protege com operacoes atomicas (sem locks)

### Inicializacao
// No master process
traction_init_shm(cf)
└── ngx_shared_memory_add()
└── shm_zone->init = traction_shm_init_zone

// Em cada worker
traction_init_process(cycle)
└── traction_shm_setup(cycle)
└── traction_shm = shm_zone->data

text

### Estrutura de Dados

```c
typedef struct {
    ngx_atomic_t requests;   // Contador de requisicoes (atomico)
    ngx_atomic_t errors;     // Contador de erros (atomico)
} traction_bucket_t;

typedef struct {
    traction_bucket_t buckets[TRACTION_BUCKETS_DEFAULT];
    ngx_uint_t last_rotate;      // Timestamp da ultima rotacao
    ngx_uint_t bucket_count;     // Numero real de buckets
} traction_shared_t;
Operacoes Atomicas
c
// Incremento thread-safe
ngx_atomic_fetch_add(&shm->buckets[bucket].requests, 1);
Compilacao
Pre-requisitos
Nginx 1.18+ (ou compativel)

GCC / Clang

Make

Passos
bash
# 1. Baixe o codigo fonte do Nginx
wget https://nginx.org/download/nginx-1.24.0.tar.gz
tar -xzf nginx-1.24.0.tar.gz
cd nginx-1.24.0

# 2. Configure com o modulo
./configure \
    --add-dynamic-module=/caminho/para/traction-control \
    --prefix=/etc/nginx \
    --sbin-path=/usr/sbin/nginx \
    --modules-path=/usr/lib/nginx/modules

# 3. Compile
make

# 4. Instale
sudo make install
Compilacao Rapida (sem instalacao)
bash
# Apenas compila o modulo
cd /caminho/para/traction-control
make -f /caminho/para/nginx/objs/Makefile
Configuracao
Diretivas
Diretiva	Tipo	Padrao	Descricao
traction_control	flag	off	Habilita o modulo
traction_bucket_count	uint	60	Numero de buckets (sliding window)
traction_emergency	uint	20	Limiar para 503 Service Unavailable
traction_critical	uint	50	Limiar para 429 Too Many Requests
traction_warning	uint	80	Limiar para log de warning
Contextos Validos
http

server

location

Configuracao Minima
nginx
load_module modules/ngx_http_traction_control_module.so;

http {
    server {
        location /api {
            traction_control on;
            proxy_pass http://backend;
        }
    }
}
Configuracao Avancada
nginx
load_module modules/ngx_http_traction_control_module.so;

http {
    # Configuracao global (padrao para todos servers)
    traction_bucket_count 120;      # 2 minutos com janela de 1s
    traction_emergency 15;          # Mais sensivel
    traction_critical 40;
    traction_warning 70;
    
    upstream backend {
        server 10.0.0.1:8080 weight=3;
        server 10.0.0.2:8080 weight=2;
        server 10.0.0.3:8080 backup;
        
        keepalive 32;
    }
    
    server {
        listen 80;
        server_name api.example.com;
        
        location / {
            traction_control on;
            proxy_pass http://backend;
            proxy_next_upstream error timeout;
        }
        
        location /health {
            # Endpoint de saude sem controle
            traction_control off;
            proxy_pass http://backend/health;
        }
        
        location /admin {
            # Configuracao mais restritiva para admin
            traction_emergency 10;
            traction_critical 30;
            proxy_pass http://backend/admin;
        }
    }
}
Exemplos
Exemplo 1: Protecao de API
nginx
location /api/v1/ {
    traction_control on;
    traction_emergency 5;    # Muito restritivo
    traction_critical 25;
    
    proxy_pass http://api_backend;
    proxy_read_timeout 30s;
}
Exemplo 2: Microservicos com Diferentes SLAs
nginx
# Servico critico (SLA 99.9%)
location /critical/ {
    traction_emergency 5;
    traction_critical 30;
    traction_bucket_count 300;  # 5 minutos de janela
    proxy_pass http://critical_backend;
}

# Servico best-effort (SLA 95%)
location /batch/ {
    traction_emergency 40;
    traction_critical 60;
    proxy_pass http://batch_backend;
}
Exemplo 3: Com Logging e Monitoramento
nginx
log_format traction '$remote_addr - $status - score=$traction_score';

location / {
    traction_control on;
    
    # Log customizado
    access_log /var/log/nginx/traction.log traction;
    
    # Headers para monitoramento
    add_header X-Traction-Status $traction_status;
    add_header X-Traction-Score $traction_score;
    
    proxy_pass http://backend;
}
API para Desenvolvedores
Registro de Erros
Para que o modulo funcione corretamente, e necessario registrar erros no upstream:

c
#include "traction_metrics.h"

// No handler de upstream
if (upstream_status >= 500 || upstream_status == 0) {
    traction_record_error();
}
Integracao com upstream_handler
c
static void
ngx_http_traction_upstream_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_t *u = r->upstream;
    
    if (u == NULL) {
        return;
    }
    
    // Registra erro se upstream falhou
    if (u->peer.connection == NULL || u->status >= 500) {
        traction_record_error();
    }
}
Headers Customizados
nginx
# Adicionar ao nginx.conf
location / {
    # Headers de debug (apenas para troubleshooting)
    add_header X-Traction-Debug $traction_debug;
    
    proxy_pass http://backend;
}
Limitacoes
Atuais
Limitacao	Impacto	Solucao
Sem merge de configuracoes	Heranca pode falhar	Usar apenas no mesmo nivel
traction_record_error() nao integrada	Erros nao registrados	Chamar manualmente
Buckets fixos (60)	Janela temporal fixa	Sera configuravel
Sem reset automatico	Overflow eventual	Sliding window planejada
Conhecidas
Overflow de contadores: ngx_atomic_t (normalmente 64-bit) leva ~584 anos para overflow com 1M req/s

Precisao temporal: Usa time() (resolucao de 1 segundo)

Sem persistencia: Dados sao perdidos em reload (comportamento esperado)

Roadmap
Versao 1.0 (MVP Atual)
Memoria compartilhada

Handler na fase PREACCESS

Decisao baseada em score

Operacoes atomicas

Versao 1.1 (Proxima)
Merge de configuracoes (heranca)

Diretivas completas

Sliding window automatica

Testes unitarios

Versao 1.2
Multiplas zonas de memoria (por upstream)

Exportacao de metricas (Prometheus)

Reset manual via API

Suporte a variaveis nginx

Versao 2.0
Algoritmos adaptativos (AI/ML leve)

Integracao com OpenTelemetry

Suporte a HTTP/3

Dashboard embutido

Troubleshooting
Logs de Erro
bash
# Verificar se o modulo carregou
nginx -V 2>&1 | grep traction

# Verificar logs
tail -f /var/log/nginx/error.log | grep traction
Mensagens Comuns
Mensagem	Significado	Acao
traction: shared memory zone initialized	OK - memoria alocada	-
traction: worker using shared memory	OK - worker conectado	-
traction: shared memory zone not found	ERRO - modulo nao configurado	Verificar load_module
Debug
bash
# Compilar com debug
./configure --with-debug --add-dynamic-module=...

# Configurar debug
error_log /var/log/nginx/error.log debug;
Contribuicao
Fork o projeto

Crie uma branch (git checkout -b feature/nova-feature)

Commit suas mudancas (git commit -am 'Adiciona nova feature')

Push para a branch (git push origin feature/nova-feature)

Abra um Pull Request

Padroes de Codigo
Seguir estilo de codigo do Nginx (K&R, indentacao 4 espacos)

Documentar funcoes publicas

Adicionar testes para novas funcionalidades

Licenca
BSD 2-Clause License

Copyright (c) 2024, Traction Control Contributors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Contato
Issues: GitHub Issues

Discussoes: GitHub Discussions

Status do Projeto: Alpha - Nao recomendado para producao