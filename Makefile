# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

OBJCOPY     = objcopy
CC          = sdcc
AS          = as-z80
ECHO        = @/bin/echo

CFLAGS      = --std-sdcc99 -mz80 --Werror -I$(INCLUDEDIR) --opt-code-speed
LDFLAGS     = --out-fmt-ihx --no-std-crt0
LDFLAGS    += --code-loc 0x0048 --code-size 0x3FB8 --data-loc 0x5C00
LDFLAGS_RAM = $(LDFLAGS) --code-loc 0x8100 --data-loc 0xc000

# SDCC/Z80 doesn't define bool due to incomplete support. Works for me, though.
CFLAGS     += -Dbool=BOOL

# =============================================================================
# COMPONENTS
# =============================================================================

# 256x64 mono bitmap for splash image
SPLASH_XBM   = speccyboot.xbm

# Auto-generated data for splash image (from SPLASH_XBM)
SPLASH_C     = tmp-splash-image.c

# Common modules (module = source + header)
MODULES      = util netboot

# EMULATOR BUILD (uses a 128k .z80 image with 48k image embedded for testing)

ifdef EMULATOR_TEST
CFLAGS			+= -DEMULATOR_TEST
else
MODULES     += eth enc28j60_spi ip arp icmp udp dhcp tftp
endif

# DEBUG/LOGGING BUILD
# (enable by 'make LOGGING=yes')

ifdef LOGGING
CFLAGS		  += -DVERBOSE_LOGGING
MODULES		  += logging
endif

OTHER_HFILES = platform.h speccyboot.h
OTHER_CFILES = main.c $(SPLASH_C)

CFILES       = $(MODULES:%=%.c) $(OTHER_CFILES)
HFILES       = $(MODULES:%=%.h) $(OTHER_HFILES)
OFILES       = crt0.o $(CFILES:.c=.o)

OFILES_RAM   = $(OFILES:crt0.o=crt0_ramimage.o)

ifdef EMULATOR_TEST
CFILES			+= tftp_fake.c
endif

# -----------------------------------------------------------------------------

# EXE:       binary file to be loaded into FRAM (address 0x0000)
# ROM:			 like EXE, but padded to 16K (for use as a ROM in FUSE)
# WAV:       .wav file of EXE, to be loaded by audio interface on real machine

EXE          = speccyboot
ROM					 = $(EXE).rom
WAV          = $(EXE).wav


# =============================================================================
# DIRECTORIES
# =============================================================================

SRCDIR       = src
INCLUDEDIR   = include
TOOLSDIR     = build-tools
IMGDIR			 = img
OBJDIR       = obj

VPATH        = $(OBJDIR)

vpath %.c    $(SRCDIR)
vpath %.asm  $(SRCDIR)
vpath %.h    $(INCLUDEDIR)
vpath %.xbm  $(IMGDIR)


# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: $(EXE) $(ROM) $(WAV)

clean:
	rm -rf $(OBJDIR) $(AUTOGENDIR) $(EXE) $(ROM) $(WAV)

.SUFFIXES:

.PHONY: clean


# =============================================================================
# BUILD TOOLS (bin2wav, xbm2speccy)
# =============================================================================

BIN2WAV     = $(OBJDIR)/bin2wav
XBM2SPECCY  = $(OBJDIR)/xbm2speccy

HOSTCC      = gcc

# -----------------------------------------------------------------------------

$(BIN2WAV): $(TOOLSDIR)/bin2wav.c
	$(HOSTCC) $< -o $@

$(XBM2SPECCY): $(TOOLSDIR)/xbm2speccy.c $(SPLASH_XBM)
	$(HOSTCC) -DXBM_SOURCEFILE=\"../$(IMGDIR)/$(SPLASH_XBM)\" $< -o $@

$(OBJDIR)/$(SPLASH_C): $(XBM2SPECCY)
	$(XBM2SPECCY) > $@

%.wav: % $(BIN2WAV)
	$(BIN2WAV) $< $@


# =============================================================================
# CROSS-COMPILATION TARGETS
# =============================================================================

# SPLASH_C seems to need a special target for Make to find it
$(SPLASH_C:%.c=%.o): $(OBJDIR)/$(SPLASH_C) $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

%.o: %.c $(OBJDIR) $(HFILES)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

%.o: %.asm $(OBJDIR)
	$(AS) -o $(OBJDIR)/$@ $<

$(OBJDIR):
	mkdir -p $@

$(EXE): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(EXE) $(OFILES:%=$(OBJDIR)/%)
	$(OBJCOPY) -I ihex -O binary $(OBJDIR)/$(EXE).ihx $@

$(ROM): $(EXE)
	cp $< $(OBJ)/rom.tmp
	dd bs=1k count=16 if=/dev/zero >> $(OBJ)/rom.tmp
	dd bs=1k count=16 if=$(OBJ)/rom.tmp of=$@