/* This file contains code to create/show/destroy a window on screen. */

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
#include "include/types.h"
#include "include/util.h"
#include "include/disp.h"
#include "include/reg.h"
#include "include/proc.h"
#include "include/page.h"
#include "include/perf.h"
#include "include/plat.h"
#include "include/damon.h"
#include "include/os/os_util.h"
#include "include/os/os_win.h"

static boolean_t s_first_load = B_TRUE;
static win_reg_t s_note_reg;
static win_reg_t s_title_reg;

/*
 * Build the readable string for caption line.
 * (window type: "WIN_TYPE_TOPNPROC")
 */
static void topnproc_caption_build(char *buf, int size)
{
	char tmp[64];

	switch (g_sortkey) {
	case SORT_KEY_PID:
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_PID);
		(void)snprintf(buf, size,
			       "%6s%15s%11s%16s%16s%11s%10s%9s%9s%9s",
			       tmp, CAPTION_PROC, CAPTION_TYPE,
			       tmp, CAPTION_END, CAPTION_SIZE,
			       CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
			       CAPTION_REMOTE);
		break;

	case SORT_KEY_START:
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_START);
		(void)snprintf(buf, size,
			       "%6s%15s%11s%16s%16s%11s%10s%9s%9s%9s",
			       CAPTION_PID, CAPTION_PROC, CAPTION_TYPE,
			       tmp, CAPTION_END, CAPTION_SIZE,
			       CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
			       CAPTION_REMOTE);
		break;

	case SORT_KEY_SIZE:
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_SIZE);
		(void)snprintf(buf, size,
			       "%6s%15s%11s%16s%16s%11s%10s%9s%9s%9s",
			       CAPTION_PID, CAPTION_PROC, CAPTION_TYPE,
			       CAPTION_START, CAPTION_END, tmp,
			       CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
			       CAPTION_REMOTE);
		break;

	case SORT_KEY_NRA:
		/* The only support sort */
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_NR_ACCESS);
		(void)snprintf(buf, size,
			       "%6s%15s%11s%16s%16s%11s%10s%9s%9s%9s",
			       CAPTION_PID, CAPTION_PROC, CAPTION_TYPE,
			       CAPTION_START, CAPTION_END, CAPTION_SIZE, tmp,
			       CAPTION_AGE, CAPTION_LOCAL, CAPTION_REMOTE);
		break;

	default:
		(void)snprintf(buf, size,
			       "%6s%15s%11s%16s%16s%11s%10s%9s%9s%9s",
			       CAPTION_PID, CAPTION_PROC, CAPTION_TYPE,
			       CAPTION_START, CAPTION_END, CAPTION_SIZE,
			       CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
			       CAPTION_REMOTE);
		break;
	}
}

/*
 *  PID           PROC       TYPE      START        END       SIZE   *ACCESS    LOCAL   REMOTE
 *    1        systemd      .text          0          0       1.10         0     1.30     0.00
 *    2       kthreadd      .text          0          0       1.10         0     1.30     0.00
 *    3         rcu_gp      .text          0          0       1.10         0     1.30     0.00
 *    4     rcu_par_gp      .text          0          0       1.10         0     1.30     0.00
 *    6    kworker/0:0      .text          0          0       1.10         0     1.30     0.00
 */
static void topnproc_data_build(char *buf, int size, topnproc_line_t * line)
{
	win_countvalue_t *value = &line->value;
	float percent_local = 0, percent_remote = 0;
	char local_str[16] = {0}, remote_str[16] = {0};
	uint64_t sz;

	sz = (value->end - value->start) >> 10;
	if (value->local || value->remote) {
		percent_local =
		    (float)value->local / (value->local + value->remote);
		percent_remote = 1 - percent_local;

		sprintf(local_str, "%8.2f%%%%", percent_local * 100);
		sprintf(remote_str, "%8.2f%%%%", percent_remote * 100);
		(void)snprintf(buf, size,
			       "%6d%15s%11s%16lx%16lx%11ld%10ld%9ld%9s%9s",
			       line->pid, line->proc_name, line->map_attr,
			       value->start, value->end, sz, value->nr_access,
			       value->age, local_str, remote_str);
	} else {
		sprintf(local_str, "%8.2f%%%%", (float)0);
		sprintf(remote_str, "%8.2f%%%%", (float)0);
		(void)snprintf(buf, size,
			       "%6d%15s%11s%16lx%16lx%11ld%10ld%9ld%9s%9s",
			       line->pid, line->proc_name, line->map_attr,
			       value->start, value->end, sz, value->nr_access,
			       value->age, local_str, remote_str);
	}
}

/*
 * Build the readable string for data line.
 * (window type: "WIN_TYPE_TOPNPROC")
 */
static void topnproc_str_build(char *buf, int size, int idx, void *pv)
{
	topnproc_line_t *lines = (topnproc_line_t *) pv;
	topnproc_line_t *line = &lines[idx];

	topnproc_data_build(buf, size, line);
}

/*
 * Build the readable string for scrolling line.
 * (window type: "WIN_TYPE_TOPNPROC")
 */
static void topnproc_line_get(win_reg_t * r, int idx, char *line, int size)
{
	topnproc_line_t *lines;

	lines = (topnproc_line_t *) (r->buf);
	topnproc_str_build(line, size, idx, (void *)lines);
}

/*
 * Initialize the display layout.
 * (window type: "WIN_TYPE_TOPNPROC")
 */
static dyn_topnproc_t *topnproc_dyn_create(int type)
{
	dyn_topnproc_t *dyn;
	void *buf;
	int i;

	if ((buf = zalloc(sizeof(topnproc_line_t) * WIN_NLINES_MAX)) == NULL) {
		return (NULL);
	}
	if ((dyn = zalloc(sizeof(dyn_topnproc_t))) == NULL) {
		free(buf);
		return (NULL);
	}

	if ((i = reg_init(&dyn->summary, 0, 1, g_scr_width, 2, A_BOLD)) < 0)
		goto L_EXIT;
	if ((i =
	     reg_init(&dyn->caption, 0, i, g_scr_width, 2,
		      A_BOLD | A_UNDERLINE)) < 0)
		goto L_EXIT;
	if ((i =
	     reg_init(&dyn->data, 0, i, g_scr_width, g_scr_height - i - 5,
		      0)) < 0)
		goto L_EXIT;

	if (type == WIN_TYPE_TOPNPROC) {
		reg_buf_init(&dyn->data, buf, topnproc_line_get);
	}

	reg_scroll_init(&dyn->data, B_TRUE);
	(void)reg_init(&dyn->hint, 0, i, g_scr_width,
		       g_scr_height - i - 1, A_BOLD);
	return (dyn);
L_EXIT:
	free(dyn);
	free(buf);
	return (NULL);
}

/*
 * Release the resources of window.
 * (window type: "WIN_TYPE_TOPNPROC")
 */
