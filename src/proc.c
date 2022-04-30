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

/* This file contains code to handle the 'tracked process'. */

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include "include/types.h"
#include "include/proc.h"
#include "include/disp.h"
#include "include/util.h"
#include "include/perf.h"
#include "include/damon.h"
#include "include/os/os_util.h"

static proc_group_t s_proc_group;
struct damon_proc_t target_procs = {0};
pid_t damontop_pid;

extern int numa_stat;

/*
 * Initialization for the process group.
 */
int proc_group_init(void)
{
	(void)memset(&s_proc_group, 0, sizeof(s_proc_group));
	if (pthread_mutex_init(&s_proc_group.mutex, NULL) != 0) {
		return (-1);
	}

	if (pthread_cond_init(&s_proc_group.cond, NULL) != 0) {
		(void)pthread_mutex_destroy(&s_proc_group.mutex);
		return (-1);
	}

	s_proc_group.inited = B_TRUE;
	return (0);
}

/*
 * Free resources of 'track_proc_t' if 'ref_count' is 0.
 */
static void proc_free(track_proc_t * proc)
{
	(void)pthread_mutex_lock(&proc->mutex);
	if (proc->ref_count > 0) {
		proc->removing = B_TRUE;
		(void)pthread_mutex_unlock(&proc->mutex);
		return;
	}

	s_proc_group.nprocs--;

	if (proc->countval_arr != NULL) {
		free(proc->countval_arr);
	}

	(void)map_proc_fini(proc);

	(void)pthread_mutex_unlock(&proc->mutex);
	(void)pthread_mutex_destroy(&proc->mutex);
	free(proc);
}

/*
 * Walk through all processes and call 'func()' for each processes.
 */
static void
proc_traverse(int (*func) (track_proc_t *, void *, boolean_t *), void *arg)
{
	track_proc_t *proc, *hash_next;
	boolean_t end;
	int i, j = 0;

	/*
	 * The mutex of s_proc_group has been taken outside.
	 */
	for (i = 0; i < PROC_HASHTBL_SIZE; i++) {
		proc = s_proc_group.hashtbl[i];
		while (proc != NULL) {
			j++;
			hash_next = proc->hash_next;
			func(proc, arg, &end);
			if (end) {
				return;
			}

			proc = hash_next;
		}

		if (j == s_proc_group.nprocs) {
			return;
		}
	}
}

static void moniproc_traverse(int (*func) (count_value_t *, void *, boolean_t *),
		track_proc_t *proc, void *arg)
{
	count_value_t *cv;
	boolean_t end;
	int i;
	int nr_nonzero = proc->nr_nonzero;

	/*
	 * The mutex of s_proc_group has been taken outside.
	 */
	for (i = 0; i < nr_nonzero; i++) {
		cv = &proc->countval_arr[i];
		func(cv, arg, &end);
		if (end) {
			return;
		}
	}
}

/* ARGSUSED */
static int
proc_free_walk(track_proc_t * proc,
	       void *arg __attribute__ ((unused)), boolean_t * end)
{
	*end = B_FALSE;
	proc_free(proc);
	return (0);
}

/*
 * Free all the resources of process group.
 */
void proc_group_fini(void)
{
	if (!s_proc_group.inited) {
		return;
	}

	(void)pthread_mutex_lock(&s_proc_group.mutex);
	proc_traverse(proc_free_walk, NULL);
	if (s_proc_group.sort_arr != NULL) {
		free(s_proc_group.sort_arr);
	}

	(void)pthread_mutex_unlock(&s_proc_group.mutex);
	(void)pthread_mutex_destroy(&s_proc_group.mutex);
	(void)pthread_cond_destroy(&s_proc_group.cond);
}

/*
 * Look for a process by specified pid.
 */
