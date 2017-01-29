
int _idemStorePtr = 0;
int _idemStore1[16];
int _idemStore2[16];

#define NDEBUG 1

#define SAVE_REGS_0
#define SAVE_REGS_1 SAVE_REGS_0
#define SAVE_REGS_2 SAVE_REGS_0
#define SAVE_REGS_3 SAVE_REGS_0
#define SAVE_REGS_4 SAVE_REGS_0
#define SAVE_REGS_5 SAVE_REGS_0
#define SAVE_REGS_6 SAVE_REGS_0 
#define SAVE_REGS_7 \
__asm__ __volatile__ ( \
    "mov r11, r6\n\t"\
    )
#define SAVE_REGS_8 \
__asm__ __volatile__ ( \
    "mov r11, r6\n\t"\
    "mov r12, r7\n\t"\
    )
#define SAVE_REGS_RET \
__asm__ __volatile__ ( \
    "mov lr, r3\n\t"\
    )

#define LOAD_CP_PTR_RET \
__asm__ __volatile__ (          \
    "ldr r1, =_idemStorePtr\n\t"\
    "ldr r1, [r1, #0]\n\t"\
    )
#define LOAD_CP_PTR_0 \
__asm__ __volatile__ (          \
    "ldr r4, =_idemStorePtr\n\t"\
    "ldr r7, [r4, #0]\n\t"\
    )
#define LOAD_CP_PTR_1 LOAD_CP_PTR_0
#define LOAD_CP_PTR_2 LOAD_CP_PTR_0
#define LOAD_CP_PTR_3 LOAD_CP_PTR_0
#define LOAD_CP_PTR_4 LOAD_CP_PTR_0
#define LOAD_CP_PTR_5 \
__asm__ __volatile__ (          \
    "ldr r7, =_idemStorePtr\n\t"\
    "ldr r7, [r7, #0]\n\t"\
    )
#define LOAD_CP_PTR_6 LOAD_CP_PTR_5
#define LOAD_CP_PTR_7 LOAD_CP_PTR_5
#define LOAD_CP_PTR_8 LOAD_CP_PTR_5


#define SAVE_SP_RET \
  __asm__ __volatile__ (\
      "stmia r1!, {r2,r3}\n\t"\
      )
#define SAVE_SP_0 \
  __asm__ __volatile__ (\
      "mov r5, sp\n\t"\
      "mov r6, lr\n\t"\
      "stmia r7!, {r5-r6}\n\t"\
      )
#define SAVE_SP_1 SAVE_SP_0 
#define SAVE_SP_2 SAVE_SP_0 
#define SAVE_SP_3 SAVE_SP_0 
#define SAVE_SP_4 SAVE_SP_0 
#define SAVE_SP_5 SAVE_SP_0 
#define SAVE_SP_6 \
  __asm__ __volatile__ (\
      "mov r6, sp\n\t"\
      "stmia r7!, {r6}\n\t"\
      "mov r6, lr\n\t"\
      "stmia r7!, {r6}\n\t"\
      )
#define SAVE_SP_7 SAVE_SP_6 
#define SAVE_SP_8 \
  __asm__ __volatile__ (\
      "mov r6, r12\n\t"\
      "stmia r7!, {r6}\n\t"\
      "mov r6, sp\n\t"\
      "stmia r7!, {r6}\n\t"\
      "mov r6, lr\n\t"\
      "stmia r7!, {r6}\n\t"\
      )

#if NDEBUG

#define SAVE_LOW_RET \
__asm__ __volatile__ (\
    "stmia r1!, {r0}\n\t"\
    "adds r1, r1, #12\n\t"\
    "stmia r1!, {r4-r7}\n\t")
#define SAVE_LOW_8 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r6}\n\t")
#define SAVE_LOW_7 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r6}\n\t"\
    "adds r7, r7, #4\n\t")
#define SAVE_LOW_6 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r5}\n\t"\
    "adds r7, r7, #8\n\t")
#define SAVE_LOW_5 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r4}\n\t"\
    "adds r7, r7, #12\n\t")
#define SAVE_LOW_4 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r3}\n\t"\
    "adds r7, r7, #16\n\t")
#define SAVE_LOW_3 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r2}\n\t"\
    "adds r7, r7, #20\n\t")
