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

#ifndef __DAMONTOP_NODE_H__
#define __DAMONTOP_NODE_H__

#include <sys/types.h>
#include <inttypes.h>
#include <pthread.h>
#include "types.h"
#include "perf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INVALID_CPUID	-1

/* Number of online CPUs */
extern int g_ncpus;

typedef struct _kdamon {
	int pid;
	int cpu;
	int sampling_intval;
	int aggregation_intval;
	int regions_update_intval;
	count_value_t countval;
} kdamon_t;

typedef struct _kdamon_group {
	pthread_mutex_t mutex;
	kdamon_t kdamons[NR_KDAMON_MAX];
	int nkdamons;
	int intval_ms;
	boolean_t inited;
} kdamon_group_t;

extern int online_ncpu_refresh(void);
extern void kdamon_refresh(void);
extern kdamon_t *kdamon_get(int kid_idx);
extern int get_kdamon_pid(void);
extern unsigned int get_nr_kdamon(void);
uint64_t get_max_countval(count_value_t * countval_arr,
		ui_count_id_t ui_count_id);

#ifdef __cplusplus
}
#endif

#endif /* _NODE_H */
