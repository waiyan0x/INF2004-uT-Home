/**
 * @file pwmwork.c
 * @brief Execute the configured bounded output service.
 */
#include "pwmwork.h"

#include "shared.h"
#include "workport.h"

/** @brief One worker evaluates its rule and writes one typed output command. */
int pwmwork_step (pwmwork_t * p_output, component_t * p_component)
{
    sampcache_entry_t entry  = {0};
    uint64_t          now_ms = 0U;
    const int         sampled =
        shared_source(p_output->rule.source_index, &entry, &now_ms);
    if ((0 != sampled) && (0 != rt_port_now_ms(&now_ms)))
    {
        now_ms =
            0U; /* Regression/unavailable sample forces the zero command. */
    }
    pwminst_command_t command   = {0};
    const int         evaluated = rulecalc_step(&p_output->rule,
                                        &p_output->state,
                                        (0 == sampled) ? &entry : NULL,
                                        now_ms,
                                        &command.duty_permille);
    int               result    = 0;
    if (command.duty_permille != p_output->instance.p_port->duty_permille)
    {
        result = component_write(p_component, &command, sizeof(command));
    }
    if ((0 != evaluated) || (0 != result))
    {
        command.duty_permille = 0U;
        const int stopped =
            component_write(p_component, &command, sizeof(command));
        /* The worker's failure and retained ownership remain observable even
         * if concurrent health accounting is busy. Cleanup must still retry. */
        (void)shared_report(UTHOME_HEALTH_DEVICE_IO);
        return (0 != stopped) ? stopped : -1;
    }
    return 0;
}

/* End of file. */

