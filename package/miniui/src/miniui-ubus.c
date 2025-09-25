// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubus.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static struct ubus_context *ctx;

static void fill_meminfo(struct blob_buf *buf)
{
	FILE *f;
	char key[32];
	signed long value;
	char unit[16];
	signed long total = -1, free = -1, available = -1;
	void *tbl;

	f = fopen("/proc/meminfo", "r");
	if (!f)
		return;

	while (fscanf(f, "%31s %ld %15s", key, &value, unit) == 3) {
		if (!strcmp(key, "MemTotal:"))
			total = value;
		else if (!strcmp(key, "MemFree:"))
			free = value;
		else if (!strcmp(key, "MemAvailable:"))
			available = value;
	}

	fclose(f);

	tbl = blobmsg_open_table(buf, "memory");
	if (total >= 0)
		blobmsg_add_u64(buf, "total", (uint64_t)total * 1024);
	if (free >= 0)
		blobmsg_add_u64(buf, "free", (uint64_t)free * 1024);
	if (available >= 0)
		blobmsg_add_u64(buf, "available", (uint64_t)available * 1024);
	blobmsg_close_table(buf, tbl);
}

static int run_command_sync(const char *path, char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0)
		return -errno;

	if (pid == 0) {
		execv(path, argv);
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0)
		return -errno;

	if (!WIFEXITED(status))
		return -EIO;

	return WEXITSTATUS(status);
}

static int run_command_detached(const char *path, char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0)
		return -errno;

	if (pid == 0) {
		pid_t child = fork();
		if (child < 0)
			_exit(127);
		if (child == 0) {
			execv(path, argv);
			_exit(127);
		}
		_exit(0);
	}

	/* Reap the intermediate child */
	waitpid(pid, NULL, 0);
	return 0;
}

static void add_status_payload(struct blob_buf *buf)
{
	FILE *f;
	double load1 = 0.0, load5 = 0.0, load15 = 0.0;
	double uptime = 0.0;
	char hostname[64] = "";
	struct utsname uts;

	f = fopen("/proc/loadavg", "r");
	if (f) {
		if (fscanf(f, "%lf %lf %lf", &load1, &load5, &load15) == 3) {
			blobmsg_add_double(buf, "load1", load1);
			blobmsg_add_double(buf, "load5", load5);
			blobmsg_add_double(buf, "load15", load15);
		}
		fclose(f);
	}

	f = fopen("/proc/uptime", "r");
	if (f) {
		if (fscanf(f, "%lf", &uptime) == 1)
			blobmsg_add_double(buf, "uptime", uptime);
		fclose(f);
	}

	if (gethostname(hostname, sizeof(hostname)) == 0)
		blobmsg_add_string(buf, "hostname", hostname);

	if (uname(&uts) == 0) {
		blobmsg_add_string(buf, "kernel", uts.release);
		blobmsg_add_string(buf, "machine", uts.machine);
	}

	time_t now = time(NULL);
	if (now > 0)
		blobmsg_add_u64(buf, "time", (uint64_t)now);

	fill_meminfo(buf);
}

static int miniui_status(struct ubus_context *context, struct ubus_object *obj,
	struct ubus_request_data *req, const char *method,
	struct blob_attr *msg)
{
	struct blob_buf buf;
	blob_buf_init(&buf, 0);
	add_status_payload(&buf);
	ubus_send_reply(context, req, buf.head);
	blob_buf_free(&buf);
	return 0;
}

enum {
	APPLY_LAN_IPADDR,
	APPLY_LAN_NETMASK,
	__APPLY_LAN_MAX
};

static const struct blobmsg_policy apply_lan_policy[__APPLY_LAN_MAX] = {
	[APPLY_LAN_IPADDR] = { .name = "ipaddr", .type = BLOBMSG_TYPE_STRING },
	[APPLY_LAN_NETMASK] = { .name = "netmask", .type = BLOBMSG_TYPE_STRING },
};

static int miniui_apply_lan(struct ubus_context *context, struct ubus_object *obj,
	struct ubus_request_data *req, const char *method,
	struct blob_attr *msg)
{
	struct blob_attr *tb[__APPLY_LAN_MAX];
	void *data = msg ? blobmsg_data(msg) : NULL;
	int len = msg ? blobmsg_data_len(msg) : 0;

	blobmsg_parse(apply_lan_policy, ARRAY_SIZE(apply_lan_policy), tb, data, len);

	if (!tb[APPLY_LAN_IPADDR])
		return UBUS_STATUS_INVALID_ARGUMENT;

	const char *ip = blobmsg_get_string(tb[APPLY_LAN_IPADDR]);
	const char *mask = tb[APPLY_LAN_NETMASK] ? blobmsg_get_string(tb[APPLY_LAN_NETMASK]) : "";

