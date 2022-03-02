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

/*
 * perf.c
 * This file contains code to retrieve performance data
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include "include/types.h"
#include "include/perf.h"
#include "include/proc.h"
#include "include/util.h"
#include "include/disp.h"
#include "include/ui_perf_map.h"
#include "include/plat.h"
#include "include/damon.h"
#include "include/os/os_perf.h"
#include <strings.h>

static perf_ctl_t s_perf_ctl;
extern int sample_type;
extern int read_format;

uint64_t g_sample_period[PERF_COUNT_NUM][PRECISE_NUM] = {
	{SMPL_PERIOD_CORECLK_DEFAULT,
	 SMPL_PERIOD_CORECLK_MIN,
	 SMPL_PERIOD_CORECLK_MAX},
	{SMPL_PERIOD_DAMON_DEFAULT,
	 SMPL_PERIOD_DAMON_MIN,
	 SMPL_PERIOD_DAMON_MAX}
};

static plat_event_config_t s_perf_config[PERF_COUNT_NUM] = {
	{PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, 0x11, 0, "cpu_clk_unhalted.core"},	/* CORE CLK */
	{PERF_TYPE_TRACEPOINT, 551, 0x1d, 0, "damon:damon_aggregated"},	/* DAMON */
};

static plat_event_config_t s_perf_ll = {
	PERF_TYPE_RAW, 0x01CD, 0x13, 128, "mem_trans_retired.latency_above_threshold"
};

void
sys_profiling_config(perf_count_id_t perf_count_id, plat_event_config_t * cfg)
{
	cfg->type = s_perf_config[perf_count_id].type;
	cfg->config = s_perf_config[perf_count_id].config;
	cfg->other_attr = s_perf_config[perf_count_id].other_attr;
	cfg->extra_value = s_perf_config[perf_count_id].extra_value;
	strncpy(cfg->desc, s_perf_config[perf_count_id].desc,
		PLAT_EVENT_DESC_SIZE);
	cfg->desc[PLAT_EVENT_DESC_SIZE - 1] = 0;
}

void sys_ll_config(plat_event_config_t * cfg)
{
	memcpy(cfg, &s_perf_ll, sizeof(plat_event_config_t));
}

static boolean_t task_valid(perf_task_t * task)
{
	switch (TASKID(task)) {
	case PERF_PROFILING_START_ID:
		/* fall through */
	case PERF_PROFILING_SMPL_ID:
		/* fall through */
	case PERF_PROFILING_PARTPAUSE_ID:
		/* fall through */
	case PERF_PROFILING_MULTIPAUSE_ID:
		/* fall through */
	case PERF_PROFILING_RESTORE_ID:
		/* fall through */
	case PERF_PROFILING_MULTI_RESTORE_ID:
		/* fall through */
	case PERF_MAPLIST_START_ID:
		/* fall through */
	case PERF_MAPLIST_SMPL_ID:
		/* fall through */
	case PERF_STOP_ID:
		/* fall through */
	case PERF_QUIT_ID:
		return (B_TRUE);
	default:
		break;
	}

	return (B_FALSE);
}

void perf_task_set(perf_task_t * task)
{
	(void)pthread_mutex_lock(&s_perf_ctl.mutex);
	(void)memcpy(&s_perf_ctl.task, task, sizeof(perf_task_t));
	(void)pthread_cond_signal(&s_perf_ctl.cond);
	(void)pthread_mutex_unlock(&s_perf_ctl.mutex);
}

void perf_status_set(perf_status_t status)
{
	(void)pthread_mutex_lock(&s_perf_ctl.status_mutex);
	s_perf_ctl.status = status;
	(void)pthread_cond_signal(&s_perf_ctl.status_cond);
	(void)pthread_mutex_unlock(&s_perf_ctl.status_mutex);
}

void perf_status_set_no_signal(perf_status_t status)
{
	(void)pthread_mutex_lock(&s_perf_ctl.status_mutex);
	s_perf_ctl.status = status;
	(void)pthread_mutex_unlock(&s_perf_ctl.status_mutex);
}

static boolean_t status_failed(perf_status_t status)
{
	switch (status) {
	case PERF_STATUS_PROFILING_FAILED:
		/* fall through */
	case PERF_STATUS_ML_FAILED:
		return (B_TRUE);

	default:
		break;
	}

	return (B_FALSE);
}

int perf_status_wait(perf_status_t status)
{
	struct timespec timeout;
	struct timeval tv;
	int s, ret = -1;

	(void)gettimeofday(&tv, NULL);
	timeout.tv_sec = tv.tv_sec + PERF_WAIT_NSEC;
	timeout.tv_nsec = tv.tv_usec * 1000;

	(void)pthread_mutex_lock(&s_perf_ctl.status_mutex);
	for (;;) {
		s = pthread_cond_timedwait(&s_perf_ctl.status_cond,
					   &s_perf_ctl.status_mutex, &timeout);

		if (s_perf_ctl.status == status) {
			ret = 0;
			break;
		}

		if (status_failed(s_perf_ctl.status)) {
			break;
		}

		if (s == ETIMEDOUT) {
			break;
		}
	}

	(void)pthread_mutex_unlock(&s_perf_ctl.status_mutex);
	return (ret);
}

