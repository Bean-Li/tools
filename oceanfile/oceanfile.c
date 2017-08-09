#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>

#define MAX_LEVEL 10 

char workdir[PATH_MAX+1] ;
int thread_num = 0;
char arch[1024+1] ; 
char type[1024+1] ;
int  dir_only = 0 ; 
int  skip_dir = 0;
ssize_t  buffer_sz = 4096 ;
ssize_t  file_sz  =  4096 ;


void usage()
{
    fprintf(stdout, "oceanfile -d work_dir -p thread_num -a directory_arch [ -s filesize ] [ -b buffersize] [ -D ] [ -S ]\n");
    fprintf(stdout, "-d   --workdir          the directory in which the tests will run\n");
    fprintf(stdout, "-D   --dironly          last level of arch are still directory , not file\n");
    fprintf(stdout, "-S   --skipdir          suppose all the directory have already exist, only create file\n");
    fprintf(stdout, "-p   --parallel         thread num in parallel\n");
    fprintf(stdout, "-a   --arch             directory tree architecture\n");
    fprintf(stdout, "-s   --filesize         size of file \n");
    fprintf(stdout, "-b   --buffersize       buffer size of every write operation \n");
    return ;
}


ssize_t parse_space_size(char* inbuf)
{
    ssize_t out_size = 0 ;
    char *p_res = NULL ;

    out_size = strtol(inbuf, &p_res, 10) ;
    if(p_res == NULL)
    {
        return out_size ;
    }

    switch(*p_res)
    {
        case 'k':
	case 'K':
	  out_size *= 1024;
	  break;
        case 'm':
	case 'M':
	  out_size *= (1024*1024);
	  break;
	case 'g':
	case 'G':
	  out_size *= (1024*1024*1024);
	  break;
	case 't':
	case 'T':
	  out_size *= (long)(1024*1024*1024)*1024;
	  break;
        default:
	  break;
    }
    return out_size ;

    
}

struct operation_stat{
    int mkdir_success;
    int mkdir_eexist ;
    int mkdir_fail;

    int create_success ; 
    int open_success ; 
    int create_eexist ;
    int open_fail;
    int create_fail;

    int write_success ; 
    int write_fail;
    char padding[24];
} __attribute__((__aligned__(64)));

struct operation_stat**  statistic;

int init_statistic(struct operation_stat* stat)
{
    stat->mkdir_success    = 0;
    stat->mkdir_eexist     = 0;
    stat->mkdir_fail       = 0;
    stat->create_success   = 0;
    stat->open_success     = 0 ;
    stat->create_eexist    = 0;
    stat->open_fail        = 0;
    stat->create_fail      = 0;

    stat->write_success    = 0 ;
    stat->write_fail       = 0;

    return 0;
}

static int print_statistic(struct operation_stat* stat)
{
    fprintf(stdout, "---------------------------------------------\n");

    fprintf(stdout, "mkdir_success   : %12d\n",   stat->mkdir_success);
    fprintf(stdout, "mkdir_eexist    : %12d\n",   stat->mkdir_eexist);
    fprintf(stdout, "mkdir_fail      : %12d\n\n", stat->mkdir_fail);
    fprintf(stdout, "create_success  : %12d\n",   stat->create_success);
    fprintf(stdout, "create_eexist   : %12d\n",   stat->create_eexist);
    fprintf(stdout, "create_fail     : %12d\n",   stat->create_fail);
    fprintf(stdout, "open_success    : %12d\n",   stat->open_success);
    fprintf(stdout, "open_fail       : %12d\n\n", stat->open_fail);
    fprintf(stdout, "write_success   : %12d\n", stat->write_success);
    fprintf(stdout, "write_fail      : %12d\n", stat->write_fail);

    fprintf(stdout, "---------------------------------------------\n");

    return 0;

}

