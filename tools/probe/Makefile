#-----------------------------------------------------------------------
# Standalone process-probe NRO (diagnostic only, not part of the product).
#   make -f tools/probe/Makefile    -> tools/probe/ra-probe.nro
#-----------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR  := $(CURDIR)
TARGET  := ra-probe

TOOLCHAIN := $(DEVKITPRO)/devkitA64
PREFIX    := $(TOOLCHAIN)/bin/aarch64-none-elf-
CC        := $(PREFIX)gcc

ARCH    := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -Wall -Wextra -O2 $(ARCH) -D__SWITCH__
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH)
LIBS    := -lnx
LIBNX   := $(DEVKITPRO)/libnx

ELF2NRO  := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool

all: $(TARGET).nro

$(TARGET).nacp:
	$(NACPTOOL) --create "ra-probe" "cheasan" "1.0" $@

$(TARGET).elf: probe.c
	$(CC) $(CFLAGS) $(LDFLAGS) -I$(LIBNX)/include $< $(LIBS) -L$(LIBNX)/lib -o $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	$(ELF2NRO) $< $@ --nacp=$(word 2,$^)

clean:
	@rm -f $(TARGET).elf $(TARGET).nro $(TARGET).nacp
