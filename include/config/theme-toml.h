/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_THEME_TOML_H
#define LABWC_THEME_TOML_H

#include "theme.h"

/*
 * Scan all *.toml files in the labwc-next config directory and apply
 * any [theme.colors.*] sections to @theme.  Called from theme_init()
 * instead of the old themerc / Openbox theme directory loading.
 */
void theme_toml_read(struct theme *theme);

#endif /* LABWC_THEME_TOML_H */
