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

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../include/types.h"
#include "../include/proc.h"
#include "../include/util.h"
#include "../include/disp.h"
#include "../include/perf.h"
#include "../include/ui_perf_map.h"
#include "../include/plat.h"
#include "../include/pfwrapper.h"
#include "../include/damon.h"
#include "../include/os/os_perf.h"
#include "../include/os/os_util.h"

precise_type_t g_precise;
perf_damon_event_t *perf_damon_conf;

typedef struct _profiling_conf {
	pf_conf_t conf_arr[PERF_COUNT_NUM];
} profiling_conf_t;

static pf_profiling_rec_t *s_profiling_recbuf = NULL;
static profiling_conf_t s_profiling_conf;
static boolean_t s_partpause_enabled;

static boolean_t damon_event_valid()
{
	return (perf_damon_conf->map_base != MAP_FAILED);
}

static void countval_diff_base(pf_profiling_rec_t * record)
{
	count_value_t *countval_last = &perf_damon_conf->countval_last;
	count_value_t *countval_new = &record->countval;
	int i;

	for (i = 0; i < PERF_COUNT_NUM; i++) {
		/*
		 * PERF_COUNT_DAMON_START
		 * PERF_COUNT_DAMON_END,
		 * PERF_COUNT_DAMON_NR_ACCESS
		 */
		countval_last->counts[i] = countval_new->counts[i];
	}
}

static void countval_max(pf_profiling_rec_t * record,
		count_value_t * max_rec)
{
	count_value_t *countval_last = &perf_damon_conf->countval_last;
	count_value_t *countval_new = &record->countval;
	count_value_t *countval_max = countval_last;
	int i;

	if (countval_last->counts[PERF_COUNT_DAMON_NR_ACCESS]
	    < countval_new->counts[PERF_COUNT_DAMON_NR_ACCESS]) {
		countval_max = countval_new;
	}

	for (i = 0; i < PERF_COUNT_NUM; i++) {
		/* Only record max value */
		max_rec->counts[i] = countval_max->counts[i];
	}
}

/*
 * smpl: update perf data for each core.
 */
static int __profiling_smpl(void)
{
	pf_profiling_rec_t *record;
	track_proc_t *proc;
	count_value_t max_record;
	int i, j, record_num;

	if (!damon_event_valid()) {
		return (0);
	}

	/*
	 * The record is grouped by pid/tid.
	 */
	pf_profiling_record(s_profiling_recbuf, &record_num);
	if (record_num == 0) {
		return 0;
	}

	/* FIXME */
	countval_diff_base(&s_profiling_recbuf[0]);

	debug_print(NULL, 2, "record number: %d\n", record_num);
	for (i = 1; i < record_num; i++) {
		record = &s_profiling_recbuf[i];

		if (record->pid == (unsigned int)-1 ||
		    record->tid == (unsigned int)-1) {
			continue;
		}

		countval_max(record, &max_record);

		if ((proc = proc_find(record->pid)) == NULL) {
			return 0;
		}

		pthread_mutex_lock(&proc->mutex);
		for (j = 0; j < PERF_COUNT_NUM; j++) {
			if (!s_partpause_enabled) {
				/*
				 * The max value in 'max_record' now.
				 * Here, the perf data will be updated to proc->countval_arr.
				 */
				proc_countval_update(proc, i, j,
						     max_record.counts[j]);
			}
		}

		pthread_mutex_unlock(&proc->mutex);
		proc_refcount_dec(proc);
	}

	return 0;
}

static int __profiling_partpause(void *arg)
{
	perf_count_id_t perf_count_id = (perf_count_id_t) arg;
	int i;

	if (perf_count_id == PERF_COUNT_INVALID ||
	    perf_count_id == PERF_COUNT_CORE_CLK) {
		return (pf_profiling_stop());
	}

	for (i = 1; i < PERF_COUNT_NUM; i++) {
		if (i != perf_count_id) {
			pf_profiling_stop();
		} else {
			pf_profiling_start();
		}
	}

	return (0);
}

