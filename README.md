Traction Control Module for Nginx
Um módulo dinâmico para Nginx que implementa controle de tração baseado em taxa de erros, protegendo o upstream contra sobrecarga e falhas em cascata.

Visão Geral
O módulo monitora continuamente a taxa de erros das requisições e aplica ações corretivas baseadas em limiares configuráveis. Quando a saúde do sistema degrada, o módulo retorna status HTTP apropriados para aliviar a carga.

Como Funciona
Coleta de Métricas: Cada requisição é registrada em um bucket temporal (padrão: 60 buckets)

Cálculo de Pontuação: A pontuação é calculada como 100 - (erros / total) * 100

Decisão: Baseada na pontuação atual:

score < 20 → 503 Service Unavailable

score < 50 → 429 Too Many Requests

score >= 50 → NGX_DECLINED (processamento normal)

Arquitetura
text
traction_handler.c       # Handler principal
├── traction_metrics.c   # Registro de métricas
├── traction_score.c     # Cálculo da pontuação
└── traction_decision.c  # Decisão baseada no score

traction_shared_memory.c # Memória compartilhada (entre workers)
traction_config.c        # Configuração do módulo
Compilação
bash
# Assumindo código fonte do Nginx em /usr/src/nginx
./configure --add-dynamic-module=/caminho/para/traction-control
make
make install
Configuração
Diretivas
nginx
location /api {
    traction_control on;
    traction_bucket_count 60;           # Número de buckets (padrão: 60)
    traction_emergency 20;              # Limiar para 503 (padrão: 20)
    traction_critical 50;               # Limiar para 429 (padrão: 50)
    traction_warning 80;                # Limiar para alerta (padrão: 80)
    
    proxy_pass http://backend;
}
Exemplo Completo
nginx
http {
    # Carrega o módulo dinâmico
    load_module modules/ngx_http_traction_control_module.so;
    
    upstream backend {
        server 10.0.0.1:8080;
        server 10.0.0.2:8080;
    }
    
    server {
        listen 80;
        
        location / {
            traction_control on;
            proxy_pass http://backend;
        }
    }
}
Registro de Erros
Para que o módulo funcione corretamente, é necessário registrar erros no momento apropriado:

c
// No handler de erro do upstream
#include "traction_metrics.h"

if (upstream_status >= 500) {
    traction_record_error();
}
TODO
Implementar ngx_shm_zone_t na traction_init_shm()

Adicionar suporte a diretivas de configuração

Implementar merge de configurações (herança)

Adicionar logging para mudanças de estado

Implementar reset periódico dos buckets

Adicionar suporte a múltiplos workers com memória compartilhada

Limitações Atuais
Memória compartilhada não totalmente implementada

Configurações fixas (hardcoded)

Sem suporte a múltiplos contextos (server/location)

Depende de chamada externa para traction_record_error()

Contribuição
Fork o projeto

Crie uma branch (git checkout -b feature/nova-feature)

Commit suas mudanças (git commit -am 'Adiciona nova feature')

Push para a branch (git push origin feature/nova-feature)

Abra um Pull Request

Licença
BSD 2-Clause License