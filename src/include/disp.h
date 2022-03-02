#ifndef _DAMONTOP_DISP_H
#define	_DAMONTOP_DISP_H

#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>
#include <pthread.h>
#include "types.h"
#include "util.h"
#include "cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISP_MIN_INTVAL 1
#define DISP_DEFAULT_INTVAL 5
#define PIPE_CHAR_QUIT 'q'
#define PIPE_CHAR_RESIZE 'r'

extern int g_disp_intval;

typedef enum {
	DISP_FLAG_NONE = 0,
	DISP_FLAG_QUIT,
	DISP_FLAG_PROFILING_DATA_READY,
	DISP_FLAG_PROFILING_DATA_FAIL,
	DISP_FLAG_ML_DATA_READY,
	DISP_FLAG_ML_DATA_FAIL,
	DISP_FLAG_CMD,
	DISP_FLAG_SCROLLUP,
	DISP_FLAG_SCROLLDOWN,
	DISP_FLAG_SCROLLENTER
} disp_flag_t;

typedef struct _disp_ctl {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_mutex_t mutex2;
	pthread_cond_t cond2;
	pthread_t thr;
	boolean_t inited;
	cmd_t cmd;
	disp_flag_t flag;
	disp_flag_t flag2;
	int intval_ms;
} disp_ctl_t;

typedef struct _cons_ctl {
	fd_set fds;
	pthread_t thr;
	int pipe[2];
	boolean_t inited;
} cons_ctl_t;

extern int g_run_secs;

extern int disp_init(void);
extern void disp_fini(void);
extern int disp_cons_ctl_init(void);
extern void disp_cons_ctl_fini(void);
extern void disp_consthr_quit(void);
extern void disp_profiling_data_ready(int);
extern void disp_profiling_data_fail(void);
extern void disp_maplist_data_ready(int);
extern void disp_maplist_data_fail(void);
extern void disp_on_resize(int);
extern void disp_intval(char *, int);
extern void disp_dispthr_quit_wait(void);
extern void disp_dispthr_quit_start(void);
extern void disp_go_home(void);
extern void disp_flag2_set(disp_flag_t);
extern disp_flag_t disp_flag2_wait(void);

#ifdef __cplusplus
}
#endif

#endif /* _DAMONTOP_DISP_H */
