// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "labwc.h"
#include "workspaces.h"

#define IPC_BUF_SIZE 1024

struct ipc_client {
	int fd;
	struct wl_event_source *source;
	struct wl_list link; /* server ipc clients list */
	char buf[IPC_BUF_SIZE];
	size_t buf_len;
};

static int server_fd = -1;
static struct wl_event_source *server_source;
static struct wl_list clients; /* struct ipc_client.link */
static char socket_path[256];

static void
client_destroy(struct ipc_client *client)
{
	wl_event_source_remove(client->source);
	close(client->fd);
	wl_list_remove(&client->link);
	free(client);
}

static void
client_send(struct ipc_client *client, const char *msg)
{
	size_t len = strlen(msg);
	ssize_t written = write(client->fd, msg, len);
	if (written < 0) {
		wlr_log_errno(WLR_DEBUG, "IPC write failed, dropping client");
		client_destroy(client);
	}
}

static void
handle_command(struct ipc_client *client, const char *cmd)
{
	if (strncmp(cmd, "workspace switch ", 17) == 0) {
		const char *name = cmd + 17;
		struct workspace *ws =
			workspaces_find(server.workspaces.current, name, false);
		if (!ws) {
			client_send(client, "error: workspace not found\n");
			return;
		}
		workspaces_switch_to(ws, /* update_focus */ true);
		/* The broadcast is sent by workspaces_switch_to() via
		 * ipc_broadcast_workspace(), so no explicit reply needed */
	} else if (strcmp(cmd, "workspace list") == 0) {
		/* Build comma-separated list of workspace names */
		char msg[IPC_BUF_SIZE];
		int pos = snprintf(msg, sizeof(msg), "workspace-list>>");
		struct workspace *ws;
		bool first = true;
		wl_list_for_each(ws, &server.workspaces.all, link) {
			int r = snprintf(msg + pos, sizeof(msg) - pos,
				"%s%s", first ? "" : ",", ws->name);
			if (r < 0 || (size_t)(pos + r) >= sizeof(msg) - 1) {
				break;
			}
			pos += r;
			first = false;
		}
		snprintf(msg + pos, sizeof(msg) - pos, "\n");
		client_send(client, msg);
	} else {
		client_send(client, "error: unknown command\n");
	}
}

static int
handle_client_readable(int fd, uint32_t mask, void *data)
{
	struct ipc_client *client = data;

	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		client_destroy(client);
		return 0;
	}

	ssize_t n = read(fd, client->buf + client->buf_len,
		sizeof(client->buf) - client->buf_len - 1);
	if (n <= 0) {
		client_destroy(client);
		return 0;
	}
	client->buf_len += n;
	client->buf[client->buf_len] = '\0';

	/* Process all complete newline-delimited commands */
	char *start = client->buf;
	char *nl;
	while ((nl = memchr(start, '\n',
			client->buf_len - (size_t)(start - client->buf)))) {
		*nl = '\0';
		/* Strip trailing CR for convenience */
		ptrdiff_t len = nl - start;
		if (len > 0 && start[len - 1] == '\r') {
			start[len - 1] = '\0';
		}
		if (*start) {
			handle_command(client, start);
		}
		start = nl + 1;
	}

	/* Move unprocessed data to front of buffer */
	size_t remaining = client->buf_len - (size_t)(start - client->buf);
	if (remaining > 0) {
		memmove(client->buf, start, remaining);
	}
	client->buf_len = remaining;

	return 0;
}

static int
handle_new_connection(int fd, uint32_t mask, void *data)
{
	(void)data;

	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		return 0;
	}

	int client_fd = accept(fd, NULL, NULL);
	if (client_fd < 0) {
		wlr_log_errno(WLR_ERROR, "IPC accept() failed");
		return 0;
	}

	fcntl(client_fd, F_SETFD, FD_CLOEXEC);
	fcntl(client_fd, F_SETFL, O_NONBLOCK);

	struct ipc_client *client = znew(*client);
	client->fd = client_fd;
	client->source = wl_event_loop_add_fd(server.wl_event_loop, client_fd,
		WL_EVENT_READABLE, handle_client_readable, client);
	wl_list_insert(&clients, &client->link);

	/* Push current workspace state immediately on connect */
	char msg[256];
	snprintf(msg, sizeof(msg), "workspace>>%s\n",
		server.workspaces.current->name);
	client_send(client, msg);

	wlr_log(WLR_DEBUG, "IPC client connected");
	return 0;
}

void
ipc_init(void)
{
	wl_list_init(&clients);

	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		wlr_log(WLR_ERROR, "IPC: XDG_RUNTIME_DIR not set, IPC disabled");
		return;
	}

	snprintf(socket_path, sizeof(socket_path),
		"%s/labwc-next.sock", runtime_dir);

	/* Remove a stale socket from a previous run */
	unlink(socket_path);

	server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_fd < 0) {
		wlr_log_errno(WLR_ERROR, "IPC socket() failed");
		return;
	}

	fcntl(server_fd, F_SETFD, FD_CLOEXEC);
	fcntl(server_fd, F_SETFL, O_NONBLOCK);

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wlr_log_errno(WLR_ERROR, "IPC bind() failed");
		close(server_fd);
		server_fd = -1;
		return;
	}

	if (listen(server_fd, 16) < 0) {
		wlr_log_errno(WLR_ERROR, "IPC listen() failed");
		close(server_fd);
		server_fd = -1;
		return;
	}

	server_source = wl_event_loop_add_fd(server.wl_event_loop, server_fd,
		WL_EVENT_READABLE, handle_new_connection, NULL);

	if (setenv("LABWC_NEXT_SOCKET", socket_path, true) < 0) {
		wlr_log_errno(WLR_ERROR, "IPC: failed to set LABWC_NEXT_SOCKET");
	}

	wlr_log(WLR_INFO, "IPC socket listening at %s", socket_path);
}

void
ipc_broadcast_workspace(const char *name)
{
	if (server_fd < 0 || wl_list_empty(&clients)) {
		return;
	}

	char msg[256];
	snprintf(msg, sizeof(msg), "workspace>>%s\n", name);

	struct ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &clients, link) {
		client_send(client, msg);
	}
}

void
ipc_finish(void)
{
	struct ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &clients, link) {
		client_destroy(client);
	}

	if (server_source) {
		wl_event_source_remove(server_source);
		server_source = NULL;
	}

	if (server_fd >= 0) {
		close(server_fd);
		server_fd = -1;
	}

	if (socket_path[0]) {
		unlink(socket_path);
	}
}