static void topnproc_win_destroy(dyn_win_t * win)
{
	dyn_topnproc_t *dyn;

	if ((dyn = win->dyn) != NULL) {
		if (dyn->data.buf != NULL) {
			free(dyn->data.buf);
			dyn->data.buf = NULL;
		}

		reg_win_destroy(&dyn->summary);
		reg_win_destroy(&dyn->caption);
		reg_win_destroy(&dyn->data);
		reg_win_destroy(&dyn->hint);
		free(dyn);
	}
}

/*
 * Seperate the value of metrics by raw perf data.
 */
static int win_countvalue_fill(win_countvalue_t * cv,
		count_value_t * countval_arr)
{
	uint64_t nr_access, age, start, end, local, remote;

	start = proc_countval_sum(countval_arr, UI_COUNT_DAMON_START);
	end = proc_countval_sum(countval_arr, UI_COUNT_DAMON_END);
	nr_access =
	    proc_countval_sum(countval_arr, UI_COUNT_DAMON_NR_ACCESS);
	age =
	    proc_countval_sum(countval_arr, UI_COUNT_DAMON_AGE);
	local = proc_countval_sum(countval_arr, UI_COUNT_DAMON_LOCAL);
	remote = proc_countval_sum(countval_arr, UI_COUNT_DAMON_REMOTE);

	cv->start = start;
	cv->end = end;
	cv->nr_access = nr_access;
	cv->age = age;
	cv->local = local;
	cv->remote = remote;

	return 0;
}

/*
 * Convert the perf data to the required format and copy
 * the converted result out via "line".
 * (window type: "WIN_TYPE_TOPNPROC")
 */
static void topnproc_data_save(track_proc_t * proc, topnproc_line_t * line)
{
	map_entry_t *entry;
	uint64_t start, end;
	uint64_t max_nr_access = 0;
	int i;
	count_value_t *countval_arr = proc->countval_arr;
	count_value_t *max_countval_arr = proc->countval_arr;

	(void)memset(line, 0, sizeof(topnproc_line_t));

	for (i=0; i<PROC_RECORD_MAX; i++) {
		countval_arr = &proc->countval_arr[i];
		if (countval_arr == NULL)
			break;
		if (max_nr_access <
				proc_countval_sum(countval_arr, UI_COUNT_DAMON_NR_ACCESS)) {
			max_nr_access =
				proc_countval_sum(countval_arr, UI_COUNT_DAMON_NR_ACCESS);
			max_countval_arr = countval_arr;
		}
	}

	start = proc_countval_sum(max_countval_arr, UI_COUNT_DAMON_START);
	end = proc_countval_sum(max_countval_arr, UI_COUNT_DAMON_END);

	if ((entry = map_entry_find_simiar(proc, start, end - start)) == NULL) {
		strncpy(line->map_attr, "----", 4);
		line->map_attr[4] = '\0';
	} else {
		/* Found */
		attr_bitmap2str(entry->attr, line->map_attr);
		line->map_attr[4] = '\0';
		strncpy(line->map_name, entry->desc, sizeof(line->map_name));
	}

	/*
	 * Cut off the process name if it's too long.
	 */
	(void)strncpy(line->proc_name, proc->name, sizeof(line->proc_name));
	line->proc_name[WIN_PROCNAME_SIZE - 1] = 0;
	line->pid = proc->pid;

	/* Only show the first record */
	(void)win_countvalue_fill(&line->value, max_countval_arr);
}

static void topnproc_data_show(dyn_win_t * win)
{
	dyn_topnproc_t *dyn;
	win_reg_t *r, *data_reg;
	char content[WIN_LINECHAR_MAX], intval_buf[16];
	int nprocs, i;
	track_proc_t *proc;
	topnproc_line_t *lines;

	dyn = (dyn_topnproc_t *) (win->dyn);
	data_reg = &dyn->data;

	/* Get the number of total processes and total threads */
	proc_count(&nprocs);
	nprocs = MIN(nprocs, WIN_NLINES_MAX);
	data_reg->nlines_total = nprocs;

	/*
	 * Convert the sampling interval (nanosecond) to
	 * a human readable string.
	 */
	disp_intval(intval_buf, 16);

	/*
	 * Display the summary message:
	 * "Monitoring xxx processes and yyy threads (interval: zzzs)"
	 */
	(void)snprintf(content, sizeof(content),
			"Monitoring %d processes (interval: %s)", nprocs, intval_buf);

	r = &dyn->summary;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	dump_write("\n*** %s\n", content);
	reg_refresh_nout(r);

	/*
	 * Display the caption of table:
	 * "PID PROC TYPE START END SIZE(KiB) ACCESS AGE LOCAL REMOTE"
	 */
	r = &dyn->caption;
	if (win->type == WIN_TYPE_TOPNPROC) {
		topnproc_caption_build(content, sizeof(content));
	} else {
		debug_print(NULL, 2, "This win type not found\n");
	}

	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	dump_write("%s\n", content);
	reg_refresh_nout(r);

	reg_erase(data_reg);
	lines = (topnproc_line_t *) (data_reg->buf);

	/*
	 * Sort the processes by specified metric which
	 * is indicated by g_sortkey
	 */
	proc_group_lock();
	proc_resort(g_sortkey);

	/*
	 * Save the perf data of processes in scrolling buffer.
	 */
	for (i = 0; i < nprocs; i++) {
		if ((proc = proc_sort_next()) == NULL) {
			break;
		}

		if (map_proc_load(proc) != 0) {
			win_warn_msg(WARN_INVALID_MAP);
		}
		if (target_procs.ready != 1 &&
				cpu_slice_proc_load(proc) != 0) {
			win_warn_msg(WARN_INVALID_MAP);
		}
		if (!target_procs.ready && i < target_procs.nr_proc)
			target_procs.pid[i] = proc->pid;

		topnproc_data_save(proc, &lines[i]);
	}

	if (!target_procs.ready) {
		uint64_t intval_ms = current_ms(&g_tvbase) - target_procs.last_ms;

		proc_resort(g_sortkey);
		for (i = 0; i < nprocs; i++) {
			if ((proc = proc_sort_next()) == NULL) {
				break;
			}
			topnproc_data_save(proc, &lines[i]);
		}

		if (intval_ms >= 10000) {
			/*
			 * It's time to choose the maximum CPU usage processes.
			 * And here, it shall restore normal settings
			 */
			nprocs = proc_monitor();
			g_disp_intval = DISP_DEFAULT_INTVAL;
		}
	}

	/*
	 * Display the processes with metrics in scrolling buffer
	 */
	if (win->type == WIN_TYPE_TOPNPROC) {
		reg_scroll_show(data_reg, (void *)lines, nprocs,
				topnproc_str_build);
	}

	proc_group_unlock();
	reg_refresh_nout(data_reg);

	/*
	 * Dispaly hint message for window type: "WIN_TYPE_TOPNPROC"
	 */
	r = &dyn->hint;
	reg_erase(r);

	if (win->type == WIN_TYPE_TOPNPROC) {
		reg_line_write(r, 1, ALIGN_LEFT,
			       "<- Hotkey for sorting: 1(PID), 2(START), 3(SIZE), "
			       "4(ACCESS), 5(RMA) ->");
	}

	reg_line_write(r, 2, ALIGN_LEFT, "CPU%% = system CPU utilization");

	reg_refresh_nout(r);
}

