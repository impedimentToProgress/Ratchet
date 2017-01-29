//===-- aeabi_idivmod.S - EABI idivmod implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

// struct { int quot, int rem} __aeabi_idivmod(int numerator, int denominator) {
//   int rem, quot;
//   quot = __divmodsi4(numerator, denominator, &rem);
//   return {quot, rem};
// }

.thumb

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_THUMB_FUNCTION(__aeabi_idivmod)
        push    { lr }
        sub     sp, sp, #4
        mov     r2, sp
        bl      SYMBOL_NAME(__divmodsi4)
        ldr     r1, [sp]
        ldr     r3, [sp, #4]
        #bl      _checkpoint_8
        add     sp, sp, #8
        bx      r3
        pop     { pc }
END_COMPILERRT_FUNCTION(__aeabi_idivmod)
