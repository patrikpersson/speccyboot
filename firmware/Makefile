# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-2012
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

VERSION      = 1.5

OBJCOPY      = objcopy
CC           = sdcc
AS           = as-z80
ECHO         = @/bin/echo

CFLAGS       = -mz80 --std-sdcc99 --Werror
CFLAGS      += -I$(INCLUDEDIR) --opt-code-size

LDFLAGS      = -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS     += --code-loc 0x0043 --data-loc 0x5C00

# SDCC/Z80 doesn't define bool due to incomplete support. Works for me, though.
CFLAGS      += -Dbool=BOOL -DVERSION='\"$(VERSION)\"'

# =============================================================================
# CONFIGURATION
# =============================================================================

# Run 'make DGBOOT=1' to build for Imrich Kolkol's DGBoot hardware design
# instead of the default SpeccyBoot

ifdef DGBOOT
TARGETFLAGS  = -DHWTARGET_DGBOOT
else
TARGETFLAGS  = -DHWTARGET_SPECCYBOOT
endif

CFLAGS      += $(TARGETFLAGS)

# =============================================================================
# COMPONENTS
# =============================================================================

# module = source + header
MODULES      = util ui file_loader context_switch enc28j60 spi globals menu
MODULES     += eth arp udp_ip dhcp tftp syslog

# -----------------------------------------------------------------------------

CFILES       = $(MODULES:%=%.c)
HFILES       = $(MODULES:%=%.h)
OFILES       = crt0.o $(CFILES:.c=.o)

# -----------------------------------------------------------------------------

# ROM:       binary file to be loaded into EEPROM (address 0x0000)

ROM          = speccyboot.rom


# =============================================================================
# DIRECTORIES
# =============================================================================

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

%.o: %.c $(OBJDIR) $(HFILES)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

# run GCC preprocessor on crt0, exclude all hashed lines
crt0.o: crt0.asm $(OBJDIR)
	gcc -E -xc-header -I$(INCLUDEDIR) $(TARGETFLAGS) $< | grep -v '^#'  > $(OBJDIR)/crt0_preprocessed.asm
	$(AS) -o $(OBJDIR)/$@ $(OBJDIR)/crt0_preprocessed.asm

$(OBJDIR):
	mkdir -p $@

$(ROM): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(ROM) $(OFILES:%=$(OBJDIR)/%)
	$(OBJCOPY) --pad-to 8192 -I ihex -O binary $(OBJDIR)/$(ROM:.rom=.ihx) $@