static void print_statistic_and_exit()
{
    int i ; 
    struct operation_stat total_stat;
    init_statistic(&total_stat);

    for(i = 0 ; i < thread_num ; i++)
    {
        total_stat.mkdir_success    += statistic[i]->mkdir_success ;    
        total_stat.mkdir_eexist     += statistic[i]->mkdir_eexist ;    
        total_stat.mkdir_fail       += statistic[i]->mkdir_fail;

        total_stat.create_success   += statistic[i]->create_success ;
        total_stat.open_success     += statistic[i]->open_success ;
        total_stat.create_eexist    += statistic[i]->create_eexist ;
        total_stat.open_fail        += statistic[i]->open_fail ;
        total_stat.create_fail      += statistic[i]->create_fail ;

        total_stat.write_success    += statistic[i]->write_success ;
        total_stat.write_fail       += statistic[i]->write_fail ;
    }

    print_statistic(&total_stat) ;
    return ;


}

struct dir_desc{
    int level;
    int peer_num;
    int base;
    int begin;
    int end ; 
    int leaf_num ;
    int leaf_type ;  // 0 mean file, 1 mean dir
};

struct arch_desc {
    struct dir_desc* d_desc[10];
    int total_level;
};


int process_level_dir(struct arch_desc* a_desc,  int level, int thread_idx)
{
    int i ; 
    char path_buf[PATH_MAX+1];
    char errmsg[1024];

    assert(level < a_desc->total_level);
    int begin = a_desc->d_desc[level]->begin;
    int end = a_desc->d_desc[level]->end;


    int pos[level+1];
    int current;
    int cur = 0 ;
    int ret = 0 ; 
    int j ; 
    struct operation_stat *stat = statistic[thread_idx];

    for(i = begin; i <=  end ; i++ )
    {
        cur = i ; 
        current = level ; 

        while(current >=0 )
        {
            pos[current] = cur % a_desc->d_desc[current]->peer_num;
            cur = cur / a_desc->d_desc[current]->peer_num ;
            current-- ;
        }

        memset(path_buf, 0 , PATH_MAX+1);
        strncpy(path_buf, workdir, PATH_MAX);

        for( j = 0 ; j <= level ; j++)
        {
            snprintf(path_buf + strlen(path_buf), PATH_MAX, "/DIR_%d", pos[j]);
        }


        ret = mkdir(path_buf, 0755);
        if(ret == 0)
        {
            stat->mkdir_success++;
        }
        else if(ret == -1 && errno == EEXIST)
        {
            stat->mkdir_eexist++;
        }
        else
        {
            strerror_r(errno, errmsg, sizeof(errmsg));
            fprintf(stderr, "THREAD-%4d failed to mkdir %s (%d: %s)\n",
                    thread_idx, path_buf, errno, errmsg);
            stat->mkdir_fail++;
        }
    }
    return 0;
}

int r_write(int fd, char* buffer, ssize_t size)
{
    int reserve_bytes = size;
    int write_bytes = 0;
    while(reserve_bytes > 0)
    {
        write_bytes = write(fd,buffer, reserve_bytes);
        if(write_bytes >= 0)
        {
            reserve_bytes -= write_bytes;
            buffer += write_bytes;
        } 
        else if(write_bytes <0 && errno != EINTR)
        {
            fprintf(stderr, "error happened when write");
            return -1;
        }
    }

    if (reserve_bytes == 0)
        return size;
    else
        return -1;

}

int write_file(int fd, char* buffer, 
               ssize_t buffer_size, ssize_t fsize,
	       struct operation_stat *stat)
{

    int ret = 0 ;
    ssize_t length = 0; 
    ssize_t current_size = 0 ;
    ssize_t write_bytes = 0 ;

    while(length < fsize)
    {
        if ((fsize - length) > buffer_size)
	    current_size = buffer_size;
	else
	    current_size = fsize - length ;

	write_bytes = r_write(fd,buffer,current_size);
	
        if(write_bytes >= 0)
        {
            stat->write_success++ ; 
	    length += write_bytes ;
        }
        else
        {
            stat->write_fail++ ; 
	    ret = -1; 
	    break;
	}
    }
    return ret ;
}

