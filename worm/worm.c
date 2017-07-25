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
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#include <linux/fanotify.h>

#include "fanotify-syscalllib.h"
#include "dlist.h"

#define FANOTIFY_ARGUMENTS "cdfhmn"
#define MAX_FILENAME 128
#define TRUE 1
#define FALSE 0
#define MAX_EVENT_NUMBER 5


#define STRCAT(dest, source)  \
    do { \
        if(dest[0] == '\0')  \
        strcat(dest, source); \
        else \
        { \
            strcat(dest,", ");\
            strcat(dest, source); \
        } \
    } while(0) 

int fan_fd;

typedef struct dir_entry{
    char name[MAX_FILENAME+1];
    time_t s_time ;
    time_t e_time ;
} dir_entry ;

typedef struct dir_entry_node{
    dir_entry  dirpath ; 
    struct list_head list ; 
}dir_entry_node;

struct list_head g_file_systems ; 
static int pipefd[2];

int add_worm_item(char* buffer , struct list_head *head)
{
    char delimit[2] = {'\t', '\0'};
    char *last ; 
    char* token ;

    char dirname[PATH_MAX+1] = {'\0',} ;
    time_t s_time = 0;
    time_t e_time = LONG_MAX;
    int idx = 0 ;
    for(token  = strtok_r(buffer,delimit, &last);
	token != NULL ;
	token  = strtok_r(NULL, delimit, &last))
    {
        idx++ ;
	if(idx == 1)
	{
	    strncpy(dirname,token,PATH_MAX) ;
	}
	else if(idx == 2)
	{
	    s_time = atoi(token);
	}
	else if(idx == 3)
	{
	    e_time = atoi(token);
	}
    }

    if(strlen(dirname) == 0)
    {
        return 0;
    }

    time_t now = time(NULL);
    if(e_time <= now)
    {
        /*worm expire*/
	return 0;
    }

    dir_entry_node *item = malloc(sizeof(struct dir_entry_node));
    if(item == NULL)
    {
        return -1;
    }
    strncpy(item->dirpath.name, dirname,MAX_FILENAME);
    item->dirpath.s_time = s_time ;
    item->dirpath.e_time = e_time ;
    list_add_tail(&(item->list),head); 
    return 0;

}

int delete_worm_items(struct list_head* head)
{
    struct dir_entry_node* current_node ; 
    struct list_head *current ;

    current = head->next ; 
    while(current != head)
    {
        current_node = list_entry(current,struct dir_entry_node, list);
        list_del(current);
        free(current_node);
        current = head->next ;
    }

    return 0;
}

int reload_worm_conf(char* conf_path, struct list_head* head)
{
    int ret = 0 ; 
    if(conf_path == NULL)
        return -1;

    FILE* fp = fopen(conf_path, "r");
    if(fp == NULL)
    {
        fprintf(stderr, "failed to reload conf (%d: %s)\n",errno, strerror(errno));
        return -2;
    }

    char buffer[PATH_MAX+1];
    delete_worm_items(head) ;

    while(fgets(buffer,PATH_MAX,fp))
    {
        if(buffer[strlen(buffer) -1] == '\n')
        {
            buffer[strlen(buffer) - 1] = '\0';
        }

        
        ret = add_worm_item(buffer, head);
        if(ret != 0)
            break;
    }
    fclose(fp) ;

    if(ret != 0)
    {
        fprintf(stderr,"failed to add worm intem, make g_file_system empty\n");
        delete_worm_items(head) ;
    }

    return ret ;

}

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


int make_file_readonly(char* path)
{
    struct stat buf ; 
    int ret ; 

    ret = stat(path,&buf);
    if(ret != 0)
    {
        fprintf(stderr, "failed to stat %s (%d)\n",path, errno);
        return -1;
    }


    mode_t  mode = buf.st_mode & (~(S_IWUSR|S_IWGRP|S_IWOTH));
    ret = chmod(path, mode);
    if(ret !=0)
    {
        fprintf(stderr,"fail to chmod %s (%d)\n", path, errno);
        return -2;
    }
    return  0;
}

int is_belong_to_worm_list(char* path,struct list_head* head)
{
    struct dir_entry_node* current_node ; 
    struct list_head *current ;
    struct timeval tv ; 

    current = head->next ; 
    while(current != head)
    {
        current_node = list_entry(current,struct dir_entry_node, list);
        if(strncmp(current_node->dirpath.name, path, strlen(current_node->dirpath.name)) == 0)
        {
	    gettimeofday(&tv, NULL);
	    if (tv.tv_sec > current_node->dirpath.s_time &&
		tv.tv_sec < current_node->dirpath.e_time
	       )
                return TRUE; 
	    else
		return FALSE;

        }
        current = current->next ;
    }
    return FALSE;

}

int process_write_close(char* path, struct list_head* head)
{
    int worm = is_belong_to_worm_list(path,head);      
    if(worm == FALSE)
    {
        return 0;
    }
    if(worm == TRUE)
    {
        return make_file_readonly(path);
    }
    return 0;
}

int setnonblocking(int fd)
{
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK ;
    fcntl(fd, F_SETFL, new_option) ;
    return old_option;
}

