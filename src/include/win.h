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

#ifndef _DAMONTOP_WIN_H
#define _DAMONTOP_WIN_H

#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>
#include <pthread.h>
#include "types.h"
#include "reg.h"
#include "./os/os_win.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_sortkey;

#define	DAMONTOP_TITLE	"DATOP v1.0, (C) 2021 Alibaba Corporation"
#define	CMD_CAPTION		"Command: "
#define	WIN_PROCNAME_SIZE	12
#define	WIN_DESCBUF_SIZE	32
#define	WIN_LINECHAR_MAX	1024
#define	WIN_NLINES_MAX		4096

#define	GO_HOME_WAIT	3

#define	NOTE_DEFAULT \
	"Q: Quit; H: Home; B: Back; R: Refresh; D: DAMON"

#define	NOTE_TOPNPROC_RAW \
	"Q: Quit; H: Home; R: Refresh; D: DAMON"

#define NOTE_TOPNPROC	NOTE_DEFAULT

#define	NOTE_TOPNLWP	NOTE_DEFAULT

#define	NOTE_MONIPROC \
	"Q: Quit; H: Home; B: Back; R: Refresh; " \
	"D: DAMON; L: Map-list"

#define	NOTE_MONILWP 	NOTE_MONIPROC

#define	NOTE_NONODE \
	"Q: Quit; H: Home; B: Back; R: Refresh"

#define	NOTE_DAMON_OVERVIEW NOTE_NONODE
#define	NOTE_DAMON_DETAIL NOTE_NONODE

#define	NOTE_INVALID_PID \
	"Invalid process id! (Q: Quit; H: Home)"

#define	NOTE_INVALID_LWPID \
	"Invalid lwp id! (Q: Quit; H: Home)"

#define	NOTE_INVALID_MAP \
	"No memory mapping found! (Q: Quit; H: Home; B: Back)"

#define	NOTE_INVALID_NUMAMAP \
	"No memory NUMA mapping found! (Q: Quit; H: Home; B: Back)"

#define	CAPTION_PID			"PID"
#define	CAPTION_LWP			"LWP"
#define	CAPTION_CPI			"CPI"
#define	CAPTION_CPU			"CPU%%"
#define	CAPTION_NID			"NODE"
#define	CAPTION_PROC		"PROC"
#define	CAPTION_ADDR		"ADDR"
#define	CAPTION_SIZE		"SIZE(KiB)"

/* For damon */
#define	CAPTION_INDEX		"INDEX"
#define	CAPTION_TYPE		"TYPE"
#define	CAPTION_START		"START"
#define	CAPTION_END			"END"
#define	CAPTION_NR_ACCESS	"ACCESS"
#define	CAPTION_AGE			"AGE"
#define	CAPTION_LOCAL		"LOCAL"
#define	CAPTION_REMOTE		"REMOTE"
#define	CAPTION_SAMPLE		"SAMPLE"
#define	CAPTION_AGGR		"AGGR"
#define	CAPTION_UPDATE		"UPDATE"

#define	CAPTION_TYPE_TEXT	".text"
#define	CAPTION_TYPE_DATA	".data"
#define	CAPTION_TYPE_HEAP	"heap"
#define	CAPTION_TYPE_STACK	"stack"

#define	CAPTION_DESC		"DESC"
#define	CAPTION_BUFHIT		"ACCESS"
#define	CAPTION_AVGLAT		"LAT(ns)"
#define	CAPTION_NPROC		"NPROC"
#define	CAPTION_RSS			"RSS"
#define CAPTION_LLC_OCCUPANCY	"LLC.OCCUPANCY(MB)"
#define CAPTION_TOTAL_BW	"MBAND.TOTAL"
#define CAPTION_LOCAL_BW	"MBAND.LOCAL"

typedef enum {
	WIN_TYPE_TOPNPROC = 0,
	WIN_TYPE_MONIPROC,
	WIN_TYPE_MAPLIST_PROC,
	WIN_TYPE_DAMON_OVERVIEW,
	WIN_TYPE_DAMON_DETAIL,
} win_type_t;

#define	WIN_TYPE_NUM		20

typedef enum {
	WARN_INVALID = 0,
	WARN_PERF_DATA_FAIL,
	WARN_INVALID_PID,
	WARN_INVALID_LWPID,
	WARN_WAIT,
	WARN_WAIT_PERF_LL_RESULT,
	WARN_NOT_IMPL,
	WARN_GO_HOME,
	WARN_INVALID_NID,
	WARN_INVALID_MAP,
	WARN_INVALID_NUMAMAP,
	WARN_LL_NOT_SUPPORT,
	WARN_STOP
} warn_type_t;

