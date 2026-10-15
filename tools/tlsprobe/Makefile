# tlsprobe: throwaway HTTPS probe for the 3DS (docs/ONLINE_SPRITE_SHARING.md §3).
# Build in the devkitARM container: tools/tlsprobe/build.sh

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET   := tlsprobe
BUILD    := build
SOURCES  := source
ROMFS    := romfs
APP_TITLE := tlsprobe
APP_DESCRIPTION := HTTPS probe for emerald3ds online sharing
APP_AUTHOR := emerald3ds

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections $(ARCH) -D__3DS__ -I$(CTRULIB)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS    := -lctru -lm
LIBDIRS := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON := $(CTRULIB)/default_icon.png
export _3DSXFLAGS := --romfs=$(CURDIR)/$(ROMFS) --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: $(BUILD) clean

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
