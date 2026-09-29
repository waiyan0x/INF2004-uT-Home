/** @file cfgrule.h
 * @brief Resolve bounded rules after device and queue admission. */
#ifndef UTHOME_CFGRULE_H
#define UTHOME_CFGRULE_H
#include "cfgplan.h"
#include "cfgjson.h"
bool cfgrule_read (const cfgjson_value_t * p_root, cfgplan_t * p_plan);
#endif
/* End of file. */

