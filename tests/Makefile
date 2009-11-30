# =============================================================================
# Makefile for SpeccyBoot test application
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

OBJCOPY     = objcopy
CC          = sdcc
AS          = as-z80

CFLAGS      = -mz80 --std-sdcc99 --Werror -Iinclude
LDFLAGS     = -mz80 --out-fmt-ihx --no-std-crt0
LDFLAGS    += --code-loc 0x7020 --code-size 0x03E0

HOSTCC      = gcc
HOSTCFLAGS  = -DDEBUG

# =============================================================================

GENZ80      = obj/gen-z80-image

Z80_1       = obj/checker1.z80
Z80_2       = obj/checker2.z80
Z80_3       = obj/checker3.z80

all: $(Z80_1) $(Z80_2) $(Z80_3)

clean:
	rm -rf obj

$(GENZ80): gen-z80-image.c obj
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

obj:
	mkdir -p $@

$(Z80_1): $(GENZ80) obj/test_app
	cat test1.data obj/test_app test2.data > $@.raw
	$(GENZ80) 1 < $@.raw > $@

$(Z80_2): $(GENZ80) obj/test_app
	cat test1.data obj/test_app test2.data > $@.raw
	$(GENZ80) 2 < $@.raw > $@

$(Z80_3): $(GENZ80) obj/test_app
	cat test1.data obj/test_app test2.data > $@.raw
	$(GENZ80) 3 < $@.raw > $@

obj/test_app.o: test_app.c obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj/crt0.o: crt0.asm obj
	$(AS) -o $@ $<

obj/test_app: obj/test_app.o obj/crt0.o obj
	$(CC) $(LDFLAGS) -o $@.ihx obj/test_app.o obj/crt0.o
	$(OBJCOPY) -I ihex -O binary $@.ihx $@.bin
	dd if=/dev/zero bs=1k count=1 >> $@.bin
	dd bs=1k count=1 if=$@.bin of=$@

.SUFFIXES:

.PHONY: clean
