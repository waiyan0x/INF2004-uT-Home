/**
 * @file cfgpart.h
 * @brief Check the exact reviewed flash layout without allocating a table.
 */
#ifndef UTHOME_CFGPART_H
#define UTHOME_CFGPART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTHOME_PART_TABLE_OFFSET  0x8000U
#define UTHOME_PART_TABLE_BYTES   3072U
#define UTHOME_PART_FLASH_BYTES   0x400000U
#define UTHOME_PART_CONFIG_OFFSET 0x110000U
#define UTHOME_PART_CONFIG_BYTES  0x6000U

/* Every byte of the table must be checked before enabling storage. This
 * stateless chunk check does not establish coverage on the caller's behalf.
 * It accepts only configuration.csv's four entries, MD5 record and FF tail. */
bool cfgpart_check (const uint8_t * p_data, size_t offset, size_t size);
/* This build permits writes only to the two configuration slots. */
bool cfgpart_writable (size_t address, size_t size);

#endif

/* End of file. */

