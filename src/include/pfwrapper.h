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

#ifndef __DAMONTOP_PFWRAPPER_H__
#define __DAMONTOP_PFWRAPPER_H__

#include <sys/types.h>
#include <pthread.h>
#include "linux/perf_event.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PF_MAP_NPAGES_MAX			1024
#define PF_MAP_NPAGES_MIN			64
#define PF_MAP_NPAGES_NORMAL		256

#if defined(__x86_64__)
#ifndef __NR_perf_event_open
#define __NR_perf_event_open 298
#endif

#define rmb() asm volatile("lfence" ::: "memory")
#define wmb() asm volatile("sfence" ::: "memory")
#define mb() asm volatile("mfence":::"memory")
#endif

#if defined(__aarch64__)
#ifndef __NR_perf_event_open
#define __NR_perf_event_open 241
#endif

#define rmb()  __asm__ __volatile__ ("" : : : "memory")
#define wmb()  __asm__ __volatile__ ("" : : : "memory")
#define mb()   __asm__ __volatile__ ("" : : : "memory")
#endif

typedef struct _pf_conf {
	perf_count_id_t perf_count_id;
	uint32_t type;
	uint64_t config;
	uint64_t config1;
	uint64_t sample_period;
} pf_conf_t;

struct damon_field {
	unsigned short common_type; 			// offset:0;       size:2; signed:0;
	unsigned char common_flags; 			// offset:2;       size:1; signed:0;
	unsigned char common_preempt_count; 	// offset:3;       size:1; signed:0;
	int common_pid; 			// offset:4;       size:4; signed:1;
	unsigned long target_id; 	// offset:8;       size:8; signed:0;
	unsigned int nr_regions; 	// offset:16;      size:4; signed:0;
	unsigned long start; 		// offset:24;      size:8; signed:0;
	unsigned long end; 			// offset:32;      size:8; signed:0;
	unsigned int nr_accesses; 	// offset:40;      size:4; signed:0;
};

typedef struct _pf_profiling_rec {
	unsigned int pid;
	unsigned int tid;
	uint64_t period;
	count_value_t countval;
} pf_profiling_rec_t;

typedef struct _pf_profiling_rbrec {
	uint32_t pid;
	uint32_t tid;
	uint64_t time_enabled;
	uint64_t time_running;
	uint64_t counts[PERF_COUNT_NUM];
	uint64_t ip_num;
} pf_profiling_rbrec_t;

struct _perf_cpu;
struct _node;

typedef int (*pfn_pf_event_op_t)(struct _perf_cpu *);

int pf_ringsize_init(void);
int pf_profiling_setup(int, pf_conf_t *);
int pf_profiling_start(void);
int pf_profiling_stop(void);
int pf_profiling_allstart(struct _perf_cpu *);
int pf_profiling_allstop(struct _perf_cpu *);
void pf_profiling_record(pf_profiling_rec_t *, int *);
void pf_resource_free(void);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_PFWRAPPER_H */
