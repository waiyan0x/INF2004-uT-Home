/**
 * @file cfgpump.h
 * @brief Bounded, single-owner transport for the configuration controller.
 */
#ifndef UTHOME_CFGPUMP_H
#define UTHOME_CFGPUMP_H
#include "cfgctrl.h"
#define UTHOME_CFGPUMP_CHUNK 64U
typedef int cfgpump_read_t (void *    p_context,
                            uint8_t * p_data,
                            size_t    capacity,
                            size_t *  p_count);
typedef int cfgpump_write_t (void *          p_context,
                             const uint8_t * p_data,
                             size_t          size,
                             size_t *        p_count);

typedef int cfgpump_observe_t (void *    p_context,
                               uint8_t * p_data,
                               size_t    capacity,
                               size_t *  p_size);

typedef int cfgpump_discover_t (void *                    p_context,
                                const cfgwire_request_t * p_request,
                                uint8_t *                 p_data,
                                size_t                    capacity,
                                size_t *                  p_size);

typedef struct
{
    cfgpump_read_t *  p_read;
    cfgpump_write_t * p_write;
} cfgpump_ops_t;

typedef struct
{
    cfgwire_t             receiver;
    cfgctrl_t *           p_control;
    const cfgpump_ops_t * p_operations;
    void *                p_context;
    uint8_t               received[UTHOME_CFGPUMP_CHUNK];
    uint8_t               reply[UTHOME_CFGWIRE_HEADER + UTHOME_CFGWIRE_PAYLOAD];
    size_t                reply_size;
    cfgpump_observe_t *   p_observe;
    void *                p_observer;
    cfgpump_observe_t *   p_capabilities;
    void *                p_capability_context;
    cfgpump_discover_t *  p_discover;
    void *                p_discovery_context;
    cfgpump_observe_t *   p_workcost;
    void *                p_workcost_context;
    size_t                received_size;
    size_t                received_used;
    size_t                sent;
    uint64_t              reply_start_ms;
    bool                  b_pending;
    bool                  b_busy;
} cfgpump_t;

/* Zero-initialize static storage. Dependencies remain borrowed for its
 * lifetime. Callbacks never block: return zero and a count in [0,capacity], or
 * an error. The owner calls step with monotonic time even while disconnected.
 * Each step receives <=64 bytes, handles <=1 request and makes <=1 transmit
 * attempt. Controller activation/flash operations remain synchronous and can
 * take longer. A reply gets 2000 ms from the NEXT step, so controller work is
 * not charged to transmission. Partial RX and TX are retained. TX expiry/error
 * discards only transport state, never controller state; retry the exact last
 * request to get its cached result. Return 0=progress/idle, -1=I/O/expiry,
 * -2=invalid/reentry. No callback may reenter this pump. No heap, task,
 * hardware or lock is owned.
 */
int cfgpump_init (cfgpump_t *           p_pump,
                  cfgctrl_t *           p_control,
                  const cfgpump_ops_t * p_operations,
                  void *                p_context);
int cfgpump_step (cfgpump_t * p_pump, uint64_t now_ms);
/* Optional owner-only observer. Bind while idle. Callback produces a bounded
 * payload; it must not mutate configuration or reenter this pump. Each transmit
 * attempt remains <=48 bytes, including variable-length observation replies. */
int cfgpump_observe (cfgpump_t *         p_pump,
                     cfgpump_observe_t * p_observe,
                     void *              p_context);
/* Bind the optional compiled-capability observer while the pump is idle. */
int cfgpump_capabilities (cfgpump_t *         p_pump,
                          cfgpump_observe_t * p_observe,
                          void *              p_context);
/* Discovery is optional, explicitly requested and bounded by its callback.
 * It never consumes controller IDs or implicitly stops/activates devices. */
int cfgpump_discovery (cfgpump_t *          p_pump,
                       cfgpump_discover_t * p_discover,
                       void *               p_context);
/* Independent read-only workload/stack evidence; no mutation retry eviction. */
int cfgpump_workcost (cfgpump_t *         p_pump,
                      cfgpump_observe_t * p_observe,
                      void *              p_context);
#endif
/* End of file. */

