#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx32/switch32_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.jpg
#     - icon.jpg
#     - <libnx folder>/default_icon.jpg
#
# CONFIG_JSON is the filename of the NPDM config file (.json), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.json
#     - config.json
#   If a JSON file is provided or autodetected, an ExeFS PFS0 (.nsp) is built instead
#   of a homebrew executable (.nro). This is intended to be used for sysmodules.
#   NACP building is skipped as well.
#---------------------------------------------------------------------------------

VITA2HOS_MAJOR	:= 0
VITA2HOS_MINOR	:= 1
VITA2HOS_PATCH	:= 0
VITA2HOS_HASH	:= "$(shell git describe --dirty --always --exclude '*')"

APP_TITLE	:= vita2hos
APP_AUTHOR	:= xerpi
APP_VERSION	:= $(VITA2HOS_MAJOR).$(VITA2HOS_MINOR).$(VITA2HOS_PATCH)
APP_TITLEID	:= 0101000000000010

TARGET		:= $(notdir $(CURDIR))
BUILD		:= build
SOURCES		:= source source/modules vita3k/gxm/src vita3k/shader/src vita3k/shader/src/translator \
		   vita3k/external/SPIRV-Cross vita3k/external/glslang/SPIRV vita3k/external/fmt/src \
		   uam/source uam/mesa-imported/tgsi uam/mesa-imported/codegen uam/mesa-imported/state_tracker \
		   uam/mesa-imported/util uam/mesa-imported/glsl uam/mesa-imported/glsl/glcpp \
		   uam/mesa-imported/compiler uam/mesa-imported/program uam/mesa-imported/main \
		   uam/build/mesa-imported/glsl uam/build/mesa-imported/glsl/glcpp
DATA		:= data
INCLUDES	:= include include/modules vita3k/gxm/include vita3k/shader/include \
		   vita3k/features/include vita3k/external/SPIRV-Cross vita3k/external/glslang \
		   vita3k/mem/include vita3k/util/include vita3k/external/rpcs3/include \
		   vita3k/external/fmt/include uam/source uam/mesa-imported uam/mesa-imported/glsl \
		   uam/build/mesa-imported/glsl uam/build/mesa-imported
SHADER		:= shader
#ROMFS		:= romfs

DEFINES		:= -DVITA2HOS_MAJOR=\"$(VITA2HOS_MAJOR)\" -DVITA2HOS_MINOR=\"$(VITA2HOS_MINOR)\" \
		   -DVITA2HOS_PATCH=\"$(VITA2HOS_PATCH)\" -DVITA2HOS_HASH=\"$(VITA2HOS_HASH)\"

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -nostartfiles

CFLAGS	:=	-g3 -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -D_ISOC11_SOURCE -DNDEBUG

CXXFLAGS	:= $(CFLAGS) -fpermissive -fno-exceptions -DVITA3K_CPP17 -DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS -Wno-unused-variable

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx32/switch32.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -ldeko3dd -lnx -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(foreach dir,$(SHADER),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CCFILES		:= 	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cc)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
GLSLFILES	:=	$(foreach dir,$(SHADER),$(notdir $(wildcard $(dir)/*.glsl)))

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

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) $(GLSLFILES:.glsl=.dksh.o)
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CCFILES:.cc=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES))) \
				$(addsuffix .h,$(subst .,_,$(GLSLFILES:.glsl=.dksh)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) \
			-idirafter $(VITASDK)/arm-vita-eabi/include

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(TARGET).map
else
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf $(TARGET).map
endif


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

else

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

%_vsh.dksh: %_vsh.glsl
	@echo {vert} $(notdir $<)
	@uam -s vert -o $@ $<

%_tcsh.dksh: %_tcsh.glsl
	@echo {tess_ctrl} $(notdir $<)
	@uam -s tess_ctrl -o $@ $<

%_tesh.dksh: %_tesh.glsl
	@echo {tess_eval} $(notdir $<)
	@uam -s tess_eval -o $@ $<

%_gsh.dksh: %_gsh.glsl
	@echo {geom} $(notdir $<)
	@uam -s geom -o $@ $<

%_fsh.dksh: %_fsh.glsl
	@echo {frag} $(notdir $<)
	@uam -s frag -o $@ $<

%.dksh: %.glsl
	@echo {comp} $(notdir $<)
	@uam -s comp -o $@ $<

%.dksh.o %_dksh.h: %.dksh
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
