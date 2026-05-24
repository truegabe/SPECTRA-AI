#ifndef SPECTRA_COMPILER_BC_H
#define SPECTRA_COMPILER_BC_H

#include "opcode.h"
#include "ast.h"

/* =========================================================================
 * bc_compile — compile a ND_PROGRAM ASTNode into a Chunk.
 *
 * Returns 1 on success, 0 on error.
 * On error, error_msg (must be at least 256 bytes) is filled with a
 * human-readable description.
 *
 * The caller is responsible for calling chunk_free() on `chunk` when
 * it is no longer needed.
 * ====================================================================== */
int bc_compile(ASTNode* program, Chunk* chunk, char error_msg[256]);

#endif /* SPECTRA_COMPILER_BC_H */