static track_proc_t *proc_find_nolock(pid_t pid)
{
	track_proc_t *proc;
	int hashidx;

	/*
	 * To speed up, check the "latest access" process first.
	 */
	if ((s_proc_group.latest != NULL) &&
	    ((s_proc_group.latest)->pid == pid)) {
		proc = s_proc_group.latest;
		goto L_EXIT;
	}

	/*
	 * Scan the process hash table.
	 */
	hashidx = PROC_HASHTBL_INDEX(pid);
	proc = s_proc_group.hashtbl[hashidx];
	while (proc != NULL) {
		if (proc->pid == pid) {
			break;
		}

		proc = proc->hash_next;
	}

L_EXIT:
	if (proc != NULL) {
		if (proc_refcount_inc(proc) != 0) {
			/*
			 * The proc is tagged as removing.
			 */
			if (s_proc_group.latest == proc) {
				s_proc_group.latest = NULL;
			}

			proc = NULL;
		}
	}

	if (proc != NULL) {
		s_proc_group.latest = proc;
	}

	return (proc);
}

/*
 * Look for a process by pid with lock protection.
 */
track_proc_t *proc_find(pid_t pid)
{
	track_proc_t *proc;

	(void)pthread_mutex_lock(&s_proc_group.mutex);
	proc = proc_find_nolock(pid);
	(void)pthread_mutex_unlock(&s_proc_group.mutex);
	return (proc);
}

/*
 * Allocation and initialization for a new 'track_proc_t' structure.
 */
static track_proc_t *proc_alloc(void)
{
	track_proc_t *proc;
	count_value_t *countval_arr;

	if ((countval_arr =
	     zalloc(PROC_RECORD_MAX * sizeof(count_value_t))) == NULL) {
		return (NULL);
	}

	if ((proc = zalloc(sizeof(track_proc_t))) == NULL) {
		free(countval_arr);
		return (NULL);
	}

	if (pthread_mutex_init(&proc->mutex, NULL) != 0) {
		free(countval_arr);
		free(proc);
		return (NULL);
	}

	proc->pid = -1;
	proc->nr_nonzero= 0;
	proc->countval_arr = countval_arr;
	proc->record_max = PROC_RECORD_MAX;
	proc->inited = B_TRUE;
	proc->cpu_usage = 0;
	proc->slice[0].process_slice = 0;
	proc->slice[0].total_slice = 0;
	proc->slice[1].process_slice = 0;
	proc->slice[1].total_slice = 0;
	return proc;
}

/*
 * Count the total number of processes and threads.
 */
void proc_count(int *nprocs)
{
	if (nprocs != NULL) {
		*nprocs = s_proc_group.nprocs;
	}
}

void proc_group_lock(void)
{
	(void)pthread_mutex_lock(&s_proc_group.mutex);
}

void proc_group_unlock(void)
{
	(void)pthread_mutex_unlock(&s_proc_group.mutex);
}

/*
 * This function should be fixed if available.
 */
static uint64_t count_value_get(track_proc_t * proc, ui_count_id_t ui_count_id)
{
	if (ui_count_id == UI_COUNT_CLK) {
		debug_print(NULL, 2, "PID: %d CPU usage: %d\n", proc->pid,
				proc->cpu_usage);
		return proc->cpu_usage;
	}

	/*
	 * return the maximum value in proc record.
	 */
	return get_max_countval(proc->countval_arr, ui_count_id);
}

static uint64_t monicount_value_get(count_value_t *countval_arr, ui_count_id_t ui_count_id)
{
	return ui_perf_count_aggr(ui_count_id, countval_arr->counts);
}

static int proc_addr_cmp(const void *a, const void *b)
{
	const count_value_t *countval1 = (count_value_t *)a;
	const count_value_t *countval2 = (count_value_t *)b;

	if (countval1->counts[UI_COUNT_DAMON_START]
	    > countval2->counts[UI_COUNT_DAMON_START]) {
		return 1;
	}

	if (countval1->counts[UI_COUNT_DAMON_START]
	    < countval2->counts[UI_COUNT_DAMON_START]) {
		return -1;
	}

	return 0;
}

/*
 * Compute the value of key for process sorting.
 */
