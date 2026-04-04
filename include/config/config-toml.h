/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_CONFIG_TOML_H
#define LABWC_CONFIG_TOML_H

#include <stdbool.h>

/*
 * Try to find and parse ~/.config/labwc/config.toml (or the path given by
 * filename when non-NULL) and populate the global rc struct.
 *
 * Returns true if a config.toml was found and parsed, false if not found
 * (so callers can fall back to rc.xml).
 */
bool config_toml_read(const char *filename);

#endif /* LABWC_CONFIG_TOML_H */
