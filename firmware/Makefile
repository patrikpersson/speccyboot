# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

VERSION      = 1.8-dev

CC           = sdcc
AS           = sdasz80
MAKEBIN      = makebin
ECHO         = @/bin/echo

CFLAGS       = -mz80 --std-sdcc99 --Werror
CFLAGS      += -I$(INCLUDEDIR) --opt-code-size

LDFLAGS      = -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS     += --code-loc 0x0043 --data-loc 0x5B60

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

CFLAGS      += $(TARGETFLAGS)

ifdef MINIMAL
CFLAGS      += -DSB_MINIMAL
endif

ifdef PAINT_STACK
CFLAGS      += -DPAINT_STACK
endif

# =============================================================================
# COMPONENTS
# =============================================================================

# module = source + header
MODULES      = util file_loader context_switch enc28j60 spi globals
MODULES     += eth arp udp_ip tftp syslog

ifdef MINIMAL
MODULES     += bootp
else
MODULES     += dhcp ui menu
endif

# -----------------------------------------------------------------------------

CFILES       = $(MODULES:%=%.c)
HFILES       = $(MODULES:%=%.h)
OFILES       = crt0.rel $(CFILES:.c=.rel)

# -----------------------------------------------------------------------------

# ROM:       binary file to be loaded into EEPROM (address 0x0000)

ROM          = speccyboot.rom


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

bin: $(ROM)

clean:
	rm -rf $(OBJDIR) $(ROM)

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

%.rel: %.c $(OBJDIR) $(HFILES)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

# run GCC preprocessor on crt0, exclude all hashed lines
crt0.rel: crt0.asm $(OBJDIR)
	gcc -E -xc-header -I$(INCLUDEDIR) $(TARGETFLAGS) $< | grep -v '^#'  > $(OBJDIR)/crt0_preprocessed.asm
	$(AS) -o $(OBJDIR)/$@ $(OBJDIR)/crt0_preprocessed.asm

$(OBJDIR):
	mkdir -p $@

$(ROM): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(ROM) $(OFILES:%=$(OBJDIR)/%)
	$(MAKEBIN) -s 8192 $(OBJDIR)/$(ROM) $@