int process_level_file(struct arch_desc* a_desc,  int level, int thread_idx)
{
    int i ; 
    char path_buf[PATH_MAX+1];
    char errmsg[1024];
    assert(level == (a_desc->total_level - 1));
    int begin = a_desc->d_desc[level]->begin;
    int end = a_desc->d_desc[level]->end;

    path_buf[PATH_MAX] = '\0';

    int pos[level+1];
    int current ;
    int cur = 0 ;
    int ret = 0 ; 
    int j ;
    struct operation_stat *stat = statistic[thread_idx];
    int fd = 0 ;

    char *buffer  = malloc(buffer_sz);
    memset(buffer,'\0',buffer_sz);

    for(i = begin; i <= end ; i++ )
    {
        cur = i ; 
        current = level ;
        while(current >=0 )
        {
            pos[current] = cur % a_desc->d_desc[current]->peer_num;
            cur = cur / a_desc->d_desc[current]->peer_num ;
            current-- ;
        }

        memset(path_buf, 0 , PATH_MAX+1);
        strncpy(path_buf, workdir, PATH_MAX);
        for( j = 0 ; j< level ; j++)
        {
            snprintf(path_buf + strlen(path_buf), PATH_MAX, "/DIR_%d", pos[j]);
        }

        snprintf(path_buf + strlen(path_buf), PATH_MAX, "/FILE_%d", pos[j]);

        fd = open(path_buf, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL, 0666);
        if(fd > 0)
        {
            stat->create_success++;
            stat->open_success++ ; 
        }
        else if(fd == -1 && errno == EEXIST)
        {
            stat->create_eexist++;
            fd = open(path_buf, O_TRUNC|O_WRONLY);
            if(fd < 0)
            {
                strerror_r(errno, errmsg, sizeof(errmsg));
                fprintf(stderr, "THREAD-%-4d: failed to open %s (%d: %s)\n",
                        thread_idx, path_buf, errno, errmsg);
                stat->open_fail++ ;
                continue;
            }
            else
            {
                stat->open_success++;
            }
        }
        else
        {
            stat->create_fail++;
            stat->open_fail++;
            strerror_r(errno, errmsg, sizeof(errmsg));
            fprintf(stderr, "THREAD-%-4d: failed to create %s (%d: %s)\n",
                    thread_idx, path_buf, errno, errmsg);
            continue;
        }


       ret =  write_file(fd, buffer, buffer_sz, file_sz, stat);
       if(ret != 0)
       {
           strerror_r(errno, errmsg, sizeof(errmsg));
           fprintf(stderr, "THREAD-%-4d:  %d errors happened while write file %s (%d: %s)\n",
		   thread_idx, -(ret), path_buf,errno, errmsg);
       }

        close(fd);

    }

    free(buffer);
    return 0;
}

