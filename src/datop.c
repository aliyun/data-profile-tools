/*
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

/* This file contains main(). */

#include <inttypes.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <signal.h>
#include <libgen.h>
#include "include/types.h"
#include "include/util.h"
#include "include/proc.h"
#include "include/disp.h"
#include "include/perf.h"
#include "include/util.h"
#include "include/plat.h"
#include "include/damon.h"
#include "include/os/os_util.h"
#include "include/os/os_perf.h"

struct arch_info {
	cpu_type_t arch;
	char *name;
};
cpu_type_t s_cpu_type = CPU_ARCH_UNSUP;

extern char *optarg;
extern int opterr;
extern int optind;
extern int optopt;

static void sigint_handler(int sig);
static void print_usage(const char *exec_name);
extern void read_damon_attrs(const char *attrs, uint64_t *sample, uint64_t *aggr,
		uint64_t *regi, uint64_t *min, uint64_t *max);
extern void write_damon_attrs(uint64_t sample, uint64_t aggr,
		uint64_t regi, uint64_t min, uint64_t max);

#define O_PID 0x0001
#define O_NUM 0x0002
#define O_REG 0x0004

/*
 * Print command-line help information.
 */
static void print_usage(const char *exec_name)
{
	char buffer[PATH_MAX];

	(void)strncpy(buffer, exec_name, PATH_MAX);
	buffer[PATH_MAX - 1] = 0;

	stderr_print("Usage: %s [option(s)]\n", basename(buffer));
	stderr_print("  -h    print help\n"
		     "  -d    path of the file to save the data in screen\n"
		     "  -l    0/1/2, the level of output warning message\n"
		     "  -f    path of the file to save warning message.\n"
		     "        e.g. damontop -l 2 -f /tmp/warn.log.\n"
		     "  -n    number of tasks which will be monitored.\n"
		     "        only be available when the task pid not specified.\n"
		     "  -p    monitor the specified process.\n"
		     "        e.g. damontop -p <pid>.\n"
		     "  -r    set min or max regions.\n"
		     "        e.g. damontop -r 10,100, the min and max regions\n"
		     "        will be set to 10 and 100, respectively.\n"
		     "  -s    sampling precision: \n"
		     "        normal: balance precision and overhead (default)\n"
		     "        high  : high sampling precision\n"
		     "                (high overhead, not recommended option)\n"
		     "        low   : low sampling precision, suitable for high"
		     " load system\n");
}

int plat_detect(void)
{
	struct utsname buf;

	if (uname(&buf))
		return CPU_ARCH_UNSUP;

	debug_print(NULL, 2, "machine: %s\n", buf.machine);
	s_cpu_type = CPU_ARCH_ARM64;

	return 0;
}
/*
 * The main function.
 */