static int proc_key_compute(track_proc_t * proc, void *arg, boolean_t * end)
{
	sort_key_t sortkey = *((sort_key_t *) arg);

	switch (sortkey) {
	case SORT_KEY_CPU:
		proc->key = count_value_get(proc, UI_COUNT_CLK);
		break;

	case SORT_KEY_PID:
		proc->key = proc->pid;
		break;

	case SORT_KEY_START:
		proc->key = count_value_get(proc, UI_COUNT_DAMON_START);
		break;

	case SORT_KEY_SIZE:
		proc->key = count_value_get(proc, UI_COUNT_DAMON_END) -
		    count_value_get(proc, UI_COUNT_DAMON_START);
		break;

	case SORT_KEY_NRA:
		proc->key = count_value_get(proc, UI_COUNT_DAMON_NR_ACCESS);
		break;

	default:
		proc->key = proc->pid;
		break;
	}

	*end = B_FALSE;
	return 0;
}

static int moniproc_key_compute(count_value_t *cv, void *arg, boolean_t * end)
{
	sort_key_t sortkey = *((sort_key_t *) arg);

	switch (sortkey) {
	case SORT_KEY_START:
		cv->key = monicount_value_get(cv, UI_COUNT_DAMON_START);
		break;

	case SORT_KEY_SIZE:
		cv->key = (monicount_value_get(cv, UI_COUNT_DAMON_END) -
			monicount_value_get(cv, UI_COUNT_DAMON_START)) >> 10;
		break;

	case SORT_KEY_NRA:
		cv->key = monicount_value_get(cv, UI_COUNT_DAMON_NR_ACCESS);
		break;

	default:
		cv->key = monicount_value_get(cv, UI_COUNT_DAMON_START);
		break;
	}

	*end = B_FALSE;
	return 0;
}

static int moniproc_key_cmp(const void *a, const void *b)
{
	const count_value_t *line1 = (count_value_t *)a;
	const count_value_t *line2 = (count_value_t *)b;

	if (line1->key > line2->key) {
		return (-1);
	}

	if (line1->key < line2->key) {
		return (1);
	}

	return (0);
}

static int proc_key_cmp(const void *a, const void *b)
{
	const track_proc_t *proc1 = *((track_proc_t * const *)a);
	const track_proc_t *proc2 = *((track_proc_t * const *)b);

	if (proc1->key > proc2->key) {
		return (-1);
	}

	if (proc1->key < proc2->key) {
		return (1);
	}

	return (0);
}

static int proc_pid_cmp(const void *a, const void *b)
{
	const track_proc_t *proc1 = *((track_proc_t * const *)a);
	const track_proc_t *proc2 = *((track_proc_t * const *)b);

	if (proc1->pid > proc2->pid) {
		return (1);
	}

	if (proc1->pid < proc2->pid) {
		return (-1);
	}

	return (0);
}

static void proc_sortkey(void)
{
	track_proc_t **sort_arr, *proc;
	int i, j = 0;

	if (s_proc_group.sort_arr != NULL) {
		free(s_proc_group.sort_arr);
		s_proc_group.sort_arr = NULL;
	}

	sort_arr = zalloc(sizeof(track_proc_t *) * s_proc_group.nprocs);
	if (sort_arr == NULL) {
		return;
	}

	for (i = 0; i < PROC_HASHTBL_SIZE; i++) {
		proc = s_proc_group.hashtbl[i];
		while (proc != NULL) {
			sort_arr[j++] = proc;
			proc = proc->hash_next;
		}

		if (j == s_proc_group.nprocs) {
			break;
		}
	}

	qsort(sort_arr, s_proc_group.nprocs,
	      sizeof(track_proc_t *), proc_pid_cmp);

	qsort(sort_arr, s_proc_group.nprocs,
	      sizeof(track_proc_t *), proc_key_cmp);

	s_proc_group.sort_arr = sort_arr;
	s_proc_group.sort_idx = 0;
}

/*
 * Resort the process by the value of key.
 */