static int __profiling_multipause(void *arg)
{
	perf_count_id_t *perf_count_ids = (perf_count_id_t *) arg;
	int i, j;
	boolean_t tmp[PERF_COUNT_NUM] = { B_FALSE };

	/*
	 * Prepare tmp. Each element of tmp will hold either
	 * True or False based on whether that event needs to
	 * be enabled or disabled.
	 */
	for (i = 1; i < PERF_COUNT_NUM; i++) {
		for (j = 0; j < UI_PERF_MAP_MAX; j++) {
			if (i == perf_count_ids[j]) {
				tmp[i] = B_TRUE;
			}
		}
	}

	for (i = 1; i < PERF_COUNT_NUM; i++) {
		if (!tmp[i]) {
			pf_profiling_stop();
		} else {
			pf_profiling_start();
		}
	}

	return (0);
}

static int __profiling_restore(void *arg)
{
	perf_count_id_t perf_count_id = (perf_count_id_t) arg;
	int i;

	if (perf_count_id == PERF_COUNT_INVALID ||
	    perf_count_id == PERF_COUNT_CORE_CLK) {
		return (pf_profiling_start());
	}

	pf_profiling_stop();

	/*
	 * Discard the existing records in ring buffer.
	 */
	pf_profiling_record(NULL, NULL);

	for (i = 1; i < PERF_COUNT_NUM; i++) {
		pf_profiling_start();
	}

	return (0);
}

static int __profiling_multi_restore(void *arg)
{
	perf_count_id_t *perf_count_ids = (perf_count_id_t *) arg;
	int i;

	for (i = 0; i < UI_PERF_MAP_MAX; i++) {
		if (perf_count_ids[i] != PERF_COUNT_INVALID &&
		    perf_count_ids[i] != PERF_COUNT_CORE_CLK) {
			pf_profiling_stop();
		}
	}

	/*
	 * Discard the existing records in ring buffer.
	 */
	pf_profiling_record(NULL, NULL);

	for (i = 1; i < PERF_COUNT_NUM; i++) {
		pf_profiling_start();
	}

	return (0);
}

static int profiling_pause(void)
{
	return pf_profiling_stop();
}

static int profiling_stop(void)
{
	profiling_pause();
	pf_resource_free();

	return (0);
}

static int profiling_start(perf_ctl_t * ctl,
		task_profiling_t * task __attribute__ ((unused)))
{
	pf_conf_t *conf_arr = s_profiling_conf.conf_arr;

	if (conf_arr[1].config == INVALID_CONFIG) {
		/*
		 * Invalid config is at the end of array.
		 */
		return -1;
	}

	if (pf_profiling_setup(1, &conf_arr[1]) != 0) {
		return -1;
	}

	profiling_pause();

	/* Start to count on each CPU. */
	if (pf_profiling_start() != 0) {
		return -1;
	}

	ctl->last_ms = current_ms(&g_tvbase);
	return 0;
}

static int profiling_smpl(perf_ctl_t * ctl,
		task_profiling_t * task __attribute__ ((unused)), int *intval_ms)
{
	int ret;
	*intval_ms = current_ms(&g_tvbase) - ctl->last_ms;
	proc_intval_update(*intval_ms);

	ret = __profiling_smpl();
	if (ret != 0)
		return -1;

	ctl->last_ms = current_ms(&g_tvbase);
	return 0;
}

static int
profiling_partpause(perf_ctl_t * ctl __attribute__ ((unused)),
		task_partpause_t * task __attribute__ ((unused)))
{
	__profiling_partpause((void *)task->perf_count_id);

	s_partpause_enabled = B_TRUE;
	return (0);
}

static int
profiling_multipause(perf_ctl_t * ctl __attribute__ ((unused)),
		     task_multipause_t * task __attribute__ ((unused)))
{
	__profiling_multipause((void *)task->perf_count_ids);
	s_partpause_enabled = B_TRUE;
	return (0);
}

static int profiling_restore(perf_ctl_t * ctl,
		task_restore_t * task __attribute__ ((unused)))
{
	__profiling_restore((void *)task->perf_count_id);

	s_partpause_enabled = B_FALSE;
	ctl->last_ms = current_ms(&g_tvbase);
	return (0);
}

static int profiling_multi_restore(perf_ctl_t * ctl,
		task_multi_restore_t * task)
{
	__profiling_multi_restore((void *)task->perf_count_ids);

	s_partpause_enabled = B_FALSE;
	ctl->last_ms = current_ms(&g_tvbase);
	return (0);
}

static int maplist_update(perf_ctl_t * ctl)
{
	ctl->last_ms = current_ms(&g_tvbase);
	return 0;
}

