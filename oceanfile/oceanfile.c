
#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>

#define MAX_LEVEL 10 
#define FILE_SIZE_NUM_MAX 6
#define MB (1024*1024)
#define GB (1024*1024*1024)

enum op_mode_t{
	MKDIR_OP,
	WRITE_OP,
	READ_OP,
	RM_OP,
	RMDIR_OP
};


int debug = 0; 
char workdir[PATH_MAX+1] ;
int thread_num = 1;
char arch[1024+1] ; 
char type[1024+1] ;
int  skip_dir = 0;
int  random_flag = 0;
int  verbose_flag = 0;
int  direct_flag = 0;
enum op_mode_t  mode = WRITE_OP;
ssize_t  g_buffer_sz[FILE_SIZE_NUM_MAX];
ssize_t  g_file_sz[FILE_SIZE_NUM_MAX] ;
double  g_size_ratio[FILE_SIZE_NUM_MAX];
int g_step_ratio[FILE_SIZE_NUM_MAX];
int g_different_size_num = 0;
int g_different_buff_num = 0;

int g_interval = 0 ;

__thread int * array = NULL ;


void usage()
{
	fprintf(stdout, "oceanfile -d work_dir -p thread_num -a directory_arch [ -s filesize ] [ -b buffersize] -m [mkdir|read|write|rm|rmdir] [ -S ]\n");
	fprintf(stdout, "-d   --workdir          the directory in which the tests will run\n");
	fprintf(stdout, "-m   --mode             the mode of test: mkdir or read or write or rm or rmdir\n");
	fprintf(stdout, "-S   --skipdir          suppose all the directory have already exist, only create file\n");
	fprintf(stdout, "-r   --random           when read or write files, not one by one ,but in random order\n");
	fprintf(stdout, "-D   --direct_write     when open , add O_DIRECT flag\n");
	fprintf(stdout, "-p   --parallel         thread num in parallel\n");
	fprintf(stdout, "-a   --arch             directory tree architecture\n");
	fprintf(stdout, "-s   --filesize         size of file, support: [ -s 10K:0.3,32K:0.5,2M:0.2 ] \n");
	fprintf(stdout, "-b   --buffersize       buffer size of every write operation, support: [-b 10K,32K,1M ] \n");
	fprintf(stdout, "-i   --interval         output statistics every [interval] second\n");
	fprintf(stdout, "-v   --verbose          if v == 2 , output latency historgram statistics every [interval] second\n");
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

int parse_file_size_vector(char* buff)
{
	int i ;
	for( i = 0; i < FILE_SIZE_NUM_MAX; i ++)
	{
		g_file_sz[i] = 0;
		g_size_ratio[i] = 0;
	}
	char *p = strstr(buff, ",");
	char *inner_p  = NULL;
	double sum = 0.0;
	int j = 0;

	char* token;
	char* last ;
	if(p != NULL)
	{
		for(
				token = strtok_r(buff, ",", &last);
				token!= NULL ;
				token = strtok_r(NULL, ",", &last)
		   )
		{
			inner_p = strstr(token, ":");
			if(inner_p == NULL)
			{
				fprintf(stderr,"not found : in -s parameter\nusage: -s size1:ratio1,size2:ratio2\n");
				return -1 ;
			}
			else
			{
				char *inner_last = NULL; 
				char *inner_token = strdup(token);
				char* size = strtok_r(inner_token, ":" , &inner_last);
				char* ratio = strtok_r(NULL, ":", &inner_last);
				g_file_sz[j] = parse_space_size(size);
				g_size_ratio[j] = atof(ratio);

				free(inner_token);
				sum += g_size_ratio[j];
				if(sum > 1.0)
				{
					fprintf(stderr, "the sum of all the ratio is beyond 1.0\n");
					return -2;
				}
				if(++j > FILE_SIZE_NUM_MAX)
				{
					fprintf(stderr,"we support %d different size at max\n", FILE_SIZE_NUM_MAX);
					return -3 ;
				}
			}
		}
		if(sum != 1.0)
		{
			fprintf(stderr, "the sum of all the ratio is beyond 1.0\n");
			return -4;
		}
		sum = 0.0;
		for(i = 0 ; i < j ; i++)
		{
			sum += g_size_ratio[i];
			g_step_ratio[i] = (int)(100*sum) ;
		}
		g_different_size_num = j;
	}
	else
	{
		g_file_sz[0] = parse_space_size(buff);
		g_step_ratio[0] = 100;
		g_different_size_num = 1;
	}
	return 0;
}

int parse_buffer_size_vector(char* buffer)
{
	char* token = NULL;
	char delimit[1] = {','};
	char* last = NULL;
	int i ;
	for( i = 0; i < FILE_SIZE_NUM_MAX; i ++)
	{
		g_buffer_sz[i] = 0;
	}

	char* buffer_dup = strdup(buffer);
	char* p = strstr(buffer_dup, ",");
	int j = 0;
	if(p == NULL)
	{
		g_buffer_sz[0] = parse_space_size(buffer_dup);
		g_different_buff_num = 1;
		free(buffer_dup);
		return 0;
	}
	else
	{
		for(
				(token = strtok_r(buffer_dup, delimit, &last));
				token!= NULL ;
				(token = strtok_r(NULL, delimit, &last))
		   )
		{
			g_buffer_sz[j++] = parse_space_size(token);
		}

		g_different_buff_num = j;
		if(g_different_buff_num != g_different_size_num)
		{
			fprintf(stderr, "buffer size's num is not equal to file size's num\n");
			free(buffer_dup);
			return -1;
		}

		free(buffer_dup);
		return 0;
	}
}

int HIST_INTERVAL[] = {0,10,50,100,200,500,
	1000,2000,3000,4000,5000,6000,7000,8000,9000,
	10000,20000,40000,60000,80000,
	100000,200000,300000,400000,500000,600000,700000,800000,900000,
	1000000,2000000,4000000,8000000};
#define  N_HIST  (sizeof(HIST_INTERVAL)/sizeof(int)) 

struct latency_stat {
	unsigned long long op_times ;
	unsigned long long total ;
	unsigned long long min ;
	unsigned long long max ;
	unsigned long long hist_array[N_HIST];
}; 

struct operation_stat{
	unsigned long long mkdir_success;
	unsigned long long mkdir_eexist ;
	unsigned long long mkdir_fail;

	unsigned long long create_success ; 
	unsigned long long open_success ; 
	unsigned long long create_eexist ;
	unsigned long long open_fail;
	unsigned long long create_fail;

	unsigned long long write_success ; 
	unsigned long long write_bytes ;
	unsigned long long write_fail;

	unsigned long long stat_success;
	unsigned long long stat_fail;
	unsigned long long read_success ; 
	unsigned long long read_bytes;
	unsigned long long read_fail;

	unsigned long long remove_success;
	unsigned long long remove_fail;
	unsigned long long rmdir_success;
	unsigned long long rmdir_enoent ;
	unsigned long long rmdir_enotempty ;
	unsigned long long rmdir_fail ;

	struct latency_stat mkdir_l_stat ;
	struct latency_stat create_l_stat ;
	struct latency_stat open_l_stat ;
	struct latency_stat write_l_stat ;
	struct latency_stat stat_l_stat ;
	struct latency_stat read_l_stat ;
	struct latency_stat remove_l_stat ;
	struct latency_stat rmdir_l_stat ;
	char padding[40];
} __attribute__((__aligned__(64)));

struct operation_stat**  statistic;

struct operation_stat last_statistic ; 

static inline void __init_latency_stat(struct latency_stat* l_stat)
{
	l_stat->op_times = 0;
	l_stat->total = 0;
	l_stat->min = 0 ;
	l_stat->max =0;
	int i = 0; 
	for(i = 0 ; i < N_HIST ; i++ )
	{
		l_stat->hist_array[i] = 0;
	}
}

static inline void update_latency_stat(struct latency_stat* l_stat, unsigned long long latency)
{
	l_stat->op_times++ ;
	l_stat->total += latency ;

	if(l_stat->min == 0 || l_stat->min > latency)
		l_stat->min = latency ;

	if(l_stat->max < latency)
		l_stat->max = latency ;

	int i = 0 ; 
	for(i = 1; i < N_HIST; i++)
	{
		if(latency <= HIST_INTERVAL[i])
		{
			l_stat->hist_array[i-1] += 1;
			break;
		}
	}
	if( i == N_HIST)
	{
		l_stat->hist_array[i-1] += 1 ;
	}
}

static inline void __summary_latency_stat(struct latency_stat* result, struct latency_stat* part)
{
	result->op_times += part->op_times ;
	result->total += part->total;

	if(result->min == 0 || result->min > part-> min)
	{
		result->min = part->min ;
	}

	if(result->max < part->max)
	{
		result->max = part->max ;
	}

	int i ; 
	for( i = 0; i < N_HIST; i++)
	{
		result->hist_array[i] += part->hist_array[i];
	}
}


static inline void print_latency_stat(struct latency_stat* l_stat)
{
	if(l_stat->op_times == 0)
	{
		fprintf(stdout, "     avg: %-8d  min: %-8d  max: %-8d\n", 0,0,0);
	}
	else
	{
		fprintf(stdout, "     avg: %-8llu  min: %-8llu  max: %-8llu\n", 
				l_stat->total/l_stat->op_times,l_stat->min,l_stat->max);
		int i ;
		unsigned long long cum = 0 ;

		fprintf(stdout, "-----------------------------HISTGRAM---------------------------------------\n");
		for(i = 0; i < N_HIST ; i++)
		{
			if(cum >= l_stat->op_times)
			{
				break ;
			}
			cum += (l_stat->hist_array[i]) ; 
			if(cum == 0)
			{
				continue;
			}
			if(i < (N_HIST - 1) &&  HIST_INTERVAL[i+1] < 1000)
			{
				fprintf(stdout, "%6d(us)~%6d(us): %8llu %6.2f%%  %6.2f%% \n",
						HIST_INTERVAL[i],HIST_INTERVAL[i+1], l_stat->hist_array[i],
						(float)(100*l_stat->hist_array[i])/(l_stat->op_times), (float)(100*cum)/(l_stat->op_times));
			}
			else if(i < N_HIST - 1 )
			{
				fprintf(stdout, "%6d(ms)~%6d(ms): %8llu %6.2f%%  %6.2f%% \n",
						HIST_INTERVAL[i]/1000,HIST_INTERVAL[i+1]/1000, l_stat->hist_array[i],
						(float)(100*l_stat->hist_array[i])/(l_stat->op_times), (float)(100*cum)/(l_stat->op_times));
			}
			else
			{
				fprintf(stdout, "%6d(ms)~          : %8llu %6.2f%%  %6.2f%% \n",
						HIST_INTERVAL[i]/1000, l_stat->hist_array[i],
						(float)(100*l_stat->hist_array[i])/(l_stat->op_times), (float)(100*cum)/(l_stat->op_times));
			}
		}
		fprintf(stdout, "----------------------------------------------------------------------------\n");
	}
}

static void print_realtime_latency_stat(char* op_type, struct latency_stat* current, struct latency_stat* last)
{
	struct latency_stat this_loop ;
	this_loop.op_times = current->op_times - last->op_times;

	if (this_loop.op_times == 0 )
		return ;
	if (verbose_flag  != 2 )
	{
		return ;
	}

	this_loop.total = current->total - last->total;
	float avg = this_loop.total / this_loop.op_times;

	int i ;

	fprintf(stdout, "-----------------%-6s HISTGRAM-------------------------\n", 
			op_type);
	fprintf(stdout, "(avg = %6.2f, total = %8llu)\n", 
			avg, this_loop.op_times);
	unsigned long long cum = 0 ;
	unsigned long long this_times = 0;
	for(i = 0; i < N_HIST ; i++)
	{
		if (cum >= this_loop.op_times)
			break;
		this_times = current->hist_array[i] - last->hist_array[i];
		if(this_times ==0)
			continue;
		cum += this_times;

		if(i < (N_HIST - 1) &&  HIST_INTERVAL[i+1] < 1000)
		{
			fprintf(stdout, "%6d(us)~%6d(us): %8llu %6.2f%%  %6.2f%% \n",
					HIST_INTERVAL[i],HIST_INTERVAL[i+1], this_times,
					(float)(100*this_times)/(this_loop.op_times), (float)(100*cum)/(this_loop.op_times));
		}
		else if(i < N_HIST - 1 )
		{
			fprintf(stdout, "%6d(ms)~%6d(ms): %8llu %6.2f%%  %6.2f%% \n",
					HIST_INTERVAL[i]/1000,HIST_INTERVAL[i+1]/1000, this_times,
					(float)(100*this_times)/(this_loop.op_times), (float)(100*cum)/(this_loop.op_times));
		}
		else
		{
			fprintf(stdout, "%6d(ms)~          : %8llu %6.2f%%  %6.2f%% \n",
					HIST_INTERVAL[i]/1000, this_times,
					(float)(100*this_times)/(this_loop.op_times), (float)(100*cum)/(this_loop.op_times));

		}

	}
	fprintf(stdout, "-----------------------------------------------------------------------------------\n");
}
static void init_latency_stat(struct operation_stat* stat)
{
	__init_latency_stat(&(stat->mkdir_l_stat));
	__init_latency_stat(&(stat->create_l_stat));
	__init_latency_stat(&(stat->open_l_stat));
	__init_latency_stat(&(stat->write_l_stat));
	__init_latency_stat(&(stat->read_l_stat));
	__init_latency_stat(&(stat->stat_l_stat));
	__init_latency_stat(&(stat->remove_l_stat));
	__init_latency_stat(&(stat->rmdir_l_stat));
}

static void summary_latency_stat(struct operation_stat* stat , struct operation_stat* part)
{
	__summary_latency_stat(&(stat->mkdir_l_stat), &(part->mkdir_l_stat));
	__summary_latency_stat(&(stat->create_l_stat),&(part->create_l_stat));
	__summary_latency_stat(&(stat->open_l_stat), &(part->open_l_stat));
	__summary_latency_stat(&(stat->write_l_stat), &(part->write_l_stat));
	__summary_latency_stat(&(stat->read_l_stat), &(part->read_l_stat));
	__summary_latency_stat(&(stat->stat_l_stat), &(part->stat_l_stat));
	__summary_latency_stat(&(stat->remove_l_stat), &(part->remove_l_stat));
	__summary_latency_stat(&(stat->rmdir_l_stat), &(part->rmdir_l_stat));
}

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
	stat->write_bytes      = 0;
	stat->write_fail       = 0;

	stat->stat_success     = 0;
	stat->stat_fail        = 0;

	stat->read_success     = 0 ;
	stat->read_bytes       = 0 ;
	stat->read_fail        = 0;

	stat->remove_success = 0;
	stat->remove_fail = 0; 
	stat->rmdir_success = 0; 
	stat->rmdir_enoent = 0; 
	stat->rmdir_enotempty = 0; 
	stat->rmdir_fail = 0;

	init_latency_stat(stat);
	return 0;
}

