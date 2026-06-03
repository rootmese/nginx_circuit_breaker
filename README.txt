================================================================================
 TRACTION CONTROL MODULE FOR NGINX - TECHNICAL DETAILS
================================================================================

1. INTRODUCTION
---------------

The ngx_http_traction_control module implements an adaptive circuit breaker
based on HTTP error rate observed in a sliding time window.

Goal: reduce pressure on degraded upstreams before abrupt collapses by applying
progressive degradation (warning → 429 → 503).

1.1 DESIGN PHILOSOPHY
---------------------

Traditional circuit breakers typically operate as binary systems:
traffic is either allowed or blocked. While simple, abrupt state
changes can create oscillation, traffic spikes during recovery,
and repeated service instability.

Traction Control follows a different approach inspired by the
control philosophy used in Formula 1, particularly the McLaren
MP4-20. Rather than applying sudden transitions, power delivery
is adjusted progressively to maintain vehicle stability when grip
conditions deteriorate.

The same principle is applied to HTTP traffic.

As upstream health degrades, the module progressively increases
intervention levels (warning → critical → emergency). When the
service recovers, traffic is not immediately restored to 100%.
Instead, requests are gradually reintroduced through a dedicated
recovery phase, reducing the risk of instability and repeated
failure cycles.

The objective is not simply to stop traffic, but to maintain
system stability under adverse conditions while allowing a
controlled return to normal operation.

2. ZONE MODEL
-------------

Each monitored service/upstream has a named "zone" with its own shared memory
across all workers.

Declaration:
  traction_zone <name> <size> [window=N];

Parameters:
  <name>     - Unique identifier (e.g. api_backend, payments)
  <size>     - Minimum SHM zone size (e.g. 1m, 512k). Automatically adjusted
               if smaller than required by the window.
  window=N   - Window in seconds (1 to 3600, default 60)

Multiple locations can reference the same zone. Locations using different
zones keep metrics fully isolated.

Example:
  traction_zone api_backend 1m window=120;
  traction_zone pay_backend 512k window=300;


3. DIRECTIVE REFERENCE
----------------------

traction_zone name size [window=N]
  Context : http
  Default : -

traction_control on | off | zone=name
  Context : http, server, location
  Default : off
  Notes   : "zone=name" implies enabled=on. When enabled=on it is mandatory
            to define a zone (directly or inherited).

traction_status zone=name
  Context : server, location
  Default : -
  Notes   : Registers a handler in the CONTENT phase. The location should be
            exclusive (no proxy_pass). Protect with allow/deny.

traction_warning_threshold N
  Context : http, server, location
  Default : 80
  Range   : 0 to 100

traction_critical_threshold N
  Context : http, server, location
  Default : 50
  Range   : 0 to 100

traction_emergency_threshold N
  Context : http, server, location
  Default : 20
  Range   : 0 to 100

Merge validation:
  emergency_threshold < critical_threshold < warning_threshold

traction_warning_action headers | off | rate_limit=N%
  Context : http, server, location
  Default : headers
  Values  :
    headers       - Only X-Traction-* headers and WARN log (no blocking)
    off           - No action in warning; blocking only occurs in critical/emergency
    rate_limit=N% - Reject N% of requests with HTTP 429 in warning;
                    admitted requests receive X-Traction-* headers
  Range N  : 1 to 99 (only for rate_limit)

3.1 TUNING / NGINX CONFIGURATION
-------------------------------

This module is tuned via standard NGINX configuration directives. There is no separate token or external tuning file required by the module itself.

Example:

  http {
      traction_zone api_backend 1m window=60;

      server {
          location /api {
              traction_control zone=api_backend;
              traction_warning_threshold 80;
              traction_critical_threshold 50;
              traction_emergency_threshold 20;
              traction_warning_action rate_limit=30%;
              proxy_pass http://api_upstream;
          }
      }
  }

You can keep tuning settings in a dedicated file and include it from `nginx.conf` using `include /path/to/traction_tuning.conf;`.

