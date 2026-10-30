/* Load .bbchart files with the native runtime's own loader (native3ds core/chart_loader.c).
 * Build + run: host\verify_charts.bat <native3ds dir> <mod2ds out dir>
 * Exit code 0 only if every chart loads with BB_LOAD_OK. */
#include <stdio.h>
#include <stdlib.h>

#include "chart_loader.h"

int main(int argc, char** argv) {
    int bad = 0;
    bb_chart_t* chart = (bb_chart_t*)malloc(sizeof(bb_chart_t));
    if (!chart) return 2;
    for (int i = 1; i < argc; i++) {
        bb_load_status_t st = bb_chart_load_from_file(chart, argv[i]);
        printf("%-22s %s\n", bb_load_status_name(st), argv[i]);
        if (st != BB_LOAD_OK) bad++;
    }
    printf("%d chart(s), %d rejected\n", argc - 1, bad);
    free(chart);
    return bad ? 1 : 0;
}
