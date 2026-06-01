================================================================================
 TRACTION CONTROL MODULE FOR NGINX - DETALHAMENTO TÉCNICO
================================================================================

1. INTRODUÇÃO
-------------

O módulo ngx_http_traction_control implementa um circuit breaker adaptativo
baseado em taxa de erros HTTP observada em janela temporal deslizante.

Objetivo: reduzir pressão sobre upstreams degradados antes de colapsos
abruptos, aplicando degradação progressiva (warning → 429 → 503).


2. MODELO DE ZONES
------------------

Cada serviço/upstream monitorado possui uma "zone" nomeada com memória
compartilhada própria entre todos os workers.

Declaração:
  traction_zone <nome> <tamanho> [window=N];

Parâmetros:
  <nome>     - Identificador único (ex.: api_backend, payments)
  <tamanho>  - Tamanho mínimo da zona SHM (ex.: 1m, 512k). Ajustado
               automaticamente se menor que o necessário para a janela.
  window=N   - Janela em segundos (1 a 3600, padrão 60)

Múltiplas locations podem referenciar a mesma zone. Locations com zones
diferentes mantêm métricas completamente isoladas.

Exemplo:
  traction_zone api_backend 1m window=120;
  traction_zone pay_backend 512k window=300;


3. REFERÊNCIA DE DIRETIVAS
--------------------------

traction_zone name size [window=N]
  Contexto : http
  Default  : -

traction_control on | off | zone=name
  Contexto : http, server, location
  Default  : off
  Notas    : "zone=name" implica enabled=on. Com enabled=on é obrigatório
             definir uma zone (direta ou herdada).

traction_status zone=name
  Contexto : server, location
  Default  : -
  Notas    : Registra handler na fase CONTENT. O location deve ser
             exclusivo (sem proxy_pass). Proteger com allow/deny.

traction_warning_threshold N
  Contexto : http, server, location
  Default  : 80
  Faixa    : 0 a 100

traction_critical_threshold N
  Contexto : http, server, location
  Default  : 50
  Faixa    : 0 a 100

traction_emergency_threshold N
  Contexto : http, server, location
  Default  : 20
  Faixa    : 0 a 100

Validação em merge:
  emergency_threshold < critical_threshold < warning_threshold

traction_warning_action headers | off | rate_limit=N%
  Contexto : http, server, location
  Default  : headers
  Valores  :
    headers       - Apenas headers X-Traction-* e log WARN (não bloqueia)
    off           - Nenhuma ação em warning; bloqueio só em critical/emergency
    rate_limit=N% - Rejeita N% das requisições com HTTP 429 em warning;
                    requisições admitidas recebem headers X-Traction-*
  Faixa N  : 1 a 99 (somente para rate_limit)


4. FLUXO DE REQUISIÇÃO
----------------------

  Cliente
     |
     v
  [PREACCESS] traction_handler
     |  - Verifica enabled + zone
     |  - Calcula score na janela da zone
     |  - emergency  -> 503 + Retry-After (window segundos)
     |  - critical   -> 429 + Retry-After (1 segundo)
     |  - warning + rate_limit -> shed N% com 429 (contador atômico)
     |  - warning/normal -> NGX_DECLINED (continua)
     v
  [UPSTREAM] proxy_pass / fastcgi / etc.
     |
     v
  [HEADER FILTER] traction_header_filter
     |  - Se warning + action headers/rate_limit: X-Traction-State,
     |    X-Traction-Score, X-Traction-Zone e log NGX_LOG_WARN
     v
  Resposta ao cliente
     |
     v
  [LOG] traction_log_handler
     |  - Somente se r->upstream != NULL
     |  - traction_record_request()
     |  - traction_record_error() se status >= 500, 502 ou 504
     v
  Fim


5. SLIDING WINDOW E BUCKETS
---------------------------

Estrutura por bucket:
  requests  (ngx_atomic_t) - contador de requisições
  errors    (ngx_atomic_t) - contador de erros
  epoch     (ngx_uint_t)   - segundo wall-clock da última escrita

Índice do bucket ativo:
  idx = current_unix_time % window

Reset lazy:
  Quando epoch != current_second, o bucket é zerado antes da escrita.

Cálculo do score (traction_calculate_stats):
  - Itera buckets [0 .. window-1]
  - Ignora buckets onde epoch + window <= now (expirados)
  - Soma requests e errors com leitura atômica (fetch_add 0)
  - score = 100 - (errors/requests * 100)
  - Sem requests na janela: score = 100.0

Memória por zone:
  sizeof(traction_zone_shm_t) + window * sizeof(traction_bucket_t)

Campos adicionais na zone SHM:
  shed_counter (ngx_atomic_t) - contador para rate_limit em warning


6. MÁQUINA DE ESTADOS
---------------------

  traction_get_state(conf, score):

  score >= warning_threshold        -> TRACTION_STATE_NORMAL
  score >= critical_threshold       -> TRACTION_STATE_WARNING
  score >= emergency_threshold      -> TRACTION_STATE_CRITICAL
  score <  emergency_threshold      -> TRACTION_STATE_EMERGENCY

  Ações:
  NORMAL    - nenhuma intervenção
  WARNING   - depende de traction_warning_action (ver seção 6.1)
  CRITICAL  - HTTP 429 (100% bloqueado)
  EMERGENCY - HTTP 503 (100% bloqueado)


6.1 AÇÃO EM WARNING (traction_warning_action)
---------------------------------------------

  headers (padrão)
    - PREACCESS: permite tráfego
    - HEADER FILTER: adiciona X-Traction-* + log WARN

  off
    - Nenhuma intervenção até critical/emergency

  rate_limit=N%
    - PREACCESS: incrementa shed_counter atômico na zone SHM
      if (counter % 100) < N -> HTTP 429 + Retry-After (1s) + log WARN
      else -> continua
    - HEADER FILTER: headers X-Traction-* nas requisições admitidas

  Distribuição entre workers:
    Contador atômico compartilhado garante ~N% de rejeição agregada.


