/**
 * @file c3pin.h
 * @brief Share pin ownership across C3 buses and simple digital outputs.
 */
#ifndef UTHOME_C3PIN_H
#define UTHOME_C3PIN_H
#include "digiout.h"
#include "digiin.h"
#include <stdint.h>

typedef struct
{
    uint8_t pin;
    bool    b_open;
} c3pin_output_t;

typedef struct
{
    uint8_t pin;
    bool    b_open;
} c3pin_input_t;

int c3pin_input_open (c3pin_input_t * p_input, uint8_t pin);
int c3pin_get (void * p_context, bool * pb_high);
int c3pin_input_close (c3pin_input_t * p_input);

/* Single lifecycle owner; affected users must be stopped before releasing.
 * Available pins exclude flash, USB, straps and the kernel's active UART. */
bool c3pin_valid (uint8_t pin);
int c3pin_reserve (uint32_t mask, const void * p_owner);
int c3pin_release (uint32_t mask, const void * p_owner);
int c3pin_open (c3pin_output_t * p_output, uint8_t pin, bool b_initial);
int c3pin_set (void * p_context, bool b_high);
int c3pin_close (c3pin_output_t * p_output);
#endif
/* End of file. */

