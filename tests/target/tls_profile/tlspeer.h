/**
 * @file tlspeer.h
 * @brief Serialized host-only TLS peer for record compatibility experiments.
 */
#ifndef UTHOME_TLSPEER_H
#define UTHOME_TLSPEER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Start one TLS 1.2 client; verify the CA and expected DNS name.
 * Path/name are borrowed only during this call. Call close after any outcome.
 * One owner serializes every API. This fixture uses the SDK's host heap.
 */
int tlspeer_start (const char * p_ca, const char * p_name);

/** @brief Free the peer and clear the host-only ciphertext transport queues. */
void tlspeer_close (void);

/** @brief Run a real SDK handshake step, returning its original status. */
int tlspeer_handshake (void);

/** @brief Read decrypted bytes; the caller owns the output buffer. */
int tlspeer_read (unsigned char * p_data, size_t capacity);

/** @brief Write plaintext; retry WANT_READ/WRITE with identical buffer/length.
 */
int tlspeer_write (const unsigned char * p_data, size_t size);

/** @brief Copy incoming ciphertext; returns false if the host queue is full. */
bool tlspeer_feed (const unsigned char * p_data, size_t size);

/** @brief Copy queued outgoing ciphertext into the caller's buffer. */
size_t tlspeer_drain (unsigned char * p_data, size_t capacity);

/** @brief Return certificate verification flags from the real TLS context. */
uint32_t tlspeer_verify (void);

/** @brief Borrow the negotiated protocol/cipher names until close. */
const char * tlspeer_protocol (void);
const char * tlspeer_cipher (void);

/** @brief Return compiled plaintext input/output limits for identity checks. */
size_t tlspeer_limit (bool b_input);

/** @brief Describe an SDK result without changing it. */
void tlspeer_error (int status, char * p_text, size_t capacity);

#endif

/* End of file. */

