/**
 * @file cfgjson.h
 * @brief Contain cJSON allocation, lifetime and strict object access.
 */
#ifndef UTHOME_CFGJSON_H
#define UTHOME_CFGJSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#define UTHOME_JSON_HEAP 32768U
typedef cJSON cfgjson_value_t;
/* One serialized owner exclusively uses cJSON through this module. init runs
 * once before other tasks; hooks remain installed. No other module may change
 * hooks or locale. open borrows input only during the call; tree pointers
 * expire at close. Heap is bounded to 32 KiB including our aligned allocation
 * headers, with 1024 allocations per parse; failure frees the entire partial
 * tree.
 */
int cfgjson_init (void);
const cfgjson_value_t * cfgjson_open (const uint8_t * p_data, size_t size);
void cfgjson_close (void);
size_t cfgjson_peak (void);
const cfgjson_value_t * cfgjson_get (const cfgjson_value_t * p_value,
                                     const char *            p_key);
const cfgjson_value_t * cfgjson_at (const cfgjson_value_t * p_value,
                                    size_t                  index);
bool cfgjson_array (const cfgjson_value_t * p_value,
                    size_t                  max,
                    size_t *                p_count);
bool cfgjson_object (const cfgjson_value_t * p_value,
                     const char * const *    pp_keys,
                     size_t                  count,
                     uint32_t                required);
bool cfgjson_is (const cfgjson_value_t * p_value, const char * p_text);
bool cfgjson_name (const cfgjson_value_t * p_value, char p_name[32]);
bool cfgjson_uint (const cfgjson_value_t * p_value,
                   uint32_t                min,
                   uint32_t                max,
                   uint32_t *              p_result);
bool cfgjson_int (const cfgjson_value_t * p_value,
                  int32_t                 min,
                  int32_t                 max,
                  int32_t *               p_result);
bool cfgjson_bool (const cfgjson_value_t * p_value, bool * pb_result);
#endif
/* End of file. */

