#ifndef SPECTRA_SIM_H
#define SPECTRA_SIM_H
#include "../runtime/specton.h"
#include "interpreter.h"

/* =========================================================
 * Terminal wave-state visualizer
 * ========================================================= */

/* Print a full box visualization of a single Specton to stdout. */
void sim_show_specton(const char* name, Specton s);

/* Print the SPECTRA simulator banner. */
void sim_show_banner(void);

/* Run a .sp file with sim_mode = 1 (interpreter will call sim_show_specton
 * on Wave Specton assignments if desired). */
void sim_run_file(const char* path);

#endif /* SPECTRA_SIM_H */
