# =============================================================================
# Top-level Makefile for SpeccyBoot
# Patrik Persson, 2009
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
# =============================================================================

export

all clean:
	$(MAKE) -C loader $@
	$(MAKE) -C tests $@
	