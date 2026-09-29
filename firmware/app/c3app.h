/**
 * @file c3app.h
 * @brief Run the general-purpose configuration application from usermain.
 */
#ifndef UTHOME_C3APP_H
#define UTHOME_C3APP_H
/* Called once by the existing 8 KiB initial task with an idle task running.
 * Returns only on initialization, clock or kernel-delay failure. The caller
 * must halt rather than reinitialize retained configuration/driver contexts. */
int c3app_run (void);
#endif
/* End of file. */