void proc_resort(sort_key_t sort)
{
	/*
	 * The lock of s_proc_group takes outside.
	 */
	proc_traverse(proc_key_compute, &sort);
	proc_sortkey();
}

static void moniproc_sortkey(track_proc_t *proc)
{
	int nr_nonzero = proc->nr_nonzero;
	count_value_t *sort_arr;

	sort_arr = zalloc(sizeof(count_value_t) * nr_nonzero);
	if (sort_arr == NULL) {
		return;
	}

	memcpy(sort_arr, proc->countval_arr, sizeof(count_value_t) * nr_nonzero);
	qsort(sort_arr, nr_nonzero, sizeof(count_value_t),
			moniproc_key_cmp);
	memcpy(proc->countval_arr, sort_arr, sizeof(count_value_t) * nr_nonzero);
	free(sort_arr);
}

/*
 * Resort the process by the value of key.
 */
void moniproc_resort(sort_key_t sort, track_proc_t *proc)
{
	moniproc_traverse(moniproc_key_compute, proc, &sort);
	moniproc_sortkey(proc);
}

/*
 * Move the 'sort_idx' to next proc node and return current one.
 */
track_proc_t *proc_sort_next(void)
{
	int idx = s_proc_group.sort_idx;

	if (s_proc_group.sort_arr == NULL) {
		return (NULL);
	}

	if (idx < s_proc_group.nprocs) {
		s_proc_group.sort_idx++;
		return (s_proc_group.sort_arr[idx]);
	}

	return (NULL);
}

void proc_countvalue_sort(count_value_t * sort_countval_arr, int *nonzero)
{
	int i, j;
	uint64_t start, end;
	int nr_nonzero = 0;

	for (i = 0; i < PROC_RECORD_MAX; i++) {
		start = proc_countval_sum(&sort_countval_arr[i], UI_COUNT_DAMON_START);
		end = proc_countval_sum(&sort_countval_arr[i], UI_COUNT_DAMON_END);
		if (start == 0 && end == 0)
			continue;
		for (j = i + 1; j < PROC_RECORD_MAX; j++) {
			uint64_t after_start =
			    proc_countval_sum(&sort_countval_arr[j], UI_COUNT_DAMON_START);
			if (after_start == 0)
				continue;
			uint64_t after_end =
			    proc_countval_sum(&sort_countval_arr[j], UI_COUNT_DAMON_END);
			uint64_t after_nr_access =
			    proc_countval_sum(&sort_countval_arr[j], UI_COUNT_DAMON_NR_ACCESS);
			if ((start >= after_start && end <= after_end) ||
					(start <= after_start && after_end <= end)) {
				/* Clear previous data. */
				sort_countval_arr[i].
				    counts[UI_COUNT_DAMON_START] = 0;
				sort_countval_arr[i].
				    counts[UI_COUNT_DAMON_END] = 0;
				sort_countval_arr[i].
				    counts[UI_COUNT_DAMON_NR_ACCESS] = 0;
				break;
			}
		}
	}

	count_value_t *new_countval_arr =
	    zalloc(sizeof(count_value_t) * PROC_RECORD_MAX);
	j = 0;
	for (i = 0; i < PROC_RECORD_MAX; i++) {
		start = proc_countval_sum(&sort_countval_arr[i], UI_COUNT_DAMON_START);
		if (start != 0) {
			nr_nonzero++;
			new_countval_arr[j].counts[UI_COUNT_DAMON_NR_REGIONS] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_NR_REGIONS];
			new_countval_arr[j].counts[UI_COUNT_DAMON_START] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_START];
			new_countval_arr[j].counts[UI_COUNT_DAMON_END] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_END];
			new_countval_arr[j].counts[UI_COUNT_DAMON_NR_ACCESS] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_NR_ACCESS];
			new_countval_arr[j].counts[UI_COUNT_DAMON_AGE] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_AGE];
			new_countval_arr[j].counts[UI_COUNT_DAMON_LOCAL] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_LOCAL];
			new_countval_arr[j].counts[UI_COUNT_DAMON_REMOTE] =
			    sort_countval_arr[i].counts[UI_COUNT_DAMON_REMOTE];
			j++;
		}
	}

	count_value_t *countval_arr =
	    zalloc(sizeof(count_value_t) * nr_nonzero);
	memcpy(countval_arr, new_countval_arr,
	       sizeof(count_value_t) * nr_nonzero);
	qsort(countval_arr, nr_nonzero, sizeof(count_value_t), proc_addr_cmp);
	memset(sort_countval_arr, 0, sizeof(count_value_t) * PROC_RECORD_MAX);
	memcpy(sort_countval_arr, countval_arr,
	       sizeof(count_value_t) * nr_nonzero);
	free(new_countval_arr);
	free(countval_arr);
	*nonzero = nr_nonzero;
}