int process_level(struct arch_desc* a_desc, int level, int thread_idx)
{
    int ret = 0 ;
    if (strlen(workdir) != 0)
    {
        ret = chdir(workdir);
        if(ret !=0)
        {
            fprintf(stderr,"THREAD-%-4d: failed to change work dir to %s\n",thread_idx, workdir);
            return -1;
        }
    }

    if(level < a_desc->total_level - 1 || (dir_only == 1))
    {
        return process_level_dir(a_desc , level, thread_idx);
    }
    else
    {
        return  process_level_file(a_desc,  level, thread_idx);
    }

}
void * work_thread(void* param)
{
    int idx = (unsigned long)(param);
    int i ; 
    struct arch_desc* a_desc = NULL;
    char * arch_dup = NULL ;

    if(idx < 0 || idx >= thread_num)
    {
        fprintf(stderr,"invalidate work thread idx:%d\n", idx);
        return NULL;
    }

    a_desc = (struct arch_desc*) malloc(sizeof(struct arch_desc));
    if(a_desc == NULL)
    {
        fprintf(stderr, "THREAD: %3d failed to malloc arch_desc\n", idx);
        goto err_ret ; 
    }

    arch_dup = strdup(arch); 
    if(arch_dup == NULL)
    {
        fprintf(stderr, "THREAD: %3d failed to malloc arch_dup\n", idx);
        goto err_ret ;
    }
    for(i = 0 ; i < MAX_LEVEL ; i++)
    {
        a_desc->d_desc[i] = (struct dir_desc*) malloc(sizeof(struct dir_desc));
        if(a_desc->d_desc[i] == NULL)
        {
            fprintf(stderr, "THREAD: %3d failed to malloc a_desc->d_desc[%d]\n", idx, i);
            return NULL;
        }
    }

    char delimit[3] = {',', ';', '\0'}; 
    char* token ; 
    char* last ; 
    int current_level = 0; 

    for(
            (token = strtok_r(arch_dup, delimit, &last));
            token!= NULL ;
            (token = strtok_r(NULL, delimit, &last))
       )
    {
        a_desc->d_desc[current_level]->level = current_level ;
        a_desc->d_desc[current_level]->peer_num = atoi(token) ;
        current_level++;

        if(current_level >= MAX_LEVEL)
        {
            fprintf(stderr, "Too many directory level, Quit task\n");
            return NULL;
        }
    }

    a_desc->total_level = current_level ; 
    int base_current = 1 ;
    for(i = a_desc->total_level - 1  ; i >= 0 ; i--)
    {
        a_desc->d_desc[i]->base = base_current;
        base_current *= a_desc->d_desc[i]->peer_num ; 

        if(i == a_desc->total_level -1)
        {
            a_desc->d_desc[i]->leaf_num = 1;
        }
        else
        {
            a_desc->d_desc[i]->leaf_num = a_desc->d_desc[i+1]->peer_num;
        }
    }

    int total_items = base_current;
    int avg_in_charge = (total_items + thread_num -1 )/thread_num ;

    int begin_in_charge = avg_in_charge * idx ; 
    int end_in_charge = avg_in_charge * (idx + 1) - 1;
    if(end_in_charge >= total_items)
    {
        end_in_charge = total_items -1;
    }

    for(i = 0 ; i < a_desc->total_level ; i++)
    {
        a_desc->d_desc[i]->begin = begin_in_charge / (a_desc->d_desc[i]->base);
        a_desc->d_desc[i]->end =   end_in_charge / (a_desc->d_desc[i]->base) ;
    }

    /*if skip_dir is true, we suppose all the dir have create already (except
     * the last level). So we only proecss the last level*/
    if(skip_dir == 1)
    {
        process_level(a_desc, a_desc->total_level - 1, idx);
    }
    else
    {
        for(i = 0 ; i < a_desc->total_level ; i++)
        {
            process_level(a_desc, i, idx);
        }
    }

err_ret:
    if(a_desc != NULL)
    {
        for(i = 0 ; i<MAX_LEVEL ; i++)
        {
            if(a_desc->d_desc[i] != NULL)
            {
                free(a_desc->d_desc[i]);
                a_desc->d_desc[i] = NULL;
            }
        }
        free(a_desc);
        a_desc = NULL ;
    }

    if(arch_dup)
    {
        free(arch_dup);
        arch_dup = NULL;
    }


    return NULL;

}

void sigint_handler(int signo)
{
    exit(130);
}

