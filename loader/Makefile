# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-
#
# Part of the SpeccyBoot project <https://github.com/patrikpersson/speccyboot>
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

ASMFILES     = init.asm
ASMFILES    += bootp.asm context_switch.asm eth.asm enc28j60.asm
ASMFILES    += menu.asm tftp.asm udp_ip.asm util.asm z80_loader.asm

INCFILES     = bootp.inc context_switch.inc eth.inc enc28j60.inc globals.inc
INCFILES    += menu.inc tftp.inc udp_ip.inc util.inc spi.inc z80_loader.inc

# -----------------------------------------------------------------------------

OFILES       = $(ASMFILES:%.asm=%.rel)

# -----------------------------------------------------------------------------

# ROM:       stage 1, binary file to be loaded into EEPROM (address 0x0000)
# STAGE2BIN: stage 2, binary file to be loaded over TFTP by stage 1
#                     (as part of menu.bin)

ROM          = speccyboot.rom
STAGE2BIN    = spboot.bin

# -----------------------------------------------------------------------------

# macro for getting the loading address of the stage2 binary

get_address  = $(shell grep 's__STAGE2_ENTRY' $(OBJDIR)/speccyboot.noi | cut -d' ' -f3 | sed -e 's/0x/obase=10;ibase=16; /g' | bc)

# =============================================================================
# DIRECTORIES
# =============================================================================

PREFIX      ?= /usr/local
FUSEROMDIR   = $(PREFIX)/share/fuse
INSTALLDIR   = $(PREFIX)/share/speccyboot

SRCDIR       = src
INCLUDEDIR   = include
TOOLSDIR     = ../utils
OBJDIR       = obj

vpath %.asm  $(SRCDIR)
vpath %.inc  $(INCLUDEDIR)
vpath %.rel  $(OBJDIR)

LINKFILE     = speccyboot.lk

MAKEBINFLAGS = -p

# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: bin

bin: $(ROM) $(STAGE2BIN)

clean:
	rm -rf $(OBJDIR) $(ROM) $(STAGE2BIN)

install: $(ROM) $(STAGE2BIN) $(INSTALLDIR)
	install --mode=a+r $(ROM) $(FUSEROMDIR)
	install --mode=a+r $(STAGE2BIN) $(INSTALLDIR)

.SUFFIXES:

.PHONY: clean

# =============================================================================
# BUILD TARGETS
# =============================================================================

IHXFILE  = $(OBJDIR)/speccyboot.ihx

# temporary result when building binary: both stages are built into a single
# binary, then split
COMBINED = $(OBJDIR)/combined.bin

$(OBJDIR) $(INSTALLDIR):
	mkdir -p $@

%.rel: %.asm $(INCFILES) $(OBJDIR)
	$(AS) $(ASFLAGS) -o $(OBJDIR)/$@ $<

$(IHXFILE): $(OFILES)
	$(LD) -n -f $(LINKFILE)

$(COMBINED): $(IHXFILE)
	$(MAKEBIN) $(MAKEBINFLAGS) $(IHXFILE) $@

$(ROM): $(COMBINED)
	dd if=$< of=$@ bs=1k count=8 status=none

$(STAGE2BIN): $(COMBINED)
	dd if=$< of=$@ bs=1 skip=${get_address} status=none
