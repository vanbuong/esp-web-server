#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "gw_http_tls.h"
#include "gw_store.h"
#include "gw_tls_csr.h"

static const char *TAG = "gw_tls";

esp_err_t gw_http_tls_csr(const char *hostname, bool new_key, char **csr_out)
{
    char key[GW_KEY_PEM_MAX];
    esp_err_t err = new_key ? ESP_ERR_NOT_FOUND : gw_store_get_pending_key(key, sizeof(key));
    if (err == ESP_ERR_NOT_FOUND) {
        err = gw_tls_new_key(key, sizeof(key));
        if (err == ESP_OK) {
            err = gw_store_set_pending_key(key);
            ESP_LOGI(TAG, "new device key generated (EC P-256)");
        }
    }
    char *csr = (err == ESP_OK) ? malloc(GW_CSR_PEM_MAX) : NULL;
    if (err == ESP_OK && !csr) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        err = gw_tls_make_csr(key, hostname, csr, GW_CSR_PEM_MAX);
    }
    memset(key, 0, sizeof(key));
    if (err != ESP_OK) {
        free(csr);
        csr = NULL;
    }
    *csr_out = csr;
    return err;
}

const char *gw_http_tls_accept(char *key, char *cert, char *ca, const char *hostname,
                               gw_tls_info_t *info)
{
    char pending[GW_KEY_PEM_MAX];
    if (!key) {
        if (gw_store_get_pending_key(pending, sizeof(pending)) != ESP_OK) {
            return "no_pending_key";   // CSR path, but no CSR was made yet
        }
        key = pending;
    }
    gw_pem_normalize(key);
    gw_pem_normalize(cert);
    gw_pem_normalize(ca);
    const char *err = gw_tls_check(key, cert, ca, hostname, info);
    if (!err && (gw_store_set_tls(key, cert, ca) != ESP_OK || gw_store_clear_pending_key() != ESP_OK)) {
        err = "storage_error";
    }
    memset(pending, 0, sizeof(pending));
    if (err) {
        ESP_LOGW(TAG, "identity rejected: %s", err);
    } else {
        ESP_LOGI(TAG, "TLS identity stored (CN=%s, issuer %s, %s, until %s)", info->subject_cn,
                 info->issuer_cn, info->key_type, info->not_after);
    }
    return err;
}
