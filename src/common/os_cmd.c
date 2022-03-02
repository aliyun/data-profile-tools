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

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include "../include/types.h"
#include "../include/page.h"
#include "../include/win.h"
#include "../include/perf.h"
#include "../include/cmd.h"
#include "../include/disp.h"
#include "../include/proc_map.h"
#include "../include/os/os_cmd.h"

int os_preop_switch2profiling(cmd_t * cmd
		__attribute__ ((unused)), boolean_t * smpl)
{
	*smpl = B_FALSE;

	if (!perf_profiling_started()) {
		perf_allstop();
		if (perf_profiling_start() != 0) {
			return (-1);
		}

		*smpl = B_TRUE;
	}

	return (0);
}

int os_preop_switch2ml(cmd_t * cmd __attribute__ ((unused)), boolean_t * smpl)
{
	*smpl = B_FALSE;
	if (!perf_maplist_is_start()) {
		perf_allstop();
		if (perf_maplist_start(0) != 0) {
			return (-1);
		}

		*smpl = B_TRUE;
	}

	return (0);
}

int
os_preop_mlrefresh(cmd_t * cmd __attribute__ ((unused)),
		   boolean_t * smpl __attribute__ ((unused)))
{
	/* Not supported on Linux. */
	return (0);
}

int
os_preop_mlmap_get(cmd_t * cmd __attribute__ ((unused)),
		   boolean_t * smpl __attribute__ ((unused)))
{
	/* Not supported on Linux. */
	return (0);
}

int
os_op_mlmap_stop(cmd_t * cmd __attribute__ ((unused)),
		 boolean_t smpl __attribute__ ((unused)))
{
	/* Not supported on Linux. */
	return (0);
}

int os_op_switch2ml(cmd_t * cmd, boolean_t smpl)
{
	page_t *cur = page_current_get();
	int type = PAGE_WIN_TYPE(cur);
	int ret = 0;

	if (type == WIN_TYPE_MONIPROC) {
		CMD_MAPLIST(cmd)->pid = DYN_MONI_PROC(cur)->pid;
		ret = op_page_next(cmd, smpl);
	} else
		ret = -1;

	return ret;
}
