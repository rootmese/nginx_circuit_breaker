TRACTION CONTROL MODULE FOR NGINX - README.txt
===============================================

Um modulo dinamico para Nginx que implementa controle de tracao baseado em
taxa de erros, protegendo o upstream contra sobrecarga e falhas em cascata.


INDICE
-------

1. Visao Geral
2. Como Funciona
3. Arquitetura
4. Memoria Compartilhada
5. Compilacao
6. Configuracao
7. Exemplos
8. API para Desenvolvedores
9. Limitacoes
10. Roadmap
11. Contribuicao
12. Licenca


1. VISAO GERAL
--------------

O modulo monitora continuamente a taxa de erros das requisicoes e aplica
acoes corretivas baseadas em limiares configuraveis. Quando a saude do
sistema degrada, o modulo retorna status HTTP apropriados para aliviar a
carga.

Principais caracteristicas:
- Operacoes atomicas para thread-safety
- Memoria compartilhada entre workers
- Sliding window temporal (buckets rotativos)
- Baixo overhead (sem locks)
- Configuravel por location


2. COMO FUNCIONA
----------------

Coleta de Metricas:
Cada requisicao e registrada em um bucket temporal baseado no timestamp
atual: bucket = time() % bucket_count

Os buckets formam uma sliding window onde os dados mais antigos sao
naturalmente sobrescritos.

Calculo da Pontuacao:
score = 100 - (erros / total) * 100

Score | Significado
------|-------------
100   | Saudavel (0% erro)
50    | Critico (50% erro)
0     | Morto (100% erro)

Decisao:
Score | Acao       | HTTP Status
------|------------|---------------------
< 20  | Emergency  | 503 Service Unavailable
< 50  | Critical   | 429 Too Many Requests
>= 50 | Normal     | NGX_DECLINED (segue fluxo)


3. ARQUITETURA
--------------

+-------------------------------------------------------------+
|                     Nginx Master Process                     |
|  +---------------------------------------------------------+ |
|  |            Shared Memory Zone (traction_shm)            | |
|  |  +-------+ +-------+ +-------+      +-------+           | |
|  |  |bucket0| |bucket1| |bucket2| ... |bucketN|            | |
|  |  |req/err| |req/err| |req/err|      |req/err|           | |
|  |  +-------+ +-------+ +-------+      +-------+           | |
|  +---------------------------------------------------------+ |
+-------------------------------------------------------------+
                              |
            +-----------------+-----------------+
            |                 |                 |
            v                 v                 v
    +--------------+  +--------------+  +--------------+
    | Worker 1     |  | Worker 2     |  | Worker N     |
    | traction_    |  | traction_    |  | traction_    |
    | handler()    |  | handler()    |  | handler()    |
    +--------------+  +--------------+  +--------------+

Estrutura de Arquivos:
traction-control/
├── ngx_http_traction_control_module.c
├── traction_handler.c
├── traction_metrics.c
├── traction_metrics.h
├── traction_score.c
├── traction_score.h
├── traction_decision.c
├── traction_decision.h
├── traction_shared_memory.c
├── traction_shared_memory.h
├── traction_config.c
├── traction_config.h
└── README.txt


4. MEMORIA COMPARTILHADA
------------------------

Como Funciona:
A memoria compartilhada e implementada usando ngx_shm_zone_t, que:
1. Aloca um segmento de memoria compartilhada durante a inicializacao
2. Mapeia o mesmo segmento em todos os workers
3. Persiste durante reloads (dados sao preservados)
4. Protege com operacoes atomicas (sem locks)

Inicializacao:
// No master process
traction_init_shm(cf)
    └── ngx_shared_memory_add()
            └── shm_zone->init = traction_shm_init_zone

// Em cada worker
traction_init_process(cycle)
    └── traction_shm_setup(cycle)
            └── traction_shm = shm_zone->data

Estrutura de Dados:
typedef struct {
    ngx_atomic_t requests;
    ngx_atomic_t errors;
} traction_bucket_t;

typedef struct {
    traction_bucket_t buckets[TRACTION_BUCKETS_DEFAULT];
    ngx_uint_t last_rotate;
    ngx_uint_t bucket_count;
} traction_shared_t;

Operacoes Atomicas:
ngx_atomic_fetch_add(&shm->buckets[bucket].requests, 1);


5. COMPILACAO
-------------

Pre-requisitos:
- Nginx 1.18+ (ou compativel)
- GCC / Clang
- Make

Passos:
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

Compilacao Rapida (sem instalacao):
cd /caminho/para/traction-control
make -f /caminho/para/nginx/objs/Makefile


6. CONFIGURACAO
---------------

Diretivas:
-------------------------------------------------------------------------------
Diretiva                | Tipo | Padrao | Descricao
------------------------|------|-------|----------------------------------------
traction_control        | flag | off   | Habilita o modulo
traction_bucket_count   | uint | 60    | Numero de buckets (sliding window)
traction_emergency      | uint | 20    | Limiar para 503 Service Unavailable
traction_critical       | uint | 50    | Limiar para 429 Too Many Requests
traction_warning        | uint | 80    | Limiar para log de warning
-------------------------------------------------------------------------------

Contextos Validos:
- http
- server
- location

Configuracao Minima:
-------------------------------------------------------------------------------
load_module modules/ngx_http_traction_control_module.so;

http {
    server {
        location /api {
            traction_control on;
            proxy_pass http://backend;
        }
    }
}
-------------------------------------------------------------------------------

