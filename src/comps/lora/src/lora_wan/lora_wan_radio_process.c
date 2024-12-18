/** -------------------------------------------------------------------------- *
 * Copyright (c) 2023-2024 SG Wireless - All Rights Reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files(the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use,  copy,  modify,  merge, publish, distribute, sublicense, and/or sell
 * copies  of  the  Software,  and  to  permit  persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”,  WITHOUT WARRANTY OF ANY KIND,  EXPRESS OR
 * IMPLIED,  INCLUDING BUT NOT LIMITED TO  THE  WARRANTIES  OF  MERCHANTABILITY
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS  OR  COPYRIGHT  HOLDERS  BE  LIABLE FOR ANY CLAIM,  DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN  CONNECTION WITH  THE SOFTWARE OR  THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * @author  Ahmed Sabry (SG Wireless)
 * 
 * @brief   lora-wan radio events processing sub-component
 * --------------------------------------------------------------------------- *
 */

/** -------------------------------------------------------------------------- *
 * includes
 * --------------------------------------------------------------------------- *
 */
#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

#include "lora_wan_process.h"
#include "radio.h"
#include "stub_timers.h"
#include "stub_system.h"
#include "lora_nvm.h"
#include "lora_wan_radio_process.h"

#define __log_subsystem     lora
#define __log_component     wan_process
#include "log_lib.h"

/* --------------------------------------------------------------------------- *
 * default settings
 * --------------------------------------------------------------------------- *
 */
#ifdef CONFIG_LORA_WAN_DEFAULT_RX_WIN_CAL_FINE_TUNE_ENABLE
    #define __rxwin_default_time_shift          \
        CONFIG_LORA_WAN_DEFAULT_RX_WIN_CAL_CTRL_FINE_TUNE_TIME_SHIFT
    #define __rxwin_default_time_extension      \
        CONFIG_LORA_WAN_DEFAULT_RX_WIN_CAL_CTRL_FINE_TUNE_TIME_EXTENTION
    #define __rxwin_default_calibration_enabled true
#else
    #define __rxwin_default_time_shift          0
    #define __rxwin_default_time_extension      0
    #define __rxwin_default_calibration_enabled false
#endif

/* --------------------------------------------------------------------------- *
 * nvm settings
 * --------------------------------------------------------------------------- *
 */

typedef struct {
    int32_t     time_shift;
    int32_t     time_extension;
    bool        calibration_enabled;

    lora_nvm_record_tail_t  nvm_record_tail;
} local_nvm_data_t;

static local_nvm_data_t s_local_nvm_data;

static const char* s_proc_name = "lw-radio-proc";

static void load_local_nvm_data_defaults( void* p_record_mem, uint32_t size )
{
    memset(p_record_mem, 0, size);
    s_local_nvm_data.time_shift = __rxwin_default_time_shift;
    s_local_nvm_data.time_extension = __rxwin_default_time_extension;
    s_local_nvm_data.calibration_enabled = __rxwin_default_calibration_enabled;
}

static void handle_local_nvm_data_change(void)
{
    __log_info("handle %s nvm data changes", s_proc_name);

    lora_nvm_handle_change(
        s_proc_name,
        &s_local_nvm_data,
        sizeof(local_nvm_data_t),
        load_local_nvm_data_defaults
    );
}

#define __update_nvm_data(_key, _value) \
    do {                                \
        s_local_nvm_data._key = _value; \
        handle_local_nvm_data_change(); \
    } while( 0 )

/* --------------------------------------------------------------------------- *
 * process access guard
 * --------------------------------------------------------------------------- *
 */
static bool s_is_access_init = false;
static SemaphoreHandle_t s_access_mutex = NULL;
#define __access_guard_init()                           \
    do {                                                \
        s_access_mutex = xSemaphoreCreateMutex();       \
        __log_assert(s_access_mutex != NULL,            \
            "failed to create %s access guard mutex",   \
            s_proc_name);                               \
        s_is_access_init = true;                        \
    } while(0)
#define __access_guard_deinit()             \
    do {                                    \
        vSemaphoreDelete(s_access_mutex);   \
        s_is_access_init = false;           \
    } while(0)
#define __access_lock() \
    if(s_is_access_init)xSemaphoreTake(s_access_mutex, portMAX_DELAY)
#define __access_unlock() \
    if(s_is_access_init)xSemaphoreGive(s_access_mutex)

static bool s_is_process_lock_init = false;
static SemaphoreHandle_t s_process_mutex = NULL;
#define __process_guard_init()                                          \
    do {                                                                \
        s_process_mutex = xSemaphoreCreateMutex();                      \
        __log_assert(s_process_mutex != NULL,                           \
            "failed to create %s processing guard mutex", s_proc_name); \
        s_is_process_lock_init = true;                                  \
    } while(0)
