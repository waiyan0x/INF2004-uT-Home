/**
 * @file bmp388.c
 * @brief Adapt Member 2's Bosch wrapper to owned contexts and finite work.
 */
#include "bmp388.h"

#include <math.h>
#include <string.h>

static int begin (bmp388_t * p_sensor);
static int result_code (const bmp388_t * p_sensor, int8_t vendor_result);
static int configure (bmp388_t * p_sensor);
static int8_t record_error (bmp388_t * p_sensor, int result);
static int8_t read_register (uint8_t   address,
                             uint8_t * p_data,
                             uint32_t  length,
                             void *    p_context);
static int8_t write_register (uint8_t         address,
                              const uint8_t * p_data,
                              uint32_t        length,
                              void *          p_context);
static void delay (uint32_t duration_us, void * p_context);

/** @brief Claim and verify a BMP388 before changing any sensor registers. */
int bmp388_open (bmp388_t * p_sensor,
                 i2cbus_t * p_bus,
                 uint8_t    address,
                 uint32_t   timeout_ms)
{
    if ((NULL == p_sensor) || (NULL == p_bus) ||
        ((0x76U != address) && (0x77U != address)) || (timeout_ms < 10U) ||
        (timeout_ms > 100U))
    {
        return I2CBUS_INVALID;
    }
    if (p_sensor->b_claimed)
    {
        return I2CBUS_STATE;
    }
    const int claim = i2cbus_claim(p_bus, address);
    if (0 != claim)
    {
        return claim;
    }
    memset(p_sensor, 0, sizeof(*p_sensor));
    p_sensor->p_bus      = p_bus;
    p_sensor->address    = address;
    p_sensor->timeout_ms = timeout_ms;
    p_sensor->b_claimed  = true;
    int     result       = begin(p_sensor);
    uint8_t identity     = 0U;
    if (0 == result)
    {
        result = read_register(BMP3_REG_CHIP_ID, &identity, 1U, p_sensor);
        result = result_code(p_sensor, result);
    }
    if ((0 == result) && (0x50U != identity))
    {
        result = I2CBUS_UNSUPPORTED;
    }
    if (0 == result)
    {
        p_sensor->vendor.intf     = BMP3_I2C_INTF;
        p_sensor->vendor.intf_ptr = p_sensor;
        p_sensor->vendor.read     = read_register;
        p_sensor->vendor.write    = write_register;
        p_sensor->vendor.delay_us = delay;
        result = result_code(p_sensor, bmp3_init(&p_sensor->vendor));
    }
    if (0 == result)
    {
        result = configure(p_sensor);
    }
    p_sensor->b_ready = (0 == result);
    /* Even failed setup retains its claim until explicit, retryable close. */
    return result;
}

/** @brief Read fresh compensated data; failed reads leave zero measurements. */
int bmp388_read (bmp388_t * p_sensor, bmp388_value_t * p_value)
{
    if ((NULL == p_sensor) || (NULL == p_value))
    {
        return I2CBUS_INVALID;
    }
    *p_value = (bmp388_value_t){0};
    if (!p_sensor->b_ready)
    {
        return I2CBUS_STATE;
    }
    int     result = begin(p_sensor);
    uint8_t status = 0U;
    while ((0 == result) && (0x60U != (status & 0x60U)))
    {
        result = result_code(
            p_sensor,
            read_register(BMP3_REG_SENS_STATUS, &status, 1U, p_sensor));
        if ((0 == result) && (0x60U != (status & 0x60U)))
        {
            delay(1000U, p_sensor);
            result = p_sensor->bus_error;
        }
    }
    struct bmp3_data data = {0};
    if (0 == result)
    {
        result = result_code(
            p_sensor,
            bmp3_get_sensor_data(BMP3_PRESS_TEMP, &data, &p_sensor->vendor));
    }
    if ((0 == result) &&
        ((!isfinite(data.pressure)) || (!isfinite(data.temperature)) ||
         (data.pressure < 30000.0) || (data.pressure > 125000.0) ||
         (data.temperature < -40.0) || (data.temperature > 85.0)))
    {
        result = I2CBUS_INVALID;
    }
    if (0 == result)
    {
        /* Validated physical ranges are finite and representable as float. */
        p_value->pressure_pa   = (float)data.pressure;
        p_value->temperature_c = (float)data.temperature;
    }
    return result;
}

/** @brief Put a touched sensor to sleep before releasing its address claim. */
int bmp388_close (bmp388_t * p_sensor)
{
    if (NULL == p_sensor)
    {
        return I2CBUS_INVALID;
    }
    if (!p_sensor->b_claimed)
    {
        return 0;
    }
    int result = 0;
    if (p_sensor->b_touched)
    {
        result              = begin(p_sensor);
        const uint8_t sleep = 0U;
        if (0 == result)
        {
            result = result_code(
                p_sensor,
                write_register(BMP3_REG_PWR_CTRL, &sleep, 1U, p_sensor));
        }
    }
    if (0 == result)
    {
        result = i2cbus_release(p_sensor->p_bus, p_sensor->address);
    }
    if (0 == result)
    {
        memset(p_sensor, 0, sizeof(*p_sensor));
    }
    return result;
}

