ARMGNU=arm-none-eabi
LLVMOBJS=/opt/llvm
CC="$LLVMOBJS/bin/clang"
AS=$ARMGNU-as
CCARM="$CC --target=thumbv6-none-eabi -mthumb"

ARMARCH="-D__ARM_ARCH_6M__"
CCASFLAGS="--target=thumbv6-none-eabi $ARMARCH"

if [ "$OPTLVL" -eq '' ]; then
	OPTLVL=-O2
fi

if [ "$NOIDEMCOMP" -eq 1 ]; then 
  IDEMOP=""
  PREFIX="/opt/arm-newlib/noidem"
else
  IDEMOP="-Xclang -mllvm -Xclang -no-stack-slot-sharing -Xclang -mllvm -Xclang -idempotence-construction=speed"
  PREFIX="/opt/arm-newlib/idem"
fi

CFLAGS="$OPTLVL $ARMARCH $IDEMOP -ffreestanding -D__SINGLE_THREAD__"

TARGET=arm-none-eabi
HOST=x86_64-apple-darwin13.4.0
OPTIONS="--with-newlib"
#OPTIONS="--disable-newlib-supplied-syscalls --with-newlib"

## Get newlib
#wget ftp://sourceware.org/pub/newlib/newlib-2.2.0.tar.gz
#tar xvf newlib-2.2.0.tar.gz
#
## Patch newlib
#cd newlib-2.2.0/newlib/libc/machine/arm
#patch < ../../../../../newlib.patch
#cd - 
#cd newlib-2.2.0/newlib/libc/string
#patch -R < ../../../../newlib.patch2
#cd -

mkdir build; cd build
export CCASFLAGS=$CCASFLAGS
CC_FOR_TARGET=$CCARM AS_FOR_TARGET=$ARMGNU-as target_configargs="--target=$ARMGNU" CC=$CC CXX=$CC++ CCAS=$ARMGNU-as AS=$ARMGNU-as AR=$ARMGNU-ar CFLAGS=$CFLAGS CCASFLAGS=$CCASFLAGS ../newlib-2.2.0/configure --prefix=$PREFIX --build=$TARGET --host=$TARGET --target=$TARGET $OPTIONS
make -j8 
