/**
 * @file cfg_main.c
 * @brief Inject interrupted records and verify request retries without
 * hardware.
 */
#ifdef NDEBUG
#error "Configuration tests require assertions."
#endif
#include "cfg_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cfgctrl.h"
#include "cfgpump.h"
static uint8_t g_slots[2][UTHOME_CFGSTORE_MIN_SLOT] = {0};

static uint8_t g_backup[2][UTHOME_CFGSTORE_MIN_SLOT] = {0};

static uint8_t g_loaded[UTHOME_CONFIG_BYTES] = {0};

static cfgwire_t g_wire = {0};

static cfgctrl_t g_control = {0};

static unsigned g_calls = 0U;

static unsigned g_fail_at = 0U;

static unsigned g_validations = 0U;

static unsigned g_activations = 0U;

static unsigned g_recoveries = 0U;

static unsigned g_writes = 0U;

static size_t g_torn = 0U;

static int g_validation_error = 0;

static int g_activation_error = 0;

static bool gb_fail_save = false;

static bool gb_reenter = false;

static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size);
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size);
static int erase_slot (void * p_context, unsigned slot);
static int sync_slots (void * p_context);
static int validate (void * p_context, const uint8_t * p_data, size_t size);
static int activate (void * p_context);
static int recover (void * p_context);
static void reset (void);
static void verify_store (void);
static void verify_faults (void);
static void verify_control (void);
static void verify_wire (void);
static void verify_pump (void);
static void verify_pump_expiry (void);
static void verify_observation (cfgwire_command_t command);
static int
observe (void * p_context, uint8_t * p_data, size_t capacity, size_t * p_size);
static int pump_read (void *    p_context,
                      uint8_t * p_data,
                      size_t    capacity,
                      size_t *  p_count);
static int pump_write (void *          p_context,
                       const uint8_t * p_data,
                       size_t          size,
                       size_t *        p_count);
static cfgpump_t           g_pump         = {0};
static uint8_t             g_input[128]   = {0};
static uint8_t             g_output[4096] = {0};
static size_t              g_input_size   = 0U;
static size_t              g_input_used   = 0U;
static size_t              g_output_used  = 0U;
static size_t              g_read_limit   = 64U;
static size_t              g_write_limit  = 7U;
static bool                gb_read_error  = false;
static bool                gb_bad_count   = false;
static const cfgpump_ops_t g_pump_ops     = {.p_read  = pump_read,
                                             .p_write = pump_write};
static uint32_t request (cfgwire_command_t command,
                         uint32_t          request_id,
                         uint32_t          revision,
                         const char *      p_text);
static void header (uint8_t * p_header, const uint8_t * p_data, size_t size);
static int consume_file (const char * p_path);
static const cfgstore_ops_t g_storage_ops = {.p_read  = read_slot,
                                             .p_write = write_slot,
                                             .p_erase = erase_slot,
                                             .p_sync  = sync_slots};

static cfgstore_t g_store = {.p_operations = &g_storage_ops,
                             .p_context    = g_slots,
                             .slot_bytes   = UTHOME_CFGSTORE_MIN_SLOT};

static const cfgctrl_ops_t g_control_ops = {
    .p_validate = validate, .p_activate = activate, .p_recover = recover};

/** @brief The optional binary fixture checks independently encoded Python
 * frames. */
int main (int argc, char ** pp_argv)
{
    if (2 == argc)
    {
        return consume_file(pp_argv[1]);
    }
    assert(1 == argc);
    verify_wire();
    verify_store();
    verify_faults();
    verify_control();
    verify_pump();
    verify_pump_expiry();
    verify_observation(CFGWIRE_TELEMETRY);
    verify_observation(CFGWIRE_CAPABILITIES);
    printf("Config framing, torn-save recovery and request retries passed. "
           "contexts=%zu,%zu,%zu\n",
           sizeof(cfgwire_t),
           sizeof(cfgstore_t),
           sizeof(cfgctrl_t));
    return 0;
}

/** @brief Reset RAM and the fake persistent medium independently of tested
 * code. */
static void reset (void)
{
    memset(g_slots, 0xFF, sizeof(g_slots));
    g_store.b_busy = false;
    g_calls        = 0U;
    g_fail_at      = 0U;
    g_writes       = 0U;
    g_torn         = 0U;
}

