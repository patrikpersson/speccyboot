# =============================================================================
# Top-level Makefile for SpeccyBoot
# Patrik Persson, 2009-
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# -----------------------------------------------------------------------------
#
# Supported 'make' targets:
#
# make all             builds firmware and tests, as usual
# make clean           removes all object files and other temporary files
# make install         installs utility script(s)
# =============================================================================


BIN2WAV     = wavloader/bin2wav
LOADER      = wavloader/loader.bin
STAGE1      = loader/speccyboot.rom
STAGE2      = loader/spboot.bin
WAV         = speccyboot.wav

export

all: $(WAV) tests_all

install:
	$(MAKE) -C utils install
	$(MAKE) -C loader install

$(WAV): $(STAGE1) $(BIN2WAV) $(LOADER)

$(STAGE1) $(STAGE2):
	$(MAKE) -C loader all

$(BIN2WAV) $(LOADER):
	$(MAKE) -C wavloader all

tests_all:
	$(MAKE) -C tests all

clean:
	$(MAKE) -C loader clean
	$(MAKE) -C tests clean
	$(MAKE) -C wavloader clean
	rm -f $(WAV)

# -------------------------------------------

$(WAV): $(STAGE1) $(BIN2WAV) $(LOADER)
	cat $(LOADER) $(STAGE1) | $(BIN2WAV) > $(WAV)
