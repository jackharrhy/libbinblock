.SUFFIXES:
.SECONDARY:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC to the devkitPPC toolchain root")
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO to the devkitPro root")
endif

include $(DEVKITPRO)/libogc2/wii_rules

TARGET := binblock-wii-demo
BUILD := build
SOURCES := source

BINBLOCK_ROOT ?= ../../..
BINBLOCK_BUILD ?= $(BINBLOCK_ROOT)/.build/wii-libogc2

CFLAGS := -g -O2 -Wall -Wextra -Werror $(MACHDEP) $(INCLUDE)
LDFLAGS := -g $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS := -lbinblock-wii -lbinblock -lwiiuse -lbte -logc -lm

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export BINBLOCK_ROOT := $(abspath $(BINBLOCK_ROOT))
export BINBLOCK_BUILD := $(abspath $(BINBLOCK_BUILD))

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export LD := $(CC)
export INCLUDE := -I$(BINBLOCK_ROOT)/include \
	-I$(BINBLOCK_ROOT)/integrations/wii/include \
	-I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS := -L$(BINBLOCK_BUILD) -L$(LIBOGC_LIB)

.PHONY: all clean run $(BUILD)
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

run:
	wiiload $(OUTPUT).dol

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