static int maplist_intval_update(perf_ctl_t * ctl, task_ml_t * task,
		int *intval_ms)
{
	*intval_ms = current_ms(&g_tvbase) - ctl->last_ms;
	proc_intval_update(*intval_ms);
	ctl->last_ms = current_ms(&g_tvbase);
	return 0;
}

boolean_t os_profiling_started(perf_ctl_t * ctl)
{
	if ((ctl->status == PERF_STATUS_PROFILING_PART_STARTED) ||
	    (ctl->status == PERF_STATUS_PROFILING_STARTED)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/* ARGSUSED */
int os_profiling_start(perf_ctl_t * ctl, perf_task_t * task)
{
	if (perf_profiling_started()) {
		perf_status_set(PERF_STATUS_PROFILING_STARTED);
		debug_print(NULL, 2, "profiling started yet\n");
		return (0);
	}

	os_allstop();

	if (profiling_start(ctl, (task_profiling_t *) (task)) != 0) {
		exit_msg_put
		    ("Fail to setup perf (probably permission denied)!\n");
		debug_print(NULL, 2, "os_profiling_start failed\n");
		perf_status_set(PERF_STATUS_PROFILING_FAILED);
		return (-1);
	}

	debug_print(NULL, 2, "os_profiling_start success\n");
	perf_status_set(PERF_STATUS_PROFILING_STARTED);
	return (0);
}

int os_profiling_smpl(perf_ctl_t * ctl, perf_task_t * task, int *intval_ms)
{
	task_profiling_t *t = (task_profiling_t *) task;
	int ret = -1;

	proc_enum_update(0);
	proc_profiling_clear();

	if (profiling_smpl(ctl, t, intval_ms) != 0) {
		perf_status_set(PERF_STATUS_PROFILING_FAILED);
		goto L_EXIT;
	}

	ret = 0;

L_EXIT:
	if (ret == 0)
		if (t->use_dispflag1)
			disp_profiling_data_ready(*intval_ms);
		else
			disp_flag2_set(DISP_FLAG_PROFILING_DATA_READY);
	else if (t->use_dispflag1)
		disp_profiling_data_fail();
	else
		disp_flag2_set(DISP_FLAG_PROFILING_DATA_FAIL);

	return (ret);
}

int os_profiling_partpause(perf_ctl_t * ctl, perf_task_t * task)
{
	profiling_partpause(ctl, (task_partpause_t *) (task));
	perf_status_set(PERF_STATUS_PROFILING_PART_STARTED);
	return (0);
}

int os_profiling_multipause(perf_ctl_t * ctl, perf_task_t * task)
{
	profiling_multipause(ctl, (task_multipause_t *) (task));
	perf_status_set(PERF_STATUS_PROFILING_MULTI_STARTED);
	return (0);
}

int os_profiling_restore(perf_ctl_t * ctl, perf_task_t * task)
{
	proc_profiling_clear();
	profiling_restore(ctl, (task_restore_t *) (task));
	perf_status_set(PERF_STATUS_PROFILING_STARTED);
	return (0);
}

int os_profiling_multi_restore(perf_ctl_t * ctl, perf_task_t * task)
{
	proc_profiling_clear();
	profiling_multi_restore(ctl, (task_multi_restore_t *) (task));
	perf_status_set(PERF_STATUS_PROFILING_STARTED);
	return (0);
}

int os_ml_start(perf_ctl_t * ctl, perf_task_t * task __attribute__ ((unused)))
{
	os_allstop();
	// proc_profiling_clear();

	if (maplist_update(ctl) != 0) {
		/*
		 * It could be failed if the kernel doesn't support PEBS LL.
		 */
		debug_print(NULL, 2, "ml_start is failed\n");
		perf_status_set(PERF_STATUS_ML_FAILED);
		return (-1);
	}

	debug_print(NULL, 2, "ml_start success\n");
	perf_status_set(PERF_STATUS_MAPLIST_STARTED);
	return (0);
}

int os_maplist_events(perf_ctl_t * ctl, perf_task_t * task, int *intval_ms)
{
	if (!perf_maplist_is_start()) {
		return (-1);
	}

	proc_enum_update(0);

	if (maplist_intval_update(ctl, (task_ml_t *) (task), intval_ms) != 0) {
		perf_status_set(PERF_STATUS_ML_FAILED);
		disp_maplist_data_fail();
		return (-1);
	}

	disp_maplist_data_ready(*intval_ms);
	return (0);
}

static void profiling_init(profiling_conf_t * conf)
{
	plat_event_config_t cfg;
	pf_conf_t *conf_arr = conf->conf_arr;
	FILE *fp = NULL;
	char *damon_format =
	    "/sys/kernel/debug/tracing/events/damon/damon_aggregated/format";
	char line[32] = { 0 };
	char key[32];
	char value[32];
	int event_ID = 0;

	sys_profiling_config(1, &cfg);
	conf_arr[1].perf_count_id = 1;
	conf_arr[1].type = cfg.type;
	switch (conf_arr[1].type) {
	case PERF_TYPE_TRACEPOINT:
		/* The event ID must been checked here. */
		fp = fopen(damon_format, "r");
		while (fgets(line, 32, fp)) {
			memset(key, 0, sizeof(char) * 32);
			memset(value, 0, sizeof(char) * 32);
			sscanf(line, "%[^:]: %s", key, value);
			if (!strcmp(key, "ID")) {
				event_ID = atoi(value);
				break;
			}
		}
		fclose(fp);
		conf_arr[1].config = event_ID;
		break;
	case PERF_TYPE_RAW:
		if (cfg.config != INVALID_CODE_UMASK) {
			conf_arr[1].config = cfg.other_attr;
		} else {
			conf_arr[1].config = INVALID_CONFIG;
		}
		break;
	case PERF_TYPE_HARDWARE:
		conf_arr[1].type = 8;
		conf_arr[1].config = cfg.other_attr;
		break;
	default:
		break;
	}

	conf_arr[1].sample_period = g_sample_period[1][g_precise];
}

int os_perf_init(void)
{
	int ringsize, size;

	s_profiling_recbuf = NULL;
	s_partpause_enabled = B_FALSE;

	ringsize = pf_ringsize_init();
	size = ((ringsize / sizeof(pf_profiling_rbrec_t)) + 1) *
	    sizeof(pf_profiling_rec_t);

	if ((s_profiling_recbuf = zalloc(size)) == NULL) {
		return (-1);
	}
	if ((perf_damon_conf = zalloc(sizeof(perf_damon_event_t))) == NULL) {
		return (-1);
	}
	perf_damon_conf->map_base = MAP_FAILED;
	perf_damon_conf->perf_fd = INVALID_FD;

	profiling_init(&s_profiling_conf);

	return 0;
}

void os_perf_fini(void)
{
	if (s_profiling_recbuf != NULL) {
		free(s_profiling_recbuf);
		s_profiling_recbuf = NULL;
	}
}

void os_perfthr_quit_wait(void)
{
	/* Not supported in Linux. */
}

int os_perf_profiling_partpause(perf_count_id_t perf_count_id)
{
	perf_task_t task;
	task_partpause_t *t;

	memset(&task, 0, sizeof(perf_task_t));
	t = (task_partpause_t *) & task;
	t->task_id = PERF_PROFILING_PARTPAUSE_ID;
	t->perf_count_id = perf_count_id;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_PROFILING_PART_STARTED));
}

