/* This file contains code to process the syscall "perf_event_open" */

#include <inttypes.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <linux/perf_event.h>
#include "./include/types.h"
#include "./include/perf.h"
#include "./include/util.h"
#include "./include/pfwrapper.h"
#include "./include/damon.h"
#include "./include/os/os_perf.h"

static int s_mapsize, s_mapmask, s_ringsize;
int sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CPU
    | PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | PERF_SAMPLE_PERIOD;
int read_format = PERF_FORMAT_ID;

extern perf_damon_event_t *perf_damon_conf;
extern int numa_stat;

static int
pf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd,
	      unsigned long flags)
{
	return (syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags));
}

unsigned long raw2data(unsigned char *data, int size)
{
	int i;
	unsigned char *p = data;
	unsigned long val = 0;
	unsigned long tmp = 0;

	for (i = 0; i < size; i++) {
		tmp = (*(p + i));
		val += (tmp) << (8 * i);
	}

	return val;
}

int mmap_buffer_read(struct perf_event_mmap_page *header,
		void *buf, size_t size)
{
	void *data;
	uint64_t data_head, data_tail;
	int data_size, ncopies;

	/*
	 * The first page is a meta-data page (struct perf_event_mmap_page),
	 * so move to the second page which contains the perf data.
	 */
	data = (void *)header + g_pagesize;

	/*
	 * data_tail points to the position where userspace last read,
	 * data_head points to the position where kernel last add.
	 * After read data_head value, need to issue a rmb().
	 */
	data_tail = header->data_tail;
	data_head = header->data_head;
	rmb();

	/*
	 * The kernel function "perf_output_space()" guarantees no data_head can
	 * wrap over the data_tail.
	 */
	if ((data_size = data_head - data_tail) < (int)size) {
		return (-1);
	}

	data_tail &= s_mapmask;

	/*
	 * Need to consider if data_head is wrapped when copy data.
	 */
	if ((ncopies = (s_ringsize - data_tail)) < (int)size) {
		memcpy(buf, data + data_tail, ncopies);
		memcpy(buf + ncopies, data, size - ncopies);
	} else {
		memcpy(buf, data + data_tail, size);
	}

	header->data_tail += size;
	return 0;
}

void mmap_buffer_skip(struct perf_event_mmap_page *header, int size)
{
	int data_head;

	data_head = header->data_head;
	rmb();

	if ((int)(header->data_tail + size) > data_head) {
		size = data_head - header->data_tail;
	}

	header->data_tail += size;
}

void mmap_buffer_reset(struct perf_event_mmap_page *header)
{
	int data_head;

	data_head = header->data_head;;
	rmb();

	header->data_tail = data_head;
}

int pf_ringsize_init(void)
{
	switch (g_precise) {
	case PRECISE_HIGH:
		s_mapsize = g_pagesize * (PF_MAP_NPAGES_MAX + 1);
		s_mapmask = (g_pagesize * PF_MAP_NPAGES_MAX) - 1;
		s_ringsize = g_pagesize * PF_MAP_NPAGES_MAX;
		break;

	case PRECISE_LOW:
		s_mapsize = g_pagesize * (PF_MAP_NPAGES_MIN + 1);
		s_mapmask = (g_pagesize * PF_MAP_NPAGES_MIN) - 1;
		s_ringsize = g_pagesize * PF_MAP_NPAGES_MIN;
		break;

	default:
		s_mapsize = g_pagesize * (PF_MAP_NPAGES_NORMAL + 1);
		s_mapmask = (g_pagesize * PF_MAP_NPAGES_NORMAL) - 1;
		s_ringsize = g_pagesize * PF_MAP_NPAGES_NORMAL;
		break;
	}

	return (s_mapsize - g_pagesize);
}

int get_perf_ringsize(void)
{
	return s_ringsize;
}

/*
 * Setup perf for each kdamon.
 */
