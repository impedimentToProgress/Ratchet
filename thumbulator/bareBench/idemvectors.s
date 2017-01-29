
/* vectors.s */
;@ .cpu cortex-m0
.thumb

.word   0x407FFFFC  /* stack top address */
.word   _start      /* 1 Reset */
.word   hang        /* 2 NMI */
.word   hang        /* 3 HardFault */
.word   hang        /* 4 MemManage */
.word   hang        /* 5 BusFault */
.word   hang        /* 6 UsageFault */
.word   hang        /* 7 RESERVED */
.word   hang        /* 8 RESERVED */
.word   hang        /* 9 RESERVED*/
.word   hang        /* 10 RESERVED */
.word   hang        /* 11 SVCall */
.word   hang        /* 12 Debug Monitor */
.word   hang        /* 13 RESERVED */
.word   hang        /* 14 PendSV */
.word   hang        /* 15 SysTick */
.word   _check_checkpoint/* 16 External Interrupt(0) */
.word   hang        /* 17 External Interrupt(1) */
.word   hang        /* 18 External Interrupt(2) */
.word   hang        /* 19 ...   */

.thumb_func
hang:   b .

.thumb_func
.global _start
_start:
  ldr r0, =_idemStorePtr
  ldr r0, [r0, #0]
  cmp r0, #0
  bne _restore_checkpoint 
  ldr r0, =_idemStore1
  ldr r1, =_idemStore2
  str r0, [r1, #60]
  str r1, [r0, #60]
  mov r1, sp
  mov r2, pc
  str r1, [r0, #32]
  str r2, [r0, #36]
  ldr r1, =_idemStorePtr
  str r0, [r1,#0]
  ldr r0, =_wdt_addr
  ldr r0, [r0, #0] 
  ldr r1, =_wdt_val
  ldr r1, [r1, #0] 
  str r1, [r0,#44]
  bl main
.global exit
exit:
  swi 1
  @ldr r0,=0xF0000000 ;@ halt
  @str r0,[r0]
  b .
_restore_checkpoint:
  mov r2, #0
  str r2, [r0, #36]
  ldr r0, [r0, #60]
  ldr r1, [r0, #32]
  ldr r3, [r0, #36]
  ldr r2, [r0, #40]
  mov sp, r1
  mov lr, r2
  mov r4, #1
  orr r3, r3, r4
  mov r12, r3
  ldmia r0, {r0-r7}
_exit_restore_checkpoint:
  mov pc, r12

.thumb_func
.global _check_checkpoint
_check_checkpoint:
  mov r12, r7
  ldr r7, =_idemStorePtr
  ldr r7, [r7, #0]
  ldr r7, [r7, #36]
  cmp r7, #0
  beq _checkpoint_except
  mov r7, r12
  bx lr

.thumb_func
.global _checkpoint_except
_checkpoint_except:
  ldr r7, =_idemStorePtr
  ldr r7, [r7, #0]
  stmia r7!, {r0-r6}
  mov r0, r12
  add r1, sp, #32
  ldr r2, [sp, #24]
  ldr r3, [sp, #20]
  stmia r7!, {r0-r3}
  ldr r0, =_idemStorePtr
  ldr r7, [r7, #16] 
  str r7, [r0, #0]
  mov r7, r12
  bx lr


.data
.global _wdt_addr
_wdt_addr:
 .long 0x80000000
.global _wdt_val
_wdt_val:
 .long 0

.end