/*
 * Show the message "Loading ..." on screen
 */
static void load_msg_show(void)
{
	char content[64];
	win_reg_t r;

	(void)snprintf(content, sizeof(content), "Loading ...");

	(void)reg_init(&r, 0, 1, g_scr_width, g_scr_height - 1, A_BOLD);
	reg_erase(&r);
	reg_line_write(&r, 1, ALIGN_LEFT, content);
	reg_refresh(&r);
	reg_win_destroy(&r);
}

/*
 * Show the title "DamonTop v1.0, (C) 2021 Alibaba Corporation"
 */
void win_title_show(void)
{
	reg_erase(&s_title_reg);
	reg_line_write(&s_title_reg, 0, ALIGN_MIDDLE, DAMONTOP_TITLE);
	reg_refresh_nout(&s_title_reg);
}

/*
 * Show the note information at the bottom of window"
 */
void win_note_show(char *note)
{
	char *content;
	char *p;

	p = NOTE_DEFAULT;

	content = (note != NULL) ? note : p;
	reg_erase(&s_note_reg);
	reg_line_write(&s_note_reg, 0, ALIGN_LEFT, content);
	reg_refresh(&s_note_reg);
	reg_update_all();
}

/*
 * Display window on screen.
 */
static boolean_t topnproc_win_draw(dyn_win_t * win)
{
	char *note;

	win_title_show();
	if (s_first_load) {
		s_first_load = B_FALSE;
		load_msg_show();
		win_note_show(NULL);
		reg_update_all();
		return (B_TRUE);
	}

	topnproc_data_show(win);

	if (win->type == WIN_TYPE_TOPNPROC) {
		note = NOTE_TOPNPROC;
		win_note_show(note);
	} else {
		note = NOTE_TOPNPROC_RAW;
		win_note_show(note);
	}

	reg_update_all();
	return (B_TRUE);
}

/*
 * The function would be called when user hits the <UP>/<DOWN> key
 * to scroll data line.
 */
static void topnproc_win_scroll(dyn_win_t * win, int scroll_type)
{
	dyn_topnproc_t *dyn = (dyn_topnproc_t *) (win->dyn);

	reg_line_scroll(&dyn->data, scroll_type);
}

/*
 * The function would be called when user hits the "ENTER" key
 * on selected data line.
 */
static void topnproc_win_scrollenter(dyn_win_t * win)
{
	dyn_topnproc_t *dyn = (dyn_topnproc_t *) (win->dyn);
	win_reg_t *r = &dyn->data;
	scroll_line_t *scroll = &r->scroll;
	topnproc_line_t *lines;
	cmd_monitor_t cmd_monitor;
	boolean_t badcmd;

	if (scroll->highlight == -1) {
		return;
	}

	/*
	 * Construct a command to switch to next window
	 * (WIN_TYPE_MONIPROC).
	 */
	lines = (topnproc_line_t *) (r->buf);
	cmd_monitor.id = CMD_MONITOR_ID;
	cmd_monitor.pid = lines[scroll->highlight].pid;

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	cmd_execute((cmd_t *) (&cmd_monitor), &badcmd);
}

/*
 * Build the readable string for caption line.
 */
static void moni_caption_build(char *buf, int size)
{
	char tmp[64];

	switch (g_sortkey) {
	case SORT_KEY_START:
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_START);
		(void)snprintf(buf, size,
		       "%6s%10s%16s%16s%11s%10s%10s%10s%10s",
		       CAPTION_INDEX, CAPTION_TYPE, tmp, CAPTION_END,
		       CAPTION_SIZE, CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
		       CAPTION_REMOTE);
		break;

	case SORT_KEY_SIZE:
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_SIZE);
		(void)snprintf(buf, size,
		       "%6s%10s%16s%16s%11s%10s%10s%10s%10s",
		       CAPTION_INDEX, CAPTION_TYPE, CAPTION_START, CAPTION_END,
		       tmp, CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
		       CAPTION_REMOTE);
		break;

	case SORT_KEY_NRA:
		/* The only support sort */
		(void)snprintf(tmp, sizeof(tmp), "*%s", CAPTION_NR_ACCESS);
		(void)snprintf(buf, size,
		       "%6s%10s%16s%16s%11s%10s%10s%10s%10s",
		       CAPTION_INDEX, CAPTION_TYPE, CAPTION_START, CAPTION_END,
		       CAPTION_SIZE, tmp, CAPTION_AGE, CAPTION_LOCAL,
		       CAPTION_REMOTE);
		break;

	default:
		(void)snprintf(buf, size,
		       "%6s%10s%16s%16s%11s%10s%10s%10s%10s",
		       CAPTION_INDEX, CAPTION_TYPE, CAPTION_START, CAPTION_END,
		       CAPTION_SIZE, CAPTION_NR_ACCESS, CAPTION_AGE, CAPTION_LOCAL,
		       CAPTION_REMOTE);
		break;
	}
}

static void moni_data_build(char *buf, int size, moni_line_t * line, int idx)
{
	win_countvalue_t *value = &line->value;
	float percent_local = 0, percent_remote = 0;
	char local_str[16] = {0}, remote_str[16] = {0};
	uint64_t sz;

	sz = (value->end - value->start) >> 10;
	if (value->local || value->remote) {
		percent_local =
		    (float)value->local / (value->local + value->remote);
		percent_remote = 1 - percent_local;

		sprintf(local_str, "%9.2f%%%%", percent_local * 100);
		sprintf(remote_str, "%9.2f%%%%", percent_remote * 100);
		(void)snprintf(buf, size,
			       "%6d%10s%16lx%16lx%11ld%10ld%10ld%10s%10s", idx,
			       line->map_attr, value->start, value->end, sz,
			       value->nr_access, value->age,
				   local_str, remote_str);
	} else {
		sprintf(local_str, "%9.2f%%%%", (float)0);
		sprintf(remote_str, "%9.2f%%%%", (float)0);
		(void)snprintf(buf, size,
			       "%6d%10s%16lx%16lx%11ld%10ld%10ld%10s%10s", idx,
			       line->map_attr, value->start, value->end, sz,
			       value->nr_access, value->age,
				   local_str, remote_str);
	}
}

/*
 * Build the readable string for data line.
 */
static void moni_str_build(char *buf, int size, int idx, void *pv)
{
	moni_line_t *lines = (moni_line_t *) pv;
	moni_line_t *line = &lines[idx];

	moni_data_build(buf, size, line, idx);
}

/*
 * Build the readable string for scrolling line.
 */
static void moni_line_get(win_reg_t * r, int idx, char *line, int size)
{
	moni_line_t *lines;

	lines = (moni_line_t *) (r->buf);
	moni_str_build(line, size, idx, (void *)lines);
}