/** @brief Read exactly one bounded region, or report an observable I/O failure.
 */
static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size)
{
    assert(g_slots == p_context);
    assert((slot < 2U) && (offset <= sizeof(g_slots[0])) &&
           (size <= (sizeof(g_slots[0]) - offset)));
    ++g_calls;
    if (g_calls == g_fail_at)
    {
        return -1;
    }
    memcpy(p_data, &g_slots[slot][offset], size);
    return 0;
}

/** @brief Simulate a NOR-style write that can fail after any byte prefix. */
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size)
{
    assert(g_slots == p_context);
    assert((slot < 2U) && (offset <= sizeof(g_slots[0])) &&
           (size <= (sizeof(g_slots[0]) - offset)));
    ++g_calls;
    ++g_writes;
    const bool   b_failed = (g_calls == g_fail_at);
    const size_t count    = (b_failed && (g_torn < size)) ? g_torn : size;
    for (size_t index = 0U; index < count; ++index)
    {
        g_slots[slot][offset + index] &= p_data[index];
    }
    return b_failed ? -1 : 0;
}

/** @brief Erasing one bank must never damage the bank containing current data.
 */
static int erase_slot (void * p_context, unsigned slot)
{
    assert((g_slots == p_context) && (slot < 2U));
    ++g_calls;
    ++g_writes;
    const bool   b_failed = (g_calls == g_fail_at);
    const size_t count    = b_failed ? g_torn : sizeof(g_slots[0]);
    assert(count <= sizeof(g_slots[0]));
    memset(g_slots[slot], 0xFF, count);
    return b_failed ? -1 : 0;
}

/** @brief Fake writes are immediate; sync still injects lost acknowledgement.
 */
static int sync_slots (void * p_context)
{
    assert(g_slots == p_context);
    ++g_calls;
    return (g_calls == g_fail_at) ? -1 : 0;
}

/** @brief Data integrity and idempotency hold at zero/max sizes and revisions.
 */
static void verify_store (void)
{
    reset();
    cfgstore_record_t record = {0};
    assert(CFGSTORE_EMPTY ==
           cfgstore_load(&g_store, g_loaded, sizeof(g_loaded), &record));
    assert(0U == record.revision);
    const uint8_t first[] = {1U, 2U, 3U};
    assert(CFGSTORE_INVALID == cfgstore_save(&g_store, first, 0U, 1U));
    assert(CFGSTORE_INVALID == cfgstore_save(&g_store, first, 3U, 0U));
    assert(CFGSTORE_OK == cfgstore_save(&g_store, first, 3U, 1U));
    const unsigned writes = g_writes;
    assert(CFGSTORE_OK == cfgstore_save(&g_store, first, 3U, 1U));
    assert(writes == g_writes);
    const uint8_t different[] = {1U, 2U, 4U};
    assert(CFGSTORE_CONFLICT == cfgstore_save(&g_store, different, 3U, 1U));
    assert(writes == g_writes);
    assert(CFGSTORE_INVALID == cfgstore_load(&g_store, g_loaded, 2U, &record));
    assert(CFGSTORE_OK == cfgstore_save(&g_store, different, 3U, 2U));
    g_slots[1][UTHOME_CFGSTORE_HEADER] ^= 1U;
    assert(CFGSTORE_OK ==
           cfgstore_load(&g_store, g_loaded, sizeof(g_loaded), &record));
    assert((1U == record.revision) && (0 == memcmp(first, g_loaded, 3U)));
    memset(g_loaded, 0xA5, sizeof(g_loaded));
    assert(CFGSTORE_OK ==
           cfgstore_save(&g_store, g_loaded, sizeof(g_loaded), UINT32_MAX));
    memset(g_loaded, 0, sizeof(g_loaded));
    assert(CFGSTORE_OK ==
           cfgstore_load(&g_store, g_loaded, sizeof(g_loaded), &record));
    assert((UINT32_MAX == record.revision) && (8192U == record.size));
    assert((0xA5U == g_loaded[0]) && (0xA5U == g_loaded[8191]));
    assert(CFGSTORE_CONFLICT == cfgstore_save(&g_store, first, 3U, 1U));
}

