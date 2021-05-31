# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

AS           = sdasz80
LD           = sdldz80
MAKEBIN      = makebin

# =============================================================================
# CONFIGURATION
# =============================================================================

# Run 'make DGBOOT=1' to build for Imrich Konkol's DGBoot hardware design
# instead of the default SpeccyBoot

ASFLAGS      = -Iinclude

ifdef DGBOOT
ASFLAGS     += -Iinclude/platform/dgboot
else
ASFLAGS     += -Iinclude/platform/speccyboot
endif

# =============================================================================
# COMPONENTS
# =============================================================================

ASMMODULES   = arp bootp context_switch eth enc28j60 globals
ASMMODULES  += menu spi tftp udp_ip ui util z80_loader

# -----------------------------------------------------------------------------

OFILES       = init.rel $(ASMMODULES:%=%.rel)

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

vpath %.asm  $(SRCDIR)
vpath %.inc  $(INCLUDEDIR)

LINKFILE     = speccyboot.lk

# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: bin

MAKEBINFLAGS=-p
bin: $(ROM) $(STAGE2BIN)

clean:
	rm -rf $(OBJDIR) $(ROM) $(STAGE2BIN)

install: $(ROM)
	install --mode=a+r $(ROM) $(FUSEROMDIR)

.SUFFIXES:

.PHONY: clean

# =============================================================================
# BUILD TARGETS
# =============================================================================

IHXFILE  = $(OBJDIR)/speccyboot.ihx

# temporary result when building binary: both stages are built into a single
# binary, then split
COMBINED = $(OBJDIR)/combined.bin

$(OBJDIR):
	mkdir -p $@

%.rel: %.asm $(OBJDIR)
	$(AS) $(ASFLAGS) -o $(OBJDIR)/$@ $<

$(IHXFILE): $(OFILES)
	$(LD) -f $(LINKFILE)

$(COMBINED): $(IHXFILE)
	$(MAKEBIN) $(MAKEBINFLAGS) $(IHXFILE) $@

$(ROM): $(COMBINED)
	dd if=$< of=$@ bs=1k count=8

$(STAGE2BIN): $(COMBINED)
	dd if=$< of=$@ bs=1 skip=$(shell grep s__STAGE2 $(OBJDIR)/speccyboot.noi | cut -d' ' -f3 | sed -e 's/0x/obase=10;ibase=16; /g' | bc)