/*
 * Initialize the display layout.
 */
static dyn_moniproc_t *moniproc_dyn_create(pid_t pid)
{
	dyn_moniproc_t *dyn;
	void *buf_cur;
	int i;

	if ((buf_cur = zalloc(sizeof(moni_line_t) * WIN_NLINES_MAX)) == NULL) {
		return (NULL);
	}
	if ((dyn = zalloc(sizeof(dyn_moniproc_t))) == NULL) {
		free(buf_cur);
		return (NULL);
	}

	dyn->pid = pid;

	if ((i = reg_init(&dyn->msg, 0, 1, g_scr_width, 3, A_BOLD)) < 0)
		goto L_EXIT;
	if ((i =
	     reg_init(&dyn->caption_cur, 0, i, g_scr_width, 2,
		      A_BOLD | A_UNDERLINE)) < 0)
		goto L_EXIT;
	if ((i =
	     reg_init(&dyn->data_cur, 0, i, g_scr_width, g_scr_height - i - 5,
		      0)) < 0)
		goto L_EXIT;

	reg_buf_init(&dyn->data_cur, buf_cur, moni_line_get);
	reg_scroll_init(&dyn->data_cur, B_TRUE);

	(void)reg_init(&dyn->hint, 0, i, g_scr_width,
		       g_scr_height - i - 1, A_BOLD);
	return (dyn);
L_EXIT:
	free(dyn);
	free(buf_cur);
	return (NULL);
}

/*
 * Convert the perf data to the required format and copy
 * the converted result out via "line".
 */
static void moniproc_data_save(track_proc_t * proc, int idx, moni_line_t * line)
{
	map_entry_t *entry;
	uint64_t start, end;
	count_value_t *countval_arr = &proc->countval_arr[idx];

	(void)memset(line, 0, sizeof(moni_line_t));
	start = proc_countval_sum(countval_arr, UI_COUNT_DAMON_START);
	end = proc_countval_sum(countval_arr, UI_COUNT_DAMON_END);

	if ((entry = map_entry_find_simiar(proc, start, end - start)) == NULL) {
		strncpy(line->map_attr, "----", 4);
		line->map_attr[4] = '\0';
	} else {
		/* Found */
		attr_bitmap2str(entry->attr, line->map_attr);
		line->map_attr[4] = '\0';
		strncpy(line->map_name, entry->desc, WIN_DESCBUF_SIZE);
		line->map_name[WIN_DESCBUF_SIZE - 1] = '\0';
	}

	line->nid = 0;
	line->pid = proc->pid;

	(void)win_countvalue_fill(&line->value, countval_arr);
}

void win_invalid_proc(void)
{
	win_warn_msg(WARN_INVALID_PID);
	win_note_show(NOTE_INVALID_PID);
	(void)sleep(GO_HOME_WAIT);
	disp_go_home();
}

static boolean_t moniproc_data_show(dyn_win_t * win, boolean_t * note_out)
{
	dyn_moniproc_t *dyn;
	win_reg_t *r;
	char content[WIN_LINECHAR_MAX], intval_buf[16];
	pid_t pid;
	track_proc_t *proc;
	int i, nr_nonzero;
	moni_line_t *lines;

	dyn = (dyn_moniproc_t *) (win->dyn);
	pid = dyn->pid;

	*note_out = B_FALSE;
	if ((proc = proc_find(pid)) == NULL) {
		win_invalid_proc();
		*note_out = B_TRUE;
		return (B_FALSE);
	}

	r = &dyn->msg;
	disp_intval(intval_buf, 16);
	(void)snprintf(content, sizeof(content),
		       "Monitoring the process \"%s\" (%d) (interval: %s)",
		       proc->name, proc->pid, intval_buf);

	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	dump_write("\n*** %s\n", content);
	reg_refresh_nout(r);

	/*
	 * Display the caption of table:
	 * "INDEX TYPE START END SIZE ACCESS LOCAL REMOTE
	 */
	moni_caption_build(content, sizeof(content));
	r = &dyn->caption_cur;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	dump_write("%s\n", content);
	reg_refresh_nout(r);

	proc_group_lock();
	/* Sort and arrange records. */
	count_value_t *sort_countval_arr =
	    malloc(sizeof(count_value_t) * PROC_RECORD_MAX);;
	memcpy(sort_countval_arr, proc->countval_arr,
	       sizeof(count_value_t) * PROC_RECORD_MAX);
	proc_countvalue_sort(sort_countval_arr, &nr_nonzero);
	memcpy(proc->countval_arr, sort_countval_arr,
			sizeof(count_value_t) * PROC_RECORD_MAX);
	free(sort_countval_arr);

	/* Display DAMON related stat */
	r = &dyn->msg;
	(void)snprintf(content, sizeof(content), "Current regions: %ld",
			proc->countval_arr[0].counts[PERF_COUNT_DAMON_NR_REGIONS]);
	reg_line_write(r, 2, ALIGN_LEFT, content);
	dump_write("\n*** %s\n", content);
	reg_refresh_nout(r);

	/* Set show lines. */
	r = &dyn->data_cur;
	reg_erase(r);
	lines = (moni_line_t *) (r->buf);
	nr_nonzero = MIN(nr_nonzero, WIN_NLINES_MAX);
	r->nlines_total = nr_nonzero;

	/*
	 * Save the per-node data with metrics of a specified process
	 * in scrolling buffer.
	 */
	proc->nr_nonzero = nr_nonzero;
	moniproc_resort(g_sortkey, proc);
	for (i = 0; i < nr_nonzero; i++) {
		moniproc_data_save(proc, i, &lines[i]);
	}
	proc_group_unlock();

	/*
	 * Display the detailed data with metrics of a specified process
	 * in scrolling buffer
	 */
	reg_scroll_show(r, (void *)lines, nr_nonzero, moni_str_build);
	reg_refresh_nout(r);
	proc_refcount_dec(proc);

	/*
	 * Dispaly hint message for window type "WIN_TYPE_MONIPROC"
	 */
	r = &dyn->hint;
	reg_erase(r);
	reg_line_write(r, r->nlines_scr - 3, ALIGN_LEFT,
			"<- Hotkey for sorting: 1(PID), 2(START), 3(SIZE), "
			"4(ACCESS), 5(RMA) ->");
	reg_line_write(r, r->nlines_scr - 2, ALIGN_LEFT,
		       "LOCAL = local numa access   REMOTE = remote numa access");
	reg_refresh_nout(r);

	return (B_TRUE);
}

/*
 * Display window on screen.
 */
static boolean_t moniproc_win_draw(dyn_win_t * win)
{
	boolean_t note_out, ret;

	win_title_show();
	ret = moniproc_data_show(win, &note_out);

	if (!note_out)
		win_note_show(NOTE_MONIPROC);

	reg_update_all();
	return (ret);
}

