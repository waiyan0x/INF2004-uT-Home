/**
 * @file c3aes.h
 * @brief Fixed AES cipher contexts for the Wi-Fi transaction image.
 */
#ifndef UTHOME_C3AES_H
#define UTHOME_C3AES_H

/** @brief Reserve one context for initialization by the cipher library. */
void * c3aes_take (void);

/** @brief Release a context after the library has erased its key. */
void c3aes_drop (void * p_context);

#endif

/* End of file. */