/*
 * The thread handler of 'perf thread'.
 */
/* ARGSUSED */
static void *perf_handler(void *arg __attribute__ ((unused)))
{
	perf_task_t task;
	int intval_ms;

	for (;;) {
		(void)pthread_mutex_lock(&s_perf_ctl.mutex);
		task = s_perf_ctl.task;
		while (!task_valid(&task)) {
			(void)pthread_cond_wait(&s_perf_ctl.cond,
						&s_perf_ctl.mutex);
			task = s_perf_ctl.task;
		}

		TASKID_SET(&s_perf_ctl.task, PERF_INVALID_ID);
		(void)pthread_mutex_unlock(&s_perf_ctl.mutex);

		switch (TASKID(&task)) {
		case PERF_QUIT_ID:
			debug_print(NULL, 2, "perf_handler: received QUIT\n");
			os_allstop();
			goto L_EXIT;

		case PERF_STOP_ID:
			os_allstop();
			perf_status_set(PERF_STATUS_IDLE);
			break;

		case PERF_PROFILING_START_ID:
			if (os_profiling_start(&s_perf_ctl, &task) != 0) {
				goto L_EXIT;
			}
			break;

		case PERF_PROFILING_SMPL_ID:
			(void)os_profiling_smpl(&s_perf_ctl, &task, &intval_ms);
			break;

		case PERF_PROFILING_PARTPAUSE_ID:
			(void)os_profiling_partpause(&s_perf_ctl, &task);
			break;

		case PERF_PROFILING_MULTIPAUSE_ID:
			(void)os_profiling_multipause(&s_perf_ctl, &task);
			break;

		case PERF_PROFILING_RESTORE_ID:
			(void)os_profiling_restore(&s_perf_ctl, &task);
			break;

		case PERF_PROFILING_MULTI_RESTORE_ID:
			(void)os_profiling_multi_restore(&s_perf_ctl, &task);
			break;

		case PERF_MAPLIST_START_ID:
			os_ml_start(&s_perf_ctl, &task);
			break;

		case PERF_MAPLIST_SMPL_ID:
			os_maplist_events(&s_perf_ctl, &task, &intval_ms);
			break;

		default:
			break;
		}
	}

L_EXIT:
	debug_print(NULL, 2, "perf thread is exiting.\n");
	return (NULL);
}

/*
 * Initialization for perf control structure.
 */
int perf_init(void)
{
	boolean_t mutex_inited = B_FALSE;
	boolean_t cond_inited = B_FALSE;
	boolean_t status_mutex_inited = B_FALSE;
	boolean_t status_cond_inited = B_FALSE;

	if (os_perf_init() != 0) {
		return (-1);
	}

	(void)memset(&s_perf_ctl, 0, sizeof(s_perf_ctl));

	if (pthread_mutex_init(&s_perf_ctl.mutex, NULL) != 0) {
		goto L_EXIT;
	}
	mutex_inited = B_TRUE;

	if (pthread_cond_init(&s_perf_ctl.cond, NULL) != 0) {
		goto L_EXIT;
	}
	cond_inited = B_TRUE;

	if (pthread_mutex_init(&s_perf_ctl.status_mutex, NULL) != 0) {
		goto L_EXIT;
	}
	status_mutex_inited = B_TRUE;

	if (pthread_cond_init(&s_perf_ctl.status_cond, NULL) != 0) {
		goto L_EXIT;
	}
	status_cond_inited = B_TRUE;

	if (pthread_create(&s_perf_ctl.thr, NULL, perf_handler, NULL) != 0) {
		goto L_EXIT;
	}

	s_perf_ctl.last_ms = current_ms(&g_tvbase);
	if (perf_profiling_start() != 0) {
		debug_print(NULL, 2, "perf_init: "
			    "perf_profiling_start() failed\n");
		goto L_EXIT;
	}

	s_perf_ctl.inited = B_TRUE;

L_EXIT:
	if (!s_perf_ctl.inited) {
		if (mutex_inited) {
			(void)pthread_mutex_destroy(&s_perf_ctl.mutex);
		}

		if (cond_inited) {
			(void)pthread_cond_destroy(&s_perf_ctl.cond);
		}

		if (status_mutex_inited) {
			(void)pthread_mutex_destroy(&s_perf_ctl.status_mutex);
		}

		if (status_cond_inited) {
			(void)pthread_cond_destroy(&s_perf_ctl.status_cond);
		}

		os_perf_fini();
		return (-1);
	}

	return (0);
}