void win_invalid_lwp(void)
{
	win_warn_msg(WARN_INVALID_LWPID);
	win_note_show(NOTE_INVALID_LWPID);
	(void)sleep(GO_HOME_WAIT);
	disp_go_home();
}

/*
 * The common interface of initializing the screen layout for
 * window type "WIN_TYPE_MONIPROC".
 */
static void *moni_dyn_create(page_t * page, boolean_t(**draw) (dyn_win_t *),
			     win_type_t * type)
{
	void *dyn;

	if ((dyn = moniproc_dyn_create(CMD_MONITOR(&page->cmd)->pid)) !=
			NULL) {
		*draw = moniproc_win_draw;
		*type = WIN_TYPE_MONIPROC;

		return (dyn);
	}

	return (NULL);
}

/*
 * Release the resources of window.
 */
static void moniproc_win_destroy(dyn_win_t * win)
{
	dyn_moniproc_t *dyn;

	if ((dyn = win->dyn) != NULL) {
		if (dyn->data_cur.buf != NULL) {
			free(dyn->data_cur.buf);
		}

		reg_win_destroy(&dyn->msg);
		reg_win_destroy(&dyn->caption_cur);
		reg_win_destroy(&dyn->data_cur);
		reg_win_destroy(&dyn->hint);
		free(dyn);
	}
}

/*
 * The function would be called when user hits the <UP>/<DOWN> key
 * to scroll data line.
 */
static void moniproc_win_scroll(dyn_win_t * win, int scroll_type)
{
	dyn_moniproc_t *dyn = (dyn_moniproc_t *) (win->dyn);

	reg_line_scroll(&dyn->data_cur, scroll_type);
}

/*
 * Build the readable string for data line.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
static void damon_overview_str_build(char *buf, int size, int idx, void *pv)
{
	damon_overview_line_t *lines = (damon_overview_line_t *) pv;
	damon_overview_line_t *line = &lines[idx];
	kdamon_t *kda;

	if ((kda = kdamon_get(idx)) != NULL) {
		os_damon_overview_data_build(buf, size, line, kda);
	}
}

/*
 * Build the readable string for scrolling line.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
static void damon_overview_line_get(win_reg_t * r, int idx, char *line, int size)
{
	damon_overview_line_t *lines;

	lines = (damon_overview_line_t *) (r->buf);
	damon_overview_str_build(line, size, idx, (void *)lines);
}

/*
 * Initialize the display layout for window type
 * "WIN_TYPE_DAMON_OVERVIEW"
 */
static dyn_damon_overview_t *damon_overview_dyn_create(void)
{
	dyn_damon_overview_t *dyn;
	void *buf_cur;
	int i, nkdamons;

	nkdamons = get_nr_kdamon();
	if ((buf_cur = zalloc(sizeof(damon_overview_line_t) * nkdamons)) == NULL) {
		return (NULL);
	}
	if ((dyn = zalloc(sizeof(dyn_damon_overview_t))) == NULL) {
		free(buf_cur);
		return (NULL);
	}

	if ((i = reg_init(&dyn->msg, 0, 1, g_scr_width, 2, A_BOLD)) < 0)
		goto L_EXIT;
	if ((i = reg_init(&dyn->caption_cur, 0, i, g_scr_width, 2,
			  A_BOLD | A_UNDERLINE)) < 0)
		goto L_EXIT;
	if ((i = reg_init(&dyn->data_cur, 0, i, g_scr_width, nkdamons, 0)) < 0)
		goto L_EXIT;

	reg_buf_init(&dyn->data_cur, buf_cur, damon_overview_line_get);
	reg_scroll_init(&dyn->data_cur, B_TRUE);

	(void)reg_init(&dyn->hint, 0, i, g_scr_width,
		       g_scr_height - i - 1, A_BOLD);
	return (dyn);
L_EXIT:
	free(dyn);
	free(buf_cur);
	return (NULL);
}

/*
 * Release the resources of window.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
static void damon_overview_win_destroy(dyn_win_t * win)
{
	dyn_damon_overview_t *dyn;

	if ((dyn = win->dyn) != NULL) {
		if (dyn->data_cur.buf != NULL) {
			free(dyn->data_cur.buf);
		}

		reg_win_destroy(&dyn->msg);
		reg_win_destroy(&dyn->caption_cur);
		reg_win_destroy(&dyn->data_cur);
		reg_win_destroy(&dyn->hint);
		free(dyn);
	}
}

/*
 * Convert the perf data to the required format and copy
 * the converted result out via "line".
 */
static void damon_overview_data_save(int kid_idx,
		int nnodes __attribute__ ((unused)),
		damon_overview_line_t * line)
{
	kdamon_t *kdamon;

	(void)memset(line, 0, sizeof(damon_overview_line_t));
	if ((kdamon = kdamon_get(kid_idx)) == NULL) {
		return;
	}

	line->pid = kdamon->pid;
	line->nr_proc = 1;
	line->rss = 0;
	line->sample = kdamon->sampling_intval;
	line->aggr = kdamon->aggregation_intval;
	line->update = kdamon->regions_update_intval;
}

static boolean_t damon_overview_data_show(dyn_win_t * win, boolean_t * note_out)
{
	dyn_damon_overview_t *dyn;
	win_reg_t *r;
	char content[WIN_LINECHAR_MAX], intval_buf[16];
	int i, nks;
	damon_overview_line_t *lines;

	*note_out = B_FALSE;
	dyn = (dyn_damon_overview_t *) (win->dyn);

	disp_intval(intval_buf, 16);
	(void)snprintf(content, sizeof(content),
			"Damon Overview (interval: %s)", intval_buf);

	r = &dyn->msg;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	reg_refresh_nout(r);
	dump_write("\n*** %s\n", content);

	kdamon_refresh();
	/*
	 * Display the caption of table:
	 */
	os_damon_overview_caption_build(content, sizeof(content));
	r = &dyn->caption_cur;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	dump_write("%s\n", content);
	reg_refresh_nout(r);

	nks = get_nr_kdamon();
	r = &dyn->data_cur;
	reg_erase(r);
	lines = (damon_overview_line_t *) (r->buf);
	r->nlines_total = nks;

	/*
	 * Save the per-node data with metrics in scrolling buffer.
	 */
	for (i = 0; i < nks; i++) {
		damon_overview_data_save(i, nks, &lines[i]);
	}

	/*
	 * Display the per-node data in scrolling buffer
	 */
	reg_scroll_show(r, (void *)lines, nks, damon_overview_str_build);
	reg_refresh_nout(r);

	/*
	 * Dispaly hint message for window type "WIN_TYPE_DAMON_OVERVIEW"
	 */
	r = &dyn->hint;
	reg_erase(r);
	reg_line_write(r, r->nlines_scr - 2, ALIGN_LEFT,
		       "CPU%% = per-node CPU utilization");
	reg_refresh_nout(r);

	return (B_TRUE);
}

