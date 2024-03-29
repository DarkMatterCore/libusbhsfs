#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------

ROOTDIR			?=	$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

BUILD_TIMESTAMP	:=	$(strip $(shell date --utc '+%Y-%m-%d %T UTC'))

TARGET			:=	usbhsfs
SOURCES			:=	source source/fatfs source/sxos
DATA			:=	data
INCLUDES		:=	include

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH		:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec

CFLAGS		:=	-g -Wall -Wextra -Werror -Wno-implicit-fallthrough -Wno-unused-function -ffunction-sections -fdata-sections $(ARCH) $(BUILD_CFLAGS) $(INCLUDE)
CFLAGS		+=	-DBUILD_TIMESTAMP="\"$(BUILD_TIMESTAMP)\"" -DLIB_TITLE="\"lib$(TARGET)\"" -fmacro-prefix-map=$(ROOTDIR)=

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS		:=	-g $(ARCH)

UC			=	$(strip $(subst a,A,$(subst b,B,$(subst c,C,$(subst d,D,$(subst e,E,$(subst f,F,$(subst g,G,$(subst h,H,$(subst i,I,$(subst j,J,$(subst k,K,\
				$(subst l,L,$(subst m,M,$(subst n,N,$(subst o,O,$(subst p,P,$(subst q,Q,$(subst r,R,$(subst s,S,$(subst t,T,$(subst u,U,$(subst v,V,\
				$(subst w,W,$(subst x,X,$(subst y,Y,$(subst z,Z,$1)))))))))))))))))))))))))))

ifeq ($(filter $(MAKECMDGOALS),clean dist-src),)
    # Check BUILD_TYPE flag
    ifneq ($(origin BUILD_TYPE),undefined)
        # Convert BUILD_TYPE flag to uppercase
        ORIG_BUILD_TYPE		:=	$(BUILD_TYPE)
        override BUILD_TYPE	:=	$(call UC,$(ORIG_BUILD_TYPE))
        # Check BUILD_TYPE flag value
        ifeq ($(BUILD_TYPE),ISC)
            # Do nothing
        else
            ifeq ($(BUILD_TYPE),GPL)
                # Update sources, set GPL_BUILD definition
                # We'll just assume the user has already installed the necessary libraries
                SOURCES	+=	source/ntfs-3g source/lwext4
                CFLAGS	+=	-DGPL_BUILD
            else
                $(error Invalid value for BUILD_TYPE flag. Expected ISC or GPL)
            endif
        endif
    else
        $(error BUILD_TYPE flag not set)
    endif
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
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

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE		:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
						$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
						-I$(CURDIR)/$(BUILD)

.PHONY: clean all release release-dir debug debug-dir lib-dir example

#---------------------------------------------------------------------------------
LIB_BRANCH	:=	$(shell git rev-parse --abbrev-ref HEAD)
LIB_HASH	:=	$(shell git rev-parse --short HEAD)
LIB_REV		:=	$(LIB_BRANCH)-$(LIB_HASH)

ifneq (, $(strip $(shell git status --porcelain 2>/dev/null)))
LIB_REV	:=	$(LIB_REV)-dirty
endif

$(eval LIB_VERSION_MAJOR = $(shell grep 'define LIBUSBHSFS_VERSION_MAJOR\b' include/usbhsfs.h | tr -s [:blank:] | cut -d' ' -f3))
$(eval LIB_VERSION_MINOR = $(shell grep 'define LIBUSBHSFS_VERSION_MINOR\b' include/usbhsfs.h | tr -s [:blank:] | cut -d' ' -f3))
$(eval LIB_VERSION_MICRO = $(shell grep 'define LIBUSBHSFS_VERSION_MICRO\b' include/usbhsfs.h | tr -s [:blank:] | cut -d' ' -f3))
$(eval LIB_VERSION = $(LIB_VERSION_MAJOR).$(LIB_VERSION_MINOR).$(LIB_VERSION_MICRO)-$(LIB_REV))

ifeq ($(BUILD_TYPE),ISC)
LIB_LICENSE	:=	ISC
else
LIB_LICENSE	:=	GPLv2+
endif

all: release debug

release: lib/lib$(TARGET).a

release-dir:
	@mkdir -p release

debug: lib/lib$(TARGET)d.a

debug-dir:
	@mkdir -p debug

lib-dir:
	@mkdir -p lib

example: all
	@$(MAKE) BUILD_TYPE=$(BUILD_TYPE) --no-print-directory -C example_callback
	@$(MAKE) BUILD_TYPE=$(BUILD_TYPE) --no-print-directory -C example_event

lib/lib$(TARGET).a: release-dir lib-dir $(SOURCES) $(INCLUDES)
	@echo release
	@$(MAKE) BUILD=release OUTPUT=$(CURDIR)/$@ \
	BUILD_CFLAGS="-DNDEBUG=1 -O2" \
	ROOTDIR=$(ROOTDIR) \
	DEPSDIR=$(CURDIR)/release \
	--no-print-directory -C release \
	-f $(CURDIR)/Makefile

lib/lib$(TARGET)d.a: debug-dir lib-dir $(SOURCES) $(INCLUDES)
	@echo debug
	@$(MAKE) BUILD=debug OUTPUT=$(CURDIR)/$@ \
	BUILD_CFLAGS="-DDEBUG=1 -Og" \
	ROOTDIR=$(ROOTDIR) \
	DEPSDIR=$(CURDIR)/debug \
	--no-print-directory -C debug \
	-f $(CURDIR)/Makefile

dist-bin: example
	@cp example_callback/libusbhsfs-example-callback.nro libusbhsfs-example-callback.nro
	@cp example_event/libusbhsfs-example-event.nro libusbhsfs-example-event.nro
	@tar --exclude=*~ -cjf lib$(TARGET)_$(LIB_VERSION)_$(LIB_LICENSE).tar.bz2 include lib LICENSE_$(LIB_LICENSE).md README.md CHANGELOG.md \
	libusbhsfs-example-callback.nro libusbhsfs-example-event.nro
	@rm libusbhsfs-example-callback.nro libusbhsfs-example-event.nro

clean:
	@echo clean ...
	@rm -fr release debug lib *.bz2
	@$(MAKE) --no-print-directory -C example_callback clean
	@$(MAKE) --no-print-directory -C example_event clean

dist-src:
	@tar --exclude=*~ -cjf lib$(TARGET)_$(LIB_VERSION)-src.tar.bz2 \
	--exclude='example_callback/build' --exclude='example_callback/*.elf' --exclude='example_callback/*.nacp' --exclude='example_callback/*.nro' \
	--exclude='example_event/build' --exclude='example_event/*.elf' --exclude='example_event/*.nacp' --exclude='example_event/*.nro' \
	example_callback example_event include source LICENSE_ISC.md LICENSE_GPLv2+.md Makefile README.md CHANGELOG.md

dist: dist-src dist-bin

install: dist-bin
	@bzip2 -cd lib$(TARGET)_$(LIB_VERSION)_$(LIB_LICENSE).tar.bz2 | tar -xf - -C $(PORTLIBS) --exclude='*.md' --exclude='*.nro'

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT)	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES)

#---------------------------------------------------------------------------------
%_bin.h %.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)


-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
