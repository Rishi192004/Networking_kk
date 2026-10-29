#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t sai_status_t;
typedef uint64_t sai_object_id_t;

#define SAI_STATUS_SUCCESS 0
#define SAI_STATUS_FAILURE -1
#define SAI_STATUS_INVALID_PARAMETER -2
#define SAI_STATUS_INSUFFICIENT_RESOURCES -3
#define SAI_STATUS_ITEM_NOT_FOUND -4
#define SAI_STATUS_ITEM_ALREADY_EXISTS -5

#define SAI_NULL_OBJECT_ID 0ULL

sai_status_t minirouter_sai_initialize(void);
sai_status_t minirouter_sai_shutdown(void);

#ifdef __cplusplus
}
#endif