/*
 * Display window on screen.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
static boolean_t damon_overview_win_draw(dyn_win_t * win)
{
	boolean_t note_out, ret;

	win_title_show();
	ret = damon_overview_data_show(win, &note_out);

	if (!note_out) {
		win_note_show(NOTE_DAMON_OVERVIEW);
	}

	reg_update_all();
	return (ret);
}

/*
 * The function would be called when user hits the <UP>/<DOWN> key
 * to scroll data line.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
static void damon_overview_win_scroll(dyn_win_t * win, int scroll_type)
{
	dyn_damon_overview_t *dyn = (dyn_damon_overview_t *) (win->dyn);

	reg_line_scroll(&dyn->data_cur, scroll_type);
}

/*
 * The function would be called when user hits the "ENTER" key
 * on selected data line.
 * (window type: "WIN_TYPE_DAMON_OVERVIEW")
 */
static void damon_overview_win_scrollenter(dyn_win_t * win)
{
	dyn_damon_overview_t *dyn = (dyn_damon_overview_t *) (win->dyn);
	win_reg_t *r = &dyn->data_cur;
	scroll_line_t *scroll = &r->scroll;
	damon_overview_line_t *lines;
	cmd_damon_detail_t cmd;
	boolean_t badcmd;

	if (scroll->highlight == -1) {
		return;
	}

	/*
	 * Construct a command to switch to next window
	 * "WIN_TYPE_DAMON_DETAIL".
	 */
	lines = (damon_overview_line_t *) (r->buf);
	cmd.id = CMD_DAMON_DETAIL_ID;
	cmd.nid = lines[scroll->highlight].nid;

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	cmd_execute((cmd_t *) (&cmd), &badcmd);
}

/*
 * Initialize the display layout for window type
 * "WIN_TYPE_DAMON_DETAIL"
 */
static dyn_damondetail_t *damon_detail_dyn_create(page_t * page)
{
	dyn_damondetail_t *dyn;
	int i;

	if ((dyn = zalloc(sizeof(dyn_damondetail_t))) == NULL) {
		return (NULL);
	}

	dyn->nid = CMD_DAMON_DETAIL(&page->cmd)->nid;

	if ((i = reg_init(&dyn->msg, 0, 1, g_scr_width, 2,
					A_BOLD | A_UNDERLINE)) < 0)
		goto L_EXIT;
	if ((i = reg_init(&dyn->node_data, 0, i, g_scr_width,
					g_scr_height - i - 4, 0)) < 0)
		goto L_EXIT;

	(void)reg_init(&dyn->hint, 0, i, g_scr_width, 3, A_BOLD);
	return (dyn);

L_EXIT:
	free(dyn);
	return NULL;
}

static boolean_t damon_detail_data_show(dyn_win_t * win, boolean_t * note_out)
{
	dyn_damondetail_t *dyn;
	win_reg_t *r;
	char content[WIN_LINECHAR_MAX], intval_buf[16];

	*note_out = B_FALSE;
	dyn = (dyn_damondetail_t *) (win->dyn);

	/*
	 * Convert the sampling interval (nanosecond) to
	 * a human readable string.
	 */
	disp_intval(intval_buf, 16);
	(void)snprintf(content, sizeof(content),
			"kdamon%d information (interval: %s)", dyn->nid, intval_buf);

	r = &dyn->msg;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	reg_refresh_nout(r);
	dump_write("\n*** %s\n", content);

	os_damondetail_data((dyn_damondetail_t *) (win->dyn), &dyn->node_data);

	r = &dyn->hint;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, "CPU%% = per-node CPU utilization");
	reg_refresh_nout(r);

	return (B_FALSE);
}

/*
 * Display window on screen.
 * (window type: "WIN_TYPE_DAMON_DETAIL")
 */
static boolean_t damon_detail_win_draw(dyn_win_t * win)
{
	boolean_t note_out = B_FALSE, ret;

	win_title_show();
	ret = damon_detail_data_show(win, &note_out);
	if (!note_out) {
		win_note_show(NOTE_DAMON_DETAIL);
	}

	reg_update_all();
	return (ret);
}

/*
 * Release the resources for window type "WIN_TYPE_DAMON_DETAIL"
 */
static void damon_detail_win_destroy(dyn_win_t * win)
{
	dyn_damondetail_t *dyn;

	if ((dyn = win->dyn) != NULL) {
		reg_win_destroy(&dyn->msg);
		reg_win_destroy(&dyn->node_data);
		reg_win_destroy(&dyn->hint);
		free(dyn);
	}
}

void win_size2str(uint64_t size, char *buf, int bufsize)
{
	uint64_t i, j;

	/*
	 * "buf" points to a big enough buffer.
	 */
	if ((i = (size / KB_BYTES)) < KB_BYTES) {
		(void)snprintf(buf, bufsize, "%" PRIu64 "K", i);
	} else if ((j = i / KB_BYTES) < KB_BYTES) {
		if ((i % KB_BYTES) == 0) {
			(void)snprintf(buf, bufsize, "%" PRIu64 "M", j);
		} else {
			(void)snprintf(buf, bufsize, "%.1fM",
				       (double)i / (double)KB_BYTES);
		}
	} else {
		if ((j % KB_BYTES) == 0) {
			(void)snprintf(buf, bufsize, "%" PRIu64 "G",
				       j / KB_BYTES);
		} else {
			(void)snprintf(buf, bufsize, "%.1fG",
				       (double)j / (double)KB_BYTES);
		}
	}
}

/*
 * Build the readable string of data line which contains buffer address,
 * buffer size, access%, and buffer description.
 */
void win_maplist_str_build(char *buf, int size, int idx, void *pv)
{
	maplist_line_t *lines = (maplist_line_t *) pv;
	maplist_line_t *line = &lines[idx];
	char size_str[32];

	win_size2str(line->bufaddr.size, size_str, sizeof(size_str));

	if (!line->nid_show) {
		(void)snprintf(buf, size,
			       "%16" PRIX64 "%11s%11d%34s",
			       line->bufaddr.addr, size_str, line->naccess, line->desc);
	}
}

static void maplist_line_get(win_reg_t * r, int idx, char *line, int size)
{
	maplist_line_t *lines;

	lines = (maplist_line_t *) (r->buf);
	win_maplist_str_build(line, size, idx, (void *)lines);
}

/*
 * Initialize the display layout for window type "WIN_TYPE_MAPLIST_PROC".
 */
static void *maplist_dyn_create(page_t * page, win_type_t * type)
{
	dyn_maplist_t *dyn;
	cmd_maplist_t *cmd_lat = CMD_MAPLIST(&page->cmd);
	int i;

	if ((dyn = zalloc(sizeof(dyn_maplist_t))) == NULL) {
		return (NULL);
	}

	if ((i = reg_init(&dyn->msg, 0, 1, g_scr_width, 2, A_BOLD)) < 0)
		goto L_EXIT;
	if ((i =
	     reg_init(&dyn->caption, 0, i, g_scr_width, 2,
		      A_BOLD | A_UNDERLINE)) < 0)
		goto L_EXIT;
	(void)reg_init(&dyn->data, 0, i, g_scr_width, g_scr_height - i - 2, 0);

	reg_buf_init(&dyn->data, NULL, maplist_line_get);
	reg_scroll_init(&dyn->data, B_TRUE);

	dyn->pid = cmd_lat->pid;
	*type = WIN_TYPE_MAPLIST_PROC;

	return (dyn);
L_EXIT:
	free(dyn);
	return (NULL);
}

