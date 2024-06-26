# ---------------------------------------------------------------------------- #
# Copyright (c) 2023-2024 SG Wireless - All Rights Reserved
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use,  copy,  modify,  merge, publish, distribute, sublicense, and/or sell
# copies  of  the  Software,  and  to  permit  persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”,  WITHOUT WARRANTY OF ANY KIND,  EXPRESS OR
# IMPLIED,  INCLUDING BUT NOT LIMITED TO  THE  WARRANTIES  OF  MERCHANTABILITY
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS  OR  COPYRIGHT  HOLDERS  BE  LIABLE FOR ANY CLAIM,  DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN  CONNECTION WITH  THE SOFTWARE OR  THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# Author    Ahmed Sabry (SG Wireless)
#
# Desc      This file responsible for supporting the build of the log_lib.
# ---------------------------------------------------------------------------- #

file(GLOB_RECURSE __defs_files
        "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/inc/*.h"
    )

__sdk_add_component( log_lib
    MPY_MODS
        ${CMAKE_CURRENT_LIST_DIR}/mpy/mod_logs.c
    
    LOGS_DEFS
        ${__defs_files}

    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/mpy/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/*.c"

    INCS_IF
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/inc

    INCS
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/inc
        ${CMAKE_CURRENT_LIST_DIR}/src

    MENU_CONFIG ${CMAKE_CURRENT_LIST_DIR}/cfg/kconfig
    MENU_PROMPT "logging library"
    MENU_GROUP  MAIN.SDK.CLIBS

    REQUIRED_SDK_LIBS
        utils_lib
)

# --- log_enforce injection -------------------------------------------------- #

__sdk_add_compile_options( -imacros ${CMAKE_CURRENT_LIST_DIR}/log_enforce.h )

if( DEFINED LOGS )
    if( "${LOGS}" STREQUAL "off" )
        __sdk_add_compile_options( -D__log_lib_global_disable )
    endif()
endif()

# --- end of file ------------------------------------------------------------ #
