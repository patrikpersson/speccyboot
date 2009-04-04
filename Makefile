MODULES      = spectrum enc28j60 logging netboot
OTHER_HFILES = speccyboot.h
OTHER_CFILES = main.c

# EXE:       binary file to be loaded into FRAM (address 0x0000)
# WAV:       .wav file of EXE, to be loaded by audio interface on real machine
# Z80:       .z80 file to be tested in RAM on an emulator (address 0x8000)

EXE          = speccyboot
WAV          = $(EXE).wav
Z80          = speccyboot-ramimage.z80

all: $(EXE) $(WAV) $(Z80)

# -----------------------------------------------------------------------------

CFILES       = $(MODULES:%=%.c) $(OTHER_CFILES)
HFILES       = $(MODULES:%=%.h) $(OTHER_HFILES)
OFILES       = $(CFILES:.c=.o)

CRT_RAMIMG   = crt0_ramimage.asm
OFILES_RAM   = $(CRT_RAMIMG:%.asm=%.o) $(OFILES)

SRCDIR       = src
INCLUDEDIR   = include
TOOLSDIR     = build-tools
OBJDIR       = obj
AUTOGENDIR   = autogen

vpath %.c   $(SRCDIR)
vpath %.asm $(SRCDIR)
vpath %.h   $(INCLUDEDIR)
vpath %.h   $(AUTOGENDIR)
vpath %.o   $(OBJDIR)
vpath %.ihx $(OBJDIR)

.SUFFIXES:

.PHONY: clean

# -----------------------------------------------------------------------------

OBJCOPY     = objcopy
CC          = sdcc
AS          = as-z80
BIN2WAV     = $(OBJDIR)/bin2wav
HOSTCC      = gcc
ECHO        = @/bin/echo

CFLAGS      = --std-sdcc99 -mz80 -I$(INCLUDEDIR) -I$(AUTOGENDIR)
LDFLAGS     = --out-fmt-ihx --data-loc 0x5B00
LDFLAGS_RAM = $(LDFLAGS) --code-loc 0x8100 --no-std-crt0

# SDCC/Z80 doesn't define bool due to incomplete support. Works for me, though.
CFLAGS     += -Dbool=BOOL

# -----------------------------------------------------------------------------

$(BIN2WAV): $(TOOLSDIR)/bin2wav.c
	$(HOSTCC) $^ -o $@

%.wav: % $(BIN2WAV)
	$(BIN2WAV) $< $@

%.o: %.c $(OBJDIR) $(HFILES)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/$@ $<

%.o: %.asm
	$(AS) -o $(OBJDIR)/$@ $<

$(OBJDIR):
	mkdir -p $@

$(AUTOGENDIR):
	mkdir -p $@

$(EXE): $(OFILES)
	$(CC) $(LDFLAGS) -o $(OBJDIR)/$(EXE) $(OFILES:%=$(OBJDIR)/%)
	$(OBJCOPY) -I ihex -O binary $(OBJDIR)/$(EXE).ihx $@

$(Z80): $(OFILES_RAM)
	$(CC) $(LDFLAGS_RAM) -o $(OBJDIR)/tmp0 $(OFILES_RAM:%=$(OBJDIR)/%)
	$(OBJCOPY) -I ihex -O binary $(OBJDIR)/tmp0.ihx $(OBJDIR)/tmp1
	$(ECHO) Generating .z80 file...
	@printf "afcblh\000\200\377\377"	 >  $(OBJDIR)/tmp2
	@dd if=/dev/zero bs=1 count=16404 >> $(OBJDIR)/tmp2 2>/dev/null
	@cat $(OBJDIR)/tmp1 >> $(OBJDIR)/tmp2
	@dd if=/dev/zero bs=1 count=32768 >> $(OBJDIR)/tmp2 2>/dev/null
	@dd if=$(OBJDIR)/tmp2 of=$@ bs=1 count=49182 2>/dev/null

clean:
	rm -rf $(OBJDIR) $(AUTOGENDIR) $(EXE) $(WAV) $(Z80)