/*
 * Add a new proc in s_process_group->hashtbl.
 */
static int proc_group_add(track_proc_t * proc)
{
	track_proc_t *head;
	int hashidx;

	/*
	 * The lock of table has been taken outside.
	 */
	hashidx = PROC_HASHTBL_INDEX(proc->pid);
	if ((head = s_proc_group.hashtbl[hashidx]) != NULL) {
		head->hash_prev = proc;
	}

	proc->hash_next = head;
	proc->hash_prev = NULL;
	s_proc_group.hashtbl[hashidx] = proc;
	s_proc_group.nprocs++;
	return (0);
}

/*
 * Remove a specifiled proc from s_process_group->hashtbl.
 */
static void proc_group_remove(track_proc_t * proc)
{
	track_proc_t *prev, *next;
	int hashidx;

	/*
	 * The lock of table has been taken outside.
	 */
	hashidx = PROC_HASHTBL_INDEX(proc->pid);

	/*
	 * Remove it from process hash-list.
	 */
	prev = proc->hash_prev;
	next = proc->hash_next;
	if (prev != NULL) {
		prev->hash_next = next;
	} else {
		s_proc_group.hashtbl[hashidx] = next;
	}

	if (next != NULL) {
		next->hash_prev = prev;
	}

	s_proc_group.nprocs--;
	if (s_proc_group.latest == proc) {
		s_proc_group.latest = NULL;
	}
}

/*
 * The process is not valid, remove it.
 */
static void proc_obsolete(pid_t pid)
{
	track_proc_t *proc;

	if ((proc = proc_find(pid)) != NULL) {
		proc_refcount_dec(proc);
		(void)pthread_mutex_lock(&s_proc_group.mutex);
		proc_group_remove(proc);
		proc_free(proc);
		(void)pthread_mutex_unlock(&s_proc_group.mutex);
	}
}

static int pid_cmp(const void *a, const void *b)
{
	const pid_t *pid1 = (const pid_t *)a;
	const pid_t *pid2 = (const pid_t *)b;

	if (*pid1 > *pid2) {
		return (1);
	}

	if (*pid1 < *pid2) {
		return (-1);
	}

	return (0);
}

static pid_t *pid_find(pid_t pid, pid_t * pid_arr, int num)
{
	pid_t *p;

	p = bsearch(&pid, (void *)pid_arr, num, sizeof(pid_t), pid_cmp);
	return (p);
}

/*
 * The array 'procs_new' contains the latest valid pid. Scan the hashtbl to
 * figure out the obsolete processes and remove them. For the new processes,
 * add them in hashtbl.
 */
