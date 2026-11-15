# ETCH 3DS -- by Synth Centric
# One `make` builds etch_3ds.3dsx (devkitARM + libctru).
#   make            check core/ against its checksums, then the .3dsx
#   make test       host tests: ETCH's 44 CORROSION tests, ETCH DS's sequencer,
#                   DSP and save tests against core/, the sound-parity test
#                   (DS path vs 3DS path), PUNCH and retro-buffer tests; and the
#                   no-division / no-float audio-path check if the ELF is built
#   make docker     the same `make`, in devkitPro's Docker image
#   make ds-check DS=/path/to/ETCH-DS
#                   prove core/ is still byte-identical to an ETCH-DS checkout
#
# core/ is ETCH DS's shared core, copied unmodified (tools/sync_core.sh).  It
# is never edited here: a change goes to ETCH-DS first and comes back with the
# sync script.

HOSTCC		?= cc
PYTHON		?= python3

.PHONY: all clean check test docker ds-check

all: check
	@$(MAKE) --no-print-directory -f Makefile.3ds

clean:
	@$(MAKE) --no-print-directory -f Makefile.3ds clean
	@rm -rf build

# CORROSION and the shared core must stay byte-identical: refuse to build otherwise
check:
	@echo "  CHECK   core/SHA256SUMS (ETCH DS's shared core, unmodified)"
	@cd core && sha256sum --quiet -c SHA256SUMS
	@echo "  CHECK   core/corrosion/SHA256SUMS (CORROSION from ETCH 2.5)"
	@cd core/corrosion && sha256sum --quiet -c SHA256SUMS
	@cd core/tests && sha256sum --quiet -c SHA256SUMS

test: check
	@$(MAKE) --no-print-directory -C tests/host HOSTCC=$(HOSTCC)
	@if [ -f etch_3ds.elf ]; then echo "== audio path (etch_3ds.elf) =="; \
		$(PYTHON) tools/check_audio_path.py etch_3ds.elf | tail -1; fi

docker:
	docker run --rm -v "$(CURDIR)":/work -w /work devkitpro/devkitarm make

ds-check:
	@test -n "$(DS)" || (echo "usage: make ds-check DS=/path/to/ETCH-DS"; exit 1)
	@tools/check_core_upstream.sh "$(DS)"