/** @brief Reboot after every failing operation and every possible write prefix.
 */
static void verify_faults (void)
{
    reset();
    const uint8_t first[]  = "old profile";
    const uint8_t second[] = "current profile";
    const uint8_t third[]  = "replacement profile";
    assert(CFGSTORE_OK == cfgstore_save(&g_store, first, sizeof(first), 1U));
    assert(CFGSTORE_OK == cfgstore_save(&g_store, second, sizeof(second), 2U));
    memcpy(g_backup, g_slots, sizeof(g_backup));
    g_calls = 0U;
    assert(CFGSTORE_OK == cfgstore_save(&g_store, third, sizeof(third), 3U));
    const unsigned calls = g_calls;
    for (unsigned failure = 1U; failure <= calls; ++failure)
    {
        for (size_t prefix = 0U; prefix <= 32U; ++prefix)
        {
            memcpy(g_slots, g_backup, sizeof(g_slots));
            g_calls   = 0U;
            g_fail_at = failure;
            g_torn    = prefix;
            assert(CFGSTORE_IO ==
                   cfgstore_save(&g_store, third, sizeof(third), 3U));
            assert(0 == memcmp(g_slots[1], g_backup[1], sizeof(g_slots[1])));
            g_fail_at                = 0U;
            cfgstore_record_t record = {0};
            assert(
                CFGSTORE_OK ==
                cfgstore_load(&g_store, g_loaded, sizeof(g_loaded), &record));
            assert((2U == record.revision) || (3U == record.revision));
            if (2U == record.revision)
            {
                assert((sizeof(second) == record.size) &&
                       (0 == memcmp(g_loaded, second, record.size)));
            }
            else
            {
                assert((sizeof(third) == record.size) &&
                       (0 == memcmp(g_loaded, third, record.size)));
            }
        }
    }
}

/** @brief Test hook deliberately is not a node-schema or target-admission
 * implementation. */
static int validate (void * p_context, const uint8_t * p_data, size_t size)
{
    assert((p_context == &g_control) && (NULL != p_data) && (size > 0U));
    ++g_validations;
    if (gb_reenter)
    {
        const cfgwire_request_t query    = {.request_id = 99U,
                                            .command    = CFGWIRE_QUERY};
        uint32_t                reply[6] = {0};
        assert(-1 == cfgctrl_handle(&g_control, &query, reply));
    }
    return g_validation_error;
}

/** @brief Save faults begin only after a successfully validated activation. */
static int activate (void * p_context)
{
    assert(p_context == &g_control);
    ++g_activations;
    if (gb_fail_save)
    {
        g_fail_at = g_calls + 1U;
    }
    return g_activation_error;
}

/** @brief Test recovery releases every simulated resource, leaving empty state.
 */
static int recover (void * p_context)
{
    assert(p_context == &g_control);
    ++g_recoveries;
    return 0;
}

/** @brief Dispatch a borrowed string and assert that output describes actual
 * state. */
static uint32_t request (cfgwire_command_t command,
                         uint32_t          request_id,
                         uint32_t          revision,
                         const char *      p_text)
{
    const cfgwire_request_t message = {
        .command    = command,
        .request_id = request_id,
        .revision   = revision,
        .size       = (NULL == p_text) ? 0U : strlen(p_text),
        /* Object representation of this live string is readable as bytes. */
        .p_data = (const uint8_t *)p_text};
    uint32_t reply[6] = {0};
    assert(0 == cfgctrl_handle(&g_control, &message, reply));
    return reply[0];
}

/** @brief Lost replies, stale IDs, save faults and activation recovery stay
 * distinct. */
