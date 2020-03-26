/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __JACKASIO_H__
#define __JACKASIO_H__

#include <windows.h>


/******************************************************************************
 * ASIO drivers (breaking the COM specification) use the Microsoft variety of
 * thiscall calling convention which gcc is unable to produce.  These macros
 * add an extra layer to fixup the registers. Borrowed from config.h and the
 * wine source code.
 */

/* From config.h */
#define __ASM_DEFINE_FUNC(name,suffix,code) asm(".text\n\t.align 4\n\t.globl " #name suffix "\n\t.type " #name suffix ",@function\n" #name suffix ":\n\t.cfi_startproc\n\t" code "\n\t.cfi_endproc\n\t.previous");
#define __ASM_GLOBAL_FUNC(name,code) __ASM_DEFINE_FUNC(name,"",code)
#define __ASM_NAME(name) name
#define __ASM_STDCALL(args) ""

/* From wine source */
#ifdef __i386__  /* thiscall functions are i386-specific */

#define THISCALL(func) __thiscall_ ## func
#define THISCALL_NAME(func) __ASM_NAME("__thiscall_" #func)
#define __thiscall __stdcall
#define DEFINE_THISCALL_WRAPPER(func,args) \
    extern void THISCALL(func)(void); \
    __ASM_GLOBAL_FUNC(__thiscall_ ## func, \
                      "popl %eax\n\t" \
                      "pushl %ecx\n\t" \
                      "pushl %eax\n\t" \
                      "jmp " __ASM_NAME(#func) __ASM_STDCALL(args) )
#else /* __i386__ */

#define THISCALL(func) func
#define THISCALL_NAME(func) __ASM_NAME(#func)
#define __thiscall __cdecl
#define DEFINE_THISCALL_WRAPPER(func,args) /* nothing */

#endif /* __i386__ */


/******************************************************************************
 *      MACRO: ENSURE_DRIVER_STATE ()
 *
 * TODO:  Do preprocessor magic to eliminate the need to pass both the enum
 *        and the enum's string representation to the macro.
 */
#define ENSURE_DRIVER_STATE(state, str)                                     \
        do {                                                                \
            if (This->wineasio_drv_state != state)                          \
            {                                                               \
                ERR ("failed; current driver state != %s\n", str);          \
                return ASE_NotPresent;                                      \
            }                                                               \
        } while (0);


/******************************************************************************
 *      MACRO: SET_NEW_STATE_TRACE ()
 *
 * TODO:  Some more magic required here so we don't have to pass a textual
 *        representation of the driver state.
 */
#define SET_NEW_STATE_TRACE(state, state_str)                               \
        do {                                                                \
            This->wineasio_drv_state = state;                               \
            TRACE ("new driver state is: `%s'\n", state_str);               \
        } while (0);


/******************************************************************************
 *      MACRO: FUNC_COMPLETION_TRUE_TRACE ()
 *
 * NOTE:  This macro will only work with result codes of
 *        type BOOL or ASIOBool, and assumes SUCCESS == TRUE.
 */
#define FUNC_COMPLETION_TRUE_TRACE(result)                                  \
        do {                                                                \
            TRACE ("%s (%d)\n", result                                      \
                ? "completed successfully" : "failed", result);             \
        } while (0);


/******************************************************************************
 *      MACRO: FUNC_COMPLETION_ASEOK_TRACE ()
 *
 * NOTE:  This macro will only work with result codes of
 *        type ASIOBool, and assumes SUCCESS == ASE_OK.
 */
#define FUNC_COMPLETION_ASEOK_TRACE(result)                                 \
        do {                                                                \
            TRACE ("%s (%d)\n", result == ASE_OK                            \
                ? "completed successfully" : "failed", result);             \
        } while (0);


/******************************************************************************
 *      Constants
 */

#define BUF_A    0              /* index for buffer A */
#define BUF_B    1              /* index for buffer B */
#define BUF_A_B  2		/* total of 2 buffer halves, A and B */

#define WINEASIO_IN     0
#define WINEASIO_OUT    1
#define WINEASIO_IN_OUT 2

#define KB       1024

#define NO_QUERY_RESULT (-1)	// FIXME: what is this for?

#define WINEASIO_NO_PORT_ID (-1)

#define WINEASIO_INTERNAL_ERROR(code)                                         \
    do                                                                        \
    {                                                                         \
        ERR ("possible internal error, code (%d); please consider filing an"  \
             " Issue at https://wineasio.org describing what you were up to." \
	     "\n", code);                                                     \
    }                                                                         \
    while (0);

/******************************************************************************
 *      External declarations
 */
extern HRESULT WINAPI WineASIOCreateInstance (REFIID  riid,
                                              LPVOID *ppobj);

#endif