static int print_statistic(struct operation_stat* stat)
{
	fprintf(stdout, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");


	/*write mode*/
	if (mode == MKDIR_OP)
	{

		fprintf(stdout, "mkdir_success   : %12llu ",   stat->mkdir_success);
		print_latency_stat(&(stat->mkdir_l_stat));
		fprintf(stdout, "mkdir_eexist    : %12llu\n",   stat->mkdir_eexist);
		fprintf(stdout, "mkdir_fail      : %12llu\n", stat->mkdir_fail);
	}
	else if(mode == WRITE_OP)
	{
		/*skip_dir = 1 mean skip create folder*/
		if(skip_dir != 1)
		{
			fprintf(stdout, "mkdir_success   : %12llu ",   stat->mkdir_success);
			print_latency_stat(&(stat->mkdir_l_stat));
			fprintf(stdout, "mkdir_eexist    : %12llu\n",   stat->mkdir_eexist);
			fprintf(stdout, "mkdir_fail      : %12llu\n", stat->mkdir_fail);
		}
		fprintf(stdout, "create_success  : %12llu ",   stat->create_success);
		print_latency_stat(&(stat->create_l_stat));
		fprintf(stdout, "create_eexist   : %12llu\n",   stat->create_eexist);
		fprintf(stdout, "create_fail     : %12llu\n",   stat->create_fail);
		fprintf(stdout, "open_success    : %12llu ",   stat->open_success);
		print_latency_stat(&(stat->open_l_stat));
		fprintf(stdout, "open_fail       : %12llu\n", stat->open_fail);
		fprintf(stdout, "write_success   : %12llu ", stat->write_success);
		print_latency_stat(&(stat->write_l_stat));
		fprintf(stdout, "write_fail      : %12llu\n", stat->write_fail);
	}
	else if(mode == READ_OP)
	{
		fprintf(stdout, "open_success    : %12llu ",   stat->open_success);
		print_latency_stat(&(stat->read_l_stat));
		fprintf(stdout, "open_fail       : %12llu\n", stat->open_fail);
		fprintf(stdout, "stat_success    : %12llu ", stat->stat_success);
		print_latency_stat(&(stat->stat_l_stat));
		fprintf(stdout, "stat_fail       : %12llu\n", stat->stat_fail);
		fprintf(stdout, "read_success    : %12llu ", stat->read_success);
		print_latency_stat(&(stat->read_l_stat));
		fprintf(stdout, "read_fail       : %12llu\n", stat->read_fail);
	}
	else if(mode == RM_OP )
	{
		fprintf(stdout, "remove_success  : %12llu ", stat->remove_success);
		print_latency_stat(&(stat->remove_l_stat));
		fprintf(stdout, "remove_fail     : %12llu\n", stat->remove_fail);
		fprintf(stdout, "rmdir_success   : %12llu ", stat->rmdir_success);
		print_latency_stat(&(stat->rmdir_l_stat));
		fprintf(stdout, "rmdir_enoent    : %12llu\n", stat->rmdir_enoent);
		fprintf(stdout, "rmdir_enotempty : %12llu\n", stat->rmdir_enotempty);
		fprintf(stdout, "rmdir_fail      : %12llu\n", stat->rmdir_fail);
	}
	else if(mode == RMDIR_OP )
	{
		fprintf(stdout, "rmdir_success   : %12llu ", stat->rmdir_success);
		print_latency_stat(&(stat->rmdir_l_stat));
		fprintf(stdout, "rmdir_enoent    : %12llu\n", stat->rmdir_enoent);
		fprintf(stdout, "rmdir_enotempty : %12llu\n", stat->rmdir_enotempty);
		fprintf(stdout, "rmdir_fail      : %12llu\n", stat->rmdir_fail);
	}

	fprintf(stdout, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
	return 0;

}

static void print_statistic_summary()
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

		total_stat.stat_success    += statistic[i]->stat_success ;
		total_stat.stat_fail       += statistic[i]->stat_fail ;
		total_stat.read_success    += statistic[i]->read_success ;
		total_stat.read_fail       += statistic[i]->read_fail ;

		total_stat.remove_success  += statistic[i]->remove_success ;
		total_stat.remove_fail     += statistic[i]->remove_fail ;
		total_stat.rmdir_success   += statistic[i]->rmdir_success ;
		total_stat.rmdir_enoent    += statistic[i]->rmdir_enoent ;
		total_stat.rmdir_enotempty += statistic[i]->rmdir_enotempty ;
		total_stat.rmdir_fail      += statistic[i]->rmdir_fail ;
		summary_latency_stat(&total_stat, statistic[i]);

	}

	print_statistic(&total_stat) ;
	return ;
}


