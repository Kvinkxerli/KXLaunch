#---------------------------------------------------------------------------------
# sInstaller Makefile - builds the sLaunch installer NRO.
# It bundles the built SD payload (from ../../SdOut) into its romfs and copies
# it onto the SD card, so no network/release is needed to install.
#---------------------------------------------------------------------------------

TARGET   := KXLaunch-Installer
BUILD    := build
SOURCES  := source
INCLUDES := include

SDOUT    := ../../SdOut
OUTDIR   := $(SDOUT)/switch/KXLaunch
ROMFS    := romfs

#---------------------------------------------------------------------------------
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE   := KXLaunch Installer
APP_AUTHOR  := Kvinkxerli
APP_VERSION := 1.6.0
APP_ICON    := ../../assets/icon.jpg

SDL_CFLAGS := -I$(DEVKITPRO)/portlibs/switch/include/SDL2 \
              -I$(DEVKITPRO)/portlibs/switch/include \
              -D__SWITCH__ -D_REENTRANT

SDL_LIBS   := -L$(DEVKITPRO)/portlibs/switch/lib \
              -lSDL2_image -ljpeg -lwebp -lSDL2_ttf \
              -lSDL2 -L$(DEVKITPRO)/libnx/lib \
              -lEGL -lglapi -ldrm_nouveau -lharfbuzz -lfreetype -lbz2 -lpng16 -lz -lm -lnx

CURL_CFLAGS := -I$(DEVKITPRO)/portlibs/switch/include
CURL_LIBS   := -L$(DEVKITPRO)/portlibs/switch/lib -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS    = -g -Wall -O2 -ffunction-sections $(ARCH) \
            $(INCLUDE) $(SDL_CFLAGS) $(CURL_CFLAGS) -D__SWITCH__ -DSL_VERSION=\"$(APP_VERSION)\"

CXXFLAGS  = $(CFLAGS) -std=c++20 -fno-rtti -fno-exceptions

LDFLAGS  := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
            -Wl,-Map,$(BUILD)/$(TARGET).map

# Network libs first (curl -> mbedtls -> z), minizip for extracting the update
# zip (needs -lz after it), then SDL.
LIBS     := -L$(DEVKITPRO)/portlibs/switch/lib -lminizip $(CURL_LIBS) $(SDL_LIBS) -lz

LIBDIRS  := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
CPPFILES := $(wildcard $(SOURCES)/*.cpp)
OFILES   := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(CPPFILES)))

INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
            -I$(LIBNX)/include -I$(PORTLIBS)/include

VPATH    := $(SOURCES)

#---------------------------------------------------------------------------------
.PHONY: all clean romfs_stage nxlink

all: romfs_stage $(OUTDIR)/$(TARGET).nro

# Stage the SD payload (everything except the installer itself) + the logo into
# the NRO's romfs.
romfs_stage:
	@echo "--- Staging installer payload ---"
	@rm -rf $(ROMFS)
	@mkdir -p $(ROMFS)/payload
	@cp -r $(SDOUT)/atmosphere $(ROMFS)/payload/ 2>/dev/null || true
	@cp -r $(SDOUT)/slaunch    $(ROMFS)/payload/ 2>/dev/null || true
	@cp ../../icon.png $(ROMFS)/icon.png 2>/dev/null || true
	@# Boot splash, already in Atmosphere's package3 layout (see InstallSplash).
	@cp -f ../../assets/splash.bin $(ROMFS)/splash.bin 2>/dev/null || true
	@# Line art for the installer's own action tiles. quit.png stopped being
	@# used when Exit moved off the tile row onto Plus, so it's not staged.
	@mkdir -p $(ROMFS)/ui
	@cp -f ../../assets/install.png ../../assets/update.png \
	       ../../assets/disable.png ../../assets/uninstall.png $(ROMFS)/ui/ 2>/dev/null || true
	@# Sprites for the Installing / Checking-for-updates screen animations.
	@cp -f ../../assets/anim/sdcard.png ../../assets/anim/switch.png ../../assets/anim/github.png \
	       $(ROMFS)/ui/ 2>/dev/null || true

# Build the NRO with romfs + icon. romfs_stage is a prerequisite (not just an
# earlier goal in `all`) so the NRO is rebuilt whenever the staged payload
# changes. Without it make saw only the .elf and .nacp, and an assets-only
# change - a new font, a new locale, a rebuilt sMenu - left the installer
# quietly shipping the previous payload.
$(OUTDIR)/$(TARGET).nro: romfs_stage $(BUILD)/$(TARGET).nacp $(BUILD)/$(TARGET).elf | $(OUTDIR)
	@elf2nro $(BUILD)/$(TARGET).elf $@ --nacp=$(BUILD)/$(TARGET).nacp \
	    --romfsdir=$(ROMFS) \
	    $(if $(wildcard $(APP_ICON)),--icon=$(APP_ICON),)
	@echo "NRO: $@"

$(BUILD)/$(TARGET).nacp: | $(BUILD)
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(BUILD)/$(TARGET).elf: $(OFILES) | $(BUILD)
	@$(CXX) $(LDFLAGS) -o $@ $^ \
	    $(foreach lib,$(LIBDIRS),-L$(lib)/lib) $(LIBS)

$(BUILD)/%.o: %.cpp | $(BUILD)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	@mkdir -p $@

$(OUTDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(ROMFS) $(OUTDIR)/$(TARGET).nro

nxlink: all
	@nxlink -s $(OUTDIR)/$(TARGET).nro $(if $(SWITCH_IP),-a $(SWITCH_IP),)

-include $(OFILES:.o=.d)
