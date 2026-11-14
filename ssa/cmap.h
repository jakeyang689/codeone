#ifndef CMAP_H
#define CMAP_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include "ast.h"

/* Render a parsed Program as cmap pseudo-C into `out`.
 *
 * cmap is a human-readable view of SSA, NOT compilable C — see cmap.md
 * for the mapping rules. Returns false on error with a message in `err`.
 */
bool cmap_emit_program(const Program *prog, FILE *out, char *err, size_t err_size);

#endif /* CMAP_H */