static void proc_group_refresh(pid_t * procs_new, int nproc_new)
{
	track_proc_t *proc, *hash_next;
	pid_t *p;
	int i, j;
	boolean_t *exist_arr;

	if ((exist_arr = zalloc(sizeof(boolean_t) * nproc_new)) == NULL) {
		return;
	}

	qsort(procs_new, nproc_new, sizeof(pid_t), pid_cmp);

	(void)pthread_mutex_lock(&s_proc_group.mutex);
	for (i = 0; i < PROC_HASHTBL_SIZE; i++) {
		proc = s_proc_group.hashtbl[i];
		while (proc != NULL) {
			hash_next = proc->hash_next;
			if ((p =
			     pid_find(proc->pid, procs_new,
				      nproc_new)) == NULL) {
				proc_group_remove(proc);
				proc_free(proc);
			} else {
				j = ((uint64_t) p - (uint64_t) procs_new) /
				    sizeof(pid_t);
				exist_arr[j] = B_TRUE;
			}

			proc = hash_next;
		}
	}

	for (i = 0; i < nproc_new; i++) {
		if (!exist_arr[i]) {
			if ((proc = proc_alloc()) != NULL) {
				proc->pid = procs_new[i];
				(void)os_procfs_pname_get(proc->pid,
							  proc->name,
							  PROC_NAME_SIZE);
				(void)proc_group_add(proc);
			}
		}
	}

	s_proc_group.nprocs = nproc_new;
	s_proc_group.nlwps = 0;
	(void)pthread_mutex_unlock(&s_proc_group.mutex);
	free(exist_arr);
}

/*
 * Update the valid processes by scanning '/proc'
 */
void proc_enum_update(pid_t pid)
{
	pid_t *procs_new;
	int nproc_new;

	if (pid > 0) {
		if (kill(pid, 0) == -1) {
			/* The process is obsolete. */
			proc_obsolete(pid);
		}
	} else {
		if (procfs_proc_enum(&procs_new, &nproc_new) == 0) {
			proc_group_refresh(procs_new, nproc_new);
			free(procs_new);
		}
	}
}

/*
 * Increment for the refcount.
 */
int proc_refcount_inc(track_proc_t * proc)
{
	int ret = -1;

	(void)pthread_mutex_lock(&proc->mutex);
	if (!proc->removing) {
		proc->ref_count++;
		ret = 0;
	}

	(void)pthread_mutex_unlock(&proc->mutex);
	return (ret);
}

/*
 * Decrement for the refcount. If the refcount turns to be 0 and the
 * 'removing' flag is set, release the 'track_proc_t' structure.
 */
void proc_refcount_dec(track_proc_t * proc)
{
	boolean_t remove = B_FALSE;

	(void)pthread_mutex_lock(&proc->mutex);
	proc->ref_count--;
	if ((proc->ref_count == 0) && (proc->removing)) {
		remove = B_TRUE;
	}
	(void)pthread_mutex_unlock(&proc->mutex);

	if (remove) {
		(void)pthread_mutex_lock(&s_proc_group.mutex);
		proc_free(proc);
		(void)pthread_mutex_unlock(&s_proc_group.mutex);
	}
}

/*
 * Update the process's per CPU perf data.
 */
int
proc_countval_update(track_proc_t * proc, int num,
		     perf_count_id_t perf_count_id, uint64_t value)
{
	count_value_t *countval;
	int record_max = proc->record_max;

	/*
	 * Check if countval_arr overflow.
	 */
	if (num >= record_max) {
		return 0;
	}

	countval = &proc->countval_arr[num];
	/* counts[1 - 3] is DAMON data */
	countval->counts[perf_count_id] = value;
	return 0;
}

uint64_t proc_countval_sum(count_value_t * countval_arr,
		ui_count_id_t ui_count_id)
{
	uint64_t value;

	switch (ui_count_id) {
	case UI_COUNT_DAMON_START:
	case UI_COUNT_DAMON_END:
	case UI_COUNT_DAMON_NR_ACCESS:
	case UI_COUNT_DAMON_AGE:
	case UI_COUNT_DAMON_LOCAL:
	case UI_COUNT_DAMON_REMOTE:
		value = countval_arr->counts[ui_count_id];
		break;
	default:
		value = 0;
		break;
	}

	return value;
}

static int intval_update(track_proc_t * proc, void *arg, boolean_t * end)
{
	int intval_ms = *((int *)arg);

	*end = B_FALSE;
	proc->intval_ms = intval_ms;
	return (0);
}

/*
 * Update with the interval of sampling.
 */
