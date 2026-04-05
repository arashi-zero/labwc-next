/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_IPC_H
#define LABWC_IPC_H

/*
 * Simple Unix socket IPC for external clients (e.g. Quickshell).
 *
 * Socket path: $XDG_RUNTIME_DIR/labwc-next.sock
 * Env var:     LABWC_NEXT_SOCKET (set to full path on startup)
 *
 * Wire format (all messages newline-terminated):
 *
 *   Events (compositor → client):
 *     workspace>><name>          current workspace changed / initial state
 *     workspace-list>><n1>,<n2>  response to 'workspace list' command
 *     windows>><id>:<appid>|...  all mapped windows (id = creation_id)
 *     error: <msg>               command failed
 *
 *   Commands (client → compositor):
 *     workspace switch <name>    activate workspace by name (or next/prev/last)
 *     workspace list             list all workspace names
 *     window list                list all mapped windows
 *     window focus <id>          focus and raise window by creation_id
 *     reconfigure                reload config and theme (same as SIGHUP)
 */

void ipc_init(void);
void ipc_finish(void);

/* Called by workspaces.c after every workspace switch */
void ipc_broadcast_workspace(const char *name);

/* Called after a window maps, unmaps, or moves to a different workspace */
void ipc_broadcast_windows(void);

#endif /* LABWC_IPC_H */
