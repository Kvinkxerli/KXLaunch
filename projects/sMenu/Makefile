#---------------------------------------------------------------------------------
# sMenu Makefile - builds Nintendo Switch library applet NSO
# Output: ../../SdOut/slaunch/bin/sMenu/main  (NSO)
#---------------------------------------------------------------------------------

TARGET   := sMenu
BUILD    := build

SOURCES  := source source/sl/menu/ui source/sl/menu/gfx source/sl/menu/smi \
            source/sl/menu/net source/sl/menu/widgets source/sl/menu/audio \
            source/sl/menu/hb source/sl/menu/play source/sl/menu/dbg \
            source/sl/menu/news source/sl/menu/cfg source/sl/menu/usb

INCLUDES := include ../../libs/sCommon/include

COMMON_SRC := ../../libs/sCommon/source/sl/os/Applications.cpp

#---------------------------------------------------------------------------------
# SDL2 (+ ttf/image/mixer) flags for Switch
SDL_CFLAGS := -I$(DEVKITPRO)/portlibs/switch/include/SDL2 \
              -I$(DEVKITPRO)/portlibs/switch/include \
              -D__SWITCH__ -D_REENTRANT

SDL_LIBS   := -L$(DEVKITPRO)/portlibs/switch/lib \
              -lSDL2_image -ljpeg -lwebpdemux -lwebp -lSDL2_ttf \
              -lSDL2_mixer -lmpg123 -lvorbisidec -logg -lopusfile -lopus -lmodplug -lFLAC \
              -lSDL2 -L$(DEVKITPRO)/libnx/lib \
              -lEGL -lglapi -ldrm_nouveau -lharfbuzz -lfreetype -lbz2 -lpng16 -lz -lm -lnx

CURL_CFLAGS := -I$(DEVKITPRO)/portlibs/switch/include
CURL_LIBS   := -L$(DEVKITPRO)/portlibs/switch/lib -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

FFMPEG_PREFIX := $(DEVKITPRO)/FFmpegLibs
FFMPEG_CFLAGS := -I$(FFMPEG_PREFIX)/include
FFMPEG_LIBS   := -L$(FFMPEG_PREFIX)/lib -lavformat -lavcodec -lavutil

# Output path on SD layout
SDOUT  := ../../SdOut
OUTDIR := $(SDOUT)/slaunch/bin/sMenu

#---------------------------------------------------------------------------------
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE   := sMenu
APP_AUTHOR  := kxlaunch
APP_VERSION := 1.6.0

#---------------------------------------------------------------------------------
INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
            -I$(DEVKITPRO)/libnx/include -I$(DEVKITPRO)/portlibs/switch/include

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS    = -g -Wall -O3 -ftree-vectorize -ffunction-sections -fdata-sections -fvisibility=default $(ARCH) \
            $(INCLUDE) $(SDL_CFLAGS) $(CURL_CFLAGS) $(FFMPEG_CFLAGS) \
            -DSL_VERSION=\"$(APP_VERSION)\" -DSL_HAS_VIDEO=1 -DSL_HAS_NVTEGRA=1

CXXFLAGS  = $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions

ASFLAGS  := -g $(ARCH)

LDFLAGS  := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
            -Wl,-Map,$(BUILD)/$(TARGET).map \
            -Wl,--unresolved-symbols=ignore-in-shared-libs

LIBS     := $(CURL_LIBS) $(FFMPEG_LIBS) $(SDL_LIBS) -lnx

LIBDIRS  := $(PORTLIBS) $(LIBNX) $(FFMPEG_PREFIX)

#---------------------------------------------------------------------------------
CFILES   := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))
CPPFILES := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))

COMMON_OBJ := $(patsubst %.cpp,$(BUILD)/sCommon/%.o,$(notdir $(COMMON_SRC)))
SFILES   := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.s))
OFILES   := $(patsubst %.cpp,$(BUILD)/%.o,$(CPPFILES)) $(COMMON_OBJ) \
            $(patsubst %.c,$(BUILD)/%.o,$(CFILES)) \
            $(patsubst %.s,$(BUILD)/%.o,$(SFILES))

INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
            -I$(LIBNX)/include -I$(PORTLIBS)/include

VPATH    := $(SOURCES) ../../libs/sCommon/source/sl/os

#---------------------------------------------------------------------------------
.PHONY: all clean install

all: $(OUTDIR)/main $(OUTDIR)/main.npdm

$(OUTDIR)/main: $(BUILD)/$(TARGET).nso | $(OUTDIR)
	@cp $< $@
	@echo "NSO installed to $@"

$(OUTDIR)/main.npdm: sMenu.json | $(OUTDIR)
	@npdmtool $< $@ > /dev/null 2>&1 || npdmtool $< $@
	@echo "NPDM installed to $@"

$(BUILD)/$(TARGET).nso: $(BUILD)/$(TARGET).elf
	@elf2nso $< $@

$(BUILD)/$(TARGET).elf: $(OFILES) | $(BUILD)
	@$(CXX) $(LDFLAGS) -o $@ $^ $(foreach lib,$(LIBDIRS),-L$(lib)/lib) $(LIBS)
	@$(NM) -CSn $@ > $(BUILD)/$(TARGET).lst

$(BUILD)/sCommon/%.o: ../../libs/sCommon/source/sl/os/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	@mkdir -p $@

$(OUTDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(OUTDIR)/main $(OUTDIR)/main.npdm

-include $(OFILES:.o=.d)