int pf_profiling_setup(int idx, pf_conf_t * conf)
{
	struct perf_event_attr attr;
	int fd, group_fd;
	pid_t pid;		/* This is kdamon.x */

	memset(&attr, 0, sizeof(attr));
	attr.type = conf->type;
	attr.config = conf->config;
	attr.sample_period = 1;
	attr.sample_type = sample_type;
	attr.read_format = read_format;
	attr.sample_id_all = 1;
	attr.watermark = 1;
	attr.size = sizeof(attr);
	attr.disabled = 1;

	debug_print(NULL, 2, "pf_profiling_setup: attr.type = 0x%lx, "
		    "attr.config = 0x%lx\n", attr.type, attr.config);

	if (idx == 1) {
		attr.disabled = 1;
		group_fd = -1;
	}

	pid = get_kdamon_pid();
	if (pid < 0) {
		stderr_print("require kdamon_pid failed!\n");
		return -1;
	}

	group_fd = -1;		/* FIXME: whether more kdamon.x belongs to same group */
	if ((fd = pf_event_open(&attr, pid, -1, group_fd,
					PERF_FLAG_FD_CLOEXEC)) < 0) {
		debug_print(NULL, 2, "pf_profiling_setup: pf_event_open is failed");
		fd = INVALID_FD;
		goto L_FAILED;
	}

	perf_damon_conf->perf_fd = fd;
	if (idx == 1) {
		if ((perf_damon_conf->map_base =
		     mmap(NULL, s_mapsize, PROT_READ | PROT_WRITE, MAP_SHARED,
			  fd, 0)) == MAP_FAILED) {
			close(fd);
			fd = INVALID_FD;
			goto L_FAILED;
		}

		perf_damon_conf->map_len = s_mapsize;
		perf_damon_conf->map_mask = s_mapmask;
	} else {
		stderr_print("error idx: %d\n", idx);
		exit(-1);
	}

	debug_print(NULL, 2, "begin to monitor: %d\n", pid);

	return 0;

L_FAILED:
	perf_damon_conf->perf_fd = INVALID_FD;
	return -1;
}

int pf_profiling_start(void)
{
	int fd = perf_damon_conf->perf_fd;

	if (fd != INVALID_FD) {
		return ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	}

	return 0;
}

int pf_profiling_stop(void)
{
	int fd = perf_damon_conf->perf_fd;

	if (fd != INVALID_FD) {
		return ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
	}

	return 0;
}

/*
 * Parsing data from perf data (binary).
 */
static int profiling_sample_read(struct perf_event_mmap_page *mhdr,
		int size, pf_profiling_rec_t * rec)
{
	struct {
		uint32_t pid, tid;
	} id;
	struct {
		uint32_t cpu, res;
	} cpu_res;
	count_value_t *countval = &rec->countval;
	uint64_t ip, time, period;
	uint32_t data_size;
	unsigned char *data = NULL;
	int  ret = -1;

	/*
	 * struct read_format {
	 *  { u64       ip;}
	 *      { u32   pid, tid; }
	 *      { u64   time; }
	 *      { u32   cpu, res; }
	 *      { u64   period; }
	 *      { u32   size; }
	 *      { char  data[size]; }
	 * };
	 */
	if (mmap_buffer_read(mhdr, &ip, sizeof(ip)) == -1) {
		debug_print(NULL, 2,
			    "profiling_sample_read: read ip failed.\n");
		goto L_EXIT;
	}
	size -= sizeof(ip);

	/* Note: this is kdamon pid here. */
	if (mmap_buffer_read(mhdr, &id, sizeof(id)) == -1) {
		debug_print(NULL, 2,
			    "profiling_sample_read: read pid/tid failed.\n");
		goto L_EXIT;
	}
	size -= sizeof(id);

	if (mmap_buffer_read(mhdr, &time, sizeof(time)) == -1) {
		debug_print(NULL, 2,
			    "profiling_sample_read: read time failed.\n");
		goto L_EXIT;
	}
	size -= sizeof(time);

	if (mmap_buffer_read(mhdr, &cpu_res, sizeof(cpu_res)) == -1) {
		debug_print(NULL, 2,
			    "profiling_sample_read: read cpu_res failed.\n");
		goto L_EXIT;
	}
	size -= sizeof(cpu_res);

	if (mmap_buffer_read(mhdr, &period, sizeof(period)) == -1) {
		debug_print(NULL, 2,
			    "profiling_sample_read: read period failed.\n");
		goto L_EXIT;
	}
	size -= sizeof(period);

	if (mmap_buffer_read(mhdr, &data_size, sizeof(data_size)) == -1) {
		debug_print(NULL, 2,
			    "profiling_sample_read: read data_size failed.\n");
		goto L_EXIT;
	}
	size -= sizeof(data_size);

	data = malloc(data_size);
	if (!data || (mmap_buffer_read(mhdr, data, data_size) == -1)) {
		debug_print(NULL, 2,
			    "profiling_sample_read: malloc or read value failed.\n");
		goto L_EXIT;
	}

	if (data_size >= 40) {
		/*
		 * These variables are useless temporally.
		 *
		 * unsigned short common_type = raw2data(&data[0], 2);
		 * unsigned char common_flags = raw2data(&data[2], 1);
		 * unsigned char common_preempt_count = raw2data(&data[3], 1);
		 * int common_pid = raw2data(&data[4], 4);
		 */
		unsigned long target_id = raw2data(&data[8], 8);
		unsigned int nr_regions = raw2data(&data[16], 4);
		unsigned long start = raw2data(&data[24], 8);
		unsigned long end = raw2data(&data[32], 8);
		unsigned int nr_accesses = raw2data(&data[40], 4);
		unsigned long age = raw2data(&data[44], 4);
		unsigned long local = numa_stat > 0 ? raw2data(&data[48], 8) : 0;
		unsigned long remote = numa_stat > 0 ? raw2data(&data[56], 8) : 0;

		countval->counts[PERF_COUNT_DAMON_NR_REGIONS] = nr_regions;
		countval->counts[PERF_COUNT_DAMON_START] = start;
		countval->counts[PERF_COUNT_DAMON_END] = end;
		countval->counts[PERF_COUNT_DAMON_NR_ACCESS] = nr_accesses;
		countval->counts[PERF_COUNT_DAMON_AGE] = age;
		countval->counts[PERF_COUNT_DAMON_LOCAL] = local;
		countval->counts[PERF_COUNT_DAMON_REMOTE] = remote;
		rec->pid = target_id;
		free(data);
	} else {
		free(data);
		debug_print(NULL, 2,
			    "profiling_sample_read: read raw failed.\n");
		goto L_EXIT;
	}

	size -= data_size;
	rec->tid = id.tid;

	ret = 0;

L_EXIT:
	if (size > 0) {
		mmap_buffer_skip(mhdr, size);
		debug_print(NULL, 2,
			    "profiling_sample_read: skip %d bytes, ret=%d\n",
			    size, ret);
	}

	return ret;
}

