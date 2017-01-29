//===-- aeabi_memcpy.S - EABI memcpy implementation -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//  void __aeabi_memcpy(void *dest, void *src, size_t n) { memcpy(dest, src, n); }

.thumb

        .p2align 2
DEFINE_COMPILERRT_THUMB_FUNCTION(__aeabi_memcpy)
        push     { lr }
        bl       memcpy
        ldr      r3, [sp, #0]
        #bl       _checkpoint_8
        add      sp, sp, #4
        bx       r3
        pop      {pc}
END_COMPILERRT_FUNCTION(__aeabi_memcpy)

DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memcpy4, __aeabi_memcpy)
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memcpy8, __aeabi_memcpy)
