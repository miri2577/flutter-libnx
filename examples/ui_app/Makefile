#---------------------------------------------------------------------------------
# Abgeleitet vom offiziellen devkitPro-Template
# (switchbrew/switch-examples: templates/application/Makefile).
# GeÃ¤ndert gegenÃ¼ber dem Original: TARGET, SOURCES, INCLUDES, APP_*.
# Die Embedder-Quellen werden direkt mitkompiliert; eine eigene Bibliothek
# lohnt sich erst, wenn mehr als ein Beispiel existiert.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
TARGET		:=	ui_app
BUILD		:=	build
# embedder/src/engine enthaelt, was den Engine-Header braucht - deshalb
# getrennt von der Plattformschicht, damit kleine Beispiele ohne Engine (etwa
# thread_probe) weiterhin bauen.
SOURCES		:=	source generated ../../embedder/src ../../embedder/src/platform ../../embedder/src/engine
DATA		:=	data
INCLUDES	:=	../../embedder/include
ROMFS	:=	romfs

APP_TITLE	:=	ui_app
APP_AUTHOR	:=	flutter-libnx
APP_VERSION	:=	0.1.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__
# Die App-Kennung bestimmt das Datenverzeichnis der Plugins
# (/switch/flutter_apps/<id>/). Sie kommt aus dem Build statt fest im
# Quelltext zu stehen - jede App bekommt so automatisch ihr eigenes.
CFLAGS	+=	-DFLUTTER_LIBNX_APP_ID=\"$(TARGET)\"

#---------------------------------------------------------------------------------
# Fuer Horizon gebaute Flutter Engine.
#
# Absolute Pfade koennen nicht ueber INCLUDES/LIBDIRS laufen: Das Template
# stellt dort $(CURDIR) voran bzw. erwartet ein lib/-Unterverzeichnis. Deshalb
# direkt ueber CFLAGS und LIBS.
#---------------------------------------------------------------------------------
ENGINE_SRC	?=	$(HOME)/engine/flutter/engine/src
ENGINE_OUT	?=	$(ENGINE_SRC)/out/horizon_release_arm64
ENGINE_LIB	:=	$(ENGINE_OUT)/obj/flutter/shell/platform/embedder/libflutter_engine.a
ENGINE_INC	:=	$(ENGINE_SRC)/flutter/shell/platform/embedder

CFLAGS	+=	-I$(ENGINE_INC)
CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++20

ASFLAGS	:=	-g $(ARCH)
#---------------------------------------------------------------------------------
# Verzeichnis-Deskriptoren gibt es auf Horizon nicht (siehe
# embedder/src/platform/posix_compat_horizon.cpp). Damit die Handles, die
# diese Schicht vergibt, auch bei dup/close/fstat erkannt werden, laufen die
# drei Aufrufe ueber Wrapper. Fuer echte Deskriptoren reichen sie unveraendert
# an libc weiter.
#---------------------------------------------------------------------------------
WRAPS	:=	-Wl,--wrap=close -Wl,--wrap=dup -Wl,--wrap=fstat
# Lesen hinter dem Dateiende: devoptab liefert -1 statt 0 (EOF). Der Wrap
# stellt die POSIX-Semantik her - ohne ihn haelt sqlite frische Datenbanken
# fuer beschaedigt. Einzelheiten in posix_compat_horizon.cpp.
WRAPS	+=	-Wl,--wrap=read
# stat() auf gerade geoeffnete Dateien: devoptab liefert EIO. Der Wrap holt
# die Werte per open+fstat nach - sqlite fragt beim Journal-Anlegen genau so
# die Rechte der offenen Datenbank ab (IOERR_FSTAT 1802).
WRAPS	+=	-Wl,--wrap=stat

# DIAGNOSE (flutter-libnx): Waechter um jede Heap-Allokation, siehe
# embedder/src/platform/heap_guard_horizon.cpp. Sucht die Speicherbeschaedigung,
# die sich als Data Abort in _malloc_r zeigt. Kostet Speicher und Zeit - nach
# der Fehlersuche wieder entfernen.
# ABGESCHALTET (2026-08-11). Der Ansatz war zu eingreifend und hat zweimal
# eigene Fehler erzeugt, ohne den gesuchten zu finden - Einzelheiten in
# docs/porting-notes.md. Wer ihn wieder einschaltet, muss vorher die dort
# genannten drei Bedingungen erfuellen.
#WRAPS	+=	-Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
#WRAPS	+=	-Wl,--wrap=memalign

# -u erzwingt die Einbindung des Absturzberichts. Aufgerufen wird er nur aus
# libnx' Assembler-Zweig, im Programm referenziert ihn niemand - ohne das darf
# der Linker ihn wegwerfen, und dann fehlt er genau dann, wenn er gebraucht
# wird.
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) $(WRAPS) -Wl,-u,__libnx_exception_handler -Wl,-Map,$(notdir $*.map)

# Statisch gelinktes sqlite3 (third_party/sqlite3, scripts/build-sqlite3.sh).
# Ueber MAKEFILE_LIST statt CURDIR, weil das Template sich selbst mit
# geaendertem Arbeitsverzeichnis erneut aufruft.
SQLITE_LIB	:= $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../../third_party/sqlite3/libsqlite3.a

LIBS	:= $(ENGINE_LIB) $(SQLITE_LIB) -lnx

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
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

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
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

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
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf
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

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
