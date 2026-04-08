#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source source/system source/system/util source/system/draw
DATA		:=	data
INCLUDES	:=	include library/include
GRAPHICS	:=	gfx
ROMFS		:=	romfs
GFXBUILD	:=	$(ROMFS)/gfx
#---------------------------------------------------------------------------------
APP_VER				:= 16
APP_TITLE			:= topos
APP_DESCRIPTION		:= topos_video player for 3ds_v0.1
APP_AUTHOR			:= mock
PRODUCT_CODE		:= CTR-Vid
UNIQUE_ID			:= 0xEC2B3

BANNER_AUDIO_SRC	:= resource/banner.wav
# 默认：makebanner 直接 -a WAV（与英文教程一致；bannertool 内部转 CWAV，loop=0）。
# 若要对照旧流程：make BANNER_USE_LOOP_CWAV=1（先 makecwav -l true 再 -ca）
BANNER_USE_LOOP_CWAV	?= 0
# 默认 3D 横幅 hopf_spiral.cgfx（bannertool -ci，内部 LZ11 一次）。要 2D+易出声：make BANNER_USE_FLAT_PNG=1
BANNER_USE_FLAT_PNG	?= 0
ifeq ($(BANNER_USE_FLAT_PNG),1)
BANNER_IMAGE		:= resource/banner.png
else
BANNER_IMAGE		:= resource/banner_model/hopf_spiral.cgfx
endif
ICON				:= resource/icon.png
RSF_PATH			:= resource/app.rsf

#---------------------------------------------------------------------------------
#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH		:= -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# We don't use :
# -Wfloat-equal -Wunsuffixed-float-constants -Wpacked -Wpadded -Waggregate-return -Wdate-time -Wconversion -Wcast-align
# -Wbad-function-cast -Wtraditional -Wstrict-overflow=5 -Wuseless-cast -Wformat-nonliteral
CFLAGS		:= -Wall -Wextra -Wpedantic -g -O3 -mword-relocations -fomit-frame-pointer -ffunction-sections -pipe $(ARCH) $(INCLUDE) -D__3DS__
CFLAGS		+= -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wnull-dereference -Winit-self -Wimplicit-fallthrough=3 -Wshift-overflow=2
CFLAGS		+= -Wunused-const-variable=2 -Wuse-after-free=3 -Wuninitialized -Wstrict-aliasing=3 -Wstring-compare -Wstringop-overflow=4
CFLAGS		+= -Walloca -Warith-conversion -Warray-bounds=2 -Wbidi-chars=any -Wduplicated-cond -Wtrampolines -Wshadow -Wundef -Wunused-macros
CFLAGS		+= -Wwrite-strings -Wdangling-else -Wdangling-pointer=2 -Wflex-array-member-not-at-end -Wlogical-op -Winvalid-utf8
CFLAGS		+= -Wdouble-promotion -Wdisabled-optimization -Winline -Winvalid-pch -Wredundant-decls -Wcast-qual -Wduplicated-branches
CFLAGS		+= -Walloc-zero -Wformat-truncation=1 -Wstack-usage=16384 -Wno-format-nonliteral

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=c++11

#C only flags.
#-U__STRICT_ANSI__ is needed for citro2d.
CFLAGS		+= -U__STRICT_ANSI__ -std=c99
CFLAGS		+= -Wjump-misses-init -Wstrict-prototypes -Wnested-externs -Wmissing-prototypes -Wmissing-variable-declarations

ASFLAGS		:= $(ARCH)
LDFLAGS		= -pipe -specs=3dsx.specs $(ARCH) -Wl,-Map,$(notdir $*.map) -z noexecstack

#Wrap everything.
#Allocator related.
LDFLAGS		+= -Wl,--wrap,malloc,--wrap,calloc,--wrap,realloc,--wrap,free,--wrap,_free_r,--wrap,memalign,--wrap,linearAlloc
LDFLAGS		+= -Wl,--wrap,linearMemAlign,--wrap,linearRealloc,--wrap,linearGetSize,--wrap,linearFree,--wrap,linearSpaceFree
#CPU usage limit related.
LDFLAGS		+= -Wl,--wrap,APT_GetAppCpuTimeLimit,--wrap,APT_SetAppCpuTimeLimit
#pthread related.
LDFLAGS		+= -Wl,--wrap,pthread_mutex_init,--wrap,pthread_mutex_lock,--wrap,pthread_mutex_unlock,--wrap,pthread_mutex_destroy
LDFLAGS		+= -Wl,--wrap,pthread_once,--wrap,pthread_cond_init,--wrap,pthread_cond_wait,--wrap,pthread_cond_signal
LDFLAGS		+= -Wl,--wrap,pthread_cond_broadcast,--wrap,pthread_cond_destroy,--wrap,pthread_create,--wrap,pthread_join
LDFLAGS		+= -Wl,--wrap,pthread_attr_init,--wrap,pthread_attr_destroy,--wrap,pthread_attr_setstacksize

