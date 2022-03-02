#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <signal.h>
#include <curses.h>
#include "../include/types.h"
#include "../include/util.h"
#include "../include/disp.h"
#include "../include/reg.h"
#include "../include/proc.h"
#include "../include/page.h"
#include "../include/perf.h"
#include "../include/win.h"
#include "../include/ui_perf_map.h"
#include "../include/plat.h"
#include "../include/damon.h"
#include "../include/os/os_perf.h"
#include "../include/os/os_util.h"
#include "../include/os/os_win.h"

/*
 * Build the readable string for caption line.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
void os_damon_overview_caption_build(char *buf, int size)
{
	(void)snprintf(buf, size,
		       "%6s%12s%12s%11s%11s%11s%12s",
		       CAPTION_PID, CAPTION_NPROC, CAPTION_RSS,
		       CAPTION_SAMPLE, CAPTION_AGGR, CAPTION_UPDATE, CAPTION_CPU);
}

void os_damon_overview_data_build(char *buf, int size,
		damon_overview_line_t *line, kdamon_t *kda)
{
	win_countvalue_t *value = &line->value;
	double s_intval, a_intval, u_intval;
	char s_intval_str[11], a_intval_str[11], u_intval_str[11];

	s_intval = (double)line->sample/1000;
	a_intval = (double)line->aggr/1000;
	u_intval = (double)line->update/1000;
	sprintf(s_intval_str, "%.1fms", s_intval);
	sprintf(a_intval_str, "%.1fms", a_intval);
	sprintf(u_intval_str, "%.1fms", u_intval);
	(void)snprintf(buf, size,
			"%6d%12d%12f%11s%11s%11s%11.1f",
			kda->pid, line->nr_proc, line->rss, s_intval_str,
			a_intval_str, u_intval_str, value->cpu);
}

static void damon_detail_line_show(win_reg_t * reg, char *title,
		char *value, int line)
{
	char s1[256];

	snprintf(s1, sizeof(s1), "%-30s%15s", title, value);
	reg_line_write(reg, line, ALIGN_LEFT, s1);
	dump_write("%s\n", s1);
}

/*
 * Display the performance statistics per node.
 */
void os_damondetail_data(dyn_damondetail_t * dyn, win_reg_t * seg)
{
	char s1[256];
	int i = 1;

	reg_erase(seg);

	/* Display the DAMON mode */
	damon_detail_line_show(seg, "mode (virt/phys):", "virt", i++);

	/*
	 * Display the sampling interval
	 */
	damon_detail_line_show(seg, "sampling interval:", "5000", i++);

	/*
	 * Display the aggregation interval
	 */
	damon_detail_line_show(seg, "aggregation interval:", "100000", i++);

	/*
	 * Display the regions update interval
	 */
	damon_detail_line_show(seg, "regions update interval:", "1000000", i++);

	/*
	 * Display the CPU utilization
	 */
	(void)snprintf(s1, sizeof(s1), "%.1f%%", 0.1 * 100.0);
	damon_detail_line_show(seg, "CPU%:", s1, i++);

	reg_refresh_nout(seg);
}

/*
 * The implementation of displaying window on screen for
 * window type "WIN_TYPE_MAPLIST_PROC".
 */
boolean_t os_maplist_win_draw(dyn_win_t * win)
{
	dyn_maplist_t *dyn = (dyn_maplist_t *) (win->dyn);
	track_proc_t *proc;
	boolean_t note_out, ret;

	if (!perf_maplist_is_start()) {
		win_warn_msg(WARN_LL_NOT_SUPPORT);
		win_note_show(NOTE_MAP_LIST);
		return (B_FALSE);
	}

	if ((proc = proc_find(dyn->pid)) == NULL) {
		win_warn_msg(WARN_INVALID_PID);
		win_note_show(NOTE_INVALID_PID);
		return (B_FALSE);
	}

	if (map_proc_load(proc) != 0) {
		proc_refcount_dec(proc);
		win_warn_msg(WARN_INVALID_MAP);
		win_note_show(NOTE_INVALID_MAP);
		return (B_FALSE);
	}

	win_title_show();
	ret = win_maplist_data_show(proc, dyn, &note_out);
	if (!note_out) {
		win_note_show(NOTE_MAP_LIST);
	}

	proc_refcount_dec(proc);
	reg_update_all();
	return (ret);
}

/*
 * "maplist_buf" points to an array which contains the process address
 * mapping. Each item in array represents a buffer in process address
 * space. "rec" points to a SMPL record.
 */
void os_maplist_buf_hit(maplist_line_t * maplist_buf, int nlines,
		os_maplist_rec_t * rec, track_proc_t *proc, uint64_t *total_sample)
{
	count_value_t record;
	uint64_t line_addr = maplist_buf->bufaddr.addr;
	uint64_t line_size = maplist_buf->bufaddr.size;
	uint64_t line_end_addr = line_addr + line_size;
	int i = 0;
	uint64_t min = 999;

	if (!proc->nr_nonzero)
		return;

	/*
	 * Check if the linear address is located in a buffer in
	 * process address space.
	 */
	for (i=0; i<proc->nr_nonzero; i++) {
		record = proc->countval_arr[i];
		uint64_t start_addr = record.counts[PERF_COUNT_DAMON_START];
		uint64_t end_addr = record.counts[PERF_COUNT_DAMON_END];

		if (line_addr <= start_addr && end_addr <= line_end_addr) {
			maplist_buf->naccess = record.counts[PERF_COUNT_DAMON_NR_ACCESS];
			return;
		}
		if (start_addr <= line_addr && line_end_addr <= end_addr) {
			maplist_buf->naccess = record.counts[PERF_COUNT_DAMON_NR_ACCESS];
			return;
		}

		if (end_addr <= line_addr || start_addr >= line_end_addr)
			continue;

		if ((start_addr >= line_addr && start_addr < line_end_addr)
				|| (end_addr > line_addr && end_addr <= line_end_addr)) {
			if (min > record.counts[PERF_COUNT_DAMON_NR_ACCESS] &&
					record.counts[PERF_COUNT_DAMON_NR_ACCESS] != 0)
				min = record.counts[PERF_COUNT_DAMON_NR_ACCESS];
		}
	}

	if (min == 999)
		min = 0;
	maplist_buf->naccess = min;
}