int main(int argc , char* argv[])
{
    int ch;
    int option_index = 0 ;

    int ret ; 
    char* res = NULL ;
    int i ; 

    char errmsg[1024];
    struct stat statbuf;

    static struct option long_options[] = {
        {"workdir",       required_argument, 0, 'd'},
        {"dironly",       no_argument,       0, 'D'},
        {"skipdir",       no_argument,       0, 'S'},
        {"parallel",      required_argument, 0, 'p'},
        {"arch",          required_argument, 0, 'a'},
        {"filesize",      required_argument, 0, 's'},
        {"buffersize",    required_argument, 0, 'b'},
        {0, 0 , 0, 0}
    };

    memset(workdir,'\0', PATH_MAX+1);
    memset(arch, '\0', 1024 + 1);
    memset(type, '\0', 1024 + 1);

    while((ch = getopt_long(argc, argv, "h?d:p:a:b:s:DS", long_options, &option_index)) != -1)
    {
        switch(ch)
        {
        case 'd':
            res = realpath(optarg, workdir);
            if(res == NULL)
            {
                strerror_r(errno, errmsg, sizeof(errmsg));
                fprintf(stderr, "failed to get realpath for %s (%d: %s)\n", optarg, errno, errmsg);
                exit(1);
            }

            ret = stat(workdir,&statbuf);
            if(ret !=0 )
            {
                strerror_r(errno, errmsg, sizeof(errmsg));
                fprintf(stderr,"failed to stat workdir %s (%d: %s)\n", workdir, errno, errmsg);
                exit(1);
            }
            if(!S_ISDIR(statbuf.st_mode))
            {
                fprintf(stderr, "workdir (%s) is not directory\n",workdir);
                exit(1);
            }
            break;

        case 'D':
            dir_only = 1;
            break;

        case 'S':
            skip_dir = 1;
            break;

        case 'p':
            thread_num = atoi(optarg);
            break;

        case 'a':
            strncpy(arch,optarg,1024);
            break;

        case 's':
            file_sz = parse_space_size(optarg);
            break; 

	case 'b':
	    buffer_sz = parse_space_size(optarg);
	    break;

        case 'h':
        case '?':
            usage();
            return 0;
            break;
        default:
            break;

        }
    }

    if(thread_num < 0)
    {
        fprintf(stderr,"Invalid thread num \n");
        exit(1);
    }

    if(strlen(workdir) == 0)
    {
        res = realpath(".", workdir);
        if(res == NULL) 
        {
            strerror_r(errno, errmsg, sizeof(errmsg));
            fprintf(stderr,"failed to translate cwd to workdir (%d: %s)\n", errno, errmsg);
            exit(1);
        }
    }
    if(strlen(arch) == 0)
    {
        fprintf(stderr , "You must specify the directory architecture\n");
        usage();
        exit(1);
    }

    statistic = malloc(thread_num * sizeof(struct operation_stat*));
    if(statistic == NULL)
    {
        fprintf(stderr, "failed to malloc statistic\n");
        exit(2);
    }

    for(i = 0 ; i < thread_num ; i++)
    {
        statistic[i] = (struct operation_stat*) malloc(sizeof(struct operation_stat));
        if(statistic[i] == NULL)
        {
            fprintf(stderr, "failed to malloc statistic[%d]\n", i);
            exit(2);
        }
        init_statistic(statistic[i]);
    }

    pthread_t *tid_array = (pthread_t*) malloc(thread_num * sizeof(pthread_t));
    unsigned long idx ;  
    for(i = 0; i < thread_num; i++)
    {
        idx = i ; 
        ret = pthread_create(&(tid_array[i]), NULL, work_thread, (void*) idx);
        if(ret !=0 )
        {
            strerror_r(errno, errmsg, sizeof(errmsg));
            fprintf(stderr, "failed to create thread %d (%s)\n", i, errmsg);
            exit(2);
        }
    }

    struct sigaction new_action, old_action;
    new_action.sa_handler = sigint_handler;
    new_action.sa_flags = 0 ; 
    sigemptyset(&new_action.sa_mask);

    sigaction(SIGINT,&new_action, &old_action);
    atexit(print_statistic_and_exit);
    for(i = 0 ; i < thread_num ; i++)
    {
        pthread_join(tid_array[i], NULL);
    }

    return 0;
}

