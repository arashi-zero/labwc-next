/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_TOML_VARS_H
#define LABWC_TOML_VARS_H

#include "tomlc17.h"

/**
 * vars_load - populate the variable table from [vars] in a toml root
 * Clears any previously loaded vars first.
 */
void vars_load(toml_datum_t root);

/**
 * vars_clear - free all loaded variables
 */
void vars_clear(void);

/**
 * resolve_var - if s starts with '$', look up and return its value.
 * Returns s unchanged otherwise. Logs an error for undefined variables.
 */
const char *resolve_var(const char *s);

#endif /* LABWC_TOML_VARS_H */
