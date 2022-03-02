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

#ifndef	_DAMONTOP_OS_PERF_H
#define	_DAMONTOP_OS_PERF_H

#include <sys/types.h>
#include <inttypes.h>
#include <sys/mman.h>
#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern precise_type_t g_precise;

#define PERF_REC_NUM	512
#define PERF_FD_NUM		NCPUS_MAX * PERF_COUNT_NUM
#define INVALID_CODE_UMASK	(uint64_t)(-1)

typedef struct _os_maplist_rec {
	uint64_t addr;
	uint64_t cpu;
	uint64_t latency;
} os_maplist_rec_t;

typedef struct _perf_cpu {
	int cpuid;
	int fds[PERF_COUNT_NUM];
	int group_idx;
	int map_len;
	int map_mask;
	void *map_base;
	boolean_t hit;
	boolean_t hotadd;
	boolean_t hotremove;
	count_value_t countval_last;
} perf_cpu_t;

typedef int (*pfn_perf_cpu_op_t)(struct _perf_cpu *, void *);

typedef struct _perf_damon_event {
	int cpuid;
	int perf_fd;
	int group_idx;
	int map_len;
	int map_mask;
	void *map_base;
	count_value_t countval_last;
} perf_damon_event_t;

struct _perf_ctl;
union _perf_task;
struct _track_proc;

extern boolean_t os_profiling_started(struct _perf_ctl *);
extern int os_profiling_start(struct _perf_ctl *, union _perf_task *);
extern int os_profiling_smpl(struct _perf_ctl *, union _perf_task *, int *);
extern int os_profiling_partpause(struct _perf_ctl *, union _perf_task *);
extern int os_profiling_multipause(struct _perf_ctl *, union _perf_task *);
extern int os_profiling_restore(struct _perf_ctl *, union _perf_task *);
extern int os_profiling_multi_restore(struct _perf_ctl *, union _perf_task *);
extern int os_ml_start(struct _perf_ctl *, union _perf_task *);
extern int os_maplist_events(struct _perf_ctl *, union _perf_task *, int *);
extern int os_perf_init(void);
extern void os_perf_fini(void);
extern void os_perfthr_quit_wait(void);
extern int os_perf_profiling_partpause(perf_count_id_t);
extern int os_perf_profiling_multipause(perf_count_id_t *);
extern int os_perf_profiling_restore(perf_count_id_t);
extern int os_perf_profiling_multi_restore(perf_count_id_t *);
extern int os_perf_maplist_smpl(struct _perf_ctl *, pid_t);
extern void os_allstop(void);
extern int os_perf_allstop(void);
extern void* os_perf_priv_alloc(boolean_t *);
extern void os_perf_priv_free(void *);
extern void os_perf_cpuarr_init(perf_cpu_t *, int, boolean_t);
extern void os_perf_cpuarr_fini(perf_cpu_t *, int, boolean_t);
extern int os_perf_cpuarr_refresh(perf_cpu_t *, int, int *, int, boolean_t);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_OS_PERF_H */
