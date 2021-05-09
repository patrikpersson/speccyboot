# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

VERSION      = 1d9

CC           = sdcc
AS           = sdasz80
MAKEBIN      = makebin
ECHO         = @/bin/echo

CFLAGS       = -mz80 --std-sdcc99 --Werror
CFLAGS      += -I$(INCLUDEDIR) --opt-code-size

LDFLAGS      = -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS     += --code-loc 0x0044 --data-loc 0x5B60

# disable warnings about z80instructionSize(), don't matter
CFLAGS      += -DVERSION='$(VERSION)' --disable-warning 218

# =============================================================================
# CONFIGURATION
# =============================================================================

# Run 'make DGBOOT=1' to build for Imrich Kolkol's DGBoot hardware design
# instead of the default SpeccyBoot

# Run 'make MINIMAL=1' for a minimal build (BOOTP instead of DHCP, no menu,
# minimal UI, direct booting to snapshot indicated in BOOTP BOOTREPLY)

ifdef DGBOOT
TARGETFLAGS  = -DHWTARGET_DGBOOT
else
TARGETFLAGS  = -DHWTARGET_SPECCYBOOT
endif

ifdef STAGE2_IN_RAM
TARGETFLAGS += -DSTAGE2_IN_RAM
endif

ifdef PAINT_STACK
TARGETFLAGS += -DPAINT_STACK
endif

CFLAGS      += $(TARGETFLAGS)

# =============================================================================
# COMPONENTS
# =============================================================================

# module = source + header
MODULES      = util context_switch enc28j60 spi globals
MODULES     += eth arp udp_ip bootp tftp bootui
MODULES     += ui menu z80_loader

# -----------------------------------------------------------------------------

CFILES       = $(MODULES:%=%.c)
HFILES       = $(MODULES:%=%.h)
OFILES       = crt0.rel $(CFILES:.c=.rel)

# -----------------------------------------------------------------------------

# ROM:       stage 1, binary file to be loaded into EEPROM (address 0x0000)
# STAGE2BIN: binary file to be loaded over TFTP by stage 1

ROM          = speccyboot.rom
STAGE2BIN    = spboot.bin

# =============================================================================
# DIRECTORIES
# =============================================================================

PREFIX      ?= /usr/local
FUSEROMDIR   = $(PREFIX)/share/fuse

SRCDIR       = src
INCLUDEDIR   = include
TOOLSDIR     = ../utils
OBJDIR       = obj

VPATH        = $(OBJDIR)

vpath %.c    $(SRCDIR)
vpath %.asm  $(SRCDIR)
vpath %.h    $(INCLUDEDIR)


# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: bin

ifdef STAGE2_IN_RAM

MAKEBINFLAGS=-p
bin: $(ROM) $(STAGE2BIN)

else

MAKEBINFLAGS=-s 8192
bin: $(ROM)

endif

clean:
	rm -rf $(OBJDIR) $(ROM) $(STAGE2BIN)

install: $(ROM)
	install --mode=a+r $(ROM) $(FUSEROMDIR)

.SUFFIXES:

.PHONY: clean

# =============================================================================
# CHECK CODE & DATA SIZES
# =============================================================================

PYTHON			= /usr/bin/env python
CHECKER			= $(PYTHON) $(TOOLSDIR)/check_sizes.py

check_sizes: bin
	$(CHECKER) $(OBJDIR)/speccyboot.sym

# =============================================================================
# CROSS-COMPILATION TARGETS
# =============================================================================

# temporary result when building binary: both stages are built into a single
# binary, then split
COMBINED = $(OBJDIR)/combined.bin

%.rel: %.c $(OBJDIR) $(HFILES)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

# run GCC preprocessor on crt0, exclude all hashed lines
crt0.rel: crt0.asm $(OBJDIR)
	gcc -E -xc-header -I$(INCLUDEDIR) $(TARGETFLAGS) $< | grep -v '^#'  > $(OBJDIR)/crt0_preprocessed.asm
	$(AS) -o $(OBJDIR)/$@ $(OBJDIR)/crt0_preprocessed.asm

$(OBJDIR):
	mkdir -p $@

$(COMBINED): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(ROM) $(OFILES:%=$(OBJDIR)/%)
	$(MAKEBIN) $(MAKEBINFLAGS) $(OBJDIR)/$(ROM) $@

$(ROM): $(COMBINED)
	dd if=$< of=$@ bs=1k count=8

$(STAGE2BIN): $(COMBINED)
	dd if=$< of=$@ bs=1 skip=$(shell grep s__STAGE2 $(OBJDIR)/speccyboot.noi | cut -d' ' -f3 | sed -e 's/0x/obase=10;ibase=16; /g' | bc)