int os_perf_profiling_multipause(perf_count_id_t * perf_count_ids)
{
	perf_task_t task;
	task_multipause_t *t;

	memset(&task, 0, sizeof(perf_task_t));
	t = (task_multipause_t *) & task;
	t->task_id = PERF_PROFILING_MULTIPAUSE_ID;
	t->perf_count_ids = perf_count_ids;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_PROFILING_MULTI_STARTED));
}

int os_perf_profiling_restore(perf_count_id_t perf_count_id)
{
	perf_task_t task;
	task_restore_t *t;

	memset(&task, 0, sizeof(perf_task_t));
	t = (task_restore_t *) & task;
	t->task_id = PERF_PROFILING_RESTORE_ID;
	t->perf_count_id = perf_count_id;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_PROFILING_STARTED));
}

int os_perf_profiling_multi_restore(perf_count_id_t * perf_count_ids)
{
	perf_task_t task;
	task_multi_restore_t *t;

	memset(&task, 0, sizeof(perf_task_t));
	t = (task_multi_restore_t *) & task;
	t->task_id = PERF_PROFILING_MULTI_RESTORE_ID;
	t->perf_count_ids = perf_count_ids;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_PROFILING_STARTED));
}

int
os_perf_maplist_smpl(perf_ctl_t * ctl __attribute__ ((unused)), pid_t pid)
{
	perf_task_t task;
	task_ml_t *t;

	perf_smpl_wait();
	memset(&task, 0, sizeof(perf_task_t));
	t = (task_ml_t *) & task;
	t->task_id = PERF_MAPLIST_SMPL_ID;
	t->pid = pid;
	perf_task_set(&task);
	return (0);
}

