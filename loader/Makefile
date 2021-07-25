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
ASMFILES    += context_switch.asm enc28j60.asm
ASMFILES    += menu.asm stack.asm util.asm z80_loader.asm

INCFILES     = bootp.inc context_switch.inc eth.inc enc28j60.inc globals.inc
INCFILES    += menu.inc tftp.inc udp_ip.inc util.inc spi.inc z80_loader.inc

# -----------------------------------------------------------------------------

OFILES       = $(ASMFILES:%.asm=%.rel)

# -----------------------------------------------------------------------------

# ROM:       stage 1, 2K binary file to be loaded into EEPROM (address 0x0000)
# ROM8K:     same as ROM, but 8K in size (to match Fuse's SpeccyBoot emulation)
# STAGE2BIN: stage 2, binary file to be loaded over TFTP by stage 1
#                     (as part of menu.bin)

ROM          = speccyboot.rom
ROM8K        = speccyboot-fuse.rom

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

MAKEBINFLAGS = -s 8192

# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: bin

bin: $(ROM) $(ROM8K)

clean:
	rm -rf $(OBJDIR) $(ROM) $(ROM8K)

install: $(ROM) $(INSTALLDIR)
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

$(OBJDIR) $(INSTALLDIR):
	mkdir -p $@

%.rel: %.asm $(INCFILES) $(OBJDIR)
	$(AS) $(ASFLAGS) -o $(OBJDIR)/$@ $<

$(IHXFILE): $(OFILES)
	$(LD) -n -f $(LINKFILE)

$(COMBINED): $(IHXFILE)
	$(MAKEBIN) $(MAKEBINFLAGS) $(IHXFILE) $@

$(ROM): $(COMBINED)
	dd if=$< of=$@ bs=1k count=2 status=none

$(ROM8K): $(COMBINED)
	dd if=$< of=$@ bs=1k count=8 status=none
