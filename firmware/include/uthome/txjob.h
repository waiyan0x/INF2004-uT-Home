/**
 * @file txjob.h
 * @brief Fixed-storage runtime transaction decisions for one application owner.
 */
#ifndef UTHOME_TXJOB_H
#define UTHOME_TXJOB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TXJOB_WIRE 96U
#define TXJOB_RX   32U
#define TXJOB_TX   30U

typedef enum
{
    TXJOB_OK = 0,
    TXJOB_INVALID,
    TXJOB_UNCONFIGURED,
    TXJOB_MISMATCH,
    TXJOB_TIMEOUT,
    TXJOB_IO,
    TXJOB_BUSY,
    TXJOB_UNSUPPORTED,
    TXJOB_STALE,
    TXJOB_CANCELLED,
    TXJOB_PENDING
} txjob_status_t;

typedef struct
{
    char     device[16];
    uint32_t revision;
    uint32_t rate;
    uint16_t timeout_ms;
    uint8_t  protocol;
    uint8_t  address;
    uint8_t  mode;
    uint8_t  dc_pin;
    uint8_t  cs_pin;
    uint8_t  dc_level;
    uint8_t  write_size;
    uint8_t  read_size;
    uint8_t  write[TXJOB_TX];
} txjob_rule_t;

typedef txjob_status_t
txjob_begin_t (void * p_context, const txjob_rule_t * p_rule, uint64_t now_ms);
typedef txjob_status_t txjob_poll_t (void *    p_context,
                                     uint64_t  now_ms,
                                     uint8_t * p_written,
                                     uint8_t * p_data,
                                     uint8_t * p_size);
typedef int txjob_close_t (void * p_context);

typedef struct
{
    txjob_begin_t * p_begin;
    txjob_poll_t *  p_poll;
    txjob_close_t * p_close;
    void *          p_context;
} txjob_ops_t;

typedef struct
{
    txjob_rule_t   rule;
    txjob_ops_t    operations;
    uint8_t        reply[TXJOB_WIRE];
    uint8_t        request[TXJOB_WIRE];
    uint8_t        cached[TXJOB_WIRE];
    uint8_t        data[TXJOB_RX];
    uint64_t       boot_id;
    uint64_t       deadline_ms;
    uint64_t       last_ms;
    uint32_t       generation;
    uint32_t       last_id;
    uint8_t        written;
    uint8_t        received;
    txjob_status_t status;
    bool           b_loaded;
    bool           b_busy;
    bool           b_closing;
    bool           b_reply;
} txjob_t;

/* Zeroed lifetime storage, disjoint buffers, serialized application owner only.
 * Operations borrow the copied rule until checked close succeeds. No allocator,
 * task or kernel object is created. A cleanup failure retains the busy lease.
 */
int txjob_init (txjob_t *           p_job,
                const txjob_ops_t * p_operations,
                uint64_t            boot_id);
/* Read-only status uses a disjoint TXJOB_WIRE-byte caller buffer, including
 * while I/O or checked cleanup is pending. It never consumes a command ID. */
int txjob_query (const txjob_t * p_job,
                 const uint8_t * p_request,
                 size_t          size,
                 uint32_t        generation,
                 bool            b_idle,
                 uint64_t        now_ms,
                 uint8_t *       p_reply);
int txjob_submit (txjob_t *       p_job,
                  const uint8_t * p_request,
                  size_t          size,
                  uint32_t        generation,
                  bool            b_idle,
                  uint64_t        now_ms);
void txjob_step (txjob_t * p_job, uint64_t now_ms);
void txjob_cancel (txjob_t * p_job);
/* A reply is copied once; the cache remains for exact retry. */
int txjob_take (txjob_t * p_job, uint8_t * p_reply, size_t capacity);
#endif

/* End of file. */

