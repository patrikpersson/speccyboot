# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009-2010
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

VERSION      = 1.4

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

%.o: %.asm $(OBJDIR)
	$(AS) -o $(OBJDIR)/$@ $<

$(OBJDIR):
	mkdir -p $@

$(ROM): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(ROM) $(OFILES:%=$(OBJDIR)/%)
	$(OBJCOPY) --pad-to 8192 -I ihex -O binary $(OBJDIR)/$(ROM:.rom=.ihx) $@
