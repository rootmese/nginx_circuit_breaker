# Traction Control Module for NGINX

Dynamic module for NGINX that implements **adaptive traffic control based on error rate**, protecting upstream services from overload, progressive degradation, and cascading failures.

---

## Overview

The module monitors HTTP traffic health by tracking error rate in a **sliding time window**, with **per-zone isolated metrics** (service/upstream).

As degradation increases, the module applies progressive actions:

| State | Behavior |
|-------|----------|
| **Normal** | Traffic allowed |
| **Warning** | Configurable: headers, partial rate limit, or off (see `traction_warning_action`) |
| **Critical** | HTTP 429 (Too Many Requests) |
| **Emergency** | HTTP 503 (Service Unavailable) |

---

## Key Features

- Atomic (*lock-free*) counters
- Shared memory across NGINX workers
- **Named zones** with independent metrics per service
- Configurable time window from **1 to 3600 seconds**
- Sliding window with rotating buckets and epoch reset
- Thresholds configurable at `http`, `server`, or `location`
- Monitoring endpoint (`traction_status`)
- Configurable warning action (`headers`, `rate_limit`, `off`)
- Low computational overhead
- Native integration with NGINX phase cycle

---

## How It Works

### Metrics Collection

Metrics are recorded in the **LOG** phase only for requests that reached an upstream (`proxy_pass`, etc.):

- **Request** — incremented for every upstream response
- **Error** — incremented for 5xx responses, 502, and 504

Requests blocked by the module itself (429/503) are **not** included in metrics.

### Sliding Window

Each zone allocates buckets proportional to the configured window:

```text
bucket_index = current_second % window
```

Expired buckets are ignored in the calculation. When the second changes, the corresponding bucket is automatically reset.

### Score Calculation

```text
Score = 100 - ((Errors / Requests) * 100)
```

If there are no requests in the window, the score defaults to **100** (healthy).

### Decision Engine

| Score | State | Action |
|-------|-------|--------|
| ≥ warning (default: 80) | Normal | Continue processing |
| ≥ critical (default: 50) | Warning | Depends on `traction_warning_action` (see below) |
| ≥ emergency (default: 20) | Critical | HTTP 429 + `Retry-After` |
| < emergency (default: 20) | Emergency | HTTP 503 + `Retry-After` |

Thresholds must satisfy: `emergency < critical < warning`.

### Warning Action (`traction_warning_action`)

| Value | Behavior |
|-------|----------|
| `headers` (default) | Traffic allowed + `X-Traction-*` headers + WARN log |
| `rate_limit=N%` | Reject **N%** of requests with HTTP 429; the rest pass with `X-Traction-*` headers |
| `off` | No action — 100% traffic allowed until critical is reached |

The rate limit uses an atomic counter in the zone shared memory, distributing the shed evenly across workers.

```nginx
location /api {
    traction_control zone=backend;
    traction_warning_action rate_limit=30%;   # drop 30% in warning
    proxy_pass http://upstream;
}
```

---

## Installation

This repository does not include a `Makefile` or its own `configure` script.
You must build the module from the NGINX source tree using NGINX's configure system.

Build as a dynamic module:

```bash
./configure --add-dynamic-module=/path/to/nginx_circuit_breaker
make modules
```

Load it in `nginx.conf`:

```nginx
load_module modules/ngx_http_traction_control_module.so;
```

---

## Configuration

### Complete Example

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

### Directives

| Directive | Context | Description |
|-----------|---------|-------------|
| `traction_zone name size [window=N]` | `http` | Declares a metrics zone (1–3600s window, default 60) |
| `traction_control on \| off \| zone=name` | `http`, `server`, `location` | Enables control and associates a zone |
| `traction_status zone=name` | `server`, `location` | Enables the status endpoint for the zone |
| `traction_warning_threshold N` | `http`, `server`, `location` | Warning threshold (default: 80) |
| `traction_critical_threshold N` | `http`, `server`, `location` | Critical threshold / 429 (default: 50) |
| `traction_emergency_threshold N` | `http`, `server`, `location` | Emergency threshold / 503 (default: 20) |
| `traction_warning_action headers \| off \| rate_limit=N%` | `http`, `server`, `location` | Action on warning state (default: `headers`) |

### Status Endpoint

With `traction_status zone=name` in a `location`, a GET request returns:

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

Protect the endpoint with `allow`/`deny` or authentication — it exposes operational metrics.

---

## Architecture

```text
NGINX Worker
 ├── traction_zone (shared memory per service)
 │    └── buckets[window]  (requests, errors, epoch)
 ├── PREACCESS phase  → traction_handler   (decision: allow/deny)
 ├── LOG phase        → traction_log_handler (metrics collection)
 ├── HEADER filter    → traction_header_filter (warning headers)
 └── CONTENT phase    → traction_status (monitoring)
```

---

## Project Structure

```text
nginx_circuit_breaker/
 ├── config                              # NGINX module build script
 ├── ngx_http_traction_control_module.c  # Main module + directives
 ├── traction_handler.c                  # PREACCESS and LOG handlers
 ├── traction_header_filter.c            # X-Traction-* headers in warning
 ├── traction_status.c                   # Monitoring endpoint
 ├── traction_shared_memory.c/h          # Zones and shared memory
 ├── traction_metrics.c/h                # Request/error recording
 ├── traction_score.c/h                  # Score and stats calculation
 ├── traction_decision.c/h               # 429/503 decision logic
 ├── traction_state.c/h                  # State machine
 ├── traction_warning.c/h                # Configurable warning action
 └── traction_config.c/h                 # Location configuration
```

---

## Project Status

**Alpha**

Functional for architecture validation and controlled environment testing. Not recommended for production without adequate load testing and observability.

---

## Technical Details

See [README.txt](README.txt) for full reference on directives, internal flow, shared memory, and implementation notes.