/*
 * Release the resources for window type: "WIN_TYPE_MAPLIST_PROC"
 */
static void maplist_win_destroy(dyn_win_t * win)
{
	dyn_maplist_t *dyn;

	if ((dyn = win->dyn) != NULL) {
		if (dyn->data.buf != NULL) {
			free(dyn->data.buf);
		}

		reg_win_destroy(&dyn->msg);
		reg_win_destroy(&dyn->caption);
		reg_win_destroy(&dyn->data);
		free(dyn);
	}
}

/*
 * Due to the limitation of screen size, the string of path
 * probablly needs to be cut. For example:
 * /export/home/wrw/usr/src/cmd/damontop
 * probably is cut to:
 * ../usr/src/cmd/damontop
 */
static void bufdesc_cut(char *dst_desc, int dst_size, char *src_desc)
{
	int src_len;
	char *start, *end;

	if ((src_len = strlen(src_desc)) < dst_size) {
		(void)strcpy(dst_desc, src_desc);
		if ((src_len == 0) && (dst_size > 0)) {
			dst_desc[0] = 0;
		}

		return;
	}

	start = src_desc + (src_len - dst_size + 1) + 2;
	end = src_desc + src_len;
	while ((start < end) && (*start != '/')) {
		start++;
	}

	if (start < end) {
		(void)snprintf(dst_desc, dst_size, "..%s", start);
		dst_desc[dst_size - 1] = 0;
	} else {
		dst_desc[0] = 0;
	}
}

/*
 * copyout the maps data to a new buffer.
 */
maplist_line_t *win_maplist_buf_create(track_proc_t * proc, int *nlines)
{
	map_proc_t *map = &proc->map;
	map_entry_t *entry;
	maplist_line_t *buf;
	int i;

	*nlines = map->nentry_cur;
	if ((buf = zalloc(sizeof(maplist_line_t) * (*nlines))) == NULL) {
		return (NULL);
	}

	for (i = 0; i < *nlines; i++) {
		entry = &map->arr[i];
		buf[i].pid = proc->pid;
		buf[i].bufaddr.addr = entry->start_addr;
		buf[i].bufaddr.size = entry->end_addr - entry->start_addr;
		buf[i].nid_show = B_FALSE;
		bufdesc_cut(buf[i].desc, WIN_DESCBUF_SIZE, entry->desc);
	}

	return (buf);
}

/*
 * Get the LL sampling data, check if the record hits one buffer in
 * process address space. If so, update the accessing statistics for
 * this buffer.
 */
void
win_maplist_buf_fill(maplist_line_t * maplist_buf, int nlines,
		track_proc_t *proc)
{
	int i;

	(void)pthread_mutex_lock(&proc->mutex);

	debug_print(NULL, 2, "maplist nonzero: %d\n", proc->nr_nonzero);
	for (i = 0; i < nlines; i++) {
		if (!proc->nr_nonzero)
			break;
		else
			os_maplist_buf_hit(&maplist_buf[i], nlines, NULL, proc, NULL);
	}

	(void)pthread_mutex_unlock(&proc->mutex);

	/* If all record access is zero, clear all maps naccess zero. */
	if (!proc->nr_nonzero) {
		for (i = 0; i < nlines; i++) {
			maplist_buf[i].naccess = 0;
			maplist_buf[i].nsamples = 0;
		}
	}
}

/*
 * The callback function used in qsort() to compare the number of
 * buffer accessing.
 */
int win_maplist_cmp(const void *p1, const void *p2)
{
	const maplist_line_t *l1 = (const maplist_line_t *)p1;
	const maplist_line_t *l2 = (const maplist_line_t *)p2;

	if (l1->naccess < l2->naccess) {
		return (1);
	}

	if (l1->naccess > l2->naccess) {
		return (-1);
	}

	return (0);
}

/*
 * Get and display the process/thread latency related information.
 */
static int
maplist_data_get(track_proc_t * proc, dyn_maplist_t * dyn)
{
	maplist_line_t *maplist_buf;
	int nlines;
	char content[WIN_LINECHAR_MAX];

	reg_erase(&dyn->caption);
	reg_refresh_nout(&dyn->caption);
	reg_erase(&dyn->data);
	reg_refresh_nout(&dyn->data);

	if ((maplist_buf = win_maplist_buf_create(proc, &nlines)) == NULL) {
		debug_print(NULL, 2, "win_maplist_buf_create failed (pid = %d)\n",
			    proc->pid);
		return (-1);
	}

	/*
	 * Fill in the memory access information.
	 */
	win_maplist_buf_fill(maplist_buf, nlines, proc);

	/*
	 * Sort the "maplist_buf" according to the number of buffer accessing.
	 */
	qsort(maplist_buf, nlines, sizeof(maplist_line_t), win_maplist_cmp);

	/*
	 * Display the caption of data table:
	 * "ADDR SIZE ACCESS DESC"
	 */
	(void)snprintf(content, sizeof(content),
		       "%16s%11s%11s%34s",
		       CAPTION_ADDR, CAPTION_SIZE, CAPTION_BUFHIT, CAPTION_DESC);

	reg_line_write(&dyn->caption, 1, ALIGN_LEFT, content);
	dump_write("%s\n", content);
	reg_refresh_nout(&dyn->caption);

	/*
	 * Save data of buffer statistics in scrolling buffer.
	 */
	dyn->data.nlines_total = nlines;
	if (dyn->data.buf != NULL) {
		free(dyn->data.buf);
	}

	/*
	 * Display the buffer with statistics in scrolling buffer
	 */
	dyn->data.buf = (void *)maplist_buf;
	reg_scroll_show(&dyn->data, (void *)(dyn->data.buf),
			nlines, win_maplist_str_build);
	reg_refresh_nout(&dyn->data);

	return 0;
}

boolean_t
win_maplist_data_show(track_proc_t * proc, dyn_maplist_t * dyn, boolean_t * note_out)
{
	win_reg_t *r;
	char content[WIN_LINECHAR_MAX], intval_buf[16], maplist_buf[32];

	*note_out = B_FALSE;

	dump_cache_enable();
	if (maplist_data_get(proc, dyn) < 0) {
		strcpy(maplist_buf, "unknown");
	}
	dump_cache_disable();

	disp_intval(intval_buf, 16);
	(void)snprintf(content, sizeof(content),
			"Monitoring memory areas (pid: %d, interval: %s)",
			proc->pid, intval_buf);

	r = &dyn->msg;
	reg_erase(r);
	reg_line_write(r, 1, ALIGN_LEFT, content);
	dump_write("\n*** %s\n", content);
	reg_refresh_nout(r);

	dump_cache_flush();

	return (B_TRUE);
}

