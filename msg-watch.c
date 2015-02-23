#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <libnotify/notify.h>
#include <libnotify/notification.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <regex.h>
#include <string.h>
#include <pthread.h>

static const char *msgsrc = "/var/log/messages";
static const char *pid = "/tmp/.msg-watch.pid";

static gboolean
notify_ready()
{
	static gboolean ready = FALSE;
	if (!ready)
		ready = notify_init("msg-watch");
	return ready;
}

static void
notify_do(const char *icon, const char *head, const char *msg)
{
	NotifyNotification *n;
	if (!notify_ready())
		return;
	n = notify_notification_new(head, msg, icon);
	notify_notification_show(n, NULL);
	g_object_unref(n);
}

static void
notify_info(const char *head, const char *msg)
{
	notify_do("info", head, msg);
}

static void
notify_error(const char *head, const char *msg)
{
	notify_do("error", head, msg);
}

static char*
msg_gets(char *buf, unsigned sz)
{
	static FILE *in = NULL;
reopen:
	while (!in) {
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "tail -n0 -f %s", msgsrc);
		in = popen(cmd, "r");
		if (!in)
			sleep(1);
	}

	if (!fgets(buf, sz, in)) {
		pclose(in);
		in = NULL;
		sleep(1);
		goto reopen;
	}

	return buf;
}

static void
die_on_parent_exit()
{
	/* set on-death signal */
	prctl(PR_SET_PDEATHSIG, SIGTERM);
}

static void dummy()
{
}

static void
daemonize()
{
	char buf[32];
	int len;
	int fd = open(pid, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		perror("can't create pid file");
		exit(EXIT_FAILURE);
	}

	/* set close-on-exec flag */
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
		perror("fcntl(FD_CLOEXEC)");
		exit(EXIT_FAILURE);
	}

	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		if (errno == EWOULDBLOCK) {
			len = read(fd, buf, sizeof(buf));
			if (len <= 0) {
				strcpy(buf, "<UNKNOWN>");
				len = sizeof("<UNKNOWN>") - 1;
			}
			fprintf(stderr, "Watcher already running with pid %.*s\n", len, buf);
			exit(EXIT_FAILURE);
		} else {
			perror("can't lock pid file");
		}
	}
	
	/* truncate */
	if (ftruncate(fd, 0) != 0) {
		perror("can't rewrite pid file");
		exit(EXIT_FAILURE);
	}

	/* daemonize */
	if (daemon(FALSE, FALSE) != 0) {
		perror("can't daemonize");
		exit(EXIT_FAILURE);
	}

	/* write new pid */
	len = snprintf(buf, sizeof(buf), "%lu", (unsigned long)getpid());
	if (write(fd, buf, len) != len) {
		perror("can't write pid file");
		/* don't exit because we've daemonized already */
	}

	pthread_atfork(dummy, dummy, die_on_parent_exit);
}

enum {
	N_INFO,
	N_ERROR
};

struct pattern {
	int				what;
	const char		*head;
	const char		*msg;
	const char		*re;
	regex_t			rc;
};

int main(int argc, const char **argv)
{
	unsigned i;
	static struct pattern pts[] = {
//		{N_INFO, "OpenVPN", "Connected",
//			"openvpn.*Initialization Sequence Completed"},
		{N_ERROR, "OpenVPN", "Disconnected (reset)",
			"openvpn.*Connection reset"},
		{N_INFO, "OpenVPN", "Disconnected",
			"openvpn.*Closing TUN/TAP interface"},
		{N_ERROR, "OpenVPN", "Authorization failed",
			"openvpn.*AUTH_FAILED"}
	};

	for (i = 0; i < sizeof(pts) / sizeof(*pts); i++) {
		if (regcomp(&pts[i].rc, pts[i].re, 0) != 0) {
			fprintf(stderr, "Invalid regex:\n\t%s\n", pts[i].re);
			exit(EXIT_FAILURE);
		}
	}

	daemonize();

	for (;;) {
		char line[4096];
		msg_gets(line, sizeof(line));

		for (i = 0; i < sizeof(pts) / sizeof(*pts); i++) {
			if (regexec(&pts[i].rc, line, 0, NULL, 0) == 0) {
				switch (pts[i].what) {
				case N_INFO:	notify_info(pts[i].head, pts[i].msg); break;
				case N_ERROR:	notify_error(pts[i].head, pts[i].msg); break;
				}
				break;
			}
		}
	}
	return 0;
}
