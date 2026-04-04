/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_CONFIG_TOML_H
#define LABWC_CONFIG_TOML_H

#include <stdbool.h>

/*
 * Scan all *.toml files in the labwc-next config directory (XDG order,
 * alphabetical within each dir) and populate the global rc struct.
 *
 * Returns true if at least one .toml file was found and parsed, false
 * if none were found (so callers can fall back to rc.xml).
 */
bool config_toml_read(void);

#endif /* LABWC_CONFIG_TOML_H */
