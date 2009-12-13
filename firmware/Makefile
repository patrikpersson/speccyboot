# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

VERSION      = 1.4

OBJCOPY      = objcopy
CC           = sdcc
AS           = as-z80
ECHO         = @/bin/echo

CFLAGS       = -mz80 --std-sdcc99 --Werror
CFLAGS      += -I$(INCLUDEDIR) --opt-code-speed

LDFLAGS      = -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS     += --code-loc 0x0043 --data-loc 0x5C00

# SDCC/Z80 doesn't define bool due to incomplete support. Works for me, though.
CFLAGS      += -Dbool=BOOL -DVERSION='\"$(VERSION)\"'

# =============================================================================
# COMPONENTS
# =============================================================================

# module = source + header
MODULES      = util z80_parser context_switch enc28j60 spi rxbuffer
MODULES     += eth arp udp_ip dhcp tftp syslog

# -----------------------------------------------------------------------------

CFILES       = $(MODULES:%=%.c)
HFILES       = $(MODULES:%=%.h)
OFILES       = crt0.o main.o $(CFILES:.c=.o)

# -----------------------------------------------------------------------------

# EXE:       binary file to be loaded into FRAM (address 0x0000)

EXE          = speccyboot.rom


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

bin: $(EXE)

clean:
	rm -rf $(OBJDIR) $(EXE)

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

$(EXE): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(EXE) $(OFILES:%=$(OBJDIR)/%)
	$(OBJCOPY) -I ihex -O binary $(OBJDIR)/$(EXE:.rom=.ihx) $@
