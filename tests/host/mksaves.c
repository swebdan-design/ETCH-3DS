/*
 * ETCH 3DS -- by Synth Centric
 * Writes the save files for the end-to-end save test in the emulator
 * (tools/run_azahar.py --saves): an ETCH DS etch_ds.sav and an ETCH Game Boy
 * etch_gb.sav, made with ETCH DS's own storage code (core/engine/storage.c):
 *
 *   etch_ds.sav slot 01  save version 2, "V2 SONG", 133 BPM, cutoff 90
 *   etch_ds.sav slot 02  save version 1 (no drum designs), "V1 SONG", 97 BPM
 *   etch_gb.sav slot 1   ETCH GB save v2, "GB SONG", 111 BPM
 *   etch_gb.sav slot 2   ETCH GB save v1, "GB V1", 105 BPM
 *
 * Loading one in the emulator must change the tempo the app reports.
 *
 *   mksaves DIR
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "storage.h"
#include "corrosion.h"
#include "app.h"

shared_t *SH;
volatile uint16_t ui_entropy;
uint16_t hw_noise(void) { return 0; }

static void named(project_t *p, const char *n, uint8_t bpm)
{
    demo_load(p);
    memset(p->name, ' ', PROJECT_NAME_LEN);
    memcpy(p->name, n, strlen(n));
    p->synth.bpm = bpm;
}

static void put_gb_slot(uint8_t *sav, int s, const void *data, uint16_t size, uint8_t ver)
{
    uint8_t *b = sav + (s >> 1) * 8192 + (s & 1) * 4096;
    uint16_t sum;
    memcpy(b + 10, data, size);
    sum = etch_fletcher16(b + 10, size);
    b[0] = 'E'; b[1] = 'T'; b[2] = 'C'; b[3] = 'H'; b[4] = ver; b[5] = 0;
    b[6] = (uint8_t)size; b[7] = (uint8_t)(size >> 8);
    b[8] = (uint8_t)sum; b[9] = (uint8_t)(sum >> 8);
}

int main(int argc, char **argv)
{
    static project_t p;
    static char ds[512], gb[512];
    static uint8_t sav[32768], data[16384];
    ds_ext_t x;
    FILE *f;
    uint8_t h[16];
    uint32_t v1 = (uint32_t)(sizeof(project_t) + offsetof(ds_ext_t, drums));
    uint16_t sum;
    if (argc < 2) { printf("usage: mksaves DIR\n"); return 2; }
    snprintf(ds, sizeof ds, "%s/etch_ds.sav", argv[1]);
    snprintf(gb, sizeof gb, "%s/etch_gb.sav", argv[1]);
    storage_path = ds;
    storage_gb_path = gb;
    remove(ds);

    memset(&x, 0, sizeof x);
    x.acid.cutoff = 90; x.acid.res = 60; x.acid.envmod = 70; x.acid.decay = 50; x.acid.level = 100;
    x.acid.accent = 80; x.acid.slide = 40;
    x.fx.crush_bits = 16; x.fx.crush_hold = 1; x.fx.dly_steps = 3; x.fx.limit = 1; x.fx.bpm = 133;
    memcpy(x.drums, foundry_presets, sizeof x.drums);
    memset(x.drum_tune, 8, sizeof x.drum_tune);
    memset(x.drum_level, 12, sizeof x.drum_level);
    memset(x.drum_decay, 8, sizeof x.drum_decay);
    named(&p, "V2 SONG", 133);
    if (!storage_save(0, &p, &x)) { printf("save failed\n"); return 1; }
    named(&p, "V1 SONG", 97);
    x.fx.bpm = 97;
    if (!storage_save(1, &p, &x)) { printf("save failed\n"); return 1; }
    /* rewrite slot 02 as save version 1: no drum designs, ETCH's checksum */
    f = fopen(ds, "r+b");
    fseek(f, 1 * 16384L, SEEK_SET);
    if (fread(h, 1, 16, f) != 16 || fread(data, 1, v1, f) != v1) { printf("read failed\n"); return 1; }
    sum = etch_fletcher16(data, v1);
    h[4] = 1; h[8] = (uint8_t)v1; h[9] = (uint8_t)(v1 >> 8); h[10] = h[11] = 0;
    h[12] = (uint8_t)sum; h[13] = (uint8_t)(sum >> 8);
    fseek(f, 1 * 16384L, SEEK_SET);
    fwrite(h, 1, 16, f);
    fclose(f);

    memset(sav, 0, sizeof sav);
    named(&p, "GB SONG", 111);
    put_gb_slot(sav, 0, &p, sizeof(project_t), 2);
    named(&p, "GB V1", 105);
    put_gb_slot(sav, 1, &p, PROJECT_V1_SIZE, 1);
    f = fopen(gb, "wb");
    fwrite(sav, 1, sizeof sav, f);
    fclose(f);

    {
        char n[11];
        printf("mksaves: %s: 01 %s (v2) 02 %s (v1); %s: 1 %s 2 %s\n", ds,
               storage_slot_status(0, n) == SLOT_OK ? "ok" : "BAD", storage_slot_status(1, n) == SLOT_OK ? "ok" : "BAD",
               gb, storage_gb_status(0, n) == SLOT_OK ? "ok" : "BAD", storage_gb_status(1, n) == SLOT_OK ? "ok" : "BAD");
    }
    return 0;
}