	char *const argv[] = {
		"/usr/libexec/miniui/apply_lan.sh",
		(char *)ip,
		(char *)mask,
		NULL,
	};

	int rc = run_command_sync(argv[0], argv);
	if (rc != 0)
		return UBUS_STATUS_UNKNOWN_ERROR;

	struct blob_buf buf;
	blob_buf_init(&buf, 0);
	blobmsg_add_string(&buf, "status", "ok");
	ubus_send_reply(context, req, buf.head);
	blob_buf_free(&buf);

	return 0;
}

static int miniui_reload_network(struct ubus_context *context, struct ubus_object *obj,
	struct ubus_request_data *req, const char *method,
	struct blob_attr *msg)
{
	char *const argv[] = {
		"/etc/init.d/network",
		"reload",
		NULL,
	};

	int rc = run_command_sync(argv[0], argv);
	if (rc != 0)
		return UBUS_STATUS_UNKNOWN_ERROR;

	struct blob_buf buf;
	blob_buf_init(&buf, 0);
	blobmsg_add_string(&buf, "status", "ok");
	ubus_send_reply(context, req, buf.head);
	blob_buf_free(&buf);
	return 0;
}

enum {
	SYSUPGRADE_SOURCE,
	SYSUPGRADE_KEEP,
	__SYSUPGRADE_MAX
};

static const struct blobmsg_policy sysupgrade_policy[__SYSUPGRADE_MAX] = {
	[SYSUPGRADE_SOURCE] = { .name = "source", .type = BLOBMSG_TYPE_STRING },
	[SYSUPGRADE_KEEP] = { .name = "keep", .type = BLOBMSG_TYPE_BOOL },
};

static bool allowed_source(const char *source)
{
	if (!source || !*source)
		return false;

	if (!strncmp(source, "http://", 7) || !strncmp(source, "https://", 8))
		return true;

	if (source[0] == '/' && !strncmp(source, "/tmp/", 5))
		return true;

	return false;
}

static int miniui_sysupgrade(struct ubus_context *context, struct ubus_object *obj,
	struct ubus_request_data *req, const char *method,
	struct blob_attr *msg)
{
	struct blob_attr *tb[__SYSUPGRADE_MAX];
	void *data = msg ? blobmsg_data(msg) : NULL;
	int len = msg ? blobmsg_data_len(msg) : 0;

	blobmsg_parse(sysupgrade_policy, ARRAY_SIZE(sysupgrade_policy), tb, data, len);

	if (!tb[SYSUPGRADE_SOURCE])
		return UBUS_STATUS_INVALID_ARGUMENT;

	const char *source = blobmsg_get_string(tb[SYSUPGRADE_SOURCE]);
	bool keep = true;

	if (tb[SYSUPGRADE_KEEP])
		keep = blobmsg_get_bool(tb[SYSUPGRADE_KEEP]);

	if (!allowed_source(source))
		return UBUS_STATUS_INVALID_ARGUMENT;

	char keep_flag[2] = { keep ? '1' : '0', '\0' };
	char *const argv[] = {
		"/usr/libexec/miniui/sysupgrade.sh",
		keep_flag,
		(char *)source,
		NULL,
	};

	int rc = run_command_detached(argv[0], argv);
	if (rc != 0)
		return UBUS_STATUS_UNKNOWN_ERROR;

	struct blob_buf buf;
	blob_buf_init(&buf, 0);
	blobmsg_add_string(&buf, "status", "running");
	ubus_send_reply(context, req, buf.head);
	blob_buf_free(&buf);
	return 0;
}

static const struct ubus_method miniui_methods[] = {
	UBUS_METHOD_NOARG("status", miniui_status),
	UBUS_METHOD("apply_lan", miniui_apply_lan, apply_lan_policy),
	UBUS_METHOD_NOARG("reload_network", miniui_reload_network),
	UBUS_METHOD("sysupgrade", miniui_sysupgrade, sysupgrade_policy),
};

static struct ubus_object_type miniui_object_type =
	UBUS_OBJECT_TYPE("miniui", miniui_methods);

static struct ubus_object miniui_object = {
	.name = "miniui",
	.type = &miniui_object_type,
	.methods = miniui_methods,
	.n_methods = ARRAY_SIZE(miniui_methods),
};

int main(int argc, char **argv)
{
	uloop_init();

	ctx = ubus_connect(NULL);
	if (!ctx)
		return 1;

	ubus_add_uloop(ctx);

	if (ubus_add_object(ctx, &miniui_object)) {
		ubus_free(ctx);
		uloop_done();
		return 1;
	}

	uloop_run();

	ubus_free(ctx);
	uloop_done();
	return 0;
}
