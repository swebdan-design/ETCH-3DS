# ETCH 3DS -- by Synth Centric
# Host tests.  Run from the top level with `make test`.
#
#   ETCH DS's tests (core/tests, unmodified) against the copied core:
#     ETCH's 44 CORROSION tests, the sequencer, the sound engine, the save file
#   ETCH 3DS's tests:
#     test_parity  the boot song + a 64 s script through the DS path and the
#                  3DS path (pump, command ring, PUNCH taps): identical output
#     test_punch   the PUNCH filter, quantised starts, the retro buffer, the ring
#     test_screens draws every screen of the port (PNG screenshots)
HOSTCC ?= cc
CORE   := ../../core
P3DS   := ../../3ds/source
CFLAGS := -O2 -Wall -Wno-unused-function -Wno-unused-result \
          -I$(CORE)/common -I$(CORE)/corrosion -I$(CORE)/engine
CORR   := $(CORE)/corrosion/corrosion.c $(CORE)/corrosion/pattern.c $(CORE)/corrosion/names.c
ENGINE := $(CORE)/engine/acid.arm.c $(CORE)/engine/foundry.arm.c $(CORE)/engine/fx.arm.c \
          $(CORE)/engine/render.arm.c $(CORE)/engine/dsp_tables.c \
          $(CORE)/engine/demo.c $(CORE)/engine/evolve.c
APP    := $(CORE)/engine/edit.c $(CORE)/engine/storage.c
WRAP   := -Wl,--wrap=foundry_render -Wl,--wrap=fx_limit
OUT    := ../../build/host

.PHONY: all
all: $(OUT)/test_generator $(OUT)/test_seq $(OUT)/test_dsp $(OUT)/test_storage \
     $(OUT)/parity_ds $(OUT)/parity_3ds $(OUT)/test_punch $(OUT)/test_screens
	@echo "== ETCH's CORROSION tests (unmodified) against core/corrosion =="
	@$(OUT)/test_generator | tail -1
	@echo "== ETCH DS sequencer tests against core/ =="
	@$(OUT)/test_seq
	@echo "== ETCH DS sound-engine tests against core/ =="
	@cd $(OUT) && ./test_dsp
	@echo "== ETCH DS save-file tests against core/ =="
	@cd $(OUT) && ./test_storage
	@echo "== sound parity: DS path vs 3DS path =="
	@$(OUT)/parity_ds $(OUT)/parity_ds.raw
	@$(OUT)/parity_3ds $(OUT)/parity_3ds.raw
	@cmp $(OUT)/parity_ds.raw $(OUT)/parity_3ds.raw && \
		echo "  identical: $$(stat -c %s $(OUT)/parity_ds.raw) bytes, every sample of both channels"
	@echo "== PUNCH, retro buffer, command ring =="
	@$(OUT)/test_punch
	@echo "== screens =="
	@cd $(OUT) && ./test_screens

$(OUT)/test_generator: $(CORE)/tests/test_generator.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_seq: $(CORE)/tests/test_seq.c $(CORE)/common/seq_core.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_dsp: $(CORE)/tests/test_dsp.c $(CORE)/common/seq_core.c $(ENGINE) $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm

$(OUT)/test_storage: $(CORE)/tests/test_storage.c $(CORE)/engine/storage.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/parity_ds: test_parity.c $(CORE)/common/seq_core.c $(ENGINE) $(APP) $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm

$(OUT)/parity_3ds: test_parity.c $(CORE)/common/seq_core.c $(ENGINE) $(APP) $(CORR) \
                   $(P3DS)/audio_pump.c $(P3DS)/punch.c
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -DPARITY_3DS -I$(P3DS) -o $@ $^ -lm $(WRAP)

$(OUT)/test_punch: test_punch.c $(CORE)/common/seq_core.c $(ENGINE) $(APP) $(CORR) \
                   $(P3DS)/audio_pump.c $(P3DS)/punch.c
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -I$(P3DS) -o $@ $^ -lm $(WRAP)

$(OUT)/test_screens: test_screens.c $(CORE)/common/seq_core.c $(ENGINE) $(APP) $(CORR) \
                     $(CORE)/engine/font_ui.c $(P3DS)/audio_pump.c $(P3DS)/punch.c \
                     $(P3DS)/gfx.c $(P3DS)/ui.c $(P3DS)/scr_top.c $(P3DS)/title.c
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -I$(P3DS) -o $@ $^ -lm $(WRAP)

# save files for the emulator's end-to-end save test (tools/run_azahar.py)
$(OUT)/mksaves: mksaves.c $(CORE)/engine/storage.c $(CORE)/engine/demo.c $(CORE)/engine/acid.arm.c \
                $(CORE)/engine/foundry.arm.c $(CORE)/engine/fx.arm.c $(CORE)/engine/dsp_tables.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm

$(OUT)/render_ref: render_ref.c $(CORE)/common/seq_core.c $(ENGINE) $(APP) $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm
