TARGET := lazarus3ds
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
CFLAGS := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ -I$(DEVKITPRO)/libctru/include -I$(DEVKITPRO)/portlibs/3ds/include -Isource
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(DEVKITPRO)/libctru/lib -L$(DEVKITPRO)/portlibs/3ds/lib
LIBS := -lcitro2d -lcitro3d -lctru -lm
SRC := $(wildcard source/*.c)
OBJ := $(SRC:.c=.o)
T3S := $(wildcard gfx/*.t3s)
T3X := $(patsubst gfx/%.t3s,romfs/gfx/%.t3x,$(T3S))

all: $(TARGET).3dsx

source/%.o: source/%.c source/data.h
	$(CC) $(CFLAGS) -c $< -o $@

romfs/gfx/%.t3x: gfx/%.t3s
	@mkdir -p romfs/gfx
	cd gfx && tex3ds -i $(notdir $<) -o ../$@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(TARGET).smdh: icon.png
	smdhtool --create "Lazarus" "Colony builder" "Axiom Games" icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh $(T3X)
	3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh --romfs=romfs

clean:
	rm -rf $(OBJ) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh romfs/gfx
