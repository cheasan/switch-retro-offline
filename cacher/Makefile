#-----------------------------------------------------------------------
# cacher/Makefile - ra-offline cacher, a homebrew app (.nro).
#
# One button caches every game on the memory card for offline play.
# Shares the cache + transport code with the sysmodule, and hashes
# games with rcheevos (third_party/rcheevos), like RetroArch does.
#
#   make -C cacher          -> cacher/ra-offline-cacher.nro
#-----------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR   := $(abspath ..)
TARGET   := ra-offline-cacher
BUILD    := build
VERSION  := 0.4.5
ICON     := $(TOPDIR)/assets/icon.jpg
RCHEEVOS := $(TOPDIR)/third_party/rcheevos

TOOLCHAIN := $(DEVKITPRO)/devkitA64
PREFIX    := $(TOOLCHAIN)/bin/aarch64-none-elf-
CC        := $(PREFIX)gcc

ARCH    := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -O2 -ffunction-sections $(ARCH) -D__SWITCH__ -DRA_NRO
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
           -Wl,-Map,$(TARGET).map

LIBS    := -lnx
LIBDIRS := $(DEVKITPRO)/portlibs/switch $(DEVKITPRO)/libnx

INCLUDE := -Iinclude -I$(TOPDIR)/include -I$(RCHEEVOS)/include \
           $(foreach dir,$(LIBDIRS),-I$(dir)/include)
LIBPATHS:= $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ELF2NRO  := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool

# Own code (warnings on) and shared/vendored code.
APP_SRC    := $(wildcard source/*.c)
SHARED_SRC := $(addprefix $(TOPDIR)/source/, ra_cache.c ra_util.c \
              json_lite.c ra_http_resp.c ra_transport_switch.c)
RHASH_SRC  := $(wildcard $(RCHEEVOS)/src/rhash/*.c) $(RCHEEVOS)/src/rc_compat.c

APP_OBJ    := $(patsubst source/%.c,$(BUILD)/app/%.o,$(APP_SRC))
SHARED_OBJ := $(patsubst $(TOPDIR)/source/%.c,$(BUILD)/shared/%.o,$(SHARED_SRC))
RHASH_OBJ  := $(patsubst $(RCHEEVOS)/src/%.c,$(BUILD)/rcheevos/%.o,$(RHASH_SRC))
OFILES     := $(APP_OBJ) $(SHARED_OBJ) $(RHASH_OBJ)

.PHONY: all clean
all: $(TARGET).nro

$(BUILD)/app/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra $(INCLUDE) -MMD -MP -c $< -o $@

$(BUILD)/shared/%.o: $(TOPDIR)/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra $(INCLUDE) -MMD -MP -c $< -o $@

$(BUILD)/rcheevos/%.o: $(RCHEEVOS)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w $(INCLUDE) -I$(RCHEEVOS)/src -MMD -MP -c $< -o $@

$(TARGET).nacp: $(ICON)
	$(NACPTOOL) --create "ra-offline cacher" "cheasan" "$(VERSION)" $@

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $^ $(LIBS) $(LIBPATHS) -o $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	$(ELF2NRO) $< $@ --nacp=$(word 2,$^) --icon=$(ICON)

clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp $(TARGET).map

-include $(OFILES:.o=.d)
