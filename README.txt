================================================================================
 TRACTION CONTROL MODULE FOR NGINX - TECHNICAL DETAILS
================================================================================

1. INTRODUCTION
---------------

The ngx_http_traction_control module implements an adaptive circuit breaker
based on HTTP error rate and latency signals observed in a sliding time window.

Goal: reduce pressure on degraded upstreams before abrupt collapses by applying
progressive degradation (warning → 429 → 503), combined with latency-aware
decision making.

1.1 DESIGN PHILOSOPHY
---------------------

Traditional circuit breakers typically operate as binary systems:
traffic is either allowed or blocked. While simple, abrupt state
changes can create oscillation, traffic spikes during recovery,
and repeated service instability.

Traction Control follows a continuous degradation model inspired by the
control philosophy used in Formula 1, particularly the McLaren MP4-20.
Instead of abrupt cutoffs, system pressure is reduced progressively as grip
conditions deteriorate.

The same principle is applied to HTTP traffic.

As upstream health degrades, the module progressively increases
intervention levels (warning → critical → emergency). When the
service recovers, traffic is not immediately restored to 100%.
Instead, requests are gradually reintroduced through a dedicated
RECOVERY phase, reducing instability and avoiding feedback oscillation.

Additionally, the system now considers latency degradation as a first-class
signal, not only error rate.

The objective is not simply to stop traffic, but to maintain system stability
under adverse conditions while enabling controlled recovery.

2. ZONE MODEL
-------------

Each monitored service/upstream has a named "zone" with isolated shared memory
across all workers.

Declaration:
  traction_zone <name> <size> [window=N];

Parameters:
  <name>     - Unique identifier (e.g. api_backend, payments)
  <size>     - Minimum SHM zone size (e.g. 1m, 512k)
               Automatically adjusted if smaller than required.
  window=N   - Window in seconds (1 to 3600, default 60)

Multiple locations may reference the same zone.
Each zone maintains independent metrics and state.

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
  Notes   : "zone=name" enables the module and binds a zone.

traction_status zone=name
  Context : location
  Default : -
  Notes   : Content handler for metrics exposure. Must be isolated.

traction_log on | off
  Context : http, server, location
  Default : off

traction_latency_threshold time
  Context : http, server, location
  Default : unset
  Notes   : Requests exceeding this duration are counted as
            latency_errors.

traction_warning_threshold N
traction_critical_threshold N
traction_emergency_threshold N
  Context : http, server, location
  Default : 80 / 50 / 20

Validation:
  emergency < critical < warning

traction_warning_action headers | off | rate_limit=N%
  Context : http, server, location
  Default : headers

  headers:
    - Allow traffic
    - Emit X-Traction-* headers

  off:
    - No intervention in WARNING state

  rate_limit=N%:
    - Probabilistic shedding using atomic counter
    - Remaining traffic receives headers

3.1 CONFIGURATION
-----------------

Standard NGINX configuration only.

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

4. REQUEST FLOW
---------------

  Client
     |
     v
  [PREACCESS] traction_handler
     |  - Zone lookup
     |  - Score computation (error + latency)
     |  - emergency  -> 503
     |  - critical   -> 429
     |  - warning    -> optional shedding
     |  - normal     -> allow
     v
  Upstream
     |
     v
  [HEADER FILTER]
     - X-Traction-State
     - X-Traction-Score
     - X-Traction-Zone
     - WARN logs (if applicable)
     v
  Response
     |
     v
  [LOG PHASE]
     - request counter increment
     - error detection (>=500)
     - latency_error if threshold exceeded

5. SLIDING WINDOW AND METRICS
-----------------------------

Bucket structure:
  requests
  errors
  latency_errors
  epoch

Score model:

  error_score   = 100 - (errors / requests * 100)
  latency_score = 100 - (latency_errors / requests * 100)

  final_score = min(error_score, latency_score)

Empty window:
  score = 100

6. STATE MACHINE
----------------

  score >= warning_threshold        -> NORMAL
  score >= critical_threshold       -> WARNING
  score >= emergency_threshold      -> CRITICAL
  score <  emergency_threshold      -> EMERGENCY


Recovery Transition:

If previous state was EMERGENCY or RECOVERY and score rises
above emergency_threshold but remains below critical_threshold:

    state = RECOVERY


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
      +----------+
      |          |
      v          v
    WARNING     NORMAL


RECOVERY behavior:

- Progressive traffic restoration:
    10% → 20% → 50%
- Remaining requests receive 429
- Prevents oscillation between EMERGENCY and NORMAL
- Ends when score rises above warning threshold

7. WARNING ACTION
-----------------

headers:
  - Allow traffic
  - Add X-Traction-* headers

off:
  - No intervention

rate_limit=N%:
  - Atomic counter-based distributed shedding
  - N% approximate global rejection

8. RESPONSE HEADERS
-------------------

X-Traction-State
X-Traction-Score
X-Traction-Zone

9. SHARED MEMORY
----------------

Each zone:

  requests
  errors
  latency_errors
  epoch
  shed_counter
  last_state

Fail-open:
  If SHM unavailable -> request allowed + WARN log

10. BUILD
--------

Build inside NGINX source tree:

  ./configure --add-dynamic-module=/path/to/nginx_circuit_breaker
  make modules

Output:
  ngx_http_traction_control_module.so

11. CODE STRUCTURE
------------------

- traction_score.c:
    error + latency scoring

- traction_state.c:
    includes RECOVERY transition logic

- traction_warning.c:
    rate limit + headers + shedding

- traction_handler.c:
    PREACCESS decision engine

- traction_metrics.c:
    request/error tracking

12. CONFIG EXAMPLE
------------------

http {
    traction_zone api_backend 1m window=60;

    location /api {
        traction_control zone=api_backend;
        traction_warning_action rate_limit=30%;
        proxy_pass http://upstream;
    }
}

13. LIMITATIONS
---------------

- HTTP-only signals (no CPU/memory upstream telemetry)
- Fixed recovery curve (10% → 20% → 50%)
- No external observability backend integration

14. FEATURE COMPARISON
----------------------

Envoy vs Traction Control:

- Envoy: resource-based control
- Traction Control: service-quality feedback control (error + latency)

15. CHANGE HISTORY
------------------

v0.4.2
  - Latency Score introduced
  - latency_errors added
  - final_score = min(error, latency)
  - improved state consistency

v0.4.1
  - Recovery refinement
  - structured recovery levels
  - header fixes

v0.4.0
  - Recovery state introduced

16. VERSIONING POLICY
---------------------

0.x = functional, evolving system
1.x = maintenance commitment

18. SUPPORT
-----------

agsilveira.7@gmail.com

================================================================================
 END OF DOCUMENT
================================================================================