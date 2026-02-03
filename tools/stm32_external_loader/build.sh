#!/bin/bash
# Build IS25LP064A external loader for STM32CubeProgrammer
# Output: IS25LP064A_DaisySeed.stldr
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Find ARM GCC toolchain
if command -v arm-none-eabi-gcc &>/dev/null; then
    GCC=arm-none-eabi-gcc
    OBJCOPY=arm-none-eabi-objcopy
    SIZE=arm-none-eabi-size
elif [ -d "/Applications/ArmGNUToolchain" ]; then
    # macOS ARM toolchain
    TOOLCHAIN=$(ls -d /Applications/ArmGNUToolchain/*/arm-none-eabi 2>/dev/null | tail -1)
    GCC="$TOOLCHAIN/bin/arm-none-eabi-gcc"
    OBJCOPY="$TOOLCHAIN/bin/arm-none-eabi-objcopy"
    SIZE="$TOOLCHAIN/bin/arm-none-eabi-size"
else
    echo "ERROR: arm-none-eabi-gcc not found"
    exit 1
fi

echo "Using: $GCC"
echo "Building IS25LP064A external loader..."

CFLAGS="-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16"
CFLAGS="$CFLAGS -Os -ffunction-sections -fdata-sections"
CFLAGS="$CFLAGS -Wall -std=c11"
CFLAGS="$CFLAGS -DSTM32H750xx"

LDFLAGS="-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16"
LDFLAGS="$LDFLAGS -T loader.ld -nostartfiles -nodefaultlibs"
LDFLAGS="$LDFLAGS -Wl,--gc-sections"
LDFLAGS="$LDFLAGS -Wl,--undefined=Init -Wl,--undefined=Write"
LDFLAGS="$LDFLAGS -Wl,--undefined=SectorErase -Wl,--undefined=MassErase"
LDFLAGS="$LDFLAGS -Wl,--undefined=Verify -Wl,--undefined=CheckSum"

OUTPUT="IS25LP064A_DaisySeed"

$GCC $CFLAGS -c Dev_Inf.c -o Dev_Inf.o
$GCC $CFLAGS -c Loader_Src.c -o Loader_Src.o
$GCC $LDFLAGS Dev_Inf.o Loader_Src.o -o "$OUTPUT.elf"

$SIZE "$OUTPUT.elf"

# .stldr is just the .elf renamed
cp "$OUTPUT.elf" "$OUTPUT.stldr"

echo ""
echo "Output: $SCRIPT_DIR/$OUTPUT.stldr"
echo "Copy to: STM32CubeProgrammer/bin/ExternalLoader/"
