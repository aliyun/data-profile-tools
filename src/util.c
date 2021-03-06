/*
 * Copyright (c) 2013, Intel Corporation
 * Copyright (c) 2021, Alibaba Group Holding Limited
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* This file contains the helper routines. */

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include "include/types.h"
#include "include/util.h"
#include "include/proc.h"
#include "include/perf.h"
#include "include/damon.h"
#include "include/os/os_util.h"

#define KERNEL_ADDR_START	0xffffffff80000000

int g_pagesize;
struct timeval g_tvbase;

static int s_debuglevel;
static FILE *s_logfile;
static debug_ctl_t s_debug_ctl;
static dump_ctl_t s_dump_ctl;
static char s_exit_msg[EXIT_MSG_SIZE];

static unsigned int msdiff(struct timeval *, struct timeval *);
extern struct damon_proc_t target_procs;
extern pid_t damontop_pid;

/*
 * Allocate a buffer and reset it to zero.
 */
void *zalloc(size_t n)
{
	void *p;

	if (n == 0) {
		return (NULL);
	}

	if ((p = malloc(n)) != NULL) {
		(void)memset(p, 0, n);
	}

	return (p);
}

/*
 * Initialization for debug_print component.
 */
int debug_init(int debug_level, FILE * log)
{
	(void)memset(s_exit_msg, 0, EXIT_MSG_SIZE);
	(void)memset(&s_debug_ctl, 0, sizeof(s_debug_ctl));
	if (pthread_mutex_init(&s_debug_ctl.mutex, NULL) != 0) {
		return (-1);
	}

	s_debug_ctl.inited = B_TRUE;
	s_logfile = log;
	s_debuglevel = debug_level;
	return (0);
}

/*
 * Clean up the resources for debug_print component.
 */
void debug_fini(void)
{
	if (s_logfile != NULL) {
		(void)fclose(s_logfile);
	}

	if (s_debug_ctl.inited) {
		(void)pthread_mutex_destroy(&s_debug_ctl.mutex);
	}
}

/*
 * Write the message into log file according to different level.
 */
void debug_print(FILE * out, int level, const char *fmt, ...)
{
	va_list ap;

	if (level <= s_debuglevel) {
		if (out == NULL) {
			if (s_logfile != NULL) {
				(void)pthread_mutex_lock(&s_debug_ctl.mutex);
				(void)fprintf(s_logfile,
					      "%" PRIu64 ": ",
					      current_ms(&g_tvbase) / 1000);
				va_start(ap, fmt);
				(void)vfprintf(s_logfile, fmt, ap);
				va_end(ap);
				(void)fflush(s_logfile);
				(void)pthread_mutex_unlock(&s_debug_ctl.mutex);
			}
		} else {
			(void)pthread_mutex_lock(&s_debug_ctl.mutex);
			(void)fprintf(out,
				      "%" PRIu64 ": ",
				      current_ms(&g_tvbase) / 1000);
			va_start(ap, fmt);
			(void)vfprintf(out, fmt, ap);
			va_end(ap);
			(void)fflush(out);
			(void)pthread_mutex_unlock(&s_debug_ctl.mutex);
		}
	}
}

/*
 * Get the current timestamp and convert it to milliseconds
 * (timing from damontop startup).
 */
uint64_t current_ms(struct timeval *tvbase)
{
	struct timeval tvnow;

	(void)gettimeofday(&tvnow, 0);
	return (msdiff(&tvnow, tvbase));
}

void sleep_ms(int ms)
{
	struct timeval delay;

	delay.tv_sec = 0;
	delay.tv_usec = ms * 1000;
	select(0, NULL, NULL, NULL, &delay);
}

double ratio(uint64_t value1, uint64_t value2)
{
	double r;

	if (value2 > 0) {
		r = (double)((double)value1 / (double)value2);
	} else {
		r = 0.0;
	}

	return (r);
}

/*
 * DAMON status:
 *		thread: 0
 *		proc: 1
 */
static int is_valid_proc(pid_t pid)
{
	char maps_path[32] = {0};
	char data[32] = {0};
	int ret, fd;

	sprintf(maps_path, "/proc/%d/maps", pid);
	if ((fd = open(maps_path, O_RDONLY)) < 0) {
		return 0;
	}

	ret = read(fd, data, 8);
	close(fd);

	return (ret > 0) ? 1 : 0;
}

/*
 * DAMON status:
 *		off: 0
 *		on: 1
 */
static int get_damon_status(void)
{
	char data[8] = {0};
	int ret = 0, fd;

	if ((fd = open("/sys/kernel/debug/damon/monitor_on", O_RDONLY)) < 0) {
		stderr_print("kdamon_pid: No such file!\n");
		return 0;
	}

	ret = read(fd, data, 8);
	if (ret < 0) {
		stderr_print("error: read kdamon_pid failed!\n");
		return -1;
	}
	close(fd);

	if (!strcmp(data, "on\n")) {
		ret = 1;
	} else {
		ret = 0;
	}

	return ret;
}

/*
 * The processes number that monitored in cpu utilization way will
 * be limited at 32.
 */