static void __print_realtime_stat(struct operation_stat* current, struct operation_stat* last)
{
	int mkdir_ops  = (current->mkdir_success - last->mkdir_success) /g_interval ;
	int create_ops = (current->create_success - last->create_success)/g_interval;
	int open_ops = (current->open_success - last->open_success)/g_interval ;
	int write_ops = (current->write_success - last->write_success)/g_interval;
	ssize_t write_bw = (current->write_bytes - last->write_bytes)/g_interval ; 
	int read_ops = (current->read_success - last->read_success)/g_interval ;
	ssize_t read_bw = (current->read_bytes - last->read_bytes)/g_interval ;
	int stat_ops = (current->stat_success - last->stat_success)/g_interval ;
	int remove_ops = (current->remove_success - last->remove_success)/g_interval ;
	int rmdir_ops = (current->rmdir_success - last->rmdir_success)/g_interval;


	time_t ltime = time(NULL); 
	struct tm now_time ; 
	localtime_r(&ltime, &now_time);

	char buf[100];
	strftime(buf, 100, "%Y-%m-%d %H:%M:%S", &now_time);
	fprintf(stdout, "%s", buf);
	if(mode == MKDIR_OP)
	{
		fprintf(stdout, "%8d mkdir op/s\n", mkdir_ops) ; 
		if ( mkdir_ops > 0 )
			print_realtime_latency_stat("mkdir", &(current->mkdir_l_stat), &(last->mkdir_l_stat));
	}
	else if(mode == WRITE_OP)
	{
		char w_bw_buffer[256] ; 
		if(write_bw < 1024)
		{
			snprintf(w_bw_buffer, 256, "%7.2f  Bps", (float)(write_bw));
		}
		else if( write_bw > 1024 && write_bw < MB)
		{
			float write_bw_kb = (1.0 * write_bw/1024);
			snprintf(w_bw_buffer,256, "%7.2f KBps", write_bw_kb);
		}
		else if(write_bw > MB && write_bw < GB)
		{
			float write_bw_mb = (1.0 * write_bw/1024/1024);
			snprintf(w_bw_buffer,256, "%7.2f MBps", write_bw_mb);
		}
		else 
		{
			float write_bw_gb = (1.0 * write_bw/1024/1024/1024);
			snprintf(w_bw_buffer, 256, "%7.2f GBps", write_bw_gb);
		}
		if(skip_dir == 0 )
		{
			fprintf(stdout, "%8d mkdir op/s  %8d create op/s  %8d open op/s   %8d write op/s   write bw %s\n", 
					mkdir_ops,create_ops,open_ops, write_ops, w_bw_buffer) ; 
			if(mkdir_ops >0)
				print_realtime_latency_stat("mkdir", &(current->mkdir_l_stat), &(last->mkdir_l_stat));
			if(create_ops > 0)
				print_realtime_latency_stat("create", &(current->create_l_stat), &(last->create_l_stat));
			if(open_ops >0 && open_ops != create_ops)
				print_realtime_latency_stat("open", &(current->open_l_stat), &(last->open_l_stat));
			if(write_ops >0)
				print_realtime_latency_stat("write", &(current->write_l_stat), &(last->write_l_stat));

		}
		else
		{
			fprintf(stdout, "%8d create op/s  %8d open op/s   %8d write op/s\n", 
					create_ops,open_ops, write_ops) ; 
			if(create_ops > 0)
				print_realtime_latency_stat("create", &(current->create_l_stat), &(last->create_l_stat));
			if(open_ops >0 && open_ops != create_ops)
				print_realtime_latency_stat("open", &(current->open_l_stat), &(last->open_l_stat));
			if(write_ops >0)
				print_realtime_latency_stat("write", &(current->write_l_stat), &(last->write_l_stat));
		}
	}
	else if(mode == READ_OP)
	{
		char r_bw_buffer[256] ; 
		if(read_bw < 1024)
		{
			snprintf(r_bw_buffer, 256, "%7.2f  Bps", (float)(read_bw));
		}
		else if( read_bw > 1024 && read_bw < MB)
		{
			float read_bw_kb = (1.0 * read_bw/1024);
			snprintf(r_bw_buffer,256, "%7.2f KBps", read_bw_kb);
		}
		else if(read_bw > MB && read_bw < GB)
		{
			float read_bw_mb = (1.0 * read_bw/1024/1024);
			snprintf(r_bw_buffer,256, "%7.2f MBps", read_bw_mb);
		}
		else 
		{
			float read_bw_gb = (1.0 * read_bw/1024/1024/1024);
			snprintf(r_bw_buffer, 256, "%7.2f GBps", read_bw_gb);
		}
		fprintf(stdout, "%8d open op/s  %8d stat op/s  %8d read op/s   read bw %s\n", 
				open_ops, stat_ops, read_ops, r_bw_buffer) ; 
		if(open_ops >0)
			print_realtime_latency_stat("open", &(current->open_l_stat), &(last->open_l_stat));
		if(stat_ops > 0)
			print_realtime_latency_stat("stat", &(current->stat_l_stat), &(last->stat_l_stat));
		if(read_ops >0)
			print_realtime_latency_stat("read", &(current->read_l_stat), &(last->read_l_stat));

	}
	else if(mode == RM_OP)
	{
		fprintf(stdout, "%8d remove op/s  %8d rmdir op/s\n", 
				remove_ops, rmdir_ops) ; 
		if(remove_ops >0)
			print_realtime_latency_stat("remove", &(current->remove_l_stat), &(last->remove_l_stat));
		if(rmdir_ops >0)
			print_realtime_latency_stat("rmdir", &(current->rmdir_l_stat), &(last->rmdir_l_stat));
	}
	else if(mode == RMDIR_OP)
	{
		fprintf(stdout, "%8d rmdir op/s\n", 
				rmdir_ops) ; 
		if(rmdir_ops >0)
			print_realtime_latency_stat("rmdir", &(current->rmdir_l_stat), &(last->rmdir_l_stat));
	}

	memcpy(last, current,sizeof(struct operation_stat)) ;
}
static void print_realtime_stat()
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
		total_stat.write_bytes       += statistic[i]->write_bytes ;
		total_stat.write_fail       += statistic[i]->write_fail ;

		total_stat.stat_success    += statistic[i]->stat_success ;
		total_stat.stat_fail       += statistic[i]->stat_fail ;
		total_stat.read_success    += statistic[i]->read_success ;
		total_stat.read_bytes      += statistic[i]->read_bytes;
		total_stat.read_fail       += statistic[i]->read_fail ;

		total_stat.remove_success  += statistic[i]->remove_success ;
		total_stat.remove_fail     += statistic[i]->remove_fail ;
		total_stat.rmdir_success   += statistic[i]->rmdir_success ;
		total_stat.rmdir_enoent    += statistic[i]->rmdir_enoent ;
		total_stat.rmdir_enotempty += statistic[i]->rmdir_enotempty ;
		total_stat.rmdir_fail      += statistic[i]->rmdir_fail ;
		summary_latency_stat(&total_stat, statistic[i]);

	}

	__print_realtime_stat(&total_stat, &last_statistic);
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


