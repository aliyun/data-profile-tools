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

#ifndef _DAMONTOP_PROC_H
#define _DAMONTOP_PROC_H

#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>
#include <pthread.h>
#include "types.h"
#include "perf.h"
#include "proc_map.h"
#include "damon.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROC_NAME_SIZE 16
#define PROC_HASHTBL_SIZE 128
#define PROC_RECORD_MAX 256
#define PROC_MAX 50

#define PROC_HASHTBL_INDEX(pid)	\
	((int)(pid) % PROC_HASHTBL_SIZE)

extern pid_t damontop_pid;

typedef struct _cpu_slice_t {
	unsigned long process_slice;
	unsigned long total_slice;
} cpu_slice_t;

typedef struct _track_proc {
	pthread_mutex_t mutex;
	int ref_count;
	boolean_t inited;
	boolean_t tagged;
	boolean_t removing;
	pid_t pid;
	int flag;
	int idarr_idx;
	int cpuid_max;
	int record_max;
	char name[PROC_NAME_SIZE];
	int intval_ms;
	uint64_t key;
	map_proc_t map;
	count_value_t *countval_arr;
	int nr_nonzero;
	cpu_slice_t slice[2];
	uint64_t cpu_usage;
	struct _track_proc *hash_prev;
	struct _track_proc *hash_next;
	struct _track_proc *sort_prev;
	struct _track_proc *sort_next;
} track_proc_t;

typedef struct _proc_group {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int nprocs;
	int nlwps;
	int sort_idx;
	boolean_t inited;
	track_proc_t *hashtbl[PROC_HASHTBL_SIZE];
	track_proc_t *latest;
	track_proc_t **sort_arr;
} proc_group_t;

struct damon_proc_t {
	int min_regions;
	int max_regions;
	int nr_proc;
	pid_t pid[PROC_MAX];
	uint64_t last_ms;
	int ready; /* 0: Not, 1: Run monitor is ok */
};
extern struct damon_proc_t target_procs;

extern int proc_group_init(void);
extern void proc_group_fini(void);
extern track_proc_t *proc_find(pid_t);
extern void proc_count(int *);
extern void proc_group_lock(void);
extern void proc_group_unlock(void);
extern void proc_resort(sort_key_t);
extern track_proc_t *proc_sort_next(void);
extern void moniproc_resort(sort_key_t sort, track_proc_t *proc);
extern void proc_enum_update(pid_t);
extern int proc_refcount_inc(track_proc_t *);
extern void proc_refcount_dec(track_proc_t *);
extern int proc_countval_update(track_proc_t *, int, perf_count_id_t, uint64_t);
extern void proc_intval_update(int);
extern int proc_intval_get(track_proc_t *);
extern void proc_profiling_clear(void);
extern uint64_t proc_countval_sum(count_value_t *, ui_count_id_t);
extern void proc_countvalue_sort(count_value_t * sort_countval_arr, int *nonzero);
extern int monitor_start(char *procs);
extern void monitor_exit(void);
extern int proc_monitor(void);
extern int cpu_slice_proc_load(track_proc_t * proc);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_PROC_H */