typedef struct _dyn_win {
	win_type_t type;
	boolean_t inited;
	win_reg_t *title;
	void *dyn;
	win_reg_t *note;
	void *page;
	boolean_t (*draw)(struct _dyn_win *);
	void (*scroll)(struct _dyn_win *, int);
	void (*scroll_enter)(struct _dyn_win *);
	void (*destroy)(struct _dyn_win *);
} dyn_win_t;

typedef struct _win_countvalue {
	double rpi;
	double lpi;
	double cpu;
	uint64_t start;
	uint64_t end;
	uint64_t nr_access;
	uint64_t age;
	uint64_t local;
	uint64_t remote;
	double cpi;
	double rma;
	double lma;
	double rl;
} win_countvalue_t;

typedef struct _dyn_topnproc {
	win_reg_t summary;
	win_reg_t caption;
	win_reg_t data;
	win_reg_t hint;
} dyn_topnproc_t;

typedef struct _topnproc_line {
	win_countvalue_t value;
	char proc_name[WIN_PROCNAME_SIZE];
	char map_name[WIN_DESCBUF_SIZE];
	char map_attr[4 + 1];
	int pid;
	int nlwp;
} topnproc_line_t;

typedef struct _dyn_moniproc {
	pid_t pid;
	win_reg_t msg;
	win_reg_t caption_cur;
	win_reg_t data_cur;
	win_reg_t hint;
} dyn_moniproc_t;

typedef struct _moni_line {
	win_countvalue_t value;
	int nid;
	char map_name[WIN_DESCBUF_SIZE];
	char map_attr[4 + 1];
	pid_t pid;
} moni_line_t;

typedef struct _dyn_topnlwp {
	pid_t pid;
	win_reg_t msg;
	win_reg_t caption;
	win_reg_t data;
	win_reg_t hint;
} dyn_topnlwp_t;

typedef struct _topnlwp_line {
	win_countvalue_t value;
	pid_t pid;
	int lwpid;
} topnlwp_line_t;

typedef struct _dyn_lat {
	pid_t pid;
	int lwpid;
	win_reg_t msg;
	win_reg_t caption;
	win_reg_t data;
} dyn_maplist_t;

typedef struct _lat_line {
	bufaddr_t bufaddr;	/* must be the first field */
	int naccess;
	int latency;
	int nsamples;
	pid_t pid;
	boolean_t nid_show;
	char desc[WIN_DESCBUF_SIZE];
} maplist_line_t;

typedef struct _dyn_damon_overview {
	win_reg_t msg;
	win_reg_t caption_cur;
	win_reg_t data_cur;
	win_reg_t hint;
} dyn_damon_overview_t;

typedef struct _damon_overview_line {
	win_countvalue_t value;
	double mem_all;
	double mem_free;
	int nid;
	int pid; /* kdamon pid */
	int nr_proc;
	double rss;
	int sample; /* us */
	int aggr;
	int update;
} damon_overview_line_t;

typedef struct _dyn_damondetail {
	int nid;
	int kid;
	win_reg_t msg;
	win_reg_t node_data;
	win_reg_t hint;
} dyn_damondetail_t;

typedef struct _dyn_warn {
	win_reg_t msg;
	win_reg_t pad;
} dyn_warn_t;

#define	DYN_MONI_PROC(page) \
	((dyn_moniproc_t *)((page)->dyn_win.dyn))

#define	DYN_MAPLIST(page) \
	((dyn_maplist_t *)((page)->dyn_win.dyn))

#define	DYN_DAMON_OVERVIEW(page) \
	((dyn_damon_overview_t *)((page)->dyn_win.dyn))

#define	DYN_DAMON_DETAIL(page) \
	((dyn_damondetail_t *)((page)->dyn_win.dyn))

/* CPU unhalted cycles in a second */
extern uint64_t g_clkofsec;

extern void win_fix_init(void);
extern void win_fix_fini(void);
extern void win_warn_msg(warn_type_t);
extern int win_dyn_init(void *);
extern void win_dyn_fini(void *);
extern void win_invalid_proc(void);
extern void win_invalid_lwp(void);
extern void win_note_show(char *);
extern void win_title_show(void);
extern boolean_t win_maplist_data_show(track_proc_t *, dyn_maplist_t *, boolean_t *);
extern maplist_line_t* win_maplist_buf_create(track_proc_t *, int *);
extern void win_maplist_buf_fill(maplist_line_t *, int, track_proc_t *);
extern int win_maplist_cmp(const void *, const void *);
extern void win_maplist_str_build(char *, int, int, void *);
extern void win_size2str(uint64_t, char *, int);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_WIN_H */
