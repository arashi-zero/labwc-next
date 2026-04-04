// SPDX-License-Identifier: GPL-2.0-only
#include "config/toml-vars.h"
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "common/mem.h"

#define VARS_MAX 64
static struct { char *name; char *value; } g_vars[VARS_MAX];
static int g_vars_count;

void
vars_clear(void)
{
	for (int i = 0; i < g_vars_count; i++) {
		free(g_vars[i].name);
		free(g_vars[i].value);
	}
	g_vars_count = 0;
}

void
vars_load(toml_datum_t root)
{
	/* Accumulates into the existing table. Call vars_clear() before the
	 * first file in a glob to reset scope. This allows a dedicated
	 * vars.toml (sorted first) to act as a global palette shared across
	 * all subsequent files.
	 *
	 * TODO: support [global-vars] (persistent) vs [vars] (file-local) for
	 * explicit two-tier scoping. */
	toml_datum_t sec = toml_get(root, "vars");
	if (sec.type != TOML_TABLE) {
		return;
	}
	for (int i = 0; i < sec.u.tab.size && g_vars_count < VARS_MAX; i++) {
		toml_datum_t val = sec.u.tab.value[i];
		if (val.type != TOML_STRING) {
			continue;
		}
		g_vars[g_vars_count].name = xstrdup(sec.u.tab.key[i]);
		g_vars[g_vars_count].value = xstrdup(val.u.s);
		g_vars_count++;
	}
}

const char *
resolve_var(const char *s)
{
	if (!s || s[0] != '$') {
		return s;
	}
	const char *name = s + 1;
	for (int i = 0; i < g_vars_count; i++) {
		if (!strcmp(g_vars[i].name, name)) {
			return g_vars[i].value;
		}
	}
	wlr_log(WLR_ERROR, "toml: undefined variable '%s'", s);
	return s;
}
