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

#ifndef _DAMONTOP_TYPES_H
#define	_DAMONTOP_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	B_FALSE = 0,
	B_TRUE
} boolean_t;

#define IP_NUM 32
#define INVALID_FD -1
#define INVALID_CONFIG (uint64_t)(-1)

/* TODO: remove */
#define CPU_TYPE_NUM 10

typedef enum {
	PRECISE_NORMAL = 0,
	PRECISE_HIGH,
	PRECISE_LOW
} precise_type_t;

#define PRECISE_NUM	3

typedef enum {
	SORT_KEY_INVALID = -1,
	SORT_KEY_CPU = 0,
	SORT_KEY_PID,
	SORT_KEY_START,
	SORT_KEY_SIZE,
	SORT_KEY_NRA,
	SORT_KEY_CPI,
	SORT_KEY_RL
} sort_key_t;

#define	MAX_VALUE	4294967295U
#define	NS_SEC		1000000000
#define	MS_SEC		1000
#define	NS_USEC		1000
#define USEC_MS		1000
#define	NS_MS		1000000
#define MICROSEC	1000000
#define GHZ		1000000000
#define	MHZ		1000000
#define	KHZ		1000
#define	GB_BYTES	1024*1024*1024
#define	KB_BYTES	1024
#define TIME_NSEC_MAX	2147483647

#ifndef PATH_MAX
#define	PATH_MAX	2048
#endif

#define SCRIPT_SIZE	4096

#define SMPL_PERIOD_INFINITE			0XFFFFFFFFFFFFFFULL
#define SMPL_PERIOD_DAMON_DEFAULT			10000
#define SMPL_PERIOD_DAMON_1_DEFAULT		10000
#define SMPL_PERIOD_CLK_DEFAULT			10000000
#define SMPL_PERIOD_CORECLK_DEFAULT		SMPL_PERIOD_INFINITE

#define SMPL_PERIOD_DAMON_MIN			5000
#define SMPL_PERIOD_DAMON_1_MIN			5000
#define SMPL_PERIOD_CLK_MIN			1000000
#define SMPL_PERIOD_CORECLK_MIN			SMPL_PERIOD_INFINITE

#define SMPL_PERIOD_DAMON_MAX			100000
#define SMPL_PERIOD_DAMON_1_MAX			100000
#define SMPL_PERIOD_CLK_MAX			100000000
#define SMPL_PERIOD_CORECLK_MAX			SMPL_PERIOD_INFINITE

typedef enum {
	UI_COUNT_INVALID = -1,
	UI_COUNT_CORE_CLK = 0,
	UI_COUNT_DAMON_NR_REGIONS,
	UI_COUNT_DAMON_START,
	UI_COUNT_DAMON_END,
	UI_COUNT_DAMON_NR_ACCESS,
	UI_COUNT_DAMON_AGE,
	UI_COUNT_DAMON_LOCAL,
	UI_COUNT_DAMON_REMOTE,
	UI_COUNT_CLK,
	UI_COUNT_NUM
} ui_count_id_t;


#define NR_KDAMON_MAX	128
#define NCPUS_MAX		256
#define NPROCS_MAX		4096

typedef enum {
	PERF_COUNT_INVALID = -1,
	PERF_COUNT_CORE_CLK = 0,
	PERF_COUNT_DAMON_NR_REGIONS,
	PERF_COUNT_DAMON_START,
	PERF_COUNT_DAMON_END,
	PERF_COUNT_DAMON_NR_ACCESS,
	PERF_COUNT_DAMON_AGE,
	PERF_COUNT_DAMON_LOCAL,
	PERF_COUNT_DAMON_REMOTE,
	PERF_COUNT_NUM
} perf_count_id_t;

typedef struct _count_value {
	uint64_t key;
	uint64_t counts[PERF_COUNT_NUM];
} count_value_t;

typedef struct _bufaddr {
	uint64_t addr;
	uint64_t size;
} bufaddr_t;

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_TYPES_H */
