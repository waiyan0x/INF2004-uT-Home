/**
 * @file tkernel.h
 * @brief Minimal source-level kernel API double for host failure injection.
 * @details Never include this header in target builds. Names and member
 * spellings follow the external kernel API (BARR-C naming deviation); the
 * structures expose only fields used by the adapter and do not reproduce the
 * complete target ABI.
 */
#ifndef TEST_TKERNEL_H
#define TEST_TKERNEL_H
#include <stdint.h>
typedef int32_t ID;
typedef int32_t INT;
typedef int32_t ER;
typedef int32_t TMO;
typedef int32_t SZ;
/** @brief Describe the kernel's generic callback signature without a cast. */
typedef void tkernel_callback_t (void);
typedef tkernel_callback_t * FP;

typedef struct
{
    int32_t  hi;
    uint32_t lo;
} SYSTIM;

typedef struct
{
    uint32_t sysstat;
    ID       runtskid;
} T_RSYS;

typedef struct
{
    uint32_t tskstat;
} T_RTSK;

typedef struct
{
    void *   exinf;
    uint32_t tskatr;
    FP       task;
    int      itskpri;
    SZ       stksz;
    void *   bufptr;
} T_CTSK;

#define E_OK       0
#define E_TMOUT    (-50)
#define E_OBJ      (-41)
#define E_QOVR     (-43)
#define TSS_TSK    0U
#define TTS_DMT    16U
#define TA_HLNG    1U
#define TA_RNG0    0U
#define TA_USERBUF 0x20U
ER tk_ref_sys (T_RSYS * p_state);
ER tk_get_otm (SYSTIM * p_time);
ER tk_slp_tsk (TMO timeout);
void tk_ext_tsk (void);
ID tk_cre_tsk (const T_CTSK * p_config);
ER tk_sta_tsk (ID h_task, INT code);
ER tk_ref_tsk (ID h_task, T_RTSK * p_state);
ER tk_wup_tsk (ID h_task);
ER tk_dly_tsk (TMO delay);
ER tk_del_tsk (ID h_task);
ER tk_chg_pri (ID h_task, int priority);
#endif

/* End of file. */