/*
 * The callback function for "WIN_TYPE_MAPLIST_PROC". This would be called
 * when user hits the <UP>/<DOWN> key to scroll data line.
 */
static void maplist_win_scroll(dyn_win_t * win, int scroll_type)
{
	dyn_maplist_t *dyn = (dyn_maplist_t *) (win->dyn);

	reg_line_scroll(&dyn->data, scroll_type);
}

/*
 * The common entry for all warning messages.
 */
void win_warn_msg(warn_type_t warn_type)
{
	dyn_warn_t dyn;
	char content[WIN_LINECHAR_MAX];
	int i;

	if ((i = reg_init(&dyn.msg, 0, 1, g_scr_width, 4, A_BOLD)) < 0)
		return;
	(void)reg_init(&dyn.pad, 0, i, g_scr_width, g_scr_height - i - 2, 0);

	reg_erase(&dyn.pad);
	reg_line_write(&dyn.pad, 0, ALIGN_LEFT, "");
	reg_refresh_nout(&dyn.pad);

	reg_erase(&dyn.msg);
	switch (warn_type) {
	case WARN_PERF_DATA_FAIL:
		(void)strncpy(content, "Perf event counting is failed!",
			      WIN_LINECHAR_MAX);
		break;

	case WARN_INVALID_PID:
		(void)strncpy(content, "Process exists, "
			      "return to home window ...", WIN_LINECHAR_MAX);
		break;

	case WARN_INVALID_LWPID:
		(void)strncpy(content, "Thread exists, "
			      "return to home window ...", WIN_LINECHAR_MAX);
		break;

	case WARN_WAIT:
		(void)strncpy(content, "Please wait ...", WIN_LINECHAR_MAX);
		break;

	case WARN_WAIT_PERF_LL_RESULT:
		(void)strncpy(content, "Retrieving latency data ...",
			      WIN_LINECHAR_MAX);
		break;

	case WARN_NOT_IMPL:
		(void)strncpy(content, "Function is not implemented yet!",
			      WIN_LINECHAR_MAX);
		break;

	case WARN_INVALID_NID:
		(void)strncpy(content, "Invalid node id, node might "
			      "be offlined.", WIN_LINECHAR_MAX);
		break;

	case WARN_INVALID_MAP:
		(void)strncpy(content, "Cannot retrieve process "
			      "address-space mapping.", WIN_LINECHAR_MAX);
		break;

	case WARN_INVALID_NUMAMAP:
		(void)strncpy(content, "Cannot retrieve process "
			      "memory NUMA mapping.", WIN_LINECHAR_MAX);
		break;

	case WARN_LL_NOT_SUPPORT:
		(void)strncpy(content, "Sampling isn't working properly.",
			      WIN_LINECHAR_MAX);
		break;

	case WARN_STOP:
		(void)strncpy(content, "Stopping ...", WIN_LINECHAR_MAX);
		break;

	default:
		content[0] = '\0';
	}

	content[WIN_LINECHAR_MAX - 1] = 0;
	reg_line_write(&dyn.msg, 1, ALIGN_LEFT, content);
	reg_refresh_nout(&dyn.msg);
	reg_update_all();

	reg_win_destroy(&dyn.msg);
	reg_win_destroy(&dyn.pad);
}

/*
 * Each window has same fix regions: the title is at the top of window
 * and the note region is at the bottom of window.
 */
void win_fix_init(void)
{
	(void)reg_init(&s_note_reg, 0, g_scr_height - 1,
		       g_scr_width, 1, A_REVERSE | A_BOLD);
	(void)reg_init(&s_title_reg, 0, 0, g_scr_width, 1, 0);
	reg_update_all();
}

/*
 * Release the resources of fix regions in window.
 */
void win_fix_fini(void)
{
	reg_win_destroy(&s_note_reg);
	reg_win_destroy(&s_title_reg);
}

/*
 * The common entry of window initialization
 */
int win_dyn_init(void *p)
{
	page_t *page = (page_t *) p;
	dyn_win_t *win = &page->dyn_win;
	cmd_id_t cmd_id = CMD_ID(&page->cmd);
	int ret = -1;

	/*
	 * Initialization for the common regions for all windows.
	 */
	win->title = &s_title_reg;
	win->note = &s_note_reg;
	win->page = page;

	/*
	 * Initialization for the private regions according to
	 * different window type.
	 */
	switch (cmd_id) {
	case CMD_HOME_ID:
		if ((win->dyn = topnproc_dyn_create(WIN_TYPE_TOPNPROC)) == NULL) {
			goto L_EXIT;
		}

		win->type = WIN_TYPE_TOPNPROC;
		win->draw = topnproc_win_draw;
		win->scroll = topnproc_win_scroll;
		win->scroll_enter = topnproc_win_scrollenter;
		win->destroy = topnproc_win_destroy;
		break;

	case CMD_MONITOR_ID:
		if ((win->dyn =
		     moni_dyn_create(page, &win->draw, &win->type)) == NULL) {
			goto L_EXIT;
		}

		if (win->type == WIN_TYPE_MONIPROC) {
			win->destroy = moniproc_win_destroy;
			win->scroll = moniproc_win_scroll;
			win->scroll_enter = NULL;
		}
		break;

	case CMD_DAMON_OVERVIEW_ID:
		if ((win->dyn = damon_overview_dyn_create()) == NULL) {
			goto L_EXIT;
		}

		win->type = WIN_TYPE_DAMON_OVERVIEW;
		win->draw = damon_overview_win_draw;
		win->scroll = damon_overview_win_scroll;
		win->scroll_enter = damon_overview_win_scrollenter;
		win->destroy = damon_overview_win_destroy;
		break;

	case CMD_DAMON_DETAIL_ID:
		if ((win->dyn = damon_detail_dyn_create(page)) == NULL) {
			goto L_EXIT;
		}

		win->type = WIN_TYPE_DAMON_DETAIL;
		win->draw = damon_detail_win_draw;
		win->destroy = damon_detail_win_destroy;
		break;

	case CMD_MAP_LIST_ID:
		if ((win->dyn = maplist_dyn_create(page, &win->type)) == NULL) {
			goto L_EXIT;
		}

		win->draw = os_maplist_win_draw;
		win->destroy = maplist_win_destroy;
		win->scroll = maplist_win_scroll;
		win->scroll_enter = NULL;
		break;

	default:
		goto L_EXIT;
	}

	win->inited = B_TRUE;
	ret = 0;

L_EXIT:
	return (ret);
}

/*
 * The common entry of window destroying
 */
void win_dyn_fini(void *p)
{
	page_t *page = (page_t *) p;
	dyn_win_t *win = &page->dyn_win;

	if (win->inited && (win->destroy != NULL)) {
		win->destroy(win);
	}

	win->inited = B_FALSE;
}