#define __process_guard_deinit()             \
    do {                                    \
        vSemaphoreDelete(s_process_mutex);  \
        s_is_process_lock_init = false;     \
    } while(0)
#define __process_lock() \
    if(s_is_process_lock_init)xSemaphoreTake(s_process_mutex, portMAX_DELAY)
#define __process_unlock() \
    if(s_is_process_lock_init)xSemaphoreGive(s_process_mutex)

/** -------------------------------------------------------------------------- *
 * lora radio process task
 * --------------------------------------------------------------------------- *
 */
static SemaphoreHandle_t s_sync_sem_handle = NULL;
static SemaphoreHandle_t s_sync_back_sem_handle = NULL;
static TaskHandle_t      s_task_handle = NULL;

static struct {
    struct {
        void (*p_handler)(void*);
        void* args;
        bool sync_wait;
    } req;

    void (*p_irq_handler)(void*);

    enum {
        __proc_idle,
        __proc_busy
    } state;
} s_exec_ctx;

static void lw_radio_process_task(void * arg)
{
    void (*p_handler)(void*);
    void* args;
    bool  is_sync;

    void (*p_irq_handler)(void*);

    while(1)
    {
        if(xSemaphoreTake(s_sync_sem_handle, portMAX_DELAY) == pdTRUE)
        {
            restart_processing:

            __access_lock();

            p_handler = s_exec_ctx.req.p_handler;
            args = s_exec_ctx.req.args;
            is_sync = s_exec_ctx.req.sync_wait;
            s_exec_ctx.req.p_handler = NULL;
            s_exec_ctx.req.args = NULL;
            s_exec_ctx.req.sync_wait = false;

            p_irq_handler = s_exec_ctx.p_irq_handler;
            s_exec_ctx.p_irq_handler = NULL;

            s_exec_ctx.state = __proc_busy;

            __access_unlock();

            if(p_irq_handler)
            {
                lm_radio_process_lock();
                p_irq_handler(NULL);
                lm_radio_process_unlock();
            }

            if(p_handler)
            {
                lm_radio_process_lock();
                p_handler(args);
                lm_radio_process_unlock();
            }

            if( ! ( p_handler || p_irq_handler ) )
            {
                __log_warn("%s triggerred without request handler",
                    s_proc_name);
            }

            __access_lock();

            s_exec_ctx.state = __proc_idle;

            if(p_handler && is_sync) {
                if(xSemaphoreGive(s_sync_back_sem_handle) != pdTRUE)
                {
                    __log_error("%s: failed to give sem sync-back",
                        s_proc_name);
                }
            }

            p_irq_handler = s_exec_ctx.p_irq_handler;

            __access_unlock();

            if(p_irq_handler)
            {
                goto restart_processing;
            }
        }
        else
        {
            __log_error("%s: failed to take sem sync", s_proc_name);
        }
    }
}

static void lw_rxwin_timing_ctrl_ctor(void);
static bool is_module_initialized = false;
void lw_radio_process_ctor(void)
{
    if(is_module_initialized)
    {
        return;
    }

    handle_local_nvm_data_change();

    lw_rxwin_timing_ctrl_ctor();

    __access_guard_init();
    __process_guard_init();

    s_sync_sem_handle = xSemaphoreCreateBinary();
    __log_assert(s_sync_sem_handle != NULL,
        "%s: failed to create sync sem", s_proc_name);
    s_sync_back_sem_handle = xSemaphoreCreateBinary();
    __log_assert(s_sync_back_sem_handle != NULL,
        "%s: failed to create sync-back sem", s_proc_name);

    xTaskCreate(
        lw_radio_process_task,      // task function
        s_proc_name,                // task name
        4 * 1024,                   // stack size
        NULL,                       // parameter to task function
        configMAX_PRIORITIES,       // priority must be higher
        &s_task_handle              // handle to the task
        );
    configASSERT( s_task_handle );

    is_module_initialized = true;
}

void lw_radio_process_dtor(void)
{
    vTaskDelete(s_task_handle);
    __access_guard_deinit();
    __process_guard_deinit();

    is_module_initialized = false;
}

