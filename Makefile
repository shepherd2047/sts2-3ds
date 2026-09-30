#---------------------------------------------------------------------------------
# 3DS build (devkitPro). `make` produces sts2-3ds.3dsx for the Homebrew Launcher.
# Run `python3 tools/build_assets.py` first to create romfs/ and icon.png from
# your own copy of the game.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := sts2-3ds
BUILD       := build3ds
SOURCES     := source source/core source/ui source/ui/screens source/spine source/gfx source/audio source/platform_3ds
DATA        :=
INCLUDES    := source
GRAPHICS    :=
ROMFS       := romfs_3ds

APP_TITLE       := Slay the Spire 2
APP_DESCRIPTION := Unofficial fan port for the New 3DS (personal use)
APP_AUTHOR      := Personal fan port - not by Mega Crit
ICON            := icon.png

# `make cia` (H4): HOME-menu banner + installable title. Title id 000400000FA57200
# (UniqueId 0xFA572, set in tools/sts2-3ds.rsf with the memory mode). banner.png /
# banner.wav / icon.png come from tools/build_assets.py (--packaging does just those).
# bannertool and makerom must be on PATH (Mac: ~/.local/bin, see CLAUDE.md).
BANNERTOOL ?= bannertool
MAKEROM    ?= makerom
RSF        := tools/sts2-3ds.rsf

#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS   += $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions -Wno-missing-field-initializers

ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS  := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
PICAFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export LD := $(CXX)
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES_BIN := $(PICAFILES:.v.pica=.shbin.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(PICAFILES:.v.pica=_shbin.h)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)
export APP_ICON := $(TOPDIR)/$(ICON)
ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(OUTPUT).smdh
endif
ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean link cia

# romfs_3ds/ = romfs/ with the textures converted to GPU formats (tools/compress_romfs.py).
PYTHON ?= $(shell for p in python3 python; do $$p -c 'import numpy, PIL' >/dev/null 2>&1 && { echo $$p; break; }; done)

all: $(BUILD)
	@if [ -n "$(PYTHON)" ]; then $(PYTHON) tools/compress_romfs.py || exit 1; 	elif [ -d romfs_3ds ]; then echo "warning: no Python with Pillow+numpy here, romfs_3ds may be stale (run tools/compress_romfs.py, or PYTHON=...)"; 	else echo "romfs_3ds missing: run python3 tools/compress_romfs.py first"; exit 1; fi
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Send to a 3DS waiting in Homebrew Launcher (press Y). hbmenu saves it to
# sdmc:/3ds/ and runs it. IP=192.168.x.x if auto-discovery fails.
link: all
	$(DEVKITPRO)/tools/bin/3dslink $(if $(IP),-a $(IP)) $(TARGET).3dsx

# Installable .cia (FBI / Azahar: File -> Install CIA). Reuses the .elf, .smdh and romfs_3ds/
# of `make`; the banner art and sound are regenerated only when missing.
cia: all
	@command -v $(BANNERTOOL) >/dev/null || { echo "bannertool not found (see CLAUDE.md, Build & test)"; exit 1; }
	@command -v $(MAKEROM) >/dev/null || { echo "makerom not found (see CLAUDE.md, Build & test)"; exit 1; }
	@if [ ! -f banner.png ] || [ ! -f banner.wav ]; then $(PYTHON) tools/build_assets.py --packaging || exit 1; fi
	@$(BANNERTOOL) makebanner -i banner.png -a banner.wav -o $(BUILD)/banner.bnr >/dev/null
	@$(MAKEROM) -f cia -o $(TARGET).cia -target t -exefslogo -rsf $(RSF) -elf $(TARGET).elf \
		-icon $(TARGET).smdh -banner $(BUILD)/banner.bnr -DAPP_ROMFS=$(ROMFS) -major 1 -minor 0 -micro 0
	@echo built ... $(TARGET).cia

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).cia $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf  : $(OFILES)
$(OFILES_SOURCES) : $(HFILES)

# GPU shaders: picasso assembles, bin2s embeds, plus a header with the symbols.
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' -e 's/[^A-Za-z0-9_]/_/g')`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' -e 's/[^A-Za-z0-9_]/_/g')`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' -e 's/[^A-Za-z0-9_]/_/g')`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