static void verify_control (void)
{
    reset();
    g_control = (cfgctrl_t){0};
    assert(-1 == cfgctrl_init(&g_control, &g_store, NULL, NULL));
    assert(0 == cfgctrl_init(&g_control, &g_store, &g_control_ops, &g_control));
    assert(CFGCTRL_EMPTY == cfgctrl_restore(&g_control));
    gb_reenter = true;
    assert(CFGCTRL_SAVED == request(CFGWIRE_APPLY, 1U, 1U, "{\"node\":1}"));
    assert((1U == g_validations) && (1U == g_activations));
    assert(CFGCTRL_SAVED == request(CFGWIRE_APPLY, 1U, 1U, "{\"node\":1}"));
    assert((1U == g_validations) && (1U == g_activations));
    assert(CFGCTRL_CONFLICT == request(CFGWIRE_APPLY, 1U, 1U, "{\"node\":2}"));
    assert(CFGCTRL_OK == request(CFGWIRE_QUERY, 2U, 0U, NULL));
    assert(CFGCTRL_STALE == request(CFGWIRE_APPLY, 1U, 1U, "{\"node\":1}"));
    assert(CFGCTRL_SAVED == request(CFGWIRE_APPLY, 3U, 1U, "{\"node\":1}"));
    assert(1U == g_activations);
    g_validation_error    = -1;
    const unsigned writes = g_writes;
    assert(CFGCTRL_REJECTED == request(CFGWIRE_APPLY, 4U, 2U, "rejected"));
    assert((1U == g_activations) && (writes == g_writes));
    assert((1U == g_control.active_revision) &&
           (1U == g_control.saved_revision));
    g_validation_error = 0;
    gb_fail_save       = true;
    assert(CFGCTRL_UNSAVED == request(CFGWIRE_APPLY, 5U, 2U, "new profile"));
    assert((2U == g_control.active_revision) && (!g_control.b_saved_known));
    g_fail_at                = 0U;
    gb_fail_save             = false;
    const unsigned activated = g_activations;
    assert(CFGCTRL_UNSAVED == request(CFGWIRE_APPLY, 5U, 2U, "new profile"));
    assert(activated == g_activations);
    assert(CFGCTRL_OK == request(CFGWIRE_QUERY, 6U, 0U, NULL));
    assert(1U == g_control.saved_revision);
    assert(CFGCTRL_SAVED == request(CFGWIRE_SAVE, 7U, 2U, NULL));
    assert((activated == g_activations) && (2U == g_control.saved_revision));
    g_activation_error = 1;
    assert(CFGCTRL_REJECTED ==
           request(CFGWIRE_APPLY, 8U, 3U, "bad activation"));
    assert(2U == g_control.active_revision);
    g_activation_error = -1;
    assert(CFGCTRL_RECOVERY == request(CFGWIRE_APPLY, 9U, 3U, "unknown state"));
    assert((0U == g_control.active_revision) &&
           (2U == g_control.saved_revision));
    assert(CFGCTRL_RECOVERY == request(CFGWIRE_APPLY, 10U, 4U, "blocked"));
    assert(CFGCTRL_EMPTY == request(CFGWIRE_RECOVER, 11U, 0U, NULL));
    assert(1U == g_recoveries);
    g_activation_error = 0;
    g_control          = (cfgctrl_t){0};
    assert(0 == cfgctrl_init(&g_control, &g_store, &g_control_ops, &g_control));
    const unsigned prior = g_activations;
    assert(CFGCTRL_SAVED == cfgctrl_restore(&g_control));
    assert((2U == g_control.active_revision) &&
           ((prior + 1U) == g_activations));
    assert(CFGCTRL_REJECTED == cfgctrl_restore(&g_control));
    assert(CFGCTRL_SAVED == request(CFGWIRE_APPLY, 1U, 2U, "new profile"));
    assert((prior + 1U) == g_activations);
}

/** @brief Construct a checksummed frame; Python fixtures provide independent
 * bytes. */
static void header (uint8_t * p_header, const uint8_t * p_data, size_t size)
{
    memset(p_header, 0, UTHOME_CFGWIRE_HEADER);
    memcpy(p_header, "UTH1", 4U);
    p_header[4] = 1U;
    p_header[5] = CFGWIRE_APPLY;
    cfgbytes_put(&p_header[8], 5U);
    cfgbytes_put(&p_header[12], 10U);
    /* All test callers bound size to 8192. */
    cfgbytes_put(&p_header[16], (uint32_t)size);
    const uint32_t crc = cfgbytes_crc(UINT32_MAX, p_header, 20U);
    cfgbytes_put(&p_header[20], ~cfgbytes_crc(crc, p_data, size));
}