static void lw_radio_process_request(
    void (*p_handler)(void*),
    void* args,
    bool asserted_request,
    bool sync,
    bool is_irq_handler
    )
{
    bool do_log_free = false;
    bool do_log_busy = true;

    do_process_request:

    __access_lock();

    if(is_irq_handler)
    {
        s_exec_ctx.p_irq_handler = p_handler;
        if(s_exec_ctx.state == __proc_idle) {
            if(xSemaphoreGive(s_sync_sem_handle) != pdTRUE)
            {
                __log_error("%s: failed to give sem sync", s_proc_name);
            }
        }
        __access_unlock();
        return;
    }

    if(s_exec_ctx.state == __proc_busy)
    {
        __access_unlock();
        if(do_log_busy)
        {
            __log_debug("-> lora-wan-radio task is busy");
            do_log_free = true;
            do_log_busy = false;
        }

        if(asserted_request)
        {
            goto do_process_request;
        }

        return;
    }

    if(do_log_free) {
        __log_debug("<- lora-wan-radio task is free\n");
    }

    s_exec_ctx.req.p_handler = p_handler;
    s_exec_ctx.req.args = args;
    s_exec_ctx.req.sync_wait = sync;
    s_exec_ctx.state = __proc_busy;

    __access_unlock();

    if(xSemaphoreGive(s_sync_sem_handle) != pdTRUE)
    {
        __log_error("%s: failed to give sem sync", s_proc_name);
    }

    if(sync)
    {
        if(xSemaphoreTake(s_sync_sem_handle, portMAX_DELAY) != pdTRUE)
        {
            __log_error("%s: failed to take sem sync-back", s_proc_name);
        }
    }
}

static volatile void* s_owner_task_handle = NULL;
static volatile void* s_owner_task_name = NULL;
static volatile uint32_t s_owner_lock_counter = 0;

void lm_radio_process_lock(void)
{
    if( ! is_module_initialized )
    {
        return;
    }
    void* current_task_handle = xTaskGetCurrentTaskHandle();
    bool do_lock = true;
    bool is_waiting = false;

    if(s_owner_task_handle == current_task_handle)
    {
        ++ s_owner_lock_counter;
        do_lock = false;
    }
    else if(s_owner_task_handle != NULL)
    {
        __log_debug("[%s] is waiting [%s] for radio access",
            pcTaskGetName(NULL), s_owner_task_name);
        is_waiting = true;
    }

    if(do_lock) {
        __process_lock();
        s_owner_task_handle = current_task_handle;
        s_owner_task_name = pcTaskGetName(NULL);
        ++ s_owner_lock_counter;
        if(is_waiting)
        {
            __log_debug("[%s] has granted radio access\n", s_owner_task_name);
        }
    }
}

void lm_radio_process_unlock(void)
{
    if( ! is_module_initialized )
    {
        return;
    }
    void* current_task_handle = xTaskGetCurrentTaskHandle();
    bool do_unlock;

    if(current_task_handle == s_owner_task_handle)
    {
        if(s_owner_lock_counter == 1)
        {
            do_unlock = true;
            s_owner_task_handle = NULL;
            s_owner_task_name = NULL;
        }
        else
        {
            do_unlock = false;
        }
        -- s_owner_lock_counter;
    }
    else
    {
        __log_output("**** non-owner task unlock mutex\n");
        __log_assert(0, "*** panic ***");
    }

    if(do_unlock)
    {
        __process_unlock();
    }
}

void lm_radio_process_assert(void)
{
    if( ! is_module_initialized )
    {
        return;
    }
    void* current_task_handle = xTaskGetCurrentTaskHandle();
    if(current_task_handle != s_owner_task_handle)
    {
        __log_output(__yellow__"%p"__default__"]("__cyan__"%s"__default__"): "
            "lm radio processing is asserted by another maybe ["
            __red__"%p"__default__"]("__red__"%s"__default__")\n",
            current_task_handle, pcTaskGetName(NULL),
            s_owner_task_handle, s_owner_task_name);
        __log_assert(0, "*** panic ***");
    }
}

/** -------------------------------------------------------------------------- *
 * lora rx-window radio timing controls
 * ====================================

 *                                              |<- S ->|          |<- E >|
 *           TX-WIN                                     |
 *          +-------+                                 +-+- ------+-+
 *          |       |                                 |e| RX-WIN |e|
 *      ----+       +------------------------x1---x2--+-+        +-+--------
 *                  ^                                   ^
 *                  |<-------- rx-act-delay ----------->|
 *                  tx-done                             rx-act-time
 * 
 *  e :: induced lora-mac sys-rx-error --> it is affecting the lora symbols
 *       timeout in the lora tranceiver itself and it should be sufficient
 *       for timing errors induced by different system parties clock
 *       mis-alignments.
 *  S :: introduced calibration control time shift. it ensures the rx-act-time
 *       is ammended based on the provided calibrated time shift control.
 *  E :: calibration control window width extension. it affects the rx-window
 *       deadline timer to be extended more to not disturb the tranceiver
 *       while it is receiving.
 *  x1, x2 :: possible timer expiration ticks. it should be very close to the
 *       ( <rx-act-time> - S ) value. but in case it is not aligned, an extra
 *       delay will be applied to ensure it is maintained.
 * --------------------------------------------------------------------------- *
 */