7. HEADERS DE RESPOSTA (WARNING)
--------------------------------

  Emitidos quando warning_action = headers ou rate_limit
  (somente em requisições que chegaram ao upstream):

  X-Traction-State: warning
  X-Traction-Score: <score com 2 casas decimais>
  X-Traction-Zone:  <nome da zone>


8. ENDPOINT traction_status
---------------------------

Métodos aceitos: GET, HEAD

Resposta Content-Type: text/plain

Campos:
  zone          - nome da zone
  window        - janela em segundos
  score         - score atual
  requests      - total na janela
  errors        - total na janela
  error_rate    - percentual de erros
  state         - normal | warning | critical | emergency
  thresholds    - valores configurados no location
  warning_action        - headers | off | rate_limit
  warning_reject_rate   - percentual N (0 se não rate_limit)

O estado "critical" ou "emergency" no status reflete o score atual; o
endpoint não bloqueia requisições — apenas reporta.


9. SHARED MEMORY
----------------

Nome interno da zona SHM: traction_zone_<nome>

Inicialização:
  - traction_zone_register() em postconfiguration (por zone)
  - traction_shm_init_zone() callback no primeiro ciclo
  - traction_zones_setup() em init_process de cada worker

Reload (SIGHUP):
  - Dados existentes são reutilizados via parâmetro data do init callback

Fail-open:
  - Se SHM indisponível no PREACCESS, requisição é permitida com log WARN


10. COMPILAÇÃO
--------------

Pré-requisitos:
  - Código-fonte do NGINX (mesma versão alvo)
  - Toolchain C padrão

Módulo dinâmico:
  ./configure --add-dynamic-module=/path/to/nginx_circuit_breaker
  make modules

Artefato:
  objs/ngx_http_traction_control_module.so  (Linux)
  objs/ngx_http_traction_control_module.so  ou .dll (conforme plataforma)

Carregamento:
  load_module modules/ngx_http_traction_control_module.so;

Arquivos compilados (config):
  ngx_http_traction_control_module.c
  traction_handler.c
  traction_shared_memory.c
  traction_metrics.c
  traction_score.c
  traction_decision.c
  traction_config.c
  traction_state.c
  traction_warning.c
  traction_header_filter.c
  traction_status.c


11. ESTRUTURA DE CÓDIGO
-----------------------

ngx_http_traction_control_module.c
  Registro do módulo NGINX, diretivas, init de fases e processo.

traction_config.c / traction_config.h
  ngx_http_traction_main_conf_t  - array de zones
  ngx_http_traction_loc_conf_t   - enabled, zone, thresholds, status
  create/merge de configuração por location.

traction_shared_memory.c / .h
  Registro de zones SHM, init callback, setup em workers.

traction_metrics.c / .h
  traction_record_request(), traction_record_error()

traction_score.c / .h
  traction_calculate_stats() -> { score, requests, errors }

traction_state.c / .h
  traction_get_state(), traction_state_name()

traction_warning.c / .h
  traction_warning_emit_headers(), traction_warning_should_shed(),
  traction_warning_action_name()

traction_decision.c / .h
  traction_decide() -> NGX_DECLINED | 429 | 503

traction_handler.c
  traction_handler()      - PREACCESS
  traction_log_handler()  - LOG

traction_header_filter.c
  traction_header_filter() - adiciona headers em warning

traction_status.c
  traction_status_content_handler() - CONTENT


12. EXEMPLO DE CONFIGURAÇÃO NGINX
---------------------------------

http {
    load_module modules/ngx_http_traction_control_module.so;

    traction_zone api_backend 1m window=60;
    traction_zone pay_backend 1m window=120;

    upstream api_upstream {
        server 127.0.0.1:8080;
    }

    upstream pay_upstream {
        server 127.0.0.1:8081;
    }

    server {
        listen 80;

        location /api/ {
            traction_control zone=api_backend;
            traction_warning_threshold 80;
            traction_critical_threshold 50;
            traction_emergency_threshold 20;
            traction_warning_action rate_limit=30%;
            proxy_pass http://api_upstream;
        }

        location /pay/ {
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


13. LIMITAÇÕES CONHECIDAS (ALPHA)
---------------------------------

  - Métricas baseadas apenas em status HTTP (sem timeout explícito como
    erro, salvo 504 Gateway Timeout).
  - Reset de bucket por epoch tem janela de corrida mínima entre workers
    (aceitável para métricas agregadas).
  - Endpoint de status expõe métricas sem autenticação própria.
  - Fail-open quando SHM indisponível (configurável apenas por código).
  - Janela máxima: 3600 segundos (TRACTION_WINDOW_MAX).


14. HISTÓRICO DE EVOLUÇÃO
-------------------------

  v0.1 (alpha inicial)
    - Score global, janela fixa 60s, sem registro de erros.

  v0.2 (alpha)
    - Zones nomeadas com SHM isolada por serviço.
    - Janela dinâmica 1-3600s.
    - Registro de erros na fase LOG.
    - Threshold warning com headers X-Traction-*.
    - Endpoint traction_status.
    - Diretivas completas de configuração.

  v0.3 (alpha atual)
    - traction_warning_action: headers, off, rate_limit=N%.
    - Shed parcial em warning via contador atômico (shed_counter).
    - Status endpoint reporta warning_action e reject rate.

================================================================================
 FIM DO DOCUMENTO
================================================================================