/** @brief Whole-frame timeout, noise, maximum length and corruption are
 * bounded. */
static void verify_wire (void)
{
    const uint8_t check[] = "123456789";
    assert(UINT32_C(0xCBF43926) == (~cfgbytes_crc(UINT32_MAX, check, 9U)));
    uint8_t bytes[UTHOME_CFGWIRE_HEADER] = {0};
    header(bytes, check, 9U);
    cfgwire_reset(&g_wire);
    assert(0 == cfgwire_feed(&g_wire, 'x', 0U));
    assert(0 == cfgwire_feed(&g_wire, 'U', 0U));
    for (size_t index = 0U; index < sizeof(bytes); ++index)
    {
        assert(0 == cfgwire_feed(&g_wire, bytes[index], 0U));
    }
    for (size_t index = 0U; index < 9U; ++index)
    {
        assert(((8U == index) ? 1 : 0) ==
               cfgwire_feed(&g_wire, check[index], 1U));
    }
    assert(-2 == cfgwire_feed(&g_wire, 0U, 1U));
    cfgwire_request_t parsed = {0};
    assert(0 == cfgwire_request(&g_wire, &parsed));
    assert((5U == parsed.request_id) && (10U == parsed.revision) &&
           (9U == parsed.size));
    assert(0 == memcmp(parsed.p_data, check, 9U));
    cfgwire_reset(&g_wire);
    assert(0 == cfgwire_feed(&g_wire, 'U', 5U));
    assert(0 == cfgwire_feed(&g_wire, 'T', 1900U));
    assert(-1 == cfgwire_expire(&g_wire, 2005U));
    assert(0U == g_wire.header_used);
    assert(0 == cfgwire_feed(&g_wire, 'U', 50U));
    assert(-1 == cfgwire_expire(&g_wire, 49U));
    header(bytes, g_loaded, sizeof(g_loaded));
    for (size_t index = 0U; index < sizeof(bytes); ++index)
    {
        assert(0 == cfgwire_feed(&g_wire, bytes[index], 0U));
    }
    for (size_t index = 0U; index < sizeof(g_loaded); ++index)
    {
        assert(((8191U == index) ? 1 : 0) ==
               cfgwire_feed(&g_wire, g_loaded[index], 1U));
    }
    cfgwire_reset(&g_wire);
    cfgbytes_put(&bytes[16], 8193U);
    for (size_t index = 0U; index < sizeof(bytes); ++index)
    {
        assert(((23U == index) ? -1 : 0) ==
               cfgwire_feed(&g_wire, bytes[index], 0U));
    }
    header(bytes, check, 9U);
    bytes[20] ^= 1U;
    for (size_t index = 0U; index < sizeof(bytes); ++index)
    {
        assert(0 == cfgwire_feed(&g_wire, bytes[index], 0U));
    }
    for (size_t index = 0U; index < 9U; ++index)
    {
        assert(((8U == index) ? -1 : 0) ==
               cfgwire_feed(&g_wire, check[index], 0U));
    }
    assert(!g_wire.b_ready);
}

/** @brief A small stdin-free binary bridge lets Python cross-check the C codec.
 */
static int consume_file (const char * p_path)
{
    FILE * p_file = fopen(p_path, "rb");
    assert(NULL != p_file);
    int result = 0;
    int byte   = fgetc(p_file);
    while (EOF != byte)
    {
        /* fgetc returned a non-EOF unsigned byte promoted to int. */
        result = cfgwire_feed(&g_wire, (uint8_t)byte, 0U);
        if (result < 0)
        {
            assert(0 == fclose(p_file));
            return 2;
        }
        byte = fgetc(p_file);
    }
    assert(0 == fclose(p_file));
    if (1 != result)
    {
        return 2;
    }
    cfgwire_request_t parsed = {0};
    assert(0 == cfgwire_request(&g_wire, &parsed));
    const uint32_t fields[6] = {
        CFGCTRL_OK, 17U, 16U, 42U, 41U, UTHOME_CFG_RECEIVED};
    uint8_t reply[UTHOME_CFGWIRE_REPLY] = {0};
    assert(0 == cfgwire_reply(&parsed, fields, reply));
    for (size_t index = 0U; index < sizeof(reply); ++index)
    {
        printf("%02x", (unsigned)reply[index]);
    }
    puts("");
    return 0;
}