static void
profiling_recbuf_update(pf_profiling_rec_t * rec_arr, int *nrec,
			pf_profiling_rec_t * rec)
{
	int i;

	if ((!nrec) || (rec->pid == 0) || (rec->tid == 0)) {
		/* Just consider the user-land process/thread. */
		return;
	}

	/*
	 * The buffer of array is enough, don't need to consider overflow.
	 */
	i = *nrec;
	memcpy(&rec_arr[i], rec, sizeof(pf_profiling_rec_t));
	*nrec += 1;
}

void pf_profiling_record(pf_profiling_rec_t * rec_arr,
		int *nrec)
{
	struct perf_event_mmap_page *mhdr = perf_damon_conf->map_base;
	struct perf_event_header ehdr;
	pf_profiling_rec_t rec;
	int size;

	if (nrec != NULL) {
		*nrec = 0;
	}

	/* update all record from ring buffer */
	for (;;) {
		if (mmap_buffer_read(mhdr, &ehdr, sizeof(ehdr)) == -1) {
			return;
		}

		if ((size = ehdr.size - sizeof(ehdr)) <= 0) {
			mmap_buffer_reset(mhdr);
			return;
		}

		if ((ehdr.type == PERF_RECORD_SAMPLE) && (rec_arr != NULL)) {
			if (profiling_sample_read(mhdr, size, &rec) == 0) {
				profiling_recbuf_update(rec_arr, nrec, &rec);
			} else {
				/* No valid record in ring buffer. */
				return;
			}
		} else {
			mmap_buffer_skip(mhdr, size);
		}
	}
}

void pf_resource_free(void)
{
	int fd = perf_damon_conf->perf_fd;

		if (fd != INVALID_FD) {
			close(fd);
			perf_damon_conf->perf_fd = INVALID_FD;
		}

	if (perf_damon_conf->map_base != MAP_FAILED) {
		munmap(perf_damon_conf->map_base,
				perf_damon_conf->map_len);
		perf_damon_conf->map_base = MAP_FAILED;
		perf_damon_conf->map_len = 0;
	}
}