/* TODO */
void os_allstop(void)
{
	debug_print(NULL, 2, "TODO: stop perf\n");
	if (perf_profiling_started()) {
		profiling_stop();
	}
}

int os_perf_allstop(void)
{
	perf_task_t task;
	task_allstop_t *t;

	memset(&task, 0, sizeof(perf_task_t));
	t = (task_allstop_t *) & task;
	t->task_id = PERF_STOP_ID;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_IDLE));
}

void *os_perf_priv_alloc(boolean_t * supported)
{
	/* Not supported in Linux. */
	*supported = B_FALSE;
	return (NULL);
}

void os_perf_priv_free(void *priv __attribute__ ((unused)))
{
	/* Not supported in Linux. */
}

void os_perf_cpuarr_init(perf_cpu_t * cpu_arr, int num, boolean_t hotadd)
{
	int i;

	for (i = 0; i < num; i++) {
		cpu_arr[i].cpuid = INVALID_CPUID;
		cpu_arr[i].hotadd = hotadd;
	}
}

void os_perf_cpuarr_fini(perf_cpu_t * cpu_arr, int num, boolean_t hotremove)
{
	int i;

	for (i = 0; i < num; i++) {
		if (cpu_arr[i].cpuid != INVALID_CPUID) {
			cpu_arr[i].hotremove = hotremove;
		}
	}
}

static perf_cpu_t *cpu_find(perf_cpu_t * cpu_arr, int cpu_num, int cpuid)
{
	int i;

	for (i = 0; i < cpu_num; i++) {
		if (cpu_arr[i].cpuid == cpuid) {
			return (&cpu_arr[i]);
		}
	}

	return (NULL);
}

static int free_idx_get(perf_cpu_t * cpu_arr, int cpu_num, int prefer_idx)
{
	int i;

	if ((prefer_idx >= 0) && (prefer_idx < cpu_num)) {
		if (cpu_arr[prefer_idx].cpuid == INVALID_CPUID) {
			return (prefer_idx);
		}
	}

	for (i = 0; i < cpu_num; i++) {
		if (cpu_arr[i].cpuid == INVALID_CPUID) {
			return (i);
		}
	}

	return (-1);
}

int
os_perf_cpuarr_refresh(perf_cpu_t * cpu_arr, int cpu_num, int *cpuid_arr,
		       int id_num, boolean_t init)
{
	int i, j, k;
	perf_cpu_t *cpu;

	for (i = 0; i < cpu_num; i++) {
		cpu_arr[i].hit = B_FALSE;
	}

	for (i = 0; i < id_num; i++) {
		if ((cpu = cpu_find(cpu_arr, cpu_num, cpuid_arr[i])) == NULL) {
			/*
			 * New CPU found.
			 */
			if ((j = free_idx_get(cpu_arr, cpu_num, i)) == -1) {
				return (-1);
			}

			cpu_arr[j].cpuid = cpuid_arr[i];
			cpu_arr[j].map_base = MAP_FAILED;
			for (k = 0; k < PERF_COUNT_NUM; k++) {
				cpu_arr[j].fds[k] = INVALID_FD;
			}

			cpu_arr[j].hit = B_TRUE;
			cpu_arr[j].hotadd = !init;

			if (cpu_arr[j].hotadd) {
				debug_print(NULL, 2, "cpu%d is hot-added.\n",
					    cpu_arr[i].cpuid);
			}

		} else {
			cpu->hit = B_TRUE;
		}
	}

	for (i = 0; i < cpu_num; i++) {
		if ((!cpu_arr[i].hit) && (cpu_arr[i].cpuid != INVALID_CPUID)) {
			/*
			 * This CPU is invalid now.
			 */
			cpu_arr[i].hotremove = B_TRUE;
			debug_print(NULL, 2, "cpu%d is hot-removed.\n",
				    cpu_arr[i].cpuid);
		}
	}

	return (0);
}

