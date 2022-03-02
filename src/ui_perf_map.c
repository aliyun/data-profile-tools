#include <stddef.h>
#include "./include/types.h"
#include "./include/ui_perf_map.h"

ui_perf_count_map_t ui_perf_count_map[UI_COUNT_NUM] = {
	{UI_COUNT_CORE_CLK, 0, {PERF_COUNT_INVALID, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_NR_REGIONS, 1, {PERF_COUNT_DAMON_NR_REGIONS, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_START, 1, {PERF_COUNT_DAMON_START, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_END, 1, {PERF_COUNT_DAMON_END, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_NR_ACCESS, 1, {PERF_COUNT_DAMON_NR_ACCESS, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_AGE, 1, {PERF_COUNT_DAMON_AGE, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_LOCAL, 1, {PERF_COUNT_DAMON_LOCAL, PERF_COUNT_INVALID}},
	{UI_COUNT_DAMON_REMOTE, 1, {PERF_COUNT_DAMON_REMOTE, PERF_COUNT_INVALID}}
};

int get_ui_perf_count_map(ui_count_id_t ui_count_id,
		      perf_count_id_t ** perf_count_ids)
{
	if (ui_count_id == UI_COUNT_INVALID) {
		return PERF_COUNT_INVALID;
	}

	*perf_count_ids = ui_perf_count_map[ui_count_id].perf_count_ids;
	return ui_perf_count_map[ui_count_id].n_perf_count;
}

uint64_t ui_perf_count_aggr(ui_count_id_t ui_count_id, uint64_t * counts)
{
	int i = 0;
	uint64_t tmp = 0;
	int n_perf_count;
	perf_count_id_t *perf_count_ids = NULL;

	n_perf_count = get_ui_perf_count_map(ui_count_id, &perf_count_ids);

	for (i = 0; i < n_perf_count; i++) {
		tmp += counts[perf_count_ids[i]];
	}
	return tmp;
}
