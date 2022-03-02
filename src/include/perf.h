#ifndef _DAMONTOP_PERF_H
#define _DAMONTOP_PERF_H

#include <sys/types.h>
#include <inttypes.h>
#include <pthread.h>
#include "types.h"
#include <linux/perf_event.h>
#include "./os/os_perf.h"
#include "plat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	PERF_WAIT_NSEC	60
#define	PERF_INTVAL_MIN_MS	1000

typedef enum {
	PERF_STATUS_IDLE = 0,
	PERF_STATUS_PROFILING_STARTED,
	PERF_STATUS_PROFILING_PART_STARTED,
	PERF_STATUS_PROFILING_MULTI_STARTED,
	PERF_STATUS_PROFILING_FAILED,
	PERF_STATUS_MAPLIST_STARTED,
	PERF_STATUS_ML_FAILED
} perf_status_t;

typedef enum {
	PERF_INVALID_ID = 0,
	PERF_PROFILING_START_ID,
	PERF_PROFILING_PARTPAUSE_ID,
	PERF_PROFILING_MULTIPAUSE_ID,
	PERF_PROFILING_RESTORE_ID,
	PERF_PROFILING_MULTI_RESTORE_ID,
	PERF_PROFILING_SMPL_ID,
	PERF_MAPLIST_START_ID,
	PERF_MAPLIST_SMPL_ID,
	PERF_STOP_ID,
	PERF_QUIT_ID,
} perf_taskid_t;

typedef struct _task_quit {
	perf_taskid_t task_id;
} task_quit_t;

typedef struct _task_allstop {
	perf_taskid_t task_id;
} task_allstop_t;

typedef struct _task_profiling {
	perf_taskid_t task_id;
	boolean_t use_dispflag1;
} task_profiling_t;

typedef struct _task_partpause {
	perf_taskid_t task_id;
	perf_count_id_t perf_count_id;
} task_partpause_t;

typedef struct _task_multipause {
	perf_taskid_t task_id;
	perf_count_id_t *perf_count_ids;
} task_multipause_t;

typedef struct _task_restore {
	perf_taskid_t task_id;
	perf_count_id_t perf_count_id;
} task_restore_t;

typedef struct _task_multi_restore {
	perf_taskid_t task_id;
	perf_count_id_t *perf_count_ids;
} task_multi_restore_t;

typedef struct _task_ml {
	perf_taskid_t task_id;
	pid_t pid;
	int lwpid;
} task_ml_t;

typedef union _perf_task {
	task_quit_t quit;
	task_allstop_t allstop;
	task_profiling_t profiling;
	task_partpause_t partpause;
	task_restore_t restore;
	task_ml_t ll;
} perf_task_t;

#define	TASKID(task_addr) \
	(*(perf_taskid_t *)(task_addr))

#define	TASKID_SET(task_addr, task_id) \
	((*(perf_taskid_t *)(task_addr)) = (task_id))

#define	PERF_PROFILING_STARTED \
	(s_perf_ctl.status == PERF_STATUS_PROFILING_STARTED)

typedef struct _perf_ctl {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_mutex_t status_mutex;
	pthread_cond_t status_cond;
	perf_status_t status;
	pthread_t thr;
	perf_task_t task;
	boolean_t inited;
	uint64_t last_ms;
} perf_ctl_t;

extern int perf_init(void);
extern void perf_fini(void);
extern int perf_allstop(void);
extern boolean_t perf_profiling_started(void);
extern int perf_profiling_start(void);
extern int perf_profiling_smpl(boolean_t);
extern int perf_profiling_partpause(ui_count_id_t);
extern int perf_profiling_restore(ui_count_id_t);
extern boolean_t perf_maplist_is_start(void);
extern int perf_maplist_start(pid_t);
extern int perf_maplist_smpl(pid_t);
extern void perf_status_set(perf_status_t);
extern void perf_status_set_no_signal(perf_status_t);
extern void* perf_priv_alloc(boolean_t *);
extern void perf_priv_free(void *);
extern void perf_task_set(perf_task_t *);
extern int perf_status_wait(perf_status_t);
extern void perf_smpl_wait(void);
extern void perf_maplist_status_set(void);
extern void sys_profiling_config(perf_count_id_t perf_count_id, plat_event_config_t *cfg);
extern void sys_ll_config(plat_event_config_t *cfg);

extern unsigned long raw2data(unsigned char *data, int size);
extern int mmap_buffer_read(struct perf_event_mmap_page *header, void *buf, size_t size);
extern void mmap_buffer_skip(struct perf_event_mmap_page *header, int size);
extern void mmap_buffer_reset(struct perf_event_mmap_page *header);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_PERF_H */