#define SAVE_LOW_2 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r1}\n\t"\
    "adds r7, r7, #24\n\t")
#define SAVE_LOW_1 \
__asm__ __volatile__ (\
    "stmia r7!, {r0}\n\t"\
    "adds r7, r7, #28\n\t")
#define SAVE_LOW_0 \
__asm__ __volatile__ (\
    "adds r7, r7, #32\n\t")

#else

#define SAVE_LOW_RET \
__asm__ __volatile__ (\
    "mov r7, r7\n\t"\
    "stmia r1!, {r3-r7}\n\t")
#define SAVE_LOW_8 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r6}\n\t")
#define SAVE_LOW_7 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r6}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_6 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r5}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_5 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r4}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_4 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r3}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_3 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r2}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_2 \
__asm__ __volatile__ (\
    "stmia r7!, {r0-r1}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_1 \
__asm__ __volatile__ (\
    "stmia r7!, {r0}\n\t"\
    "mov r7, r7\n\t")
#define SAVE_LOW_0 \
__asm__ __volatile__ (\
    "mov r7, r7\n\t")

#endif

#if NDEBUG

#define STORE_CP_PTR_RET \
__asm__ __volatile__ (          \
    "ldr r3, =_idemStorePtr\n\t"\
     "ldr r1, [r1, #20]\n\t"\
    "str r1, [r3, #0]\n\t"\
    )
#define STORE_CP_PTR_0 \
__asm__ __volatile__ (          \
    "ldr r7, [r7, #20]\n\t"\
    "str r7, [r4, #0]\n\t"\
    )
#define STORE_CP_PTR_1 STORE_CP_PTR_0
#define STORE_CP_PTR_2 STORE_CP_PTR_0
#define STORE_CP_PTR_3 STORE_CP_PTR_0
#define STORE_CP_PTR_4 STORE_CP_PTR_0
#define STORE_CP_PTR_5 \
__asm__ __volatile__ (          \
    "ldr r6, =_idemStorePtr\n\t"\
    "ldr r7, [r7, #20]\n\t"\
    "str r7, [r6, #0]\n\t"\
    )
#define STORE_CP_PTR_6 STORE_CP_PTR_5
#define STORE_CP_PTR_7 STORE_CP_PTR_5
#define STORE_CP_PTR_8 STORE_CP_PTR_5

#else

#define STORE_CP_PTR_RET \
__asm__ __volatile__ (          \
    "ldr r2, =_idemStorePtr\n\t"\
    "ldr r1, [r1, #20]\n\t"\
    "str r1, [r2, #0]\n\t"\
    )
#define STORE_CP_PTR_0 \
__asm__ __volatile__ (          \
    "ldr r7, [r7, #48]\n\t"\
    "str r7, [r4, #0]\n\t"\
    )
#define STORE_CP_PTR_1 \
__asm__ __volatile__ (          \
    "ldr r7, [r7, #44]\n\t"\
    "str r7, [r4, #0]\n\t"\
    )
#define STORE_CP_PTR_2 \
__asm__ __volatile__ (          \
    "ldr r7, [r7, #40]\n\t"\
    "str r7, [r4, #0]\n\t"\
    )
#define STORE_CP_PTR_3 \
__asm__ __volatile__ (          \
    "ldr r7, [r7, #36]\n\t"\
    "str r7, [r4, #0]\n\t"\
    )
#define STORE_CP_PTR_4 \
__asm__ __volatile__ (          \
    "ldr r7, [r7, #32]\n\t"\
    "str r7, [r4, #0]\n\t"\
    )
#define STORE_CP_PTR_5 \
__asm__ __volatile__ (          \
    "ldr r6, =_idemStorePtr\n\t"\
    "ldr r7, [r7, #28]\n\t"\
    "str r7, [r6, #0]\n\t"\
    )
#define STORE_CP_PTR_6 \
__asm__ __volatile__ (          \
    "ldr r6, =_idemStorePtr\n\t"\
    "ldr r7, [r7, #24]\n\t"\
    "str r7, [r6, #0]\n\t"\
    )
#define STORE_CP_PTR_7 \
__asm__ __volatile__ (          \
    "ldr r6, =_idemStorePtr\n\t"\
    "ldr r7, [r7, #20]\n\t"\
    "str r7, [r6, #0]\n\t"\
    )
#define STORE_CP_PTR_8 \
__asm__ __volatile__ (          \
    "ldr r6, =_idemStorePtr\n\t"\
    "ldr r7, [r7, #20]\n\t"\
    "str r7, [r6, #0]\n\t"\
    )
#endif


#define RESTORE_REGS_0
#define RESTORE_REGS_1 RESTORE_REGS_0
#define RESTORE_REGS_2 RESTORE_REGS_0
#define RESTORE_REGS_3 RESTORE_REGS_0
#define RESTORE_REGS_4 RESTORE_REGS_0
#define RESTORE_REGS_5 RESTORE_REGS_0
#define RESTORE_REGS_6 RESTORE_REGS_0
#define RESTORE_REGS_7 \
__asm__ __volatile__ ( \
    "mov r6, r11\n\t"\
    )
#define RESTORE_REGS_8 \
__asm__ __volatile__ ( \
    "mov r6, r11\n\t"\
    "mov r7, r12\n\t"\
    )
#define RESTORE_REGS_RET \
__asm__ __volatile__ ( \
    "mov sp, r2\n\t"\
    )


#define CHECKPOINT_FUNC(nlive)  \
void _checkpoint_##nlive() {     \
  SAVE_REGS_##nlive; \
  LOAD_CP_PTR_##nlive;\
  SAVE_LOW_##nlive;\
  SAVE_SP_##nlive;\
  STORE_CP_PTR_##nlive;\
  RESTORE_REGS_##nlive; \
}

