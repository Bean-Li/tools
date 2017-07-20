
#define _XOPEN_SOURCE 600
#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>


#define MILLION (1000000)

int interval[] = {0,1,2,4,6,8,10,15,20,25,30,40,50,60,70,80,90,100,120,150,200,300,400,500,1000,2000};
#define  N_HIST  (sizeof(interval)/sizeof(int)) 


struct statistic {
    int frames;
    int drop_frame;
    unsigned long long  total_time;
    int hist[N_HIST];

};

struct worker_args {
    int index;
    char* p_workdir;
    size_t filesize;
    int filenum;
    int begin_index;
    int speed;
    int frames;
    struct statistic* stat; 
};

int thread_num = 0 ;
struct statistic summary ;
struct worker_args* args_array = NULL ;
int buffering = 0 ;


int initialize_statistic(struct statistic* stat)
{
    stat->frames = 0; 
    stat->drop_frame = 0;
    stat->total_time = 0; 

    int i = 0 ;
    for (i= 0 ; i < N_HIST; i++)
    {
        stat->hist[i] = 0;
    }

    return 0;
}

int print_hist(int* hist)
{

    char header[256];
    int j ; 
    fprintf(stderr,"----------------------------------------------\n");
    fprintf(stderr,"%20s:\t%10s\n","time-range (ms)", "times");
    for(j = 0; j < N_HIST; j++)
    {
        if(j != N_HIST -1)
        {
            snprintf(header, 256, "[%5d~%6d]",interval[j], interval[j+1]);
        }
        else
        {
            snprintf(header, 256, "[%5d~%6s]",interval[j], "N/A");
        }
        fprintf(stderr,"%20s:\t%10d\n", header, hist[j]);
    }
    fprintf(stderr,"----------------------------------------------\n\n");
    return 0;
}


int print_header(int thread_num, int speed, int frame, int filesize, 
                 int filenum, int begin_index)
{
    fprintf(stdout,"============SUMMARY:===========\n");
    fprintf(stdout,"parallel             : %d\n",thread_num);
    fprintf(stdout,"speed                : %d (KBPS)\n",(speed/1024));
    fprintf(stdout,"frame rate           : %d (frames/s)\n",frame);
    fprintf(stdout,"single file size     : %d (MB)\n",(filesize/1024/1024));
    fprintf(stdout,"filenum per thread   : %d\n",filenum);
    fprintf(stdout,"filename begin_index : %d\n",begin_index);
    fprintf(stdout,"buffering writer     : %s\n\n",buffering == 0?"false":"true");
    return 0;
}
int print_summary(struct statistic* summary)
{

    fprintf(stdout,"total_frame          : %d\n",summary->frames);
    fprintf(stdout,"avg time per frame   : %lld us\n",(summary->total_time/summary->frames));
    fprintf(stdout,"drop_frame           : %d\n", summary->drop_frame);
    fprintf(stdout,"drop rate            : %.4f \n",(float)(summary->drop_frame)*100.0/summary->frames);

    print_hist(summary->hist);

    return 0; 
}

static void print_result_and_exit()
{
    int idx = 0 ; 
    initialize_statistic(&summary);

    int j ; 
    for(idx = 0 ; idx < thread_num; idx++)
    {
        summary.frames += args_array[idx].stat->frames;
        summary.drop_frame += args_array[idx].stat->drop_frame;
        summary.total_time += args_array[idx].stat->total_time;

        for(j=0; j < N_HIST;j++)
        {
            summary.hist[j] += args_array[idx].stat->hist[j];
        }
    }
    print_summary(&summary);
}