static int damon_is_target_proc(pid_t pid)
{
	char *target_ids = "/sys/kernel/debug/damon/target_ids";
	int fd, ret = 0;
	char data[32 * 8];
	char *token;
	pid_t pid2;

	if ((fd = open(target_ids, O_RDONLY)) < 0) {
		debug_print(NULL, 2, "target_ids: No such file!\n");
		return 0;
	}

	memset(data, 0, 32*8 - 1);
	ret = read(fd, data, 32*8);
	if (ret < 0) {
		debug_print(NULL, 2, "target_ids!\n");
		close(fd);
		return 0;
	}
	close(fd);

	ret = 0;
	token = strtok(data, " ");
	pid2 = atoi(token);
	if (pid2 == pid)
		return 1;
	while (token != NULL) {
		token = strtok(NULL, " ");
		if (token == NULL)
			break;

		pid2 = atoi(token);
		if (pid2 <= 0) {
			debug_print(NULL, 2, "Invalid pid %d.\n", pid2);
			break;
		}
		if (pid2 == pid) {
			ret = 1;
			break;
		}
	}

	return ret;
}

static int procfs_walk(char *path, int **id_arr, int *num)
{
	static DIR *dirp;
	struct dirent *dentp;
	int i = 0, size = *num, id;
	int *arr1 = *id_arr, *arr2;

	if ((dirp = opendir(path)) == NULL) {
		return (-1);
	}

	while ((dentp = readdir(dirp)) != NULL) {
		if (dentp->d_name[0] == '.') {
			/* skip "." and ".." */
			continue;
		}

		if ((id = atoi(dentp->d_name)) == 0) {
			/* Not a valid pid or lwpid. */
			continue;
		}

		if (get_damon_status() == 1 &&
				damon_is_target_proc(id) == 0) {
			/* Not be traced in DAMON. */
			continue;
		}

		if (!is_valid_proc(id) || damontop_pid == id)
			continue;

		if (i >= size) {
			size = size << 1;
			if ((arr2 = realloc(arr1, size * sizeof(int))) == NULL) {
				free(arr1);
				*id_arr = NULL;
				*num = 0;
				return (-1);
			}

			arr1 = arr2;
		}

		arr1[i] = id;
		i++;
	}

	*id_arr = arr1;
	*num = i;

	(void)closedir(dirp);
	return (0);
}

int procfs_enum_id(char *path, int **id_arr, int *nids)
{
	int *ids, num = PROCFS_ID_NUM;

	if ((ids = zalloc(PROCFS_ID_NUM * sizeof(int))) == NULL) {
		return (-1);
	}

	if (procfs_walk(path, &ids, &num) != 0) {
		if (ids != NULL) {
			free(ids);
		}

		return (-1);
	}

	*id_arr = ids;
	*nids = num;
	return (0);
}

/*
 * Retrieve the process's pid from '/proc'
 */
int procfs_proc_enum(pid_t ** pids, int *num)
{
	/*
	 * It's possible that the id in return buffer is 0,
	 * the caller needs to check again.
	 */
	return (procfs_enum_id("/proc", (int **)pids, num));
}

/*
 * Get the interval in milliseconds between 2 timestamps.
 */
static unsigned int msdiff(struct timeval *tva, struct timeval *tvb)
{
	time_t sdiff = tva->tv_sec - tvb->tv_sec;
	suseconds_t udiff = tva->tv_usec - tvb->tv_usec;

	if (sdiff < 0) {
		return (0);
	}

	if (udiff < 0) {
		udiff += MICROSEC;
		sdiff--;
	}

	if (sdiff < 0) {
		return (0);
	}

	if (sdiff >= (MAX_VALUE / MS_SEC)) {
		return ((unsigned int)MAX_VALUE);
	}

	return ((unsigned int)(sdiff * MS_SEC + udiff / MS_SEC));
}

void exit_msg_put(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(s_exit_msg, EXIT_MSG_SIZE, fmt, ap);
	va_end(ap);
}

void exit_msg_print(void)
{
	if (strlen(s_exit_msg) > 0) {
		(void)printf("%s", s_exit_msg);
	}
}

/*
 * Convert the CPU cycles to nanoseconds.
 */
uint64_t cyc2ns(uint64_t cyc)
{
	uint64_t ns;

	ns = (uint64_t) (cyc * g_nsofclk);
	return (ns);
}

/*
 * Initialization for dump control structure.
 */
int dump_init(FILE * dump_file)
{
	(void)memset(&s_dump_ctl, 0, sizeof(s_dump_ctl));
	if ((s_dump_ctl.fout = dump_file) != NULL) {
		if ((s_dump_ctl.cache = zalloc(DUMP_CACHE_SIZE)) == NULL) {
			return (-1);
		}

		s_dump_ctl.pcur = s_dump_ctl.cache;
		s_dump_ctl.rest_size = DUMP_CACHE_SIZE;
	}

	return (0);
}

/*
 * Clean up resources of dump control structure.
 */
