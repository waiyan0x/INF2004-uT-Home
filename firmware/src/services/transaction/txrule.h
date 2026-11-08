/**
 * @file txrule.h
 * @brief Caller-owned runtime register-read rules; one serialized owner.
 */
#ifndef UTHOME_TXRULE_H
#define UTHOME_TXRULE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TXRULE_NAME_SIZE 32U
#define TXRULE_DATA_SIZE 16U

typedef enum
{
    TXRULE_OK = 0,
    TXRULE_INVALID,
    TXRULE_UNCONFIGURED,
    TXRULE_MISMATCH,
    TXRULE_TIMEOUT,
    TXRULE_IO
} txrule_status_t;

typedef struct
{
    uint32_t version;
    uint32_t revision;
    char     device[TXRULE_NAME_SIZE];
    uint32_t address;
    uint32_t reg;
    uint32_t read_size;
} txrule_config_t;

typedef struct
{
    txrule_config_t active;
    bool            b_loaded;
} txrule_t;

typedef struct
{
    txrule_config_t rule;
    uint8_t         data[TXRULE_DATA_SIZE];
    uint8_t         size;
    txrule_status_t status;
} txrule_result_t;

/* A synchronous complete read, returning OK, TIMEOUT or IO. The callback
 * borrows all pointers only for this call. It must enforce its own timeout. */
typedef txrule_status_t txrule_read_t (void *                  p_context,
                                       const txrule_config_t * p_rule,
                                       uint8_t *               p_data);

#ifdef __cplusplus
extern "C"
{
#endif

    void txrule_init (txrule_t * p_state);
    txrule_status_t txrule_load (txrule_t *              p_state,
                                 const txrule_config_t * p_candidate);
    /* The caller supplies disjoint state, request, result and callback storage.
     * Expected revision/device prevent execution against an unexpected rule.
     * Results are cleared every time; zero bytes are valid successful data. */
    void txrule_execute (const txrule_t *  p_state,
                         const char *      p_device,
                         uint32_t          revision,
                         txrule_read_t *   p_read,
                         void *            p_context,
                         txrule_result_t * p_result);
#ifdef __cplusplus
}
#endif

#endif

/* End of file. */

