#ifndef SPECTRA_VM_H
#define SPECTRA_VM_H

#include "opcode.h"
#include "env.h"

/* =========================================================================
 * VM limits
 * ====================================================================== */

#define VM_STACK_MAX  4096
#define VM_CALL_MAX   256

/* =========================================================================
 * VM — the bytecode execution engine
 * ====================================================================== */

typedef struct {
    Chunk*    chunk;                  /* chunk currently being executed    */
    uint8_t*  ip;                     /* instruction pointer               */
    Value*    stack[VM_STACK_MAX];    /* operand stack (each is retained)  */
    int       sp;                     /* stack pointer: next free slot     */
    Env*      globals;                /* global environment                */
    Env*      current_env;            /* innermost active environment      */
    int       sim_mode;               /* 1 = print wave visualisations     */
    int       had_error;
    char      error_msg[512];
    int       error_line;
} VM;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/* Allocate and initialise a new VM (registers built-ins). */
VM*  vm_new(void);

/* Tear down and free a VM. */
void vm_free(VM* vm);

/* Execute `chunk`.  Returns 1 on success, 0 on runtime error. */
int  vm_run(VM* vm, Chunk* chunk);

/* Install all SPECTRA built-ins into vm->globals. */
void vm_register_builtins(VM* vm);

#endif /* SPECTRA_VM_H */