void _checkpoint_ret() {
  SAVE_REGS_RET;
  LOAD_CP_PTR_RET;  
  SAVE_LOW_RET;
  SAVE_SP_RET;
  STORE_CP_PTR_RET;
  RESTORE_REGS_RET;
}



//void _restore_checkpoint()
//{
//  __asm__ __volatile__(
//      "ldr r6, =_idemStorePtr\n\t"
//      "ldr r6, [r6, #0]\n\t"
//      "ldr r6, [r6, #60]\n\t"
//      "ldr r7, [r6, #0]\n\t"
//      "ldr r1, [r6, #4]\n\t"
//      "ldr r2, [r6, #8]\n\t"
//      "mov sp, r1\n\t"
//      "mov r12, r2\n\t"
//      "ldr r0, [r6, #12]\n\t"
//      "ldr r1, [r6, #16]\n\t"
//      "ldr r2, [r6, #20]\n\t"
//      "ldr r3, [r6, #24]\n\t"
//      "ldr r4, [r6, #28]\n\t"
//      "ldr r5, [r6, #32]\n\t"
//      "ldr r6, [r6, #36]\n\t"
//      "mov pc, r12\n\t"
//      );
//}


CHECKPOINT_FUNC(0);
CHECKPOINT_FUNC(1);
CHECKPOINT_FUNC(2);
CHECKPOINT_FUNC(3);
CHECKPOINT_FUNC(4);
CHECKPOINT_FUNC(5);
CHECKPOINT_FUNC(6);
CHECKPOINT_FUNC(7);
CHECKPOINT_FUNC(8);

//void _checkpoint()
//{
//  SAVE_REGS_8;
//  LOAD_CP_PTR_8;
//  SAVE_SP_8;
//  SAVE_LOW_8;
//  STORE_CP_PTR_8;
//  RESTORE_REGS_8; 
//}

#define UNDO_LOG_LENGTH 100

int _idemUndoLogLog[UNDO_LOG_LENGTH];
int *_idemUndoLogEnd = &_idemUndoLogLog[UNDO_LOG_LENGTH-1];
int *_idemUndoLog[2] = {_idemUndoLogLog, &_idemUndoLogLog[UNDO_LOG_LENGTH-1]};

//int _undo_log(void * addr)
//{
//  _idemUndoLogLog[0] = (int *) addr;
//  _idemUndoLogLog[1] = *addr;
//  &_idemUndoLogLog[0] = &(_idemUndoLogLog[2]);
//  if (_idemUndoLogLog >= _idemUndoLogEnd)
//    _checkpoint_8();
//}