void sigint_handler(int signo)
{
    exit(130);
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

unsigned long long time_us()
{
    struct timeval tv ;
    gettimeofday(&tv, NULL);

    return (unsigned long long)(tv.tv_sec*1000000 + tv.tv_usec);
}

void * worker(void* param)
{
    struct worker_args* args = param;
    int index = args->index;
    size_t filesize = args->filesize;
    int filenum = args->filenum;
    int begin_index = args->begin_index;
    int speed = args->speed;
    int frames = args->frames;
    char* workdir = args->p_workdir;
    struct statistic* stat = args->stat;

    initialize_statistic(stat);


    int buf_size = speed/frames;
    if (buf_size >1024*1024)
        buf_size = 1024*1024;

    char* buffer = (char*)malloc(buf_size);
    memset(buffer, 0 , buf_size);

    int i = 0 ;
    int j = 0;
    ssize_t length = 0 ;
    ssize_t write_bytes = 0;
    ssize_t current_size = 0; 
    char filename[256];
    int fd = 0;
    FILE* fp = NULL ;
    unsigned long long tm_begin, tm_end, current_us;
    unsigned long long sleep_time_us;
    int time_slice;
    unsigned long long  drop_frame = 0;
    unsigned long long total_time = 0 ;
    int loop = 0 ; 

    int *hist = (int*) malloc(N_HIST*(sizeof(int)));
    int ret ; 
    if (strlen(workdir) != 0)
    {
        ret = chdir(workdir);
        if(ret !=0)
        {
            fprintf(stderr,"failed to change work dir to %s\n",workdir);
            return NULL;
        }
    }

    for(i = 0; i < filenum ; i++)
    {
        memset(filename,0,256);
        snprintf(filename, 256,"%d-%d-%d.mp4",begin_index, index, i);
	if (buffering == 0)
	{
	    fd = open(filename, O_CREAT|O_RDWR, S_IRWXU|S_IRGRP|S_IROTH);
	    if(fd < 0)
	    {
		fprintf(stderr,"failed to open %s (errno: %d)\n", filename, errno);
		pthread_exit(NULL);
	    }
	}
	else
	{
	    fp = fopen(filename, "w+");
	    if(fp == NULL)
	    {
		fprintf(stderr,"failed to fopen %s (errno: %d)\n", filename, errno);
		pthread_exit(NULL);
	    }
	}

        time_slice = (1000000/frames);

        length = 0;
        drop_frame = 0;
        total_time = 0; 
        loop = 0 ;
        for(j = 0 ; j<N_HIST; j++)
        {
            hist[j] = 0;    
        }

        while(length < filesize)
        {
            tm_begin = time_us();

            if(filesize - length > buf_size)
                current_size = buf_size;
            else
                current_size = filesize - length;

	    if(buffering == 0)
	    {
                write_bytes = r_write(fd, buffer, current_size);
	    }
	    else
	    {
	        write_bytes = fwrite(buffer,1,current_size,fp);
	    }
            if(write_bytes < 0 || write_bytes != current_size)
            {
                fprintf(stderr,"failed to write %s (%zd)",filename, length);
                pthread_exit(NULL);
            }
            else
            {
                length += write_bytes;
            }
            tm_end = time_us();
            loop++;
            stat->frames++ ;

            current_us = tm_end - tm_begin;
            total_time += current_us;
            stat->total_time += current_us;

            for(j = 1; j < N_HIST ; j++)
            {
                if(current_us < interval[j]*1000) // ms to us
                {
                    hist[j-1]++; 
                    stat->hist[j-1]++;
                    break;
                }
            }
            if(j == N_HIST)
            {
                hist[j-1]++;
                stat->hist[j-1]++;
            }

            if(tm_end - tm_begin < time_slice)
            {
                sleep_time_us = time_slice - (tm_end - tm_begin);
                usleep(sleep_time_us);
            }
            else
            {
                drop_frame++;
                stat->drop_frame++;
            }
        }
	if(buffering == 0)
            close(fd);
	else
	    fclose(fp);
    }

    return NULL;
}

void usage()
{
    fprintf(stdout, "streamwriter -d dest_dir -p thread_num -s speed -n filenum -f frames_num -S filesize  -b begin_index [-B]\n");
    fprintf(stdout, "-d   --workdir    destination directory\n");
    fprintf(stdout, "-p   --parallel   thread num in parallel\n");
    fprintf(stdout, "-s   --speed      write speed in every single thread (KB/s)\n");
    fprintf(stdout, "-f   --frames     frames we should write in one second \n");
    fprintf(stdout, "-n   --number     file numbers than every thread should generate \n");
    fprintf(stdout, "-S   --size       file size of each file  (MB)\n");
    fprintf(stdout, "-b   --prefix     file name's prefix \n");
    fprintf(stdout, "-B   --buffering  use fwrite instead of write \n");
}



int main(int argc, char* argv[])
{
    static struct option long_options[] = {
        {"workdir",        required_argument, 0, 'd'},
        {"thread_num",     required_argument, 0, 'p'},
        {"speed",          required_argument, 0, 's'},
        {"size",           required_argument, 0, 'S'},
        {"fnum",           required_argument, 0, 'n'},
        {"frame",          required_argument, 0, 'f'},
        {"prefix",         required_argument, 0, 'b'},
        {"buffering",      no_argument,       0, 'B'},
        {0,                0 ,                0, 0  }
    };

    int c;
    const char* short_options = "h?d:p:s:f:S:n:b:B";

    int option_index = 0;

    int speed = 0 ;  // unit KB
    int frame = 0  ;  // 10 frame per second 
    ssize_t filesize = 0 ;
    int file_num = 0;
    int begin_index = 0 ;
    char *workdir = (char*)malloc(PATH_MAX+1);

    struct stat statbuf;
    int ret ;
    char *res;

    memset(workdir,'\0', PATH_MAX+1);
    while((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
    {
        switch(c)
        {
        case 'd':
            res = realpath(optarg, workdir);
            if(!res)
            {
                fprintf(stderr, "failed to get realpath for %s\n", optarg);
                exit(1);
            }

            ret = stat(workdir,&statbuf);
            if(ret !=0 )
            {
                fprintf(stderr,"failed to stat workdir %s\n", workdir);
                exit(1);
            }
            if(!S_ISDIR(statbuf.st_mode))
            {
                fprintf(stderr, "workdir (%s) is not directory\n",workdir);
                exit(1);
            }
            break;
        case 'p':
            thread_num = atoi(optarg);
            break;
        case 's':
            speed = atoi(optarg); 
            speed *= 1024 ; 
            break;
        case 'n':
            file_num = atoi(optarg); 
            break;
        case 'S':
            filesize = atoll(optarg); 
            filesize = filesize*1024*1024 ;
            break;
        case 'f':
            frame = atoi(optarg); 
            break;
        case 'b':
            begin_index = atoi(optarg); 
            break;
	case 'B':
	    buffering = 1;
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

    if(speed <=0 || file_num <= 0 || filesize <=0 ||frame <=0)
    {
        usage();
        exit(1);
    }

    int idx = 0 ; 

    pthread_t *tids =(pthread_t*) malloc(thread_num*sizeof(pthread_t));
    args_array = (struct worker_args*)malloc(thread_num*sizeof(struct worker_args)) ; 

    for(idx = 0 ; idx < thread_num; idx++)
    {
        args_array[idx].filesize = filesize;
        args_array[idx].p_workdir = workdir;
        args_array[idx].filenum = file_num;
        args_array[idx].begin_index = begin_index;
        args_array[idx].speed = speed;
        args_array[idx].frames = frame;
        args_array[idx].index = idx;
        args_array[idx].stat = (struct statistic*) malloc(sizeof(struct statistic));
    }


    for(idx = 0 ; idx < thread_num; idx++)
    {
        pthread_create(&(tids[idx]),NULL, worker, (void*)&(args_array[idx]));
    }

    struct sigaction new_action, old_action;
    new_action.sa_handler = sigint_handler;
    new_action.sa_flags = 0 ; 
    sigemptyset(&new_action.sa_mask);

    sigaction(SIGINT,&new_action, &old_action);

    atexit(print_result_and_exit);
    print_header(thread_num,speed,frame,filesize, file_num,begin_index);
    for(idx = 0 ; idx<thread_num; idx++)
    {
        pthread_join(tids[idx],NULL);
    }


    return 0;

}

