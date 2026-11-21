/*
 * wg_main.c - command-line front end for the IEC 62586 waveform generator.
 *
 * Only file output is offered here; streaming is a C-API capability.
 */
#include "wg.h"
#include "wg_config.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *p_file)
{
    fprintf(p_file,
            "usage:\n"
            "  wg gen [--test ID] [--config FILE] [options]\n"
            "  wg list\n"
            "\n"
            "options:\n"
            "  -t, --test ID          scenario name (see 'wg list')\n"
            "      --system 50|60     system frequency (default 50)\n"
            "  -c, --config FILE      single-level JSON configuration\n"
            "  -d, --duration TIME    60, 60s, 2min, 11min\n"
            "      --allow-long       allow duration > 7300 s (prints size estimate)\n"
            "      --freq HZ          fundamental frequency\n"
            "      --amp Ua=80%%,Ub=80%%  amplitudes (%% or V/A)\n"
            "      --phase 0,-120,120 phase per channel\n"
            "      --harmonics \"3=10%%,180;7=5%%,0\"\n"
            "      --interharmonics \"1.5=5%%,0\"\n"
            "      --tone 1060=3%%,0\n"
            "      --mod \"sine:2.3:10:0\"  AM on the fundamental (repeatable)\n"
            "      --mod \"rect:10:5:50:0\"  FM:DEPTH:DUTY[:PHASE]\n"
            "      --phase-step \"5.0:30\"   phase step at TIME_S:ANGLE_DEG\n"
            "      --rate HZ          sample rate (default 51200)\n"
            "      --format f64|f32|i24\n"
            "      --frame N          samples per frame (default 256)\n"
            "      --start TIME       RFC3339 or epoch seconds\n"
            "  -o, --out FILE         output file (default out.bin)\n"
            "      --list             print scenarios and exit\n");
}

int main(int argc, char **argv)
{
    int i;
    wg_config_t st_cfg;
    wg_handle_t *pst_wg;
    char a_out[512];
    char a_err[256];

    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "list") == 0) {
        wg_scenario_print_all(stdout);
        return 0;
    }

    if (strcmp(argv[1], "gen") != 0) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        fprintf(stderr, "wg: unknown command '%s'\n", argv[1]);
        return 2;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0) {
            wg_scenario_print_all(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--stream") == 0) {
            fprintf(stderr, "stream mode: use C API\n");
            return 2;
        }
    }

    if (wg_cli_parse(argc, argv, &st_cfg, a_out, sizeof(a_out), a_err,
                     sizeof(a_err)) != 0) {
        fprintf(stderr, "%s\n", a_err);
        return 1;
    }
    if (a_out[0] == '\0') {
        snprintf(a_out, sizeof(a_out), "out.bin");
    }

    pst_wg = wg_create(&st_cfg, a_err, sizeof(a_err));
    if (pst_wg == NULL) {
        fprintf(stderr, "%s\n", a_err);
        return 1;
    }

    if (wg_run_file(pst_wg, a_out) != 0) {
        wg_destroy(pst_wg);
        fprintf(stderr, "generate: failed to write '%s'\n", a_out);
        return 1;
    }

    wg_destroy(pst_wg);
    return 0;
}