struct {
    int32_t     time_shift;
    int32_t     time_extension;
    bool        calibration_enabled;

    uint32_t    last_tx_done_ts;

    uint32_t    rxwin_sys_err;
    uint32_t    rxwin_expire_ts;
    int32_t     calibrated_time_shift;

    uint32_t    rxwin_act_delay;
    enum {
        __rx_win_none,
        __rx_win_1,
        __rx_win_2
    } curr_handled_rxwin;
    enum {
        __rx_state_idle = 0,
        __rx_state_done = 1,
        __rx_state_tout = 2,
        __rx_state_error = 3
    } rx1_state, rx2_state;
    void (*p_win_timer_expire_handler)(void*);
    volatile bool is_rxwin_processing;

    bool debug_verbose;

} s_rxwin_ctrl_ctx = {
    .rx1_state = __rx_state_idle,
    .rx2_state = __rx_state_idle,
    .curr_handled_rxwin = __rx_win_none,
    .debug_verbose = false
};

static void lw_rxwin_timing_ctrl_ctor(void)
{
    s_rxwin_ctrl_ctx.time_shift = s_local_nvm_data.time_shift;
    s_rxwin_ctrl_ctx.time_extension = s_local_nvm_data.time_extension;
    s_rxwin_ctrl_ctx.calibration_enabled = s_local_nvm_data.calibration_enabled;
}

void lw_rxwin_set_sys_time_err(uint32_t sys_time_err)
{
    s_rxwin_ctrl_ctx.rxwin_sys_err = sys_time_err;
}

int32_t lw_rxwin_calibration_get_time_shift(void)
{
    return s_rxwin_ctrl_ctx.time_shift;    
}
void lw_rxwin_calibration_set_time_shift(int32_t time_shift)
{
    s_rxwin_ctrl_ctx.time_shift = time_shift;
    __update_nvm_data(time_shift, time_shift);
}

int32_t lw_rxwin_calibration_get_time_extension(void)
{
    return s_rxwin_ctrl_ctx.time_extension;
}
void lw_rxwin_calibration_set_time_extension(int32_t time_extension)
{
    s_rxwin_ctrl_ctx.time_extension = time_extension;
    __update_nvm_data(time_extension, time_extension);
}

bool lw_rxwin_calibration_get_enablement(void)
{
    return s_rxwin_ctrl_ctx.calibration_enabled;
}
void lw_rxwin_calibration_set_enablement(bool enable)
{
    s_rxwin_ctrl_ctx.calibration_enabled = enable;
    __update_nvm_data(calibration_enabled, enable);
}

void lw_rxwin_set_last_tx_done_timestamp(uint32_t timestamp)
{
    s_rxwin_ctrl_ctx.last_tx_done_ts = timestamp;
    s_rxwin_ctrl_ctx.curr_handled_rxwin = __rx_win_none;
    s_rxwin_ctrl_ctx.rx1_state = __rx_state_idle;
    s_rxwin_ctrl_ctx.rx2_state = __rx_state_idle;
}

uint32_t lm_rxwin_get_delay(uint32_t win_act_delay)
{
    uint32_t offset =
        lora_stub_get_timestamp_ms() - s_rxwin_ctrl_ctx.last_tx_done_ts;
    if(s_rxwin_ctrl_ctx.calibration_enabled)
    {
        return win_act_delay
                - offset
                - s_rxwin_ctrl_ctx.time_shift;
    }
    else
    {
        return win_act_delay;
    }
}

static void lm_rxwin_rx_conclude(void);
void lm_rxwin_set_rx_state(int state)
{
    if(s_rxwin_ctrl_ctx.curr_handled_rxwin == __rx_win_1)
    {
        s_rxwin_ctrl_ctx.rx1_state = state;
        lm_rxwin_rx_conclude();
    }
    else if(s_rxwin_ctrl_ctx.curr_handled_rxwin == __rx_win_2)
    {
        s_rxwin_ctrl_ctx.rx2_state = state;
        lm_rxwin_rx_conclude();
    }
}

void lm_rxwin_toggle_debug_verbosity(void)
{
    s_rxwin_ctrl_ctx.debug_verbose = !s_rxwin_ctrl_ctx.debug_verbose;
}