4. REQUEST FLOW
---------------

  Client
     |
     v
  [PREACCESS] traction_handler
     |  - Checks enabled + zone
     |  - Calculates score in the zone window
     |  - emergency  -> 503 + Retry-After (window seconds)
     |  - critical   -> 429 + Retry-After (1 second)
     |  - warning + rate_limit -> shed N% with 429 (atomic counter)
     |  - warning/normal -> NGX_DECLINED (continue)
     v
  [UPSTREAM] proxy_pass / fastcgi / etc.
     |
     v
  [HEADER FILTER] traction_header_filter
     |  - If warning + action headers/rate_limit: X-Traction-State,
     |    X-Traction-Score, X-Traction-Zone and NGX_LOG_WARN
     v
  Response to client
     |
     v
  [LOG] traction_log_handler
     |  - Only if r->upstream != NULL
     |  - traction_record_request()
     |  - traction_record_error() if status >= 500, 502 or 504
     v
  End


5. SLIDING WINDOW AND BUCKETS
-----------------------------

Bucket structure:
  requests  (ngx_atomic_t) - request counter
  errors    (ngx_atomic_t) - error counter
  epoch     (ngx_atomic_t) - bucket timestamp (Unix second)

Active bucket index:
  idx = current_unix_time % window

Lazy reset:
  When epoch != current_second, the bucket is zeroed before the write.

Score calculation (traction_calculate_stats):
  - Iterate buckets [0 .. window-1]
  - Ignore buckets where epoch + window <= now (expired)
  - Sum requests and errors with atomic read (fetch_add 0)
  - score = 100 - (errors/requests * 100)
  - No requests in the window: score = 100.0

Memory per zone:
  sizeof(traction_zone_shm_t) + window * sizeof(traction_bucket_t)

Additional fields in zone SHM:

  shed_counter (ngx_atomic_t)
      Atomic counter used by warning rate_limit=N%
      to distribute request shedding across workers.

  last_state (ngx_atomic_t)
      Stores the previous global state of the zone.
      Used by the recovery state machine to detect
      transitions from EMERGENCY to RECOVERY.

6. STATE MACHINE
----------------

  traction_get_state(conf, score):

  score >= warning_threshold        -> TRACTION_STATE_NORMAL
  score >= critical_threshold       -> TRACTION_STATE_WARNING
  score >= emergency_threshold      -> TRACTION_STATE_CRITICAL
  score <  emergency_threshold      -> TRACTION_STATE_EMERGENCY


  Recovery Transition:

  If the previous state was EMERGENCY or RECOVERY and the score
  rises above emergency_threshold but remains below
  critical_threshold, the state becomes:

      TRACTION_STATE_RECOVERY


  State Flow:

      NORMAL
         |
         v
      WARNING
         |
         v
      CRITICAL
         |
         v
      EMERGENCY
         |
         v
      RECOVERY
         |
         +----------------+
         |                |
         v                v
      WARNING          NORMAL


  Actions:

  NORMAL
      - no intervention

  WARNING
      - depends on traction_warning_action
        (see section 6.1)

  CRITICAL
      - HTTP 429 (Too Many Requests)
      - 100% of requests rejected

  EMERGENCY
      - HTTP 503 (Service Unavailable)
      - 100% of requests rejected

  RECOVERY
      - gradual traffic restoration
      - requests are partially allowed based on
        recovery progress
      - traffic release levels:

            10% allowed
            20% allowed
            50% allowed

      - remaining requests receive HTTP 429


  Notes:

  RECOVERY is entered only after an EMERGENCY state.

  While in RECOVERY, traffic is progressively restored
  as the score improves.

  RECOVERY remains active while the score is between
  emergency_threshold and critical_threshold.

  If the score rises above warning_threshold, the state
  returns directly to NORMAL.

  The RECOVERY state prevents oscillation between
  EMERGENCY and NORMAL by progressively reintroducing
  traffic as service health improves.

6.1 WARNING ACTION (traction_warning_action)
-------------------------------------------

  headers (default)
    - PREACCESS: allow traffic
    - HEADER FILTER: add X-Traction-* + WARN log

  off
    - No intervention until critical/emergency

  rate_limit=N%
    - PREACCESS: increment atomic shed_counter in the zone SHM
      if (counter % 100) < N -> HTTP 429 + Retry-After (1s) + WARN log
      else -> continue
    - HEADER FILTER: X-Traction-* headers on admitted requests

  Worker distribution:
    Shared atomic counter ensures ~N% aggregate rejection.


7. RESPONSE HEADERS (WARNING)
-----------------------------

  Emitted when warning_action = headers or rate_limit
  (only for requests that reached the upstream):

  X-Traction-State: warning
  X-Traction-Score: <score with 2 decimal places>
  X-Traction-Zone:  <zone name>