/** @brief Share one clock budget across vendor reads, writes and delays. */
static int begin (bmp388_t * p_sensor)
{
    p_sensor->bus_error = i2cbus_deadline(
        p_sensor->p_bus, p_sensor->timeout_ms, &p_sensor->deadline_ms);
    return p_sensor->bus_error;
}

/** @brief Preserve transport errors and reject vendor compensation warnings. */
static int result_code (const bmp388_t * p_sensor, int8_t vendor_result)
{
    if (0 != p_sensor->bus_error)
    {
        return p_sensor->bus_error;
    }
    uint64_t now = 0U;
    if (0 != i2cbus_now(p_sensor->p_bus, &now))
    {
        return I2CBUS_IO;
    }
    if (now >= p_sensor->deadline_ms)
    {
        return I2CBUS_TIMEOUT;
    }
    return (BMP3_OK == vendor_result) ? 0 : I2CBUS_INVALID;
}

/** @brief Select Member 2's pressure/temperature sampling policy. */
static int configure (bmp388_t * p_sensor)
{
    p_sensor->settings.press_en              = BMP3_ENABLE;
    p_sensor->settings.temp_en               = BMP3_ENABLE;
    p_sensor->settings.odr_filter.press_os   = BMP3_OVERSAMPLING_4X;
    p_sensor->settings.odr_filter.temp_os    = BMP3_OVERSAMPLING_2X;
    p_sensor->settings.odr_filter.iir_filter = BMP3_IIR_FILTER_COEFF_3;
    p_sensor->settings.odr_filter.odr        = BMP3_ODR_25_HZ;
    const uint32_t selected = BMP3_SEL_PRESS_EN | BMP3_SEL_TEMP_EN |
                              BMP3_SEL_PRESS_OS | BMP3_SEL_TEMP_OS |
                              BMP3_SEL_IIR_FILTER | BMP3_SEL_ODR;
    int result =
        result_code(p_sensor,
                    bmp3_set_sensor_settings(
                        selected, &p_sensor->settings, &p_sensor->vendor));
    if (0 == result)
    {
        p_sensor->settings.op_mode = BMP3_MODE_NORMAL;
        result                     = result_code(
            p_sensor, bmp3_set_op_mode(&p_sensor->settings, &p_sensor->vendor));
    }
    return result;
}

/** @brief Retain the first error even when the vendor delay cannot return it.
 */
static int8_t record_error (bmp388_t * p_sensor, int result)
{
    if ((0 == p_sensor->bus_error) && (0 != result))
    {
        p_sensor->bus_error = result;
    }
    return (0 == p_sensor->bus_error) ? BMP3_INTF_RET_SUCCESS : -1;
}

/** @brief Perform one bounded repeated-START register read. */
static int8_t read_register (uint8_t   address,
                             uint8_t * p_data,
                             uint32_t  length,
                             void *    p_context)
{
    bmp388_t * p_sensor = p_context;
    if ((0U == length) || (length > UTHOME_I2C_RX_MAX))
    {
        return record_error(p_sensor, I2CBUS_INVALID);
    }
    if (0 != p_sensor->bus_error)
    {
        return -1;
    }
    /* Length is at most 32, representable by size_t on both supported ABIs. */
    const i2cbus_request_t request = {.address     = p_sensor->address,
                                      .p_write     = &address,
                                      .write_size  = 1U,
                                      .p_read      = p_data,
                                      .read_size   = (size_t)length,
                                      .deadline_ms = p_sensor->deadline_ms};
    return record_error(p_sensor, i2cbus_transfer(p_sensor->p_bus, &request));
}

/** @brief Write explicit register/value pairs without the vendor spare byte. */
static int8_t write_register (uint8_t         address,
                              const uint8_t * p_data,
                              uint32_t        length,
                              void *          p_context)
{
    bmp388_t * p_sensor = p_context;
    if ((NULL == p_data) || (0U == length) || (length > 32U))
    {
        return record_error(p_sensor, I2CBUS_INVALID);
    }
    uint8_t pair[2] = {address, p_data[0]};
    for (uint32_t index = 0U; (0 == p_sensor->bus_error) && (index < length);
         index += 2U)
    {
        if (0U != index)
        {
            pair[0] = p_data[index - 1U];
            pair[1] = p_data[index];
        }
        const i2cbus_request_t request = {.address     = p_sensor->address,
                                          .p_write     = pair,
                                          .write_size  = sizeof(pair),
                                          .deadline_ms = p_sensor->deadline_ms};
        p_sensor->b_touched            = true;
        const int result = i2cbus_transfer(p_sensor->p_bus, &request);
        if (0 != record_error(p_sensor, result))
        {
            break;
        }
        /* v2.0.6 fills 2*N-1 bytes but reports 2*N for multiple registers.
         * The loop consumes only complete pairs, never the final spare byte. */
    }
    return (0 == p_sensor->bus_error) ? BMP3_INTF_RET_SUCCESS : -1;
}

/** @brief Stop further I/O if a required delay exceeds the operation budget. */
static void delay (uint32_t duration_us, void * p_context)
{
    bmp388_t * p_sensor = p_context;
    if (0 == p_sensor->bus_error)
    {
        p_sensor->bus_error =
            i2cbus_delay(p_sensor->p_bus, duration_us, p_sensor->deadline_ms);
    }
}

/* End of file. */

