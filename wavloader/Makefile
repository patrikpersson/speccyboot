# =============================================================================
# Makefile for SpeccyBoot .wav loader
# Patrik Persson, 2009-
#
# Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
# =============================================================================

AS           = sdasz80
LD           = sdldz80
MAKEBIN      = makebin
DD           = dd

HOSTCC       = gcc
HOSTCFLAGS   = -Wall -Wextra -Werror -ansi -pedantic

LOADER       = wavloader.bin
LINKFILE     = wavloader.lk
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

$(LOADER): src/wavloader.asm $(OBJDIR)
	$(AS) -o $(OBJDIR)/wavloader.rel $<
	$(LD) -n -f $(LINKFILE)
	$(MAKEBIN) -s 65536 -p $(OBJDIR)/wavloader.ihx $(OBJDIR)/$(LOADER).tmp
	$(DD) if=$(OBJDIR)/$(LOADER).tmp of=$@ bs=1 skip=28672

# -----------------------------------------------------------------------------

$(BIN2WAV): src/bin2wav.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@
