/**
 * @file cfgjson.h
 * @brief Bounded JSON views over caller-owned configuration bytes.
 */
#ifndef UTHOME_CFGJSON_H
#define UTHOME_CFGJSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTHOME_JSON_NODES 512U
#define UTHOME_JSON_STEPS 1024U

/* Compact preorder view. Offsets are into the borrowed input; next is the
 * index after this value's complete subtree. Keys start at their opening quote.
 * Fields are read-only to callers; obtain views only through this module. */
typedef struct
{
    uint16_t start;
    uint16_t end;
    uint16_t next;
    uint16_t key;
} cfgjson_value_t;

#define UTHOME_JSON_BYTES (UTHOME_JSON_NODES * sizeof(cfgjson_value_t))

/* One serialized owner. init runs once. open borrows unmodified input through
 * close, and every returned view expires at close. At most 512 values and 1024
 * value/key/string/number work units are admitted. No heap, copied document or
 * decoded text pool. Failed open releases its partial views; a second open
 * while a tree is live leaves that tree intact. See the area guide for grammar
 * limits.
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
/** @brief Decode a string including NUL into bounded caller storage.
 * Failure may leave a partial output; use it only after success. */
bool cfgjson_text (const cfgjson_value_t * p_value,
                   char *                  p_text,
                   size_t                  capacity);
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

