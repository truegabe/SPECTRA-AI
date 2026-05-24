#ifndef SPECTRA_EDITOR_H
#define SPECTRA_EDITOR_H

/* Start the TUI editor. filename may be NULL for a new file.
 * Returns when user quits (Ctrl+Q). */
void editor_run(const char* filename);

#endif /* SPECTRA_EDITOR_H */