/** @brief Read arbitrary chunks and prove callbacks cannot reenter the pump. */
static int pump_read (void *    p_context,
                      uint8_t * p_data,
                      size_t    capacity,
                      size_t *  p_count)
{
    assert(NULL == p_context);
    assert(-2 == cfgpump_step(&g_pump, 0U));
    size_t count = g_input_size - g_input_used;
    count        = (count < capacity) ? count : capacity;
    count        = (count < g_read_limit) ? count : g_read_limit;
    memcpy(p_data, &g_input[g_input_used], count);
    g_input_used += count;
    *p_count = gb_bad_count ? capacity + 1U : count;
    return gb_read_error ? -1 : 0;
}

/** @brief Limit each write to simulate backpressure and preserve exact bytes.
 */
static int pump_write (void *          p_context,
                       const uint8_t * p_data,
                       size_t          size,
                       size_t *        p_count)
{
    assert(NULL == p_context);
    assert(-2 == cfgpump_step(&g_pump, 0U));
    assert(size <= 48U);
    const size_t count = (size < g_write_limit) ? size : g_write_limit;
    assert((g_output_used + count) <= sizeof(g_output));
    memcpy(&g_output[g_output_used], p_data, count);
    g_output_used += count;
    *p_count = gb_bad_count ? size + 1U : count;
    return 0;
}

/** @brief Coalesced requests and tiny fragments retain both data and replies.
 */
static void verify_pump (void)
{
    reset();
    g_control = (cfgctrl_t){0};
    assert(0 == cfgctrl_init(&g_control, &g_store, &g_control_ops, &g_control));
    assert(CFGCTRL_EMPTY == cfgctrl_restore(&g_control));
    assert(-2 == cfgpump_init(NULL, &g_control, &g_pump_ops, NULL));
    assert(0 == cfgpump_init(&g_pump, &g_control, &g_pump_ops, NULL));
    assert(-2 == cfgpump_init(&g_pump, &g_control, &g_pump_ops, NULL));
    const uint8_t payload[] = "{}";
    header(g_input, payload, 2U);
    memcpy(&g_input[24], payload, 2U);
    memcpy(&g_input[26], g_input, 26U);
    g_input_size             = 52U;
    const unsigned activated = g_activations;
    assert(0 == cfgpump_step(&g_pump, 0U));
    assert((52U == g_input_used) && (26U == g_pump.received_used));
    /* Slow controller work does not consume the transmission timeout. */
    for (unsigned step = 0U; step < 20U; ++step)
    {
        assert(0 == cfgpump_step(&g_pump, 5000U + step));
    }
    assert(96U == g_output_used);
    assert(0 == memcmp(g_output, &g_output[48], 48U));
    assert((activated + 1U) == g_activations);
    const unsigned calls = g_calls;
    assert(5U == request(CFGWIRE_PROBE, 1U, 0U, NULL));
    assert((5U == g_control.last_id) && (calls == g_calls));
    assert(CFGCTRL_SAVED == request(CFGWIRE_APPLY, 5U, 10U, "{}"));
    assert((activated + 1U) == g_activations);
    /* A fragmented identical request must still reproduce the same reply. */
    g_input_size  = 26U;
    g_input_used  = 0U;
    g_output_used = 0U;
    g_read_limit  = 1U;
    for (unsigned step = 0U; step < 40U; ++step)
    {
        assert(0 == cfgpump_step(&g_pump, 6000U + step));
    }
    assert((48U == g_output_used) && ((activated + 1U) == g_activations));
}