void addfd(int epollfd, int fd)
{
    struct epoll_event event ; 
    event.data.fd = fd; 
    event.events = EPOLLIN |EPOLLET ;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void sig_handler(int sig)
{
    int save_errno = errno ;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno ;
}

void addsig(int sig)
{
    struct sigaction sa ; 
    memset(&sa, '\0',sizeof(sa));

    sa.sa_handler = sig_handler ; 
    sa.sa_flags |= SA_RESTART ;
    sigfillset(&sa.sa_mask) ;

    assert(sigaction(sig, &sa, NULL) != -1) ;
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
            "-m: place mark on the whole mount point, not just the inode\n", 
            progname);
    exit(status);
}

int main(int argc, char *argv[])
{
    int opt;
    uint64_t fan_mask = FAN_OPEN | FAN_CLOSE | FAN_ACCESS | FAN_MODIFY;
    unsigned int mark_flags = FAN_MARK_ADD, init_flags = 0;
    bool opt_child, opt_on_mount, opt_add_perms, opt_fast;
    ssize_t len;
    char buf[4096];
    struct sigaction sa;
    int ret  = 0; 
    int i ;
    int j;
    int stop_loop = FALSE;

    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = usr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
        goto fail;

    opt_child = opt_on_mount = opt_add_perms = opt_fast = false;

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
        case 'm':
                  opt_on_mount = true;
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


    if (fan_mask & FAN_ALL_PERM_EVENTS)
        init_flags |= FAN_CLASS_CONTENT;
    else
        init_flags |= FAN_CLASS_NOTIF;


    INIT_LIST_HEAD(&g_file_systems);
    char conf_path[PATH_MAX+1] = "/etc/worm.conf" ; 
    ret = reload_worm_conf(conf_path,&g_file_systems) ;
    assert(ret == 0);


    struct epoll_event events[MAX_EVENT_NUMBER] ;

    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0 ,pipefd) ;
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0]);


    fan_fd = fanotify_init(init_flags, O_RDONLY);
    if (fan_fd < 0)
        goto fail;

    for (; optind < argc; optind++)
        if (mark_object(fan_fd, argv[optind], AT_FDCWD, fan_mask, mark_flags) != 0)
            goto fail;


    addfd(epollfd, fan_fd) ;
    addsig(SIGHUP);
    addsig(SIGTERM);


    while(stop_loop != TRUE)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && (errno != EINTR))
        {
            fprintf(stderr, "epoll wait failure (%d)\n",errno);
            break;
        }

        for(i = 0 ; i < number; i++)
        {
            int current_fd = events[i].data.fd ; 
            if(current_fd == fan_fd)
            {
                while ((len = read(fan_fd, buf, sizeof(buf))) > 0)
                {

                    struct fanotify_event_metadata *metadata;
                    char path[PATH_MAX];
                    int path_len;
                    char multi_event_buffer[30];

                    metadata = (void *)buf;
                    while(FAN_EVENT_OK(metadata, len)) 
                    {
                        if (metadata->vers < 2) {
                            fprintf(stderr, "Kernel fanotify version too old\n");
                            goto fail;
                        }

                        if (metadata->fd >= 0 &&
                                opt_fast &&
                                set_ignored_mask(fan_fd, metadata->fd,
                                    FAN_ALL_EVENTS | FAN_ALL_PERM_EVENTS))
                            goto fail;

                        if (metadata->fd >= 0) {
                            sprintf(path, "/proc/self/fd/%d", metadata->fd);
                            path_len = readlink(path, path, sizeof(path)-1);
                            if (path_len < 0)
                                goto fail;
                            path[path_len] = '\0';
                        }
                        else
                            continue;


                        memset(multi_event_buffer,'\0',sizeof(multi_event_buffer));
                        if (metadata->mask & FAN_CLOSE) {
                            if (metadata->mask & FAN_CLOSE_WRITE)
                            {
                                STRCAT(multi_event_buffer, "close(writable)");
                                multi_event_buffer[24] = '\0';
                                print_timestamp();
                                printf(" pid=%-8ld", (long)metadata->pid);
                                print_progname(metadata->pid);
                                printf(" %-25s",  multi_event_buffer);
                                printf("\t%s", path);
                                printf("\n");
                                process_write_close(path, &g_file_systems);
                            }
                        }

                        fflush(stdout);

                        if (metadata->fd >= 0 && close(metadata->fd) != 0)
                            goto fail;
                        metadata = FAN_EVENT_NEXT(metadata, len);
                    }

                }
            }
            else if(current_fd == pipefd[0] && (events[i].events &EPOLLIN))
            {
                char signals[1024];

                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1)
                {
                    continue;
                }
                else if(ret == 0)
                {
                    continue ;
                }
                else
                {
                    for(j = 0 ; j < ret ; j++)
                    {
                        switch(signals[j])
                        {
                        case SIGHUP:
                            ret = reload_worm_conf(conf_path,&g_file_systems) ;
                            if(ret != 0)
                                stop_loop = TRUE;
                            break;
                        case SIGTERM:
                        case SIGINT:
                            stop_loop = TRUE;
                            break;
                        }
                    }
                }

            }
            else
            {
                fprintf(stderr, "unknown fd %d\n", current_fd);
            }

        }
    }

fail:
    fprintf(stderr, "%s\n", strerror(errno));
    return 1;
}
