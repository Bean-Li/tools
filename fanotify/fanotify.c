#define _ATFILE_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <linux/fanotify.h>

#include "fanotify-syscalllib.h"

#define FANOTIFY_ARGUMENTS "cdfhmnp"

int fan_fd;

int print_progname(pid_t pid)
{
    char proc_item_name[100];
    char progname[100];
    int fd ; 
    int len = 0;

    snprintf(proc_item_name,sizeof(proc_item_name), "/proc/%i/comm",pid);
    fd = open(proc_item_name,O_RDONLY);
    if(fd >=0)
    {
        len = read(fd, progname, sizeof(progname));

	while(len > 0 && progname[len-1] == '\n')
	{
	    len--;
	}
    }

    if(len > 0)
    {
        progname[len] = '\0';
    }
    else
    {
        strcpy(progname, "unknown");
    }

    if(fd >=0 )
    {
        close(fd);
    }
    printf(" %-25s ",progname);
    return 0;
}


int print_timestamp()
{
    struct timeval tv;
    struct tm tminfo ;

    char  buffer[100];
    gettimeofday(&tv, NULL);

    localtime_r(&(tv.tv_sec), &tminfo);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tminfo);
    printf("%s.%06li ", buffer, tv.tv_usec);
    return 0;
}
static void usr1_handler(int sig __attribute__((unused)),
	siginfo_t *si __attribute__((unused)),
	void *unused __attribute__((unused)))
{
    fanotify_mark(fan_fd, FAN_MARK_FLUSH, 0, 0, NULL);
}

int mark_object(int fan_fd, const char *path, int fd, uint64_t mask, unsigned int flags)
{
    return fanotify_mark(fan_fd, flags, mask, fd, path);
}

int set_special_ignored(int fan_fd, int fd, char *path)
{
    unsigned int flags = (FAN_MARK_ADD | FAN_MARK_IGNORED_MASK |
	    FAN_MARK_IGNORED_SURV_MODIFY);
    uint64_t mask = FAN_ALL_EVENTS | FAN_ALL_PERM_EVENTS;

    if (strcmp("/var/log/audit/audit.log", path) &&
	    strcmp("/var/log/messages", path) &&
	    strcmp("/var/log/wtmp", path) &&
	    strcmp("/var/run/utmp", path))
	return 0;

    return mark_object(fan_fd, NULL, fd, mask, flags);
}

int set_ignored_mask(int fan_fd, int fd, uint64_t mask)
{
    unsigned int flags = (FAN_MARK_ADD | FAN_MARK_IGNORED_MASK);

    return mark_object(fan_fd, NULL, fd, mask, flags);
}

int handle_perm(int fan_fd, struct fanotify_event_metadata *metadata)
{
    struct fanotify_response response_struct;
    int ret;

    response_struct.fd = metadata->fd;
    response_struct.response = FAN_ALLOW;

    ret = write(fan_fd, &response_struct, sizeof(response_struct));
    if (ret < 0)
	return ret;

    return 0;
}

void synopsis(const char *progname, int status)
{
    FILE *file = status ? stderr : stdout;

    fprintf(file, "USAGE: %s [-" FANOTIFY_ARGUMENTS "] "
	    "[-o {open,close,access,modify,open_perm,access_perm}] "
	    "file ...\n"
	    "-c: learn about events on children of a directory (not decendants)\n"
	    "-d: send events which happen to directories\n"
	    "-f: set premptive ignores (go faster)\n"
	    "-h: this help screen\n"
	    "-m: place mark on the whole mount point, not just the inode\n"
	    "-n: do not ignore repeated permission checks\n"
	    "-p: check permissions, not just notification\n"
	    "-s N: sleep N seconds before replying to perm events\n",
	    progname);
    exit(status);
}