/** @brief Idle expiry, disconnect, dishonest counts and TX stalls recover. */
static void verify_pump_expiry (void)
{
    g_input[0]   = 'U';
    g_input_used = 0U;
    g_input_size = 1U;
    assert(0 == cfgpump_step(&g_pump, 7000U));
    assert(1U == g_pump.receiver.header_used);
    assert(0 == cfgpump_step(&g_pump, 9000U));
    assert(0U == g_pump.receiver.header_used);
    gb_read_error = true;
    assert(-1 == cfgpump_step(&g_pump, 9001U));
    gb_read_error = false;
    gb_bad_count  = true;
    assert(-1 == cfgpump_step(&g_pump, 9002U));
    gb_bad_count            = false;
    const uint8_t payload[] = "{}";
    header(g_input, payload, 2U);
    memcpy(&g_input[24], payload, 2U);
    g_input_size  = 26U;
    g_input_used  = 0U;
    g_output_used = 0U;
    g_read_limit  = 64U;
    g_write_limit = 0U;
    assert(0 == cfgpump_step(&g_pump, 10000U));
    assert(0 == cfgpump_step(&g_pump, 10001U));
    assert(0 == cfgpump_step(&g_pump, 12000U));
    assert(-1 == cfgpump_step(&g_pump, 12001U));
    assert((!g_pump.b_pending) && (10U == g_control.active_revision));
    /* Cached result remains usable after timeout; impossible TX count fails. */
    g_input_used = 0U;
    assert(0 == cfgpump_step(&g_pump, 12002U));
    gb_bad_count = true;
    assert(-1 == cfgpump_step(&g_pump, 12003U));
    gb_bad_count  = false;
    g_write_limit = 48U;
    g_input_used  = 0U;
    assert(0 == cfgpump_step(&g_pump, 12004U));
    assert(0 == cfgpump_step(&g_pump, 12005U));
    assert((48U == g_output_used) &&
           (CFGCTRL_SAVED == cfgbytes_get(&g_output[24])));
}

/** @brief Fill the maximum observation and reject callback reentry. */
static int
observe (void * p_context, uint8_t * p_data, size_t capacity, size_t * p_size)
{
    assert(p_context == &g_pump);
    assert(UTHOME_CFGWIRE_PAYLOAD == capacity);
    assert(-2 == cfgpump_step(&g_pump, 0U));
    memset(p_data, 0xA5, capacity);
    *p_size = capacity;
    return 0;
}

/** @brief Telemetry must preserve the previous mutation's exact cached result.
 */
static void verify_observation (cfgwire_command_t command)
{
    assert(0 == cfgpump_observe(&g_pump, observe, &g_pump));
    assert(0 == cfgpump_capabilities(&g_pump, observe, &g_pump));
    memset(g_input, 0, 24U);
    memcpy(g_input, "UTH1", 4U);
    g_input[4] = 1U;
    g_input[5] = (uint8_t)
        command; /* Two explicitly supplied read-only enum values fit. */
    cfgbytes_put(&g_input[8], UINT32_MAX);
    cfgbytes_put(&g_input[20], ~cfgbytes_crc(UINT32_MAX, g_input, 20U));
    g_input_used             = 0U;
    g_input_size             = 24U;
    g_output_used            = 0U;
    g_write_limit            = 7U;
    const unsigned calls     = g_calls;
    uint32_t       cached[6] = {0};
    memcpy(cached, g_control.reply, sizeof(cached));
    for (unsigned step = 0U; step < 310U; ++step)
    {
        assert(0 == cfgpump_step(&g_pump, 13000U + step));
    }
    assert(2072U == g_output_used);
    assert(2048U == cfgbytes_get(&g_output[16]));
    uint32_t sum = cfgbytes_crc(UINT32_MAX, g_output, 20U);
    sum          = cfgbytes_crc(sum, &g_output[24], 2048U);
    assert((~sum) == cfgbytes_get(&g_output[20]));
    assert((calls == g_calls) && (5U == g_control.last_id));
    assert(0 == memcmp(cached, g_control.reply, sizeof(cached)));
    assert(CFGCTRL_SAVED == request(CFGWIRE_APPLY, 5U, 10U, "{}"));
    g_input_used  = 0U;
    g_output_used = 0U;
    assert(0 == cfgpump_step(&g_pump, 14000U));
    assert(0 == cfgpump_step(&g_pump, 14001U));
    g_write_limit = 0U;
    assert(-1 == cfgpump_step(&g_pump, 16001U));
    assert((!g_pump.b_pending) && (7U == g_output_used));
    assert(0 == memcmp(cached, g_control.reply, sizeof(cached)));
}

/* End of file. */

