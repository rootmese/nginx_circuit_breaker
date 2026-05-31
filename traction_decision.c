#include <ngx_http.h>

ngx_int_t
traction_decide(double score)
{
    if (score < 20.0)
    {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    if (score < 50.0)
    {
        return NGX_HTTP_TOO_MANY_REQUESTS;
    }

    return NGX_DECLINED;
}