unsigned long long time_us()                                                                                                                           
{
	struct timeval tv ;
	gettimeofday(&tv, NULL);

	return (unsigned long long)(tv.tv_sec*1000000 + tv.tv_usec);
}

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
	unsigned long long begin_us, end_us ;

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

		if(mode != RM_OP && (mode != RMDIR_OP))
		{

			begin_us = time_us();
			ret = mkdir(path_buf, 0755);
			if(ret == 0)
			{
				end_us = time_us();
				stat->mkdir_success++;
				update_latency_stat(&(stat->mkdir_l_stat), end_us - begin_us);
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
		else if(mode == RM_OP || mode == RMDIR_OP)
		{
			begin_us = time_us();
			ret = rmdir(path_buf);
			if(ret == 0)
			{
				end_us = time_us();
				stat->rmdir_success++;
				update_latency_stat(&(stat->rmdir_l_stat), end_us - begin_us);
			}
			else if(ret == -1 && errno == ENOENT)
			{
				stat->rmdir_enoent++;
			}
			else if(ret == -1 && errno == ENOTEMPTY)
			{
				/*a directory may be operated by different thread, so this may
				 * happen, and this is not a actually failure*/
				stat->rmdir_enotempty++;
			}
			else
			{
				strerror_r(errno, errmsg, sizeof(errmsg));
				fprintf(stderr, "THREAD-%4d failed to rmdir %s (%d: %s)\n",
						thread_idx, path_buf, errno, errmsg);
				stat->rmdir_fail++;
			}
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
	unsigned long long begin , end ;

	while(length < fsize)
	{
		if ((fsize - length) > buffer_size)
			current_size = buffer_size;
		else
			current_size = fsize - length ;

		begin = time_us();
		write_bytes = r_write(fd,buffer,current_size);

		if(write_bytes >= 0)
		{
			end = time_us();
			stat->write_success++ ; 
			stat->write_bytes += write_bytes ;
			length += write_bytes ;
			update_latency_stat(&(stat->write_l_stat), end-begin);
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


int r_read(int fd, char* buffer, ssize_t size)
{
	int reserve_bytes = size;
	int read_bytes = 0;
	while(reserve_bytes > 0)
	{
		read_bytes = read(fd, buffer, reserve_bytes);
		if(read_bytes >= 0)
		{
			reserve_bytes -= read_bytes;
			buffer += read_bytes;
		} 
		else if(read_bytes <0 && errno != EINTR)
		{
			fprintf(stderr, "error happened when read");
			return -1;
		}
	}

	if (reserve_bytes == 0)
		return size;
	else
		return -1;

}

int read_file(int fd, char* buffer, 
		ssize_t buffer_size, ssize_t fsize,
		struct operation_stat *stat)
{

	int ret = 0 ;
	ssize_t length = 0; 
	ssize_t current_size = 0 ;
	ssize_t read_bytes = 0 ;
	unsigned long long begin, end ;

	while(length < fsize)
	{
		if ((fsize - length) > buffer_size)
			current_size = buffer_size;
		else
			current_size = fsize - length ;

		begin = time_us();
		read_bytes = r_read(fd, buffer,current_size);

		if(read_bytes >= 0)
		{
			end = time_us();
			stat->read_success++ ; 
			stat->read_bytes += read_bytes;
			length += read_bytes ;
			update_latency_stat(&(stat->read_l_stat), end - begin);
		}
		else
		{
			stat->read_fail++ ; 
			ret = -1; 
			break;
		}
	}
	return ret ;
}

int shuffle(int * array, size_t n)
{
	struct timeval tv;
	struct drand48_data buffer;
	double result;
	gettimeofday(&tv, NULL);
	int usec = tv.tv_usec;
	srand48_r(usec, &buffer);

	if (n > 1) 
	{
		size_t i;
		for (i = n - 1; i > 0; i--) 
		{
			drand48_r(&buffer, &result);
			size_t j = (unsigned int) (result*(i+1));
			int t = array[j];
			array[j] = array[i];
			array[i] = t;
		}
	}
	return 0;
}


ssize_t calc_2_power_lower(ssize_t value)
{
    ssize_t cur = 1;
	while(cur < MB && (cur<<=1) < value)
	{
	    ;
	}
	if (cur > MB)
		cur = MB;
	return cur ;
}
int calc_file_size_and_buffer_size(ssize_t *file_sz, ssize_t* buffer_sz)
{
	int r = rand() % 100;
	int i ;
	for(i = 0 ; i < g_different_size_num; i++)
	{
		if(r < g_step_ratio[i])
		{
			*file_sz = g_file_sz[i];
			if(g_different_buff_num == 0)
			{
				*buffer_sz = calc_2_power_lower(*file_sz);
				return 0;
			}
			else if(g_different_buff_num == 1)
			{
				*buffer_sz = g_buffer_sz[0];
				return 0;
			}
			else
			{
				*buffer_sz = g_buffer_sz[i];
				return i ;
			}

		}
	}

	return -1;
}



int calc_suitable_buffer_size(ssize_t file_sz, ssize_t* buffer_sz)
{
	int i ; 
	if(g_different_buff_num == 0)
	{
        /*1MB buffer max*/
		*buffer_sz = calc_2_power_lower(file_sz);
		return 0;
	}

	if(g_different_buff_num == 1)
	{
		*buffer_sz = g_buffer_sz[0];
		return 0;
	}

	for(i = 0 ; i < g_different_size_num; i++)
	{
		if (g_file_sz[i] == file_sz)
		{
			*buffer_sz = g_buffer_sz[i];
			return i;
		}
	}
	int cur = -1;
	for(i = 0 ; i < g_different_buff_num; i++)
	{
		if(file_sz % g_buffer_sz[i] == 0)
		{
			*buffer_sz = g_buffer_sz[i];
			cur = i ; 
		}
	}
	return cur;

}
int process_level_file(struct arch_desc* a_desc,  int level, int thread_idx)
{
	int i ; 
	char path_buf[PATH_MAX+1];
	char errmsg[1024];
	assert(level == (a_desc->total_level - 1));
	int begin = a_desc->d_desc[level]->begin;
	int end = a_desc->d_desc[level]->end;

	unsigned long long begin_us , end_us ;
	ssize_t file_sz, buffer_sz ;

	if(end < begin)
	{
		return 0;
	}

	path_buf[PATH_MAX] = '\0';

	int pos[level+1];
	int current ;
	int cur = 0 ;
	int ret = 0 ; 
	int j ;
	struct operation_stat *op_stat = statistic[thread_idx];
	int fd = 0 ;
	char** buffer = NULL;
	struct stat statbuf;
	int index;

	if(g_different_buff_num != 0)
	{
		buffer = malloc(g_different_buff_num * sizeof(char*));
		for(i = 0 ; i < g_different_buff_num; i++)
		{
			buffer[i]  = malloc(g_buffer_sz[i]);
			memset(buffer[i],'b', g_buffer_sz[i]);
		}
	}
	else
	{
		buffer = malloc(sizeof(char*));
		buffer[0] = malloc(1024*1024);
	}


	array = (int*) malloc((end-begin+1)*sizeof(int));
	for(i = begin; i <=end ; i++)
	{
		array[i-begin] = i;
	}
	if(random_flag == 1)
	{
		shuffle(array, end-begin+1);
	}

	for(i = begin; i <= end ; i++ )
	{
		cur = array[i-begin] ; 
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
		if(debug)
			printf("Thread-%d : process %s\n", thread_idx, path_buf);

		if(mode == WRITE_OP)
		{
			begin_us = time_us();
			if (direct_flag == 1){
				fd = open(path_buf, O_CREAT|O_TRUNC|O_DIRECT|O_WRONLY|O_EXCL, 0666);
			}
			else {
				fd = open(path_buf, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL, 0666);
			}
			if(fd > 0)
			{
				end_us = time_us();
				op_stat->create_success++;
				update_latency_stat(&(op_stat->create_l_stat), end_us - begin_us);
				op_stat->open_success++ ; 
				update_latency_stat(&(op_stat->open_l_stat), end_us - begin_us);
			}
			else if(fd == -1 && errno == EEXIST)
			{
				op_stat->create_eexist++;
				begin_us = time_us();
				fd = open(path_buf, O_TRUNC|O_WRONLY);
				if(fd < 0)
				{
					strerror_r(errno, errmsg, sizeof(errmsg));
					fprintf(stderr, "THREAD-%-4d: failed to open %s (%d: %s)\n",
							thread_idx, path_buf, errno, errmsg);
					op_stat->open_fail++ ;
					continue;
				}
				else
				{
					end_us = time_us();
					op_stat->open_success++;
					update_latency_stat(&(op_stat->open_l_stat), end_us - begin_us);
				}
			}
			else
			{
				op_stat->create_fail++;
				op_stat->open_fail++;
				strerror_r(errno, errmsg, sizeof(errmsg));
				fprintf(stderr, "THREAD-%-4d: failed to create %s (%d: %s)\n",
						thread_idx, path_buf, errno, errmsg);
				continue;
			}


			if(g_different_size_num != 0)
			{
				index = calc_file_size_and_buffer_size(&file_sz, &buffer_sz);
				ret =  write_file(fd, buffer[index], buffer_sz, file_sz, op_stat);
				if(ret != 0)
				{
					strerror_r(errno, errmsg, sizeof(errmsg));
					fprintf(stderr, "THREAD-%-4d:  %d errors happened while write file %s (%d: %s)\n",
							thread_idx, -(ret), path_buf,errno, errmsg);
				}
			}

			close(fd);
		}
		else if (mode == READ_OP)
		{
			ssize_t current_file_sz; 
			if(g_different_size_num == 0)
			{
				goto just_readopen;
			}
			begin_us = time_us();
			ret = stat(path_buf, &statbuf);
			if(ret)
			{
				strerror_r(errno, errmsg, sizeof(errmsg));
				fprintf(stderr, "THREAD-%-4d:  %d errors happened while stat file %s (%d: %s)\n",
						thread_idx, -(ret), path_buf,errno, errmsg);
				op_stat->stat_fail++;
				continue;
			}
			else
			{
				end_us = time_us();
				op_stat->stat_success++;
				update_latency_stat(&(op_stat->stat_l_stat), end_us - begin_us);
			}

just_readopen:
			if(g_different_size_num != 0)
				current_file_sz = statbuf.st_size;
			else 
				current_file_sz = 0;

			begin_us = time_us();
			fd = open(path_buf, O_RDONLY);
			if(fd < 0)
			{
				strerror_r(errno, errmsg, sizeof(errmsg));
				fprintf(stderr, "THREAD-%-4d: failed to open %s (%d: %s)\n",
						thread_idx, path_buf, errno, errmsg);
				op_stat->open_fail++ ;
				continue;
			}
			else
			{
				end_us = time_us();
				op_stat->open_success++;
				update_latency_stat(&(op_stat->open_l_stat), end_us - begin_us);
			}

			if(current_file_sz > 0 )
			{

				index = calc_suitable_buffer_size(current_file_sz, &buffer_sz);
				if(index < 0)
				{
					fprintf(stderr, "THREAD-%-4d:  %d errors happened while read file %s (%s)\n",
							thread_idx, -(ret), path_buf, "invalid buffer sz specified");
					close(fd);
					continue;
				}
				ret = read_file(fd, buffer[index], buffer_sz, current_file_sz, op_stat);
				if(ret != 0)
				{
					strerror_r(errno, errmsg, sizeof(errmsg));
					fprintf(stderr, "THREAD-%-4d:  %d errors happened while read file %s (%d: %s)\n",
							thread_idx, -(ret), path_buf,errno, errmsg);
				}
			}
			close(fd);
		}
		else if (mode == RM_OP)
		{
			begin_us = time_us();
			ret = remove(path_buf);
			if(ret == 0)
			{
				end_us = time_us();
				op_stat->remove_success++;
				update_latency_stat(&(op_stat->remove_l_stat), end_us-begin_us);
			}
			else
			{
				op_stat->remove_fail++;
				strerror_r(errno, errmsg, sizeof(errmsg));
				fprintf(stderr, "THREAD-%-4d:  %d errors happened while rm file %s (%d: %s)\n",
						thread_idx, -(ret), path_buf,errno, errmsg);
			}
		}


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

	if(level < a_desc->total_level - 1 || (mode == MKDIR_OP) || mode == RMDIR_OP)
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
	 * the last level). So we only proecss the last level
	 * if the mode == 1 mean we want to read , we suppose all the dir has create
	 * alreay, so we only process the last level, I mean read the files */
	if(skip_dir == 1 || mode == READ_OP || mode == RM_OP || mode == RMDIR_OP)
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

	if(mode == RM_OP || mode == RMDIR_OP)
	{
		for(i = a_desc->total_level - 2 ; i >=0 ; i-- )
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

	if(array != NULL)
	{
		free(array);
		array = NULL;
	}


	return NULL;

}

void signal_handler(int signo)
{
	switch(signo)
	{
	case SIGINT:
		exit(130);
		break;
	case SIGUSR1:
		print_statistic_summary();
		break;
	}
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
		{"direct_write",  no_argument,       0, 'D'},
		{"skipdir",       no_argument,       0, 'S'},
		{"random",        no_argument,       0, 'r'},
		{"parallel",      required_argument, 0, 'p'},
		{"arch",          required_argument, 0, 'a'},
		{"filesize",      required_argument, 0, 's'},
		{"buffersize",    required_argument, 0, 'b'},
		{"interval",      required_argument, 0, 'i'},
		{"verbose",       required_argument, 0, 'v'},
		{"mode",          required_argument, 0, 'm'},
		{0, 0 , 0, 0}
	};

	memset(workdir,'\0', PATH_MAX+1);
	memset(arch, '\0', 1024 + 1);
	memset(type, '\0', 1024 + 1);

	while((ch = getopt_long(argc, argv, "h?d:p:a:b:m:s:i:v:SDr", long_options, &option_index)) != -1)
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

		case 'S':
			skip_dir = 1;
			break;
		case 'D':
			direct_flag =1 ;
			break;
		case 'r':
			random_flag = 1;
			break;

		case 'p':
			thread_num = atoi(optarg);
			break;

		case 'a':
			strncpy(arch,optarg,1024);
			break;

		case 's':
			ret = parse_file_size_vector(optarg);
			if(ret !=0)
			{
				fprintf(stderr, "invalid filesize specified\n");
				exit(1);
			}
			break; 

		case 'b':
			ret = parse_buffer_size_vector(optarg);
			if(ret != 0)
			{
				fprintf(stderr, "invalid buffer size specified\n");
				exit(1);
			}
			break;

		case 'm':
			if(strcmp(optarg, "read")==0)
				mode = READ_OP;
			else if (strcmp(optarg, "rm") == 0)
				mode = RM_OP;
			else if(strcmp(optarg, "mkdir") ==0)
				mode = MKDIR_OP;
			else if(strcmp(optarg, "rmdir") == 0)
				mode = RMDIR_OP;
			else
				mode = WRITE_OP;

			break;

		case 'i':
			g_interval = atoi(optarg) ;
			break ;

		case 'v':
			verbose_flag = atoi(optarg);
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
	if(g_interval <0)
	{
		fprintf(stderr, "realtime statistic interval must bigger than zero\n");
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
	new_action.sa_handler = signal_handler;
	new_action.sa_flags = 0 ; 
	sigemptyset(&new_action.sa_mask);

	sigaction(SIGINT,&new_action, &old_action);
	sigaction(SIGUSR1,&new_action, &old_action);
	atexit(print_statistic_summary);


	if(g_interval > 0)
	{
		init_statistic(&last_statistic);

		struct sigaction timer_action, old_timer_action ; 
		timer_action.sa_handler = print_realtime_stat ;
		timer_action.sa_flags = 0;
		sigemptyset(&new_action.sa_mask);

		sigaction(SIGALRM, &timer_action, &old_timer_action);

		struct itimerval tick ; 
		memset(&tick, 0, sizeof(tick));

		tick.it_value.tv_sec = g_interval ;
		tick.it_value.tv_usec = 0 ;
		tick.it_interval.tv_sec = g_interval;
		tick.it_interval.tv_usec = 0 ;

		ret = setitimer(ITIMER_REAL, &tick, NULL);
		if(ret)
		{
			fprintf(stderr, "failed to set timer for realtime ops, ignore this error and continue\n");
		}
	}

	for(i = 0 ; i < thread_num ; i++)
	{
		pthread_join(tid_array[i], NULL);
	}

	return 0;
}

