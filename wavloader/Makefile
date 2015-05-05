# =============================================================================
# Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

AS           = sdasz80
CC           = sdcc
MAKEBIN      = makebin
DD           = dd
LDFLAGS      = -mz80 --out-fmt-ihx --no-std-crt0

HOSTCC       = gcc
HOSTCFLAGS   = -Wall -Wextra -Werror -ansi -pedantic

LOADER       = loader.bin
BIN2WAV      = bin2wav

SRCDIR       = src
OBJDIR       = obj

VPATH        = $(OBJDIR)

vpath %.asm  $(SRCDIR)

# =============================================================================
# COMMAND-LINE TARGETS
# =============================================================================

all: $(LOADER) $(BIN2WAV)

clean:
	rm -rf $(OBJDIR) $(LOADER) $(BIN2WAV)

.SUFFIXES:

.PHONY: clean

# =============================================================================
# COMPILATION TARGETS
# =============================================================================

$(OBJDIR):
	mkdir -p $@

$(LOADER): src/loader.asm $(OBJDIR)
	$(AS) -o $(OBJDIR)/loader.rel $<
	$(CC) $(LDFLAGS) -o $(OBJDIR)/loader.ihx $(OBJDIR)/loader.rel
	$(MAKEBIN) -s 65536 -p $(OBJDIR)/loader.ihx $(OBJDIR)/$(LOADER).tmp
	$(DD) if=$(OBJDIR)/$(LOADER).tmp of=$@ bs=1 skip=28672

# -----------------------------------------------------------------------------

$(BIN2WAV): src/bin2wav.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@