8. traction_status ENDPOINT
---------------------------

Accepted methods: GET, HEAD

Response Content-Type: text/plain

Fields:
  zone                  - zone name
  window                - window in seconds
  score                 - current score
  requests              - total in window
  errors                - total in window
  error_rate            - error percentage
  state                 - normal | warning | critical | emergency
  thresholds            - values configured in location
  warning_action        - headers | off | rate_limit
  warning_reject_rate   - percentage N (0 if not rate_limit)

The "critical" or "emergency" state in the status reflects the current score;
this endpoint does not block requests — it only reports.


9. SHARED MEMORY
---------------

Internal SHM zone name: traction_zone_<name>

Initialization:
  - traction_zone_register() in postconfiguration (per zone)
  - traction_zone_init() callback on the first cycle
  - traction_zones_setup() in init_process of each worker

Reload (SIGHUP):
  - Existing data is reused via the init callback data parameter

Fail-open:
  - If SHM is unavailable in PREACCESS, the request is allowed with WARN log


10. BUILD
--------

Prerequisites:
  - NGINX source tree matching the target version
  - Standard C toolchain

Note: this repository does not include a `Makefile` or `configure` script.
Build the module from within the NGINX source tree using NGINX's build system.

Dynamic module:
  ./configure --add-dynamic-module=/path/to/nginx_circuit_breaker
  make modules

Artifact:
  objs/ngx_http_traction_control_module.so  (Linux)
  objs/ngx_http_traction_control_module.so  or .dll (depending on platform)

Load:
  load_module modules/ngx_http_traction_control_module.so;

Compiled files (config):
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


11. CODE STRUCTURE
-----------------

ngx_http_traction_control_module.c
  NGINX module registration, directives, phase init, and process setup.

traction_config.c / traction_config.h
  ngx_http_traction_main_conf_t  - zones array
  ngx_http_traction_loc_conf_t   - enabled, zone, thresholds, status
  create/merge configuration per location.

traction_shared_memory.c / .h
  SHM zone registration, init callback, worker setup.

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
  traction_header_filter() - adds warning headers

traction_status.c
  traction_status_content_handler() - CONTENT


12. NGINX CONFIGURATION EXAMPLE
-----------------------------

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


13. TROUBLESHOOTING
-------------------

Check whether the module loaded:
  nginx -V 2>&1 | grep traction

Logs:
  tail -f /var/log/nginx/error.log | grep traction

Common messages:
  traction: zone shared memory ready          - OK, zone initialized
  traction: worker attached to shared memory  - OK, worker attached
  traction: unknown traction zone             - ERROR, zone not declared
  traction: shared memory unavailable         - WARN, fail-open active

Debug:
  ./configure --with-debug --add-dynamic-module=...
  error_log /var/log/nginx/error.log debug;


14. KNOWN LIMITATIONS (BETHA)
----------------------------

  - Metrics are based only on HTTP status (no explicit timeout treated as
    error, except 504 Gateway Timeout).
  - Bucket reset by epoch has a small race window across workers
    (acceptable for aggregated metrics).
  - The status endpoint exposes metrics without built-in authentication.
  - Fail-open when SHM is unavailable (configurable only in code).
  - Maximum window: 3600 seconds (TRACTION_WINDOW_MAX).


15. CHANGE HISTORY
------------------

  v0.1 (initial alpha)
    - Global score, fixed 60s window, no error recording.

  v0.2 (alpha)
    - Named zones with service-isolated SHM.
    - Dynamic window 1-3600s.
    - Error recording in LOG phase.
    - Warning threshold with X-Traction-* headers.
    - traction_status endpoint.
    - Full configuration directives.

  v0.3 (current alpha)
    - traction_warning_action: headers, off, rate_limit=N%.
    - Partial shed in warning via atomic counter (shed_counter).
    - Status endpoint reports warning_action and reject rate.

  v0.4 (beta)

  - Recovery state introduced.
  - Progressive traffic restoration.
  - Recovery state reporting.
  - Recovery response headers.

  ================================================================================

  16. SUPPORT
  -----------

  For questions or support, contact: agsilveira.7@gmail.com

  ================================================================================
   END OF DOCUMENT
  ================================================================================
