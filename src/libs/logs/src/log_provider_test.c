/** -------------------------------------------------------------------------- *
 * @copyright Copyright (c) 2023-2024 SG Wireless - All Rights Reserved
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
 * @brief   This file represents the implementations of the log test type
 *          provider
 * --------------------------------------------------------------------------- *
 */

/* --- include -------------------------------------------------------------- */

#include <stdio.h>

#include "log_lib.h"
#include "log_config.h"
#include "log_provider.h"
#include "log_buf_mgr.h"

/* --- type private implementation ------------------------------------------ */

__opt_paste( __opt_log_type_test, y,
    static void log_provide_test(log_info_t* p_log_info);
)

__log_type_def(log_provide_test, test, 1);


#if __opt_test( __opt_log_type_test, y )
static void log_provide_test(log_info_t* p_log_info)
{
    log_info_test_t* p_info = (log_info_test_t*)p_log_info;
    void* p_basic_info = p_info->log_info_base.p_basic_info;

    log_provide_printf(p_basic_info, "[%4lu] ( %s"__default__" )",
        p_info->test_id,
        p_info->verdict ? __green__ "PASS" : __red__ "FAIL");
    log_provide_printf(p_basic_info, "\"%s\"", p_info->test_case_name);
}
#endif

/* --- end ------------------------------------------------------------------ */
