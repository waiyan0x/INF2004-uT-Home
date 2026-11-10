/**
 * @file c3rule.h
 * @brief Isolated USB transaction-rule diagnostic entry point.
 */
#ifndef UTHOME_C3RULE_H
#define UTHOME_C3RULE_H

/* Initial application task only; replaces normal configuration/peripherals.
 * Fixed reviewed wiring: I2C0 SDA0/SCL1, 100 kHz. No persistence or networking.
 */
int c3rule_run (void);

#endif
/* End of file. */