static void perfthr_quit_wait(void)
{
	perf_task_t task;
	task_quit_t *t;

	os_perfthr_quit_wait();

	debug_print(NULL, 2, "Send PERF_QUIT_ID to perf thread\n");
	(void)memset(&task, 0, sizeof(perf_task_t));
	t = (task_quit_t *) & task;
	t->task_id = PERF_QUIT_ID;
	perf_task_set(&task);
	(void)pthread_join(s_perf_ctl.thr, NULL);
	debug_print(NULL, 2, "perf thread exit yet\n");
}

/*
 * Release the resources of perf control structure.
 */
void perf_fini(void)
{
	if (s_perf_ctl.inited) {
		perfthr_quit_wait();
		(void)pthread_mutex_destroy(&s_perf_ctl.mutex);
		(void)pthread_cond_destroy(&s_perf_ctl.cond);
		(void)pthread_mutex_destroy(&s_perf_ctl.status_mutex);
		(void)pthread_cond_destroy(&s_perf_ctl.status_cond);
		s_perf_ctl.inited = B_FALSE;
	}

	os_perf_fini();
}

int perf_allstop(void)
{
	return (os_perf_allstop());
}

boolean_t perf_profiling_started(void)
{
	return (os_profiling_started(&s_perf_ctl));
}

int perf_profiling_start(void)
{
	perf_task_t task;
	task_profiling_t *t;

	(void)memset(&task, 0, sizeof(perf_task_t));
	t = (task_profiling_t *) & task;
	t->task_id = PERF_PROFILING_START_ID;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_PROFILING_STARTED));
}

/*
 * The user may refresh the current window frequently.
 * One refresh operation would invoke one time perf data
 * sampling. If the sampling interval is too small, the
 * counting of an event with predefined threshold probably
 * doesn't get chance to overflow. Then the sampling data
 * is not very accurate.
 *
 * For example:
 * Suppose the user refreshes the window in each 100ms. The
 * overflow threshold for RMA is 100,000. Suppose for a
 * workload, it's overflowed in each 200ms. Then the user
 * can only see the RMA is 0 after he refreshes the window.
 */
void perf_smpl_wait(void)
{
	int intval_diff;

	intval_diff = current_ms(&g_tvbase) - s_perf_ctl.last_ms;

	if (PERF_INTVAL_MIN_MS > intval_diff) {
		intval_diff = PERF_INTVAL_MIN_MS - intval_diff;
		(void)usleep(intval_diff * USEC_MS);
	}
}

int perf_profiling_smpl(boolean_t use_dispflag1)
{
	perf_task_t task;
	task_profiling_t *t;

	perf_smpl_wait();
	(void)memset(&task, 0, sizeof(perf_task_t));
	t = (task_profiling_t *) & task;
	t->task_id = PERF_PROFILING_SMPL_ID;
	t->use_dispflag1 = use_dispflag1;
	perf_task_set(&task);
	return (0);
}

int perf_profiling_partpause(ui_count_id_t ui_count_id)
{
	perf_count_id_t *perf_count_ids = NULL;
	int n_perf_count;

	n_perf_count = get_ui_perf_count_map(ui_count_id, &perf_count_ids);

	if (n_perf_count == PERF_COUNT_INVALID)
		return (-1);

	if (n_perf_count > 1) {
		return (os_perf_profiling_multipause(perf_count_ids));
	} else {
		return (os_perf_profiling_partpause(perf_count_ids[0]));
	}
}

int perf_profiling_restore(ui_count_id_t ui_count_id)
{
	perf_count_id_t *perf_count_ids = NULL;
	int n_perf_count;

	n_perf_count = get_ui_perf_count_map(ui_count_id, &perf_count_ids);

	if (n_perf_count == PERF_COUNT_INVALID)
		return (-1);

	if (n_perf_count > 1) {
		return (os_perf_profiling_multi_restore(perf_count_ids));
	} else {
		return (os_perf_profiling_restore(perf_count_ids[0]));
	}
}

boolean_t perf_maplist_is_start(void)
{
	if (s_perf_ctl.status == PERF_STATUS_MAPLIST_STARTED) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

int perf_maplist_start(pid_t pid)
{
	perf_task_t task;
	task_ml_t *t;

	(void)memset(&task, 0, sizeof(perf_task_t));
	t = (task_ml_t *) & task;
	t->task_id = PERF_MAPLIST_START_ID;
	t->pid = pid;
	perf_task_set(&task);
	return (perf_status_wait(PERF_STATUS_MAPLIST_STARTED));
}

int perf_maplist_smpl(pid_t pid)
{
	return (os_perf_maplist_smpl(&s_perf_ctl, pid));
}

void *perf_priv_alloc(boolean_t * supported)
{
	return (os_perf_priv_alloc(supported));
}

void perf_priv_free(void *priv)
{
	os_perf_priv_free(priv);
}

void perf_maplist_status_set(void)
{
	perf_status_set(PERF_STATUS_MAPLIST_STARTED);
}