void proc_intval_update(int intval_ms)
{
	(void)pthread_mutex_lock(&s_proc_group.mutex);
	proc_traverse(intval_update, &intval_ms);
	(void)pthread_mutex_unlock(&s_proc_group.mutex);
}

int proc_intval_get(track_proc_t * proc)
{
	return (proc->intval_ms);
}

static int profiling_clear(track_proc_t * proc,
		void *arg __attribute__ ((unused)), boolean_t * end)
{
	*end = B_FALSE;
	(void)memset(proc->countval_arr, 0,
		     sizeof(count_value_t) * proc->cpuid_max);
	return (0);
}

void proc_profiling_clear(void)
{
	proc_traverse(profiling_clear, NULL);
}

int monitor_start(char *procs)
{
	struct stat sts;
	char proc_pid[16] = { 0 };
	char *cmd;
	char data[8] = { 0 };
	int ret = 0, fd;
	int i;

	for (i = 0; i < target_procs.nr_proc; i++) {
		sprintf(proc_pid, "/proc/%d", target_procs.pid[i]);
		if (stat(proc_pid, &sts) == -1 && errno == ENOENT) {
			/* process doesn't exist. */
			return -1;
		}
	}

	if ((fd = open("/sys/kernel/debug/damon/monitor_on", O_RDONLY)) < 0) {
		printf("kdamon_pid: No such file!\n");
		return -1;
	}

	ret = read(fd, data, 8);
	if (ret < 0) {
		perror("kdamon_pid!\n");
		return -1;
	}
	close(fd);

	if (!strcmp(data, "on\n")) {
		stderr_print("DAMON had been enabled\n");
		return -1;
	}

	/* Add <pid> into DAMON. */
	for (i = 0; i < (int)strlen(procs); i++) {
		if (procs[i] == ',')
			procs[i] = ' ';
	}

	cmd = malloc(strlen(procs) + 48);
	sprintf(cmd, "echo %s > /sys/kernel/debug/damon/target_ids", procs);
	system(cmd);
	free(cmd);

	system("echo on > /sys/kernel/debug/damon/monitor_on");
	if (numa_stat)
		system("echo on > /sys/kernel/debug/damon/numa_stat");

	return 0;
}

void monitor_exit(void)
{
	int fd, ret = 0;
	char data[8] = {0};

	if ((fd = open("/sys/kernel/debug/damon/monitor_on", O_RDONLY)) < 0) {
		stderr_print("monitor_on: No such file!\n");
		return;
	}

	ret = read(fd, data, 8);
	if (ret < 0) {
		stderr_print("/sys/kernel/debug/damon/monitor_on: read fail!\n");
		return;
	}
	close(fd);

	if (!strcmp(data, "on\n"))
		system("echo off > /sys/kernel/debug/damon/monitor_on");
}

static int file2str(const char *directory, const char *what, char *ret, int cap)
{
	static char filename[80];
	int fd, num_read;

	sprintf(filename, "%s/%s", directory, what);
	fd = open(filename, O_RDONLY, 0);
	if(fd == -1)
		return -1;

	num_read = read(fd, ret, cap - 1);
	close(fd);

	if(num_read <= 0)
		return -1;

	ret[num_read] = '\0';

	return num_read;
}

static int do_stats(pid_t pid, uint64_t *slice)
{
	char sbuf[1024] = {0};
	char discard_str[1024] = {0};
	char proc_path[64] = {0};
	char state;
	int ppid, pgrp, session, tty, tpgid;
	unsigned long flags, min_flt, cmin_flt, maj_flt, cmaj_flt;
	unsigned long long utime, stime, cutime, cstime;
	char *tmp;
	char *psbuf;
	int ret = 0;

	sprintf(proc_path, "/proc/%d", pid);
	if(file2str(proc_path, "stat", sbuf, sizeof(sbuf)) == -1)
		return -1;

	tmp = strrchr(sbuf, ')');
	psbuf = tmp + 2;
	sscanf(psbuf,
		"%c "
		"%d %d %d %d %d "
		"%lu %lu %lu %lu %lu "
		"%Lu %Lu %Lu %Lu "  /* utime stime cutime cstime */
		"%s", /* discard */
		&state,
		&ppid, &pgrp, &session, &tty, &tpgid,
		&flags, &min_flt, &cmin_flt, &maj_flt, &cmaj_flt,
		&utime, &stime, &cutime, &cstime,
		discard_str);

	*slice = utime + stime + cutime + cstime;
	return ret;
}