int main(int argc, char *argv[])
{
    int opt;
    uint64_t fan_mask = FAN_OPEN | FAN_CLOSE | FAN_ACCESS | FAN_MODIFY;
    unsigned int mark_flags = FAN_MARK_ADD, init_flags = 0;
    bool opt_child, opt_on_mount, opt_add_perms, opt_fast, opt_ignore_perm;
    int opt_sleep;
    ssize_t len;
    char buf[4096];
    fd_set rfds;
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = usr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
	goto fail;

    opt_child = opt_on_mount = opt_add_perms = opt_fast = false;
    opt_ignore_perm = true;
    opt_sleep = 0;

    while ((opt = getopt(argc, argv, "o:s:"FANOTIFY_ARGUMENTS)) != -1) {
	switch(opt) {
	case 'o': {
		      char *str, *tok;

		      fan_mask = 0;
		      str = optarg;
		      while ((tok = strtok(str, ",")) != NULL) {
			  str = NULL;
			  if (strcmp(tok, "open") == 0)
			      fan_mask |= FAN_OPEN;
			  else if (strcmp(tok, "close") == 0)
			      fan_mask |= FAN_CLOSE;
			  else if (strcmp(tok, "access") == 0)
			      fan_mask |= FAN_ACCESS;
			  else if (strcmp(tok, "modify") == 0)
			      fan_mask |= FAN_MODIFY;
			  else if (strcmp(tok, "open_perm") == 0)
			      fan_mask |= FAN_OPEN_PERM;
			  else if (strcmp(tok, "access_perm") == 0)
			      fan_mask |= FAN_ACCESS_PERM;
			  else
			      synopsis(argv[0], 1);
		      }
		      break;
		  }
	case 'c':
		  opt_child = true;
		  break;
	case 'd':
		  fan_mask |= FAN_ONDIR;
		  break;
	case 'f':
		  opt_fast = true;
		  opt_ignore_perm = true;
		  break;
	case 'm':
		  opt_on_mount = true;
		  break;
	case 'n':
		  opt_fast = false;
		  opt_ignore_perm = false;
		  break;
	case 'p':
		  opt_add_perms = true;
		  break;
	case 's':
		  opt_sleep = atoi(optarg);
		  break;
	case 'h':
		  synopsis(argv[0], 0);
	default:  /* '?' */
		  synopsis(argv[0], 1);
	}
    }
    if (optind == argc)
	synopsis(argv[0], 1);

    if (opt_child)
	fan_mask |= FAN_EVENT_ON_CHILD;

    if (opt_on_mount)
	mark_flags |= FAN_MARK_MOUNT;

    if (opt_add_perms)
	fan_mask |= FAN_ALL_PERM_EVENTS;

    if (fan_mask & FAN_ALL_PERM_EVENTS)
	init_flags |= FAN_CLASS_CONTENT;
    else
	init_flags |= FAN_CLASS_NOTIF;

    fan_fd = fanotify_init(init_flags, O_RDONLY);
    if (fan_fd < 0)
	goto fail;

    for (; optind < argc; optind++)
	if (mark_object(fan_fd, argv[optind], AT_FDCWD, fan_mask, mark_flags) != 0)
	    goto fail;

    FD_ZERO(&rfds);
    FD_SET(fan_fd, &rfds);

    while (select(fan_fd+1, &rfds, NULL, NULL, NULL) < 0)
	if (errno != EINTR)
	    goto fail;

    while ((len = read(fan_fd, buf, sizeof(buf))) > 0) {
	struct fanotify_event_metadata *metadata;
	char path[PATH_MAX];
	int path_len;

	metadata = (void *)buf;
	while(FAN_EVENT_OK(metadata, len)) {
	    if (metadata->vers < 2) {
		fprintf(stderr, "Kernel fanotify version too old\n");
		goto fail;
	    }

	    if (metadata->fd >= 0 &&
		    opt_fast &&
		    set_ignored_mask(fan_fd, metadata->fd,
			FAN_ALL_EVENTS | FAN_ALL_PERM_EVENTS))
		goto fail;

	    print_timestamp();
	    if (metadata->fd >= 0) {
		sprintf(path, "/proc/self/fd/%d", metadata->fd);
		path_len = readlink(path, path, sizeof(path)-1);
		if (path_len < 0)
		    goto fail;
		path[path_len] = '\0';
	  	//printf("%s:", path);
	    } else
		//printf("?:");

	    set_special_ignored(fan_fd, metadata->fd, path);

	    printf(" pid=%-8ld", (long)metadata->pid);
	    print_progname(metadata->pid);

	    if (metadata->mask & FAN_ACCESS)
		printf(" %-18s", "access");
	    if (metadata->mask & FAN_OPEN)
		printf(" %-18s", "open");
	    if (metadata->mask & FAN_MODIFY)
		printf(" %-18s", "modify");
	    if (metadata->mask & FAN_CLOSE) {
		if (metadata->mask & FAN_CLOSE_WRITE)
		    printf(" %-18s", "close(writable)");
		if (metadata->mask & FAN_CLOSE_NOWRITE)
		    printf(" %198s","close");
	    }
	    if (metadata->mask & FAN_OPEN_PERM)
		printf(" %-18s", "open_perm");
	    if (metadata->mask & FAN_ACCESS_PERM)
		printf(" %-18s", "access_perm");
	    if (metadata->mask & FAN_ALL_PERM_EVENTS) {
		if (opt_sleep)
		    sleep(opt_sleep);

		if (handle_perm(fan_fd, metadata))
		    goto fail;
		if (metadata->fd >= 0 &&
			opt_ignore_perm &&
			set_ignored_mask(fan_fd, metadata->fd,
			    metadata->mask))
		    goto fail;
	    }
   
	    if(metadata->fd)
	    {
	  	printf("\t%s", path);
	    } else
            {
	        printf("\t?");
	    }
	    printf("\n");
	    fflush(stdout);

	    if (metadata->fd >= 0 && close(metadata->fd) != 0)
		goto fail;
	    metadata = FAN_EVENT_NEXT(metadata, len);
	}
	while (select(fan_fd+1, &rfds, NULL, NULL, NULL) < 0)
	    if (errno != EINTR)
		goto fail;
    }
    if (len < 0)
	goto fail;
    return 0;

fail:
    fprintf(stderr, "%s\n", strerror(errno));
    return 1;
}