LIBS		:= -lswresample -lavformat -lswscale -lavcodec -lavutil -lcitro2d -lcitro3d -lx264 -lmp3lame
LIBS		+= -ldav1d -lctru -lz -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := library/lib

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES := $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(CURDIR)/$(dir))

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all 3dsx cia_all cia_banner cia_icon cia_normal_ram cia_high_ram check_cia_banner clean

#---------------------------------------------------------------------------------
MAKEROM					?= makerom
MAKEROM_ARGS			:= -elf "$(OUTPUT).elf" -rsf "$(RSF_PATH)" -banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)"
MAKEROM_HIGH_RAM_ARGS	:= -elf "$(OUTPUT).elf" -rsf "$(RSF_HIGH_RAM_PATH)" -banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)"

ifneq ($(strip $(LOGO)),)
	MAKEROM_ARGS			+= -logo "$(LOGO)"
	MAKEROM_HIGH_RAM_ARGS	+= -logo "$(LOGO)"
endif
ifneq ($(strip $(ROMFS)),)
	MAKEROM_ARGS			+= -DAPP_ROMFS="$(ROMFS)"
	MAKEROM_HIGH_RAM_ARGS	+= -DAPP_ROMFS="$(ROMFS)"
endif

BANNERTOOL		?= bannertool
PYTHON			?= python

# bannertool always LZ11-compresses CGFX inside CBMD (see cbmd.cpp lz11_compress). Pass raw .cgfx to -ci only;
# pre-compressing with bannertool lz11 then feeding -ci double-compresses and crashes HOME Menu.
ifeq ($(suffix $(BANNER_IMAGE)),.cgfx)
	BANNER_IMAGE_ARG := -ci
else
	BANNER_IMAGE_ARG := -i
endif
SMDH_FLAGS	:= visible,allow3d,recordusage

#---------------------------------------------------------------------------------

all: 3dsx cia_all

3dsx: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES)
	@echo Building 3dsx...
	@$(MAKE) -j -s --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@echo

cia_all: cia_normal_ram

$(BUILD)/banner.bcwav: $(BANNER_AUDIO_SRC) | $(BUILD)
	@$(BANNERTOOL) makecwav -i $(BANNER_AUDIO_SRC) -o $(BUILD)/banner.bcwav -l true

ifeq ($(BANNER_USE_LOOP_CWAV),1)
cia_banner: $(BANNER_IMAGE) $(BUILD)/banner.bcwav | $(BUILD)
	@$(BANNERTOOL) makebanner $(BANNER_IMAGE_ARG) $(BANNER_IMAGE) -ca $(BUILD)/banner.bcwav -o $(BUILD)/banner.bnr
else
cia_banner: $(BANNER_IMAGE) $(BANNER_AUDIO_SRC) | $(BUILD)
	@$(BANNERTOOL) makebanner $(BANNER_IMAGE_ARG) $(BANNER_IMAGE) -a $(BANNER_AUDIO_SRC) -o $(BUILD)/banner.bnr
endif

cia_icon: $(APP_ICON) | $(BUILD)
	@$(BANNERTOOL) makesmdh -r regionfree -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p $(APP_AUTHOR) -i $(APP_ICON) -f $(SMDH_FLAGS) -o $(BUILD)/icon.icn

# After building .cia, run: make check_cia_banner
check_cia_banner: | $(BUILD)
	@$(PYTHON) "$(CURDIR)/scripts/verify_cia_banner.py" "$(OUTPUT).cia"

# Depend on phony `3dsx` so parallel `make -j all` cannot run makerom before the ELF is linked.
cia_normal_ram: 3dsx cia_banner cia_icon
	@echo Building cia...
	@$(MAKEROM) -f cia -o $(OUTPUT).cia -target t -exefslogo $(MAKEROM_ARGS) -ver $(APP_VER)
	@echo

cia_high_ram: 3dsx cia_banner cia_icon
	@echo Building high ram cia...
	@$(MAKEROM) -f cia -o $(OUTPUT)_high_ram.cia -target t -exefslogo $(MAKEROM_HIGH_RAM_ARGS) -ver $(APP_VER)
	@echo

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia $(TARGET)_high_ram.cia

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x	%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $*.d -o $*.t3x

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
