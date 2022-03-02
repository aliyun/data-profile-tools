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

#ifndef _DAMONTOP_CMD_H
#define _DAMONTOP_CMD_H

#include <sys/types.h>
#include <inttypes.h>
#include "types.h"
#include "win.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	CMD_HOME_CHAR		'h'
#define	CMD_REFRESH_CHAR	'r'
#define	CMD_QUIT_CHAR		'q'
#define	CMD_BACK_CHAR		'b'
#define	CMD_MAPLIST_CHAR	'l'
#define	CMD_DAMON_OVERVIEW_CHAR	'd'
#define	CMD_1_CHAR		'1'
#define	CMD_2_CHAR		'2'
#define	CMD_3_CHAR		'3'
#define	CMD_4_CHAR		'4'
#define CMD_5_CHAR		'5'
#define CMD_MAP_GET_CHAR	'm'
#define CMD_MAP_STOP_CHAR	's'

typedef enum {
	CMD_INVALID_ID = 0,
	CMD_HOME_ID,
	CMD_MONITOR_ID,
	CMD_MAP_LIST_ID,
	CMD_DAMON_OVERVIEW_ID,
	CMD_DAMON_DETAIL_ID,
	CMD_MAP_GET_ID,
	CMD_MAP_STOP_ID,
	CMD_1_ID, /* sort reserved */
	CMD_2_ID, /* sort reserved */
	CMD_3_ID, /* sort reserved */
	CMD_4_ID, /* sort reserved */
	CMD_5_ID, /* sort reserved */
	CMD_REFRESH_ID,
	CMD_QUIT_ID,
	CMD_BACK_ID,
	CMD_RESIZE_ID,
} cmd_id_t;

#define CMD_NUM	25

typedef struct _cmd_home {
	cmd_id_t id;
} cmd_home_t;

typedef struct _cmd_ir_normalize {
	cmd_id_t id;
} cmd_ir_normalize_t;

typedef struct _cmd_monitor {
	cmd_id_t id;
	pid_t pid;
} cmd_monitor_t;

typedef struct _cmd_maplist {
	cmd_id_t id;
	pid_t pid;
} cmd_maplist_t;

typedef struct _cmd_damon_overview {
	cmd_id_t id;
} cmd_damon_overview_t;

typedef struct _cmd_damon_detail {
	cmd_id_t id;
	int nid;
} cmd_damon_detail_t;

typedef union _cmd {
	cmd_home_t home;
	cmd_ir_normalize_t ir_normalize;
	cmd_monitor_t monitor;
	cmd_maplist_t lat;
	cmd_damon_overview_t kdamon_list;
	cmd_damon_detail_t node_detail;
} cmd_t;

typedef int (*pfn_switch_preop_t)(cmd_t *, boolean_t *);
typedef int (*pfn_switch_op_t)(cmd_t *, boolean_t);

typedef struct _switch {
	pfn_switch_preop_t preop;
	pfn_switch_op_t op;
} switch_t;

#define	CMD_ID_SET(cmd_addr, id) \
	(*(int *)(cmd_addr) = (id))

#define	CMD_ID(cmd_addr) \
	(*(int *)(cmd_addr))

#define	CMD_MAPLIST(cmd) \
	((cmd_maplist_t *)(cmd))

#define	CMD_MONITOR(cmd) \
	((cmd_monitor_t *)(cmd))

#define	CMD_DAMON_DETAIL(cmd) \
	((cmd_damon_detail_t *)(cmd))

extern void switch_table_init(void);
extern int cmd_id_get(char);
extern void cmd_execute(cmd_t *, boolean_t *);
extern int op_refresh(cmd_t *, boolean_t);
extern int op_page_next(cmd_t *, boolean_t);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_CMD_H */
