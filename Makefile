# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

VERSION							= 1.0.0-work

OBJCOPY							= objcopy
CC									= sdcc
AS									= as-z80
ECHO								= @/bin/echo

CFLAGS							= -mz80 --std-sdcc99 --Werror
CFLAGS						 += -I$(INCLUDEDIR) --opt-code-speed

LDFLAGS							= -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS						 += --code-loc 0x0043 --data-loc 0x5C00

# SDCC/Z80 doesn't define bool due to incomplete support. Works for me, though.
CFLAGS						 += -Dbool=BOOL -DVERSION='\"$(VERSION)\"'

# =============================================================================
# COMPONENTS
# =============================================================================

# Common modules (module = source + header)
MODULES      = util z80_parser context_switch enc28j60_spi 

# -----------------------------------------------------------------------------

# EMULATOR BUILD (uses a 128k .z80 image with 48k image embedded for testing)
# CHECK_STACK build, reports stack use before context switch

ifdef EMULATOR_TEST
CFLAGS			+= -DEMULATOR_TEST
else
MODULES     += ip eth rxbuffer arp icmp udp dhcp tftp syslog
endif

ifdef CHECK_STACK
CFLAGS			+= -DCHECK_STACK
endif

# -----------------------------------------------------------------------------

CFILES       = $(MODULES:%=%.c)
HFILES       = $(MODULES:%=%.h)
OFILES       = crt0.o main.o $(CFILES:.c=.o)

ifdef EMULATOR_TEST
CFILES			+= tftp_fake.c
endif

# -----------------------------------------------------------------------------

# EXE:       binary file to be loaded into FRAM (address 0x0000)
# WAV:       .wav file of EXE, to be loaded by audio interface on real machine

EXE          = speccyboot.rom
WAV          = $(EXE:.rom=.wav)


# =============================================================================
# DIRECTORIES
# =============================================================================

SRCDIR       = src
INCLUDEDIR   = include
TOOLSDIR     = utils/build
OBJDIR       = obj

VPATH        = $(OBJDIR)

vpath %.c    $(SRCDIR)
vpath %.asm  $(SRCDIR)
vpath %.h    $(INCLUDEDIR)


# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: bin

bin: $(EXE) $(WAV)

clean:
	rm -rf $(OBJDIR) $(AUTOGENDIR) $(EXE) $(WAV)

.SUFFIXES:

.PHONY: clean

# =============================================================================
# BUILD TOOLS (bin2wav)
# =============================================================================

BIN2WAV     = $(OBJDIR)/bin2wav
HOSTCC      = gcc

# -----------------------------------------------------------------------------

$(BIN2WAV): $(TOOLSDIR)/bin2wav.c
	$(HOSTCC) $< -o $@

%.wav: %.rom $(BIN2WAV)
	$(BIN2WAV) $< $@

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
