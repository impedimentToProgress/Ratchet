FILES = main.c
OBJS = main.o

all: main.elf

main.o: main.c
	arm-none-eabi-gcc -fomit-frame-pointer -mthumb -c main.c -o main.o

include ../Makefile.mk