static void lm_rxwin_verbose(void)
{
    if(s_rxwin_ctrl_ctx.debug_verbose)
    {
        int rx_state = __rx_state_idle;
        if(s_rxwin_ctrl_ctx.curr_handled_rxwin == __rx_win_1)
            rx_state = s_rxwin_ctrl_ctx.rx1_state;
        else if(s_rxwin_ctrl_ctx.curr_handled_rxwin == __rx_win_2)
            rx_state = s_rxwin_ctrl_ctx.rx2_state;
        __log_output(
            "= RX%d TS:%2d WE:%2d WD:%4d e:%2d Elp:%4d Align:%+2d V:%s\n"
            , s_rxwin_ctrl_ctx.curr_handled_rxwin
            , s_rxwin_ctrl_ctx.time_shift
            , s_rxwin_ctrl_ctx.time_extension
            , s_rxwin_ctrl_ctx.rxwin_act_delay
            , s_rxwin_ctrl_ctx.rxwin_sys_err
            , s_rxwin_ctrl_ctx.rxwin_expire_ts
                - s_rxwin_ctrl_ctx.last_tx_done_ts
            , s_rxwin_ctrl_ctx.calibrated_time_shift
            , rx_state == __rx_state_done ? "done"
                : rx_state == __rx_state_tout ? "tout"
                : rx_state == __rx_state_error ? "error"
                : "idle"
        );
    }
}

static void lm_rxwin_rx_conclude(void)
{
    lm_rxwin_verbose();
}

void lm_rxwin_time_ctrl_align(void)
{
    uint32_t curr_ts = lora_stub_get_timestamp_ms();
    int32_t elapsed_time = curr_ts - s_rxwin_ctrl_ctx.last_tx_done_ts;
    int32_t diff = (int32_t)s_rxwin_ctrl_ctx.rxwin_act_delay - elapsed_time;
    int32_t alignment_delay = diff - s_rxwin_ctrl_ctx.time_shift;
    s_rxwin_ctrl_ctx.rxwin_expire_ts = curr_ts;
    s_rxwin_ctrl_ctx.calibrated_time_shift = alignment_delay;
    if(s_rxwin_ctrl_ctx.is_rxwin_processing &&
        s_rxwin_ctrl_ctx.calibration_enabled)
    {
        if( alignment_delay > 0 )
        {
            lora_stub_delay_msec(alignment_delay);
        }
    }
}

static void lw_rxwin_timer_expire_handler(void* args)
{
    if(s_rxwin_ctrl_ctx.p_win_timer_expire_handler)
    {
        s_rxwin_ctrl_ctx.is_rxwin_processing = true;
        s_rxwin_ctrl_ctx.p_win_timer_expire_handler(args);
        s_rxwin_ctrl_ctx.is_rxwin_processing = false;

        s_rxwin_ctrl_ctx.p_win_timer_expire_handler = NULL;
    }
}

void lw_radio_process_rxwin_timer_expire(
    void (*p_handler)(void*),
    uint32_t rx_act_delay,
    int rx_win)
{
    __access_lock();
    if(s_rxwin_ctrl_ctx.curr_handled_rxwin == __rx_win_1
        && rx_win == __rx_win_2
        && s_rxwin_ctrl_ctx.rx1_state == __rx_state_done)
    {
        __log_output(__red__
            "rx2 timer expire while rx1 succeeded, ignore rx2\n"__default__);
        __access_unlock();
        return;
    }
    s_rxwin_ctrl_ctx.p_win_timer_expire_handler = p_handler;
    s_rxwin_ctrl_ctx.rxwin_act_delay = rx_act_delay;
    s_rxwin_ctrl_ctx.curr_handled_rxwin = rx_win;
    __access_unlock();
    __log_debug("rxwin timer expire process req");
    lw_radio_process_request(lw_rxwin_timer_expire_handler, NULL, true, false,
        false);
}

static void lw_radio_process_irqs_handler(void* args)
{
    if( Radio.IrqProcess != NULL )
    {
        Radio.IrqProcess( );
    }

    lora_wan_process_request(__LORA_WAN_PROCESS_PROCESS_MAC, NULL);
}
void lw_radio_process_events(void)
{
    __log_debug("radio irq process req");
    lw_radio_process_request(lw_radio_process_irqs_handler, NULL, true, false,
        true);
}

void lw_radio_process(void (p_process_handler)(void*), void* arg, bool sync)
{
    lw_radio_process_request(p_process_handler, arg, true, sync, false);
}

/* --- end of file ---------------------------------------------------------- */
