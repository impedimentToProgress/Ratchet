# Vectors must be first so that it gets loaded at address 0x0
# Must not use -nostdlib, otherwise math funcs will disappear,
# but this may cause problems with malloc and free
#LLVMOBJSDIR = /opt/llvm/Debug+Asserts
LLVMOBJSDIR = /opt/llvm
CC  = $(LLVMOBJSDIR)/bin/clang

ifeq ($(OPTLVL), '')
	OPTLVL=-O2
endif

ARMGNU = arm-none-eabi
CLANGFLAGS = -DBARE_METAL -Wall $(OPTLVL) -target $(ARMGNU) -mcpu=cortex-m0 -mthumb --specs=nosys.specs -nostartfiles -ffreestanding -std=c99 -fomit-frame-pointer -fno-optimize-sibling-calls -fno-builtin
LIBS = -lnosys -lc -lm
LOADIDEM = -Xclang -mllvm -Xclang -idempotence-construction=size #-Xclang -mllvm -Xclang --debug-pass=Structure -Xclang -load -Xclang $(IDEMPASSOBJ)

FILES = absvdi2.o absvsi2.o absvti2.o adddf3.o addsf3.o addtf3.o addvdi3.o addvsi3.o addvti3.o apple_versioning.o ashldi3.o ashlti3.o ashrdi3.o ashrti3.o assembly.h clzdi2.o clzsi2.o clzti2.o cmpdi2.o cmpti2.o comparedf2.o comparesf2.o comparetf2.o ctzdi2.o ctzsi2.o ctzti2.o divdf3.o divdi3.o divmoddi4.o divmodsi4.o divsi3.o divsf3.o divtf3.o divti3.o extenddftf2.o extendsfdf2.o extendsftf2.o ffsdi2.o ffsti2.o fixdfdi.o fixdfsi.o fixdfti.o fixsfdi.o fixsfsi.o fixsfti.o fixunsdfdi.o fixunsdfsi.o fixunsdfti.o fixunssfdi.o fixunssfsi.o fixunssfti.o fixunsxfdi.o fixunsxfsi.o fixunsxfti.o fixxfdi.o fixxfti.o floatdidf.o floatdisf.o floatdixf.o floatsidf.o floatsisf.o floatsitf.o floattidf.o floattisf.o floattixf.o floatundidf.o floatundisf.o floatundixf.o floatunsidf.o floatunsisf.o floatunsitf.o floatuntidf.o floatuntisf.o floatuntixf.o fp_add_impl.inc fp_extend.h fp_extend_impl.inc fp_lib.h fp_mul_impl.inc fp_trunc.h fp_trunc_impl.inc int_endianness.h int_lib.h int_math.h int_types.h int_util.o int_util.h lshrdi3.o lshrti3.o moddi3.o modsi3.o modti3.o muldc3.o muldf3.o muldi3.o mulodi4.o mulosi4.o muloti4.o mulsc3.o mulsf3.o multf3.o multi3.o mulvdi3.o mulvsi3.o mulvti3.o negdf2.o negdi2.o negsf2.o negti2.o negvdi2.o negvsi2.o negvti2.o paritydi2.o paritysi2.o parityti2.o popcountdi2.o popcountsi2.o popcountti2.o powidf2.o powisf2.o powitf2.o powixf2.o subdf3.o subtf3.o subvdi3.o subvsi3.o subvti3.o trampoline_setup.o truncdfsf2.o trunctfdf2.o trunctfsf2.o ucmpdi2.o ucmpti2.o udivdi3.o udivmoddi4.o udivmodsi4.o udivmodti4.o udivsi3.o udivti3.o umoddi3.o umodsi3.o umodti3.o aeabi_idivmod.o aeabi_uidivmod.o aeabi_memcpy.o


INC = assembly.h fp_extend.h fp_lib.h fp_trunc.h int_endianness.h int_lib.h int_math.h int_types.h int_util.h idem.h

ifeq ($(NOIDEMCOMP), 1)
	LOADIDEM=""
	INSTALLDIR="/opt/arm-newlib/noidem/arm-none-eabi/lib"
else
	LOADIDEM=-Xclang -mllvm -Xclang -no-stack-slot-sharing -Xclang -mllvm -Xclang -idempotence-construction=speed#-Xclang -load -Xclang $(IDEMPASSOBJ)
	INSTALLDIR="/opt/arm-newlib/idem/arm-none-eabi/lib"
endif


all: $(FILES) 
	$(ARMGNU)-ar rcs libbuiltins.a $(FILES)

int_util.o: int_util.c
	$(CC) $(LOADIDEM) -D__APPLE__ -D__LITTLE_ENDIAN__=1 -I/opt/arm-newlib/arm-none-eabi/include $(CLANGFLAGS) -o $@ $<

%.o: %.c int_util.o
	$(CC) $(LOADIDEM) -D__APPLE__ -D__LITTLE_ENDIAN__=1 ${CLANGFLAGS} -c -o $@ $< int_util.o 

%.o: arm/%.S 
	$(CC) $(CLANGFLAGS) -D__ELF__ -D__arm__ -c -o $@ $< 
	#/opt/gcc-$(ARMGNU)/bin/$(ARMGNU)-as -o $@ $< 

install:
	cp libbuiltins.a $(INSTALLDIR) 

clean:
	rm -rf *.o *~
	rm -f tmp/*