int main(int argc, char *argv[])
{
	int ret = 1, debug_level = 0;
	int options = 0;
	FILE *log = NULL, *dump = NULL;
	boolean_t locked = B_FALSE;
	uint64_t orig_sampling_intval, orig_aggr_intval, orig_regions_update;
	uint64_t orig_min, orig_max;
	pid_t pid;
	int c;
	const char delim[2] = ",";
	char *token;
	char *procs;

	if (!os_authorized()) {
		return (1);
	}

	if (access("/sys/kernel/debug/damon", 0)) {
		stderr_print("Not support DAMON!\n");
		goto L_EXIT0;
	}

	if (access("/sys/kernel/debug/damon/numa_stat", 0)) {
		stderr_print("Not support NUMA fault stat (DAMON)!\n");
		goto L_EXIT0;
	}

	damontop_pid = getpid();
	g_sortkey = SORT_KEY_NRA;
	g_precise = PRECISE_NORMAL;
	g_run_secs = TIME_NSEC_MAX;
	g_disp_intval = DISP_DEFAULT_INTVAL;
	optind = 1;
	opterr = 0;
	(void)gettimeofday(&g_tvbase, 0);

	read_damon_attrs("/sys/kernel/debug/damon/attrs", &orig_sampling_intval,
			&orig_aggr_intval, &orig_regions_update, &orig_min, &orig_max);
	online_ncpu_refresh();
	memset(&target_procs, 0, sizeof(target_procs));
	/*
	 * Parse command line arguments.
	 */
	while ((c = getopt(argc, argv, "d:l:o:p:f:n:t:hf:r:s:")) != EOF) {
		switch (c) {
		case 'h':
			print_usage(argv[0]);
			ret = 0;
			goto L_EXIT0;

		case 'l':
			debug_level = atoi(optarg);
			if ((debug_level < 0) || (debug_level > 2)) {
				stderr_print("Invalid log_level %d.\n",
					     debug_level);
				print_usage(argv[0]);
				goto L_EXIT0;
			}
			break;

		case 'f':
			if (optarg == NULL) {
				stderr_print("Invalid output file.\n");
				goto L_EXIT0;
			}

			if ((log = fopen(optarg, "w")) == NULL) {
				stderr_print("Cannot open '%s' for writing.\n",
					     optarg);
				goto L_EXIT0;
			}
			break;

		case 'n':
			target_procs.nr_proc = atoi(optarg);
			if ((target_procs.nr_proc < 0)
					|| (target_procs.nr_proc > PROC_MAX)) {
				stderr_print("Invalid process number %d.\n",
						target_procs.nr_proc);
				print_usage(argv[0]);
				goto L_EXIT0;
			}
			target_procs.ready = 0;
			target_procs.last_ms = current_ms(&g_tvbase);
			g_sortkey = SORT_KEY_CPU;
			g_disp_intval = DISP_MIN_INTVAL;
			break;

		case 'p':
			procs = strdup(optarg);

			memset(&target_procs, 0, sizeof(struct damon_proc_t));
			token = strtok(optarg, delim);
			target_procs.pid[0] = atoi(token);
			target_procs.nr_proc++;
			while (token != NULL) {
				token = strtok(NULL, delim);
				if (token == NULL)
					break;
				if (target_procs.nr_proc >= PROC_MAX) {
					stderr_print
					    ("The traced proc number over 32.\n");
					goto L_EXIT0;
				}

				pid = atoi(token);
				if (pid <= 0) {
					stderr_print("Invalid pid %d.\n", pid);
					print_usage(argv[0]);
					goto L_EXIT0;
				}
				target_procs.pid[target_procs.nr_proc] = pid;
				target_procs.nr_proc++;
			}

			target_procs.ready = 1;
			options |= O_PID;
			break;

		case 'r':
			token = strtok(optarg, delim);
			target_procs.min_regions = atoi(token);
			if (token != NULL) {
				token = strtok(NULL, delim);
				target_procs.max_regions =
					(token != NULL) ? atoi(token) : orig_max;
			}
			/* check valid regions */
			if (target_procs.max_regions < target_procs.min_regions
					|| target_procs.min_regions < 0
					|| target_procs.max_regions <= 0) {
				stderr_print("Invalid min/max regions: %d %d\n",
						target_procs.max_regions, target_procs.max_regions);
				goto L_EXIT0;
			} else {
				write_damon_attrs(orig_sampling_intval, orig_aggr_intval,
						orig_regions_update,
						target_procs.min_regions,
						target_procs.max_regions);
			}

			options |= O_REG;
			break;

		case 's':
			if (optarg == NULL) {
				print_usage(argv[0]);
				goto L_EXIT0;
			}

			if (strcasecmp(optarg, "high") == 0) {
				g_precise = PRECISE_HIGH;
				break;
			}

			if (strcasecmp(optarg, "low") == 0) {
				g_precise = PRECISE_LOW;
				break;
			}

			if (strcasecmp(optarg, "normal") == 0) {
				g_precise = PRECISE_NORMAL;
				break;
			}

			stderr_print("Invalid sampling_precision '%s'.\n",
				     optarg);
			print_usage(argv[0]);
			goto L_EXIT0;

		case 'd':
			if (optarg == NULL) {
				stderr_print("Invalid dump file.\n");
				goto L_EXIT0;
			}

			if ((dump = fopen(optarg, "w")) == NULL) {
				stderr_print("Cannot open '%s' for dump.\n",
					     optarg);
				goto L_EXIT0;
			}
			break;

		case 't':
			g_run_secs = atoi(optarg);
			if (g_run_secs <= 0) {
				stderr_print("Invalid run time %d.\n",
					     g_run_secs);
				print_usage(argv[0]);
				goto L_EXIT0;
			}
			break;

		case ':':
			stderr_print("Missed argument for option %c.\n",
				     optopt);
			print_usage(argv[0]);
			goto L_EXIT0;

		case '?':
			stderr_print("Unrecognized option %c.\n", optopt);
			print_usage(argv[0]);
			goto L_EXIT0;
		}
	}

	if (target_procs.nr_proc == 0) {
		/* set process number by default. */
		target_procs.nr_proc = 3;
		target_procs.ready = 0;
		target_procs.last_ms = current_ms(&g_tvbase);
		g_sortkey = SORT_KEY_CPU;
		g_disp_intval = DISP_MIN_INTVAL;
	}

	/* procs = "pid1,pid2,pid3" */
	if (options & O_PID) {
		if (monitor_start(procs) < 0) {
			stderr_print("Monitor %s failed.\n", procs);
			free(procs);
			goto L_EXIT0;
		}
		free(procs);
	}

	if (plat_detect() != 0) {
		stderr_print("CPU is not supported!\n");
		ret = 2;
		goto L_EXIT0;
	}

	if (os_damontop_lock(&locked) != 0) {
		stderr_print("Fail to lock damontop!\n");
		goto L_EXIT0;
	}

	if (locked) {
		stderr_print("Another damontop instance is running!\n");
		goto L_EXIT0;
	}

	if (debug_init(debug_level, log) != 0) {
		goto L_EXIT1;
	}

	log = NULL;

	if (dump_init(dump) != 0) {
		goto L_EXIT2;
	}

	dump = NULL;

	if (map_init() != 0) {
		goto L_EXIT3;
	}

	/*
	 * Initialize for the "window-switching" table.
	 */
	switch_table_init();

	if (proc_group_init() != 0) {
		goto L_EXIT4;
	}

	/*
	 * Calculate how many nanoseconds for a TSC cycle.
	 */
	os_calibrate(&g_nsofclk, &g_clkofsec);

	debug_print(NULL, 2, "Detected %d online CPUs\n", g_ncpus);
	stderr_print("DamonTOP is starting ...\n");

	if (disp_cons_ctl_init() != 0) {
		goto L_EXIT5;
	}

	/*
	 * Catch signals from terminal.
	 */
	if ((signal(SIGINT, sigint_handler) == SIG_ERR) ||
	    (signal(SIGHUP, sigint_handler) == SIG_ERR) ||
	    (signal(SIGQUIT, sigint_handler) == SIG_ERR) ||
	    (signal(SIGTERM, sigint_handler) == SIG_ERR) ||
	    (signal(SIGPIPE, sigint_handler) == SIG_ERR)) {
		goto L_EXIT7;
	}

	/*
	 * Initialize the perf sampling facility.
	 */
	if (perf_init() != 0) {
		debug_print(NULL, 2, "perf_init() is failed\n");
		goto L_EXIT7;
	}

	/*
	 * Initialize for display and create console thread & display thread.
	 */
	if (disp_init() != 0) {
		perf_fini();
		goto L_EXIT7;
	}

	/*
	 * Wait the disp thread to exit. The disp thread would
	 * exit when user hits the hotkey 'Q' or press "CTRL+C".
	 */
	disp_dispthr_quit_wait();

	/*
	 * Notify cons thread to exit.
	 */
	disp_consthr_quit();

	disp_fini();
	stderr_print("DamonTop is exiting ...\n");
	(void)fflush(stdout);
	ret = 0;

L_EXIT7:
	disp_cons_ctl_fini();

L_EXIT5:
	monitor_exit();		/* Stop tracing pid when exiting */
	/* restore DAMON config */
	if (options & O_REG)
		write_damon_attrs(orig_sampling_intval, orig_aggr_intval,
				orig_regions_update,
				orig_min, orig_max);
	proc_group_fini();

L_EXIT4:
	map_fini();

L_EXIT3:
	dump_fini();

L_EXIT2:
	debug_fini();

L_EXIT1:
	os_damontop_unlock();
	exit_msg_print();

L_EXIT0:
	if (dump != NULL) {
		(void)fclose(dump);
	}

	if (log != NULL) {
		(void)fclose(log);
	}

	return (ret);
}

/*
 * The signal handler.
 */
static void sigint_handler(int sig)
{
	switch (sig) {
	case SIGINT:
		(void)signal(SIGINT, sigint_handler);
		break;

	case SIGHUP:
		(void)signal(SIGHUP, sigint_handler);
		break;

	case SIGQUIT:
		(void)signal(SIGQUIT, sigint_handler);
		break;

	case SIGPIPE:
		(void)signal(SIGPIPE, sigint_handler);
		break;

	case SIGTERM:
		(void)signal(SIGTERM, sigint_handler);
		break;

	default:
		return;
	}

	/*
	 * It's same as the operation when user hits the hotkey 'Q'.
	 */
	disp_dispthr_quit_start();
}
