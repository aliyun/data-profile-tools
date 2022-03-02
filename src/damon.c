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

/* This file contains code to handle the node. */

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include "./include/types.h"
#include "./include/util.h"
#include "./include/proc.h"
#include "./include/ui_perf_map.h"
#include "./include/pfwrapper.h"
#include "./include/os/os_util.h"
#include "./include/damon.h"

const char *damon_kdamon_pid = "/sys/kernel/debug/damon/kdamond_pid";
static kdamon_group_t s_kdamon_group;
int g_ncpus;

int online_ncpu_refresh(void)
{
	/* Refresh the number of online CPUs */
	g_ncpus = os_sysfs_online_ncpus();
	return 0;
}

void read_damon_attrs(const char *attrs, uint64_t *sample, uint64_t *aggr,
		uint64_t *regi, uint64_t *min, uint64_t *max)
{
	int fd;
	char data[32];
	char *token;
	int ret;

	if ((fd = open(attrs, O_RDONLY)) < 0) {
		stderr_print("%s: No such file!\n", attrs);
		return;
	}

	memset(data, 0, 32);
	ret = read(fd, data, 32);
	if (ret < 0) {
		stderr_print("%s failed!\n", __func__);
		return;
	}
	close(fd);

	token = strtok(data, " ");
	*sample = atol(token);
	if (token != NULL) {
		token = strtok(NULL, " ");
		if (token == NULL)
			goto fail;
		*aggr = atol(token);

		token = strtok(NULL, " ");
		*regi = atol(token);

		token = strtok(NULL, " ");
		*min = atol(token);

		token = strtok(NULL, " ");
		*max = atol(token);
		return;
	}

fail:
	stderr_print("%s: failed!", __func__);
}

void write_damon_attrs(uint64_t sample, uint64_t aggr,
		uint64_t regi, uint64_t min, uint64_t max)
{
	char cmd[100] = {0};
	char *attr = "/sys/kernel/debug/damon/attrs";

	sprintf(cmd, "echo %ld %ld %ld %ld %ld > %s",
			sample, aggr, regi, min, max,
			attr);
	system(cmd);
}

void kdamon_refresh(void)
{
	char *nkdamons_cmd =
		"ps -e|grep kdamon|wc|awk '{print $1}'";
	char kdamon_pid_cmd[64] = {0};
	unsigned long kdamon_pid;
	int i;
	uint64_t sampling_intval, aggr_intval, regions_update, min, max;
	int nr_kdamons = (int)exec_cmd_return_ulong(nkdamons_cmd, 10);

	read_damon_attrs("/sys/kernel/debug/damon/attrs", &sampling_intval,
			&aggr_intval, &regions_update, &min, &max);
	s_kdamon_group.nkdamons = nr_kdamons;
	for (i=1; i<=nr_kdamons; i++) {
		sprintf(kdamon_pid_cmd, "ps -e | grep kdamon | awk 'NR==%d {print $1}'", i);
		kdamon_pid = exec_cmd_return_ulong(kdamon_pid_cmd, 10);
		if (kdamon_pid <= 0) {
			stderr_print("kdamonn pid initial failed.");
			break;
		}

		s_kdamon_group.kdamons[i - 1].pid = kdamon_pid;
		s_kdamon_group.kdamons[i - 1].sampling_intval = sampling_intval;
		s_kdamon_group.kdamons[i - 1].aggregation_intval = aggr_intval;
		s_kdamon_group.kdamons[i - 1].regions_update_intval = regions_update;
	}
}

int get_kdamon_pid(void)
{
	int fd, ret;
	char data[32];
	char *endptr;
	pid_t pid;

	if ((fd = open(damon_kdamon_pid, O_RDONLY)) < 0) {
		stderr_print("kdamon_pid: No such file!\n");
		return -1;
	}

	ret = read(fd, data, 32);
	if (ret < 0) {
		stderr_print("kdamon_pid!\n");
		return -1;
	}
	close(fd);

	pid = strtol(data, &endptr, 10);

	return pid;
}

unsigned int get_nr_kdamon(void)
{
	return 1;
}

kdamon_t *kdamon_get(int kid_idx)
{
	return (&s_kdamon_group.kdamons[kid_idx]);
}

uint64_t get_max_countval(count_value_t * countval_arr,
		ui_count_id_t ui_count_id)
{
	uint64_t value = 0;
	uint64_t max = 0;
	int i;

	for (i = 0; i < PROC_RECORD_MAX; i++) {
		value = ui_perf_count_aggr(ui_count_id, countval_arr[i].counts);
		if (max < value)
			max = value;
	}

	return max;
}
