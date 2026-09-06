#include "bmoe/meitte.h"

int main(void) {
    meitte_config config = meitte_default_config();
    char error[128];
    if (meitte_abi_version() != MEITTE_ABI_VERSION || !meitte_version()[0]) return 1;
    if (meitte_open(&config, error, sizeof(error)) || !error[0]) return 1;

    meitte_request request = meitte_default_request();
    meitte_result * result = meitte_generate(NULL, &request, NULL, NULL);
    if (!result || meitte_result_ok(result) || !meitte_result_error(result)[0]) return 1;
    meitte_result_free(result);
    meitte_cancel(NULL);
    meitte_close(NULL);
    return 0;
}
