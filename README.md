# Traction Control Module for NGINX

Módulo dinâmico para NGINX que implementa **controle adaptativo de tráfego baseado em taxa de erros**, protegendo serviços upstream contra sobrecarga, degradação progressiva e falhas em cascata.

---

# Visão Geral

O módulo monitora continuamente a saúde operacional do tráfego HTTP por meio da observação da taxa de erros em uma janela temporal deslizante (*sliding window*).

Conforme a degradação aumenta, o módulo aplica ações progressivas para reduzir a pressão sobre os serviços upstream, evitando colapsos abruptos e melhorando o tempo de recuperação da aplicação.

## Principais Características

- Operações atômicas (*lock-free*)
- Memória compartilhada entre workers
- Sliding Window baseada em buckets rotativos
- Baixo overhead computacional
- Integração nativa com NGINX
- Configuração por `http`, `server` ou `location`
- Arquitetura preparada para evolução para controle adaptativo completo

---

# Como Funciona

## Coleta de Métricas

Cada requisição é registrada em um bucket temporal.

```text
bucket = current_time % bucket_count
```

## Cálculo do Score

```text
Score = 100 - ((Errors / Requests) * 100)
```

## Motor de Decisão

| Score | Estado | Ação |
|---------|---------|---------|
| >= 50 | Normal | Continua o fluxo |
| < 50 | Crítico | Retorna HTTP 429 |
| < 20 | Emergência | Retorna HTTP 503 |

---

# Arquitetura

```text
NGINX
 ├── Shared Memory
 ├── Buckets
 ├── Workers
 └── Traction Handler
```

---

# Status do Projeto

**Alpha**

Não recomendado para ambientes de produção neste estágio. O foco atual está na validação da arquitetura, do algoritmo de score e do modelo de controle adaptativo de tráfego.

---

#Detalhamento Técnico

Ver arquivo README.txt