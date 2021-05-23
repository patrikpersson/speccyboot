# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

CC           = sdcc
AS           = sdasz80
MAKEBIN      = makebin
ECHO         = @/bin/echo

LDFLAGS      = -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS     += --code-loc 0x008A --data-loc 0x5B20

# =============================================================================
# CONFIGURATION
# =============================================================================

# Run 'make DGBOOT=1' to build for Imrich Kolkol's DGBoot hardware design
# instead of the default SpeccyBoot

TARGETFLAGS  =

ifdef DGBOOT
TARGETFLAGS += -DHWTARGET_DGBOOT
else
TARGETFLAGS += -DHWTARGET_SPECCYBOOT
endif

ifdef PAINT_STACK
TARGETFLAGS += -DPAINT_STACK
endif

# =============================================================================
# COMPONENTS
# =============================================================================

# NOTE: eth must be first here, as it contains the main function, and
# crt0 will continue executing right into it (saves one JP)
ASMMODULES   = eth
ASMMODULES  += arp bootp context_switch enc28j60 globals
ASMMODULES  += menu spi tftp udp_ip ui util z80_loader

# -----------------------------------------------------------------------------

OFILES       = crt0.rel $(ASMMODULES:%=%.rel)

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

# temporary result when building binary: both stages are built into a single
# binary, then split
COMBINED = $(OBJDIR)/combined.bin

# run GCC preprocessor on assembly files, exclude all hashed lines
%.rel: %.asm $(OBJDIR)
	gcc -E -xc-header -I$(INCLUDEDIR) $(TARGETFLAGS) $< | grep -v '^#'  > $(OBJDIR)/$(<:src/%.asm=%_preprocessed.asm)
	$(AS) -o $(OBJDIR)/$@ $(OBJDIR)/$(<:src/%.asm=%_preprocessed.asm)

$(OBJDIR):
	mkdir -p $@

$(COMBINED): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(ROM) $(OFILES:%=$(OBJDIR)/%)
	$(MAKEBIN) $(MAKEBINFLAGS) $(OBJDIR)/$(ROM) $@

$(ROM): $(COMBINED)
	dd if=$< of=$@ bs=1k count=8

$(STAGE2BIN): $(COMBINED)
	dd if=$< of=$@ bs=1 skip=$(shell grep s__STAGE2 $(OBJDIR)/speccyboot.noi | cut -d' ' -f3 | sed -e 's/0x/obase=10;ibase=16; /g' | bc)
