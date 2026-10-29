#include "minirouter/api/minirouter_sai.h"

extern "C" {

sai_status_t minirouter_sai_initialize(void) {
    return SAI_STATUS_SUCCESS;
}

sai_status_t minirouter_sai_shutdown(void) {
    return SAI_STATUS_SUCCESS;
}

}
