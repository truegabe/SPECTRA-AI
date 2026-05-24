#ifndef SPECTRA_MODULES_H
#define SPECTRA_MODULES_H
#include "env.h"

/* Load a module by name into env.
 * Checks built-in modules first, then searches for <name>.sp in:
 *   1. current working directory
 *   2. directory of the current running file (if known)
 *   3. ~/.spectra/lib/
 * Returns 1 on success, 0 on failure (sets error_msg).
 */
int modules_load(Env* env, const char* module_name,
                 const char* names[], int name_count,
                 char error_msg[256]);

/* Register the built-in "neural" module into env */
void modules_register_neural(Env* env);

/* Register the built-in "math" module into env */
void modules_register_math(Env* env);

/* Register the built-in "random" module into env */
void modules_register_random(Env* env);

#endif /* SPECTRA_MODULES_H */