void dump_fini(void)
{
	if (s_dump_ctl.fout != NULL) {
		(void)fclose(s_dump_ctl.fout);
	}

	if (s_dump_ctl.cache != NULL) {
		free(s_dump_ctl.cache);
	}
}

/*
 * Write the message into dump file.
 */
void dump_write(const char *fmt, ...)
{
	va_list ap;
	int n;

	if (s_dump_ctl.fout == NULL) {
		return;
	}

	if (!s_dump_ctl.cache_mode) {
		va_start(ap, fmt);
		(void)vfprintf(s_dump_ctl.fout, fmt, ap);
		va_end(ap);
		(void)fflush(s_dump_ctl.fout);
	} else {
		va_start(ap, fmt);
		n = vsnprintf(s_dump_ctl.pcur, s_dump_ctl.rest_size, fmt, ap);
		va_end(ap);
		s_dump_ctl.pcur += n;
		s_dump_ctl.rest_size -= n;
	}
}

void dump_cache_enable(void)
{
	s_dump_ctl.cache_mode = B_TRUE;
}

void dump_cache_disable(void)
{
	s_dump_ctl.cache_mode = B_FALSE;
}

void dump_cache_flush(void)
{
	if (s_dump_ctl.fout != NULL) {
		(void)fprintf(s_dump_ctl.fout, "%s", s_dump_ctl.cache);
		(void)fflush(s_dump_ctl.fout);

		(void)memset(s_dump_ctl.cache, 0, DUMP_CACHE_SIZE);
		s_dump_ctl.pcur = s_dump_ctl.cache;
		s_dump_ctl.rest_size = DUMP_CACHE_SIZE;
		s_dump_ctl.cache_mode = B_FALSE;
	}
}

/*
 * Print the message to STDERR.
 */
void stderr_print(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	/* LINTED E_SEC_PRINTF_VAR_FMT */
	(void)vfprintf(stderr, format, ap);
	(void)fprintf(stderr, "\r");
	va_end(ap);
}

int array_alloc(void **pp, int *ncur, int *nmax, int size, int num)
{
	int i;
	void *p;

	if (*ncur == *nmax) {
		if (*pp == NULL) {
			if ((*pp = zalloc(num * size)) == NULL) {
				return (-1);
			}

			*nmax = num;
		} else {
			i = (*nmax) << 1;
			if ((p = realloc(*pp, i * size)) == NULL) {
				if (*pp != NULL) {
					free(*pp);
					*pp = NULL;
				}

				return (-1);
			}

			*pp = p;
			*nmax = i;
		}
	}

	return (0);
}

void pagesize_init(void)
{
	g_pagesize = getpagesize();
}

/*
* Get the TSC cycles.
*/
#ifdef __x86_64__
uint64_t rdtsc(void)
{
	uint64_t var;
	uint32_t hi, lo;

	__asm volatile
	 ("rdtsc":"=a" (lo), "=d"(hi));

	/* LINTED E_VAR_USED_BEFORE_SET */
	var = ((uint64_t) hi << 32) | lo;
	return (var);
}
#elif __aarch64__
/* TODO: support aarch64 */
uint64_t rdtsc(void)
{
	uint64_t var = 0;

	__asm__ volatile ("":::"memory");

	return var;
}
#else
uint64_t rdtsc(void)
{
	uint64_t var = 0;

	__asm volatile
	 ("rdtsc":"=A" (var));

	return var;
}
#endif

/*
 * Check the cpu name in proc info. Intel CPUs always have @ x.y
 * Ghz and that is the TSC frequency.
 */
int arch__cpuinfo_freq(double *freq, char *unit)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	int ret = -1;

	if ((f = fopen(CPUINFO_PATH, "r")) == NULL) {
		return (-1);
	}

	while (getline(&line, &len, f) > 0) {
		if (strncmp(line, "model name", sizeof("model name") - 1) != 0) {
			continue;
		}

		if (sscanf(line + strcspn(line, "@") + 1, "%lf%10s",
			   freq, unit) == 2) {
			if (strcasecmp(unit, "GHz") == 0) {
				*freq *= GHZ;
			} else if (strcasecmp(unit, "Mhz") == 0) {
				*freq *= MHZ;
			}
			ret = 0;
			break;
		}
	}

	free(line);
	fclose(f);
	return ret;
}

int is_userspace(uint64_t ip)
{
	return ip < KERNEL_ADDR_START;
}

unsigned long exec_cmd_return_ulong(char *cmd, int base)
{
	char buf[100] = {'\0'};
	int des_p[2], pid;

	if(pipe(des_p) == -1) {
		stderr_print(NULL, 2, "Pipe failed");
		exit(1);
	}

	pid = fork();
	if(pid == 0) {
		close(STDOUT_FILENO);
		/* replacing stdout with pipe write */
		dup2(des_p[1], STDOUT_FILENO);
		close(des_p[0]);

		system(cmd);
		exit(0);
	} else {
		/* parent */
		int status;

		close(des_p[1]);
		waitpid(pid, &status, 0);
		read(des_p[0], buf, sizeof(buf));
		if (buf[0] == '\0') {
			return -1;
		}
	}

	return strtoul(buf, NULL, base);
}