static uint64_t read_cpu_jiffy(void)
{
	FILE *fp = NULL;
	char buf[1024];
	int num;
	unsigned long long u, n, s, i, w, x, y, z; /* as represented in /proc/stat */
	uint64_t total_jiffy = 0;

	if (!(fp = fopen("/proc/stat", "r"))) {
		stderr_print("failed /proc/stat open\n");
		return 0;
	}

	if (!fgets(buf, sizeof(buf), fp)) {
		stderr_print("failed /proc/stat read\n");
		return 0;
	}

	num = sscanf(buf, "cpu %Lu %Lu %Lu %Lu %Lu %Lu %Lu %Lu",
			&u, &n, &s, &i, &w, &x, &y, &z);
	if (num < 4) {
		stderr_print("failed /proc/stat read\n");
		return 0;
	}
	total_jiffy = u + n + s + i + w + x + y + z;

	return total_jiffy;
}

int cpu_slice_proc_load(track_proc_t * proc)
{
	double cpu_usage = 0;
	uint64_t proc_slice, total_slice;
	int ret = 0;

	total_slice = read_cpu_jiffy();
	if (total_slice == 0)
		return -1;

	ret = do_stats(proc->pid, &proc_slice);
	if (ret < 0)
		return -1;

	if (proc->slice[0].total_slice == 0) {
		/* First time */
		proc->slice[0].total_slice = total_slice;
		proc->slice[0].process_slice = proc_slice;
		if (proc->slice[1].total_slice == 0) {
			sleep_ms(5);
			total_slice = read_cpu_jiffy();
			do_stats(proc->pid, &proc_slice);
			proc->slice[1].total_slice = total_slice;
			proc->slice[1].process_slice = proc_slice;
		}
	} else {
		proc->slice[1].total_slice = total_slice;
		proc->slice[1].process_slice = proc_slice;
	}

	if (proc->slice[1].process_slice != 0) {
		double proc_slice_diff = (double)proc->slice[1].process_slice - proc->slice[0].process_slice;
		double total_slice_diff = (double)proc->slice[1].total_slice - proc->slice[0].total_slice;
		double proc_slice = fabs(proc_slice_diff);
		double total_slice = fabs(total_slice_diff);

		if (total_slice == 0) {
			proc->cpu_usage = 0;
			return 0;
		}

		/*
		 * The most task's cpu usage is about 0.x%, so x1000
		 * is necessary.
		 */
		cpu_usage = 1000 * (double)(proc_slice * g_ncpus) / total_slice;
		proc->cpu_usage = (uint64_t)cpu_usage;
	}

	return 0;
}

int proc_monitor(void)
{
	int i;
	pid_t pid;
	char *procs = (char *)malloc(target_procs.nr_proc * 8);
	char pid_str[12] = {0};

	memset(procs, 0, target_procs.nr_proc * 8);
	for (i=0; i<target_procs.nr_proc; i++) {
		pid = target_procs.pid[i];
		if (i == 0)
			sprintf(procs, "%d", pid);
		else {
			sprintf(pid_str, ",%d", pid);
			strcat(procs, pid_str);
			memset(pid_str, '\0', sizeof(pid_str));
		}
	}
	monitor_start(procs);
	target_procs.ready = 1;

	perf_status_set(PERF_STATUS_IDLE);
	perf_profiling_start();
	debug_print(NULL, 2, "start monitoring process: %s\n", procs);
	free(procs);

	return target_procs.nr_proc;
}
