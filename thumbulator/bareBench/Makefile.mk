LLVMOBJSDIR = /opt/llvm
CC = /opt/llvm/bin/clang

ifeq ($(OPTLVL), '')
	OPTLVL=-O0
endif

ARMGNU = arm-none-eabi
CLANGFLAGS = -DBARE_METAL -Wall $(OPTLVL) -target $(ARMGNU) -mcpu=cortex-m0 -mthumb --specs=nosys.specs -nostartfiles -ffreestanding -std=c99 -fomit-frame-pointer -fno-optimize-sibling-calls #-static
LIBS = --start-group -lm -lc -lbuiltins --end-group 

ifeq ($(NOIDEMCOMP), 1)
	ARGS=
	LINKDIR=-L/opt/arm-newlib/noidem/arm-none-eabi/lib
	INCLIB = -I/opt/arm-newlib/noidem/arm-none-eabi/include 
	#OBJS := iv.o checkpoint.o $(OBJS)
	OBJS := v.o $(OBJS)
else
	#LOADIDEM=-Xclang -mllvm -Xclang -no-stack-slot-sharing -Xclang -mllvm -Xclang -idempotence-construction=size #-Xclang -load -Xclang $(IDEMPASSOBJ)
	ARGS= -no-stack-slot-sharing -idempotence-construction=speed
	LINKDIR=-L/opt/arm-newlib/idem/arm-none-eabi/lib
	INCLIB = -I/opt/arm-newlib/idem/arm-none-eabi/include 
	OBJS := iv.o checkpoint.o $(OBJS)
endif

llvm-arg = -Xclang -mllvm -Xclang $(1)
LLVMARGS = $(foreach ARG, $(ARGS), $(call llvm-arg,$(ARG)))

all: main.elf 

iv.o: ../idemvectors.s
	/opt/gcc-$(ARMGNU)/bin/$(ARMGNU)-as $(ARMFLAGS) ../idemvectors.s -o iv.o 

v.o: ../vectors.s
	/opt/gcc-$(ARMGNU)/bin/$(ARMGNU)-as $(ARMFLAGS) ../vectors.s -o v.o 

checkpoint.o: ../checkpoint.c
	$(CC) $(CLANGFLAGS) -c ../checkpoint.c -o checkpoint.o 

%.o: %.c
	$(CC) $(LLVMARGS) $(CLANGFLAGS) $(INCLIB) -c -o $@ $< 

%.o: ../%.c
	$(CC) $(LLVMARGS) $(CLANGFLAGS) $(INCLIB) -c -o $@ $< 

main.elf: $(OBJS) 
	/opt/gcc-$(ARMGNU)/bin/$(ARMGNU)-ld -T ../memmap $(LINKDIR) $(OBJS) -o main.elf $(LIBS)
	/opt/gcc-$(ARMGNU)/bin/$(ARMGNU)-objdump -D main.elf > main.lst
	/opt/gcc-$(ARMGNU)/bin/$(ARMGNU)-objcopy main.elf main.bin -O binary

overhead: main.elf
	make clean; NOIDEMCOMP=0 make all
	../../sim_main main.bin | grep "Program exit after [0-9]*" | awk '{print $$4}' > tmp.txt
	stat -x main.elf | grep "Size: [0-9]*" | awk '{print $$2}' >> tmp.txt
	make clean; NOIDEMCOMP=1 make all
	../../sim_main main.bin | grep "Program exit after [0-9]*" | awk '{print $$4}' >> tmp.txt
	stat -x main.elf | grep "Size: [0-9]*" | awk '{print $$2}' >> tmp.txt
	cat tmp.txt


clean:
	rm -rf main.llvmout *.o *.elf output* *.lst *.bin *~
	rm -f tmp/*
