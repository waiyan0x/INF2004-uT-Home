/**
 * @file cfgdev.h
 * @brief Decode the reviewed compiled device descriptors into plain settings.
 */
#ifndef UTHOME_CFGDEV_H
#define UTHOME_CFGDEV_H
#include "cfgjson.h"
#include "cfgplan.h"
/* Internal admission helpers, never hardware dispatch. */
bool cfgdev_read (const cfgjson_value_t * p_value,
                  cfgplan_t *             p_plan,
                  size_t                  index,
                  uint32_t                compiled_mask);
bool cfgdev_limits (const cfgplan_t * p_plan);
const char * cfgdev_name (cfgplan_driver_t driver);
#endif
/* End of file. */

