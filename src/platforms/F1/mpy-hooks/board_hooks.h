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
 * @brief   This file contains board specific hooks on different software
 *          components
 * --------------------------------------------------------------------------- *
 */
#ifndef __BOARD_HOOKS_H__
#define __BOARD_HOOKS_H__

#ifdef __cplusplus
extern "C" {
#endif

/** -------------------------------------------------------------------------- *
 * @def __hook_mpy_uart_stdout_access_lock
 * @def __hook_mpy_uart_stdout_access_unlock
 * 
 * @details using these hooks, the other firmware component can stall the access
 *          of standard output of micropython to do something else.
 * --------------------------------------------------------------------------- *
 */
extern void hook_mpy_uart_stdout_access_lock(void);
#define __hook_mpy_uart_stdout_access_lock() \
    hook_mpy_uart_stdout_access_lock()

extern void hook_mpy_uart_stdout_access_unlock(void);
#define __hook_mpy_uart_stdout_access_unlock() \
    hook_mpy_uart_stdout_access_unlock()

/* -- end of file ----------------------------------------------------------- */
#ifdef __cplusplus
}
#endif
#endif /* __BOARD_HOOKS_H__ */
