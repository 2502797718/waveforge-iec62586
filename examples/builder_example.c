/*
 * builder_example.c - write a 1 s waveform with the WaveForge builder API.
 *
 * Build from the repository root:
 *   make lib
 *   cc -std=c99 -Iinclude examples/builder_example.c build/libwaveforge.a \
 *      -lm -o build/builder_example
 *
 * Run:
 *   ./build/builder_example /tmp/builder_example.bin
 *
 * The example builds Ua at 50 Hz, 100 % of 220 V nominal, plus H3 at 7 %
 * of the fundamental with a 45 deg phase, and writes 1 s of f64 samples.
 */
#include <stdio.h>
#include <string.h>

#include "wg.h"

int main(int argc, char **argv)
{
    const char *pc_path = (argc > 1) ? argv[1] : "builder_example.bin";
    const uint8_t au8_ch[1] = { (uint8_t)WG_CH_UA };
    const double af64_nominal[1] = { 220.0 };
    wg_builder_t *pst_b;
    wg_handle_t *pst_wg;
    wg_component_t st_comp;
    char a_err[256];

    pst_b = wg_builder_create(51200u, 0u);
    if (pst_b == NULL) {
        fprintf(stderr, "builder: out of memory\n");
        return 1;
    }
    if (wg_builder_set_channels(pst_b, au8_ch, 1u, af64_nominal) != 0
        || wg_builder_set_duration(pst_b, 1.0) != 0
        || wg_builder_set_output(pst_b, (uint8_t)WG_FMT_F64, 256u) != 0) {
        wg_builder_destroy(pst_b);
        fprintf(stderr, "builder: cannot configure the output\n");
        return 1;
    }

    memset(&st_comp, 0, sizeof(st_comp));
    st_comp.kind = (uint8_t)WG_COMP_FUNDAMENTAL;
    st_comp.order = 1.0;
    st_comp.channel_mask = (uint8_t)(1u << (unsigned)WG_CH_UA);
    st_comp.amplitude_ref = 0u;
    st_comp.amp_pct[WG_CH_UA] = 100.0;
    st_comp.phase_deg[WG_CH_UA] = 0.0;
    if (wg_builder_add_component(pst_b, &st_comp) != 0) {
        wg_builder_destroy(pst_b);
        fprintf(stderr, "builder: cannot add the fundamental\n");
        return 1;
    }

    memset(&st_comp, 0, sizeof(st_comp));
    st_comp.kind = (uint8_t)WG_COMP_HARMONIC;
    st_comp.order = 3.0;
    st_comp.channel_mask = (uint8_t)(1u << (unsigned)WG_CH_UA);
    st_comp.amplitude_ref = 1u;
    st_comp.amp_pct[WG_CH_UA] = 7.0;
    st_comp.phase_deg[WG_CH_UA] = 45.0;
    if (wg_builder_add_component(pst_b, &st_comp) != 0) {
        wg_builder_destroy(pst_b);
        fprintf(stderr, "builder: cannot add the harmonic\n");
        return 1;
    }

    pst_wg = wg_create_from_builder(pst_b, a_err, sizeof(a_err));
    wg_builder_destroy(pst_b);
    if (pst_wg == NULL) {
        fprintf(stderr, "builder: %s\n", a_err);
        return 1;
    }

    if (wg_run_file(pst_wg, pc_path) != 0) {
        wg_destroy(pst_wg);
        fprintf(stderr, "builder: cannot write '%s'\n", pc_path);
        return 1;
    }
    wg_destroy(pst_wg);

    printf("wrote %s\n", pc_path);
    return 0;
}