Configuracao Avancada:
-------------------------------------------------------------------------------
load_module modules/ngx_http_traction_control_module.so;

http {
    traction_bucket_count 120;
    traction_emergency 15;
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
            traction_control off;
            proxy_pass http://backend/health;
        }
        
        location /admin {
            traction_emergency 10;
            traction_critical 30;
            proxy_pass http://backend/admin;
        }
    }
}
-------------------------------------------------------------------------------


7. EXEMPLOS
-----------

Exemplo 1: Protecao de API
-------------------------------------------------------------------------------
location /api/v1/ {
    traction_control on;
    traction_emergency 5;
    traction_critical 25;
    
    proxy_pass http://api_backend;
    proxy_read_timeout 30s;
}
-------------------------------------------------------------------------------

Exemplo 2: Microservicos com Diferentes SLAs
-------------------------------------------------------------------------------
# Servico critico (SLA 99.9%)
location /critical/ {
    traction_emergency 5;
    traction_critical 30;
    traction_bucket_count 300;
    proxy_pass http://critical_backend;
}

# Servico best-effort (SLA 95%)
location /batch/ {
    traction_emergency 40;
    traction_critical 60;
    proxy_pass http://batch_backend;
}
-------------------------------------------------------------------------------

Exemplo 3: Com Logging e Monitoramento
-------------------------------------------------------------------------------
log_format traction '$remote_addr - $status - score=$traction_score';

location / {
    traction_control on;
    
    access_log /var/log/nginx/traction.log traction;
    
    add_header X-Traction-Status $traction_status;
    add_header X-Traction-Score $traction_score;
    
    proxy_pass http://backend;
}
-------------------------------------------------------------------------------


8. API PARA DESENVOLVEDORES
---------------------------

Registro de Erros:
Para que o modulo funcione corretamente, e necessario registrar erros no
upstream:

#include "traction_metrics.h"

if (upstream_status >= 500 || upstream_status == 0) {
    traction_record_error();
}

Integracao com upstream_handler:
static void
ngx_http_traction_upstream_handler(ngx_http_request_t *r)
{
    ngx_http_upstream_t *u = r->upstream;
    
    if (u == NULL) {
        return;
    }
    
    if (u->peer.connection == NULL || u->status >= 500) {
        traction_record_error();
    }
}

Headers Customizados:
location / {
    add_header X-Traction-Debug $traction_debug;
    proxy_pass http://backend;
}


9. LIMITACOES
-------------

Atuais:
-------------------------------------------------------------------------------
Limitacao                 | Impacto                 | Solucao
--------------------------|-------------------------|--------------------------
Sem merge de configuracoes| Heranca pode falhar     | Usar apenas no mesmo nivel
traction_record_error()   | Erros nao registrados   | Chamar manualmente
nao integrada             |                         |
Buckets fixos (60)        | Janela temporal fixa    | Sera configuravel
Sem reset automatico      | Overflow eventual       | Sliding window planejada
-------------------------------------------------------------------------------

Conhecidas:
1. Overflow de contadores: ngx_atomic_t (64-bit) leva ~584 anos para
   overflow com 1M req/s
2. Precisao temporal: Usa time() (resolucao de 1 segundo)
3. Sem persistencia: Dados sao perdidos em reload (comportamento esperado)


10. ROADMAP
-----------

Versao 1.0 (MVP Atual):
- Memoria compartilhada
- Handler na fase PREACCESS
- Decisao baseada em score
- Operacoes atomicas

Versao 1.1 (Proxima):
- Merge de configuracoes (heranca)
- Diretivas completas
- Sliding window automatica
- Testes unitarios

Versao 1.2:
- Multiplas zonas de memoria (por upstream)
- Exportacao de metricas (Prometheus)
- Reset manual via API
- Suporte a variaveis nginx

Versao 2.0:
- Algoritmos adaptativos (AI/ML leve)
- Integracao com OpenTelemetry
- Suporte a HTTP/3
- Dashboard embutido


11. TROUBLESHOOTING
-------------------

Logs de Erro:
# Verificar se o modulo carregou
nginx -V 2>&1 | grep traction

# Verificar logs
tail -f /var/log/nginx/error.log | grep traction

Mensagens Comuns:
-------------------------------------------------------------------------------
Mensagem                                   | Significado | Acao
-------------------------------------------|-------------|------------------
traction: shared memory zone initialized  | OK - memoria alocada | -
traction: worker using shared memory      | OK - worker conectado | -
traction: shared memory zone not found    | ERRO - modulo nao configurado | Verificar load_module
-------------------------------------------------------------------------------

Debug:
# Compilar com debug
./configure --with-debug --add-dynamic-module=...

# Configurar debug
error_log /var/log/nginx/error.log debug;


12. CONTRIBUICAO
----------------

1. Fork o projeto
2. Crie uma branch (git checkout -b feature/nova-feature)
3. Commit suas mudancas (git commit -am 'Adiciona nova feature')
4. Push para a branch (git push origin feature/nova-feature)
5. Abra um Pull Request

Padroes de Codigo:
- Seguir estilo de codigo do Nginx (K&R, indentacao 4 espacos)
- Documentar funcoes publicas
- Adicionar testes para novas funcionalidades


13. LICENCA
-----------

BSD 2-Clause License

Copyright (c) 2024, Traction Control Contributors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


Status do Projeto: Alpha - Nao recomendado para producao
Ultima Atualizacao: Maio 2026
