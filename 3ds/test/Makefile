ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
CFLAGS := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ -I$(DEVKITPRO)/libctru/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(DEVKITPRO)/libctru/lib
all: t1.elf t2.elf t3.elf t1.smdh t2.smdh t3.smdh
t%.elf: test.c
	$(CC) $(CFLAGS) -DLEVEL=$* test.c $(LDFLAGS) -lctru -lm -o $@
t%.smdh: ../icon.png
	smdhtool --create "LZ Test $*" "boot test" "Axiom Games" ../icon.png $@
