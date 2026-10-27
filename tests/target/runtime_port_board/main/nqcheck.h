/**
 * @file nqcheck.h
 * @brief Native fixed-queue checks without peripheral I/O or new tasks.
 */
#ifndef UTHOME_NQCHECK_H
#define UTHOME_NQCHECK_H

/** @brief Borrow one dormant permanent worker; return zero on success. */
int nqcheck_run (void);

#endif

/* End of file. */

