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

#ifndef _DAMONTOP_OS_WIN_H
#define	_DAMONTOP_OS_WIN_H

#include <sys/types.h>
#include <inttypes.h>
#include "../types.h"
#include "../proc.h"
#include "../damon.h"
#include "os_perf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	NOTE_MAP_LIST \
	"Q: Quit; H: Home; B: Back; R: Refresh"

#define	NOTE_LATNODE \
	"Q: Quit; H: Home; B: Back; R: Refresh"

#define NOTE_LLCALLCHAIN	\
	"Q: Quit; H: Home; B: Back; R: Refresh"

struct _damon_overview_line;
struct _dyn_damondetail;
struct _dyn_win;
struct _page;
struct _lat_line;
struct _win_reg;
struct _track_proc;

extern void os_damon_overview_caption_build(char *, int);
extern void os_damon_overview_data_build(char *, int,
    struct _damon_overview_line *, kdamon_t *);
extern void os_damondetail_data(struct _dyn_damondetail *dyn,
		struct _win_reg *seg);
extern void os_maplist_buf_hit(struct _lat_line *, int, os_maplist_rec_t *,
	struct _track_proc *, uint64_t *);
extern boolean_t os_maplist_win_draw(struct _dyn_win *);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_OS_WIN_H */
