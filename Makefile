# =============================================================================
# Top-level Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================


BIN2WAV     = wavloader/bin2wav
LOADER      = wavloader/loader.bin
FIRMWARE    = firmware/speccyboot.rom
WAV         = speccyboot.wav

export

all: $(WAV) tests_all

$(WAV): $(FIRMWARE) $(BIN2WAV) $(LOADER)

$(FIRMWARE):
	$(MAKE) -C firmware all

$(BIN2WAV) $(LOADER):
	$(MAKE) -C wavloader all

tests_all:
	$(MAKE) -C tests all

clean:
	$(MAKE) -C firmware clean
	$(MAKE) -C tests clean
	$(MAKE) -C wavloader clean
	rm -f $(WAV)

# -------------------------------------------

$(WAV): $(FIRMWARE) $(BIN2WAV) $(LOADER)
	cat $(LOADER) $(FIRMWARE) | $(BIN2WAV) > $(WAV)
