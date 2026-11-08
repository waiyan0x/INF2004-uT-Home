/**
 * @file c3start.c
 * @brief Trace native startup only in the isolated Wi-Fi diagnostic.
 */
#include "c3start.h"

#include "esp_rom_sys.h"

/** @brief Identify whether device-manager initialization returns an error. */
ER c3start_devmgr (void)
{
    esp_rom_printf("[static-start] phase=device-manager begin\n");
    const ER result = c3start_native_devmgr();
    esp_rom_printf("[static-start] phase=device-manager result=%d\n",
                   (int)result);
    return result;
}

/** @brief Expose the exact CreateMLock result without bypassing its work. */
ER c3start_mlock (FastMLock * p_lock, const UB * p_name)
{
    const ER result = c3start_native_mlock(p_lock, p_name);
    esp_rom_printf(
        "[static-start] phase=device-manager-lock result=%d flag=%d\n",
        (int)result,
        (int)p_lock->id);
    return result;
}

/** @brief Identify whether platform device startup returns an error. */
ER c3start_device (void)
{
    esp_rom_printf("[static-start] phase=device-start begin\n");
    const ER result = c3start_native_device();
    esp_rom_printf("[static-start] phase=device-start result=%d\n",
                   (int)result);
    return result;
}

/** @brief Expose the exact native serial-device initialization result. */
ER c3start_serial (UW unit)
{
    const ER result = c3start_native_serial(unit);
    esp_rom_printf("[static-start] phase=device-serial result=%d\n",
                   (int)result);
    return result;
}

/** @brief Expose the exact native ADC-device initialization result. */
ER c3start_adc (UW unit)
{
    const ER result = c3start_native_adc(unit);
    esp_rom_printf("[static-start] phase=device-adc result=%d\n", (int)result);
    return result;
}

/* End of file. */

