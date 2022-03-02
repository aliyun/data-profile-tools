#define _GNU_SOURCE
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <sys/wait.h>
#include "../include/types.h"
#include "../include/util.h"
#include "../include/os/os_util.h"

uint64_t g_clkofsec;
double g_nsofclk;

boolean_t os_authorized(void)
{
	return (B_TRUE);
}

int os_damontop_lock(boolean_t * locked __attribute__ ((unused)))
{
	/* Not supported on Linux */
	return (0);
}

void os_damontop_unlock(void)
{
	/* Not supported on Linux */
}

int
os_procfs_psinfo_get(pid_t pid __attribute__ ((unused)),
		     void *info __attribute__ ((unused)))
{
	/* Not supported on Linux */
	return (0);
}

/*
 * Retrieve the process's executable name from '/proc'
 */
int os_procfs_pname_get(pid_t pid, char *buf, int size)
{
	char pname[PATH_MAX];
	int procfd;		/* file descriptor for /proc/nnnnn/comm */
	int len;

	snprintf(pname, sizeof(pname), "/proc/%d/comm", pid);
	if ((procfd = open(pname, O_RDONLY)) < 0) {
		return (-1);
	}

	if ((len = read(procfd, buf, size)) < 0) {
		(void)close(procfd);
		return (-1);
	}

	buf[len - 1] = 0;
	(void)close(procfd);
	return (0);
}

/*
 * Retrieve the lwpid in process from '/proc'.
 */
int os_procfs_lwp_enum(pid_t pid, int **lwps, int *num)
{
	char path[PATH_MAX];

	(void)snprintf(path, sizeof(path), "/proc/%d/task", pid);
	return (procfs_enum_id(path, lwps, num));
}

/*
 * Check if the specified pid/lwpid can be found in '/proc'.
 */
boolean_t
os_procfs_lwp_valid(pid_t pid __attribute__ ((unused)),
		    int lwpid __attribute__ ((unused)))
{
	/* Not supported on Linux */
	return (B_TRUE);
}

/*
 * Bind current thread to a cpu or unbind current thread
 * from a cpu.
 */
int processor_bind(int cpu)
{
	cpu_set_t cs;

	CPU_ZERO(&cs);
	CPU_SET(cpu, &cs);

	if (sched_setaffinity(0, sizeof(cs), &cs) < 0) {
		debug_print(NULL, 2, "Fail to bind to CPU%d\n", cpu);
		return (-1);
	}

	return (0);
}

int processor_unbind(void)
{
	cpu_set_t cs;
	int i;

	CPU_ZERO(&cs);
	for (i = 0; i < g_ncpus; i++) {
		CPU_SET(i, &cs);
	}

	if (sched_setaffinity(0, sizeof(cs), &cs) < 0) {
		debug_print(NULL, 2, "Fail to unbind from CPU\n");
		return (-1);
	}

	return (0);
}

static int calibrate_cpuinfo(double *nsofclk, uint64_t * clkofsec)
{
	char unit[11] = { 0 };
	double freq = 0.0;

	if (arch__cpuinfo_freq(&freq, unit)) {
		return -1;
	}

	if (fabsl(freq) < 1.0E-6) {
		return (-1);
	}

	*clkofsec = freq;
	*nsofclk = (double)NS_SEC / *clkofsec;

	debug_print(NULL, 2, "calibrate_cpuinfo: nsofclk = %.4f, "
		    "clkofsec = %lu\n", *nsofclk, *clkofsec);

	return (0);
}

/*
 * On all recent Intel CPUs, the TSC frequency is always
 * the highest p-state. So get that frequency from sysfs.
 * e.g. 2262000
 */
static int calibrate_cpufreq(double *nsofclk, uint64_t * clkofsec)
{
	int fd, i;
	char buf[32];
	uint64_t freq;

	if ((fd = open(CPU0_CPUFREQ_PATH, O_RDONLY)) < 0) {
		return (-1);
	}

	if ((i = read(fd, buf, sizeof(buf) - 1)) <= 0) {
		close(fd);
		return (-1);
	}

	close(fd);
	buf[i] = 0;
	if ((freq = atoll(buf)) == 0) {
		return (-1);
	}

	*clkofsec = freq * KHZ;
	*nsofclk = (double)NS_SEC / *clkofsec;

	debug_print(NULL, 2, "calibrate_cpufreq: nsofclk = %.4f, "
		    "clkofsec = %lu\n", *nsofclk, *clkofsec);

	return (0);
}

/*
 * Measure how many TSC cycles in a second and how many
 * nanoseconds in a TSC cycle.
 */
static void calibrate_by_tsc(double *nsofclk, uint64_t * clkofsec)
{
	uint64_t start_ms, end_ms, diff_ms;
	uint64_t start_tsc, end_tsc;
	int i;

	for (i = 0; i < g_ncpus; i++) {
		/*
		 * Bind current thread to cpuN to ensure the
		 * thread can not be migrated to another cpu
		 * while the rdtsc runs.
		 */
		if (processor_bind(i) == 0) {
			break;
		}
	}

	if (i == g_ncpus) {
		return;
	}

	/*
	 * Make sure the start_ms is at the beginning of
	 * one millisecond.
	 */
	end_ms = current_ms(&g_tvbase);
	while ((start_ms = current_ms(&g_tvbase)) == end_ms) {
	}

	start_tsc = rdtsc();
	while ((end_ms = current_ms(&g_tvbase)) < (start_ms + 100)) {
	}
	end_tsc = rdtsc();

	diff_ms = end_ms - start_ms;
	*nsofclk = (double)(diff_ms * NS_MS) / (double)(end_tsc - start_tsc);

	*clkofsec = (uint64_t) ((double)NS_SEC / *nsofclk);

	/*
	 * Unbind current thread from cpu once the measurement completed.
	 */
	processor_unbind();

	debug_print(NULL, 2, "calibrate_by_tsc: nsofclk = %.4f, "
		    "clkofsec = %lu\n", *nsofclk, *clkofsec);
}

/*
 * calibrate_by_tsc() is the last method used by os_calibrate()
 * to calculate cpu frequency if cpu freq is not available by both
 * procfs and sysfs.
 *
 * On intel, calibrate_by_tsc() uses TSC register which gets updated
 * in sync of processor clock and thus cpu freq can be calculated
 * programmatically using this register.
 *
 * OTOH, PowerPC does not have analogue to TSC. There is a register
 * called TB (Time Base) but it's get updated at constant freq and
 * thus we can't find cpu frequency using TB register. But for
 * powerpc, cpu frequency is always gets exposed via either procfs
 * or sysfs and thus there is no point for depending on any other
 * method for powerpc.
 */
void os_calibrate(double *nsofclk, uint64_t * clkofsec)
{
	if (calibrate_cpuinfo(nsofclk, clkofsec) == 0) {
		return;
	}

	if (calibrate_cpufreq(nsofclk, clkofsec) == 0) {
		return;
	}

	calibrate_by_tsc(nsofclk, clkofsec);
}

static boolean_t int_get(char *str, int *digit)
{
	char *end;
	long val;

	/* Distinguish success/failure after strtol */
	errno = 0;
	val = strtol(str, &end, 10);
	if (((errno == ERANGE) && ((val == LONG_MAX) || (val == LONG_MIN))) ||
	    ((errno != 0) && (val == 0))) {
		return (B_FALSE);
	}

	if (end == str) {
		return (B_FALSE);
	}

	*digit = val;
	return (B_TRUE);
}

/*
 * The function is only called for processing small digits.
 * For example, if the string is "0-9", extract the digit 0 and 9.
 */
static boolean_t hyphen_int_extract(char *str, int *start, int *end)
{
	char tmp[DIGIT_LEN_MAX];

	if (strlen(str) >= DIGIT_LEN_MAX) {
		return (B_FALSE);
	}

	if (sscanf(str, "%511[^-]", tmp) <= 0) {
		return (B_FALSE);
	}

	if (!int_get(tmp, start)) {
		return (B_FALSE);
	}

	if (sscanf(str, "%*[^-]-%511s", tmp) <= 0) {
		return (B_FALSE);
	}

	if (!int_get(tmp, end)) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
arrary_add(int *arr, int arr_size, int index, int value, int num)
{
	int i;

	if ((index >= arr_size) || ((index + num) > arr_size)) {
		return (B_FALSE);
	}

	for (i = 0; i < num; i++) {
		arr[index + i] = value + i;
	}

	return (B_TRUE);
}

/*
 * Extract the digits from string. For example:
 * "1-2,5-7"	return 1 2 5 6 7 in "arr".
 */
static boolean_t str_int_extract(char *str, int *arr, int arr_size, int *num)
{
	char *p, *cur, *scopy;
	int start, end, total = 0;
	int len = strlen(str);
	boolean_t ret = B_FALSE;

	if ((scopy = malloc(len + 1)) == NULL) {
		return (B_FALSE);
	}

	strncpy(scopy, str, len);
	scopy[len] = 0;
	cur = scopy;

	while (cur < (scopy + len)) {
		if ((p = strchr(cur, ',')) != NULL) {
			*p = 0;
		}

		if (strchr(cur, '-') != NULL) {
			if (hyphen_int_extract(cur, &start, &end)) {
				if (arrary_add
				    (arr, arr_size, total, start,
				     end - start + 1)) {
					total += end - start + 1;
				} else {
					goto L_EXIT;
				}
			}
		} else {
			if (int_get(cur, &start)) {
				if (arrary_add(arr, arr_size, total, start, 1)) {
					total++;
				} else {
					goto L_EXIT;
				}
			}
		}

		if (p != NULL) {
			cur = p + 1;
		} else {
			break;
		}
	}

	*num = total;
	ret = B_TRUE;

L_EXIT:
	free(scopy);
	return (ret);
}

static boolean_t file_int_extract(char *path, int *arr, int arr_size, int *num)
{
	FILE *fp;
	char buf[LINE_SIZE];

	if ((fp = fopen(path, "r")) == NULL) {
		return (B_FALSE);
	}

	if (fgets(buf, LINE_SIZE, fp) == NULL) {
		fclose(fp);
		return (B_FALSE);
	}

	fclose(fp);
	return (str_int_extract(buf, arr, arr_size, num));
}

boolean_t os_sysfs_cpu_enum(int nid, int *cpu_arr, int arr_size, int *num)
{
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "%s/node%d/cpulist", NODE_INFO_ROOT, nid);
	return (file_int_extract(path, cpu_arr, arr_size, num));
}

int os_sysfs_online_ncpus(void)
{
	int cpu_arr[NCPUS_MAX], num;
	char path[PATH_MAX];

	if (sysconf(_SC_NPROCESSORS_CONF) > NCPUS_MAX) {
		return (-1);
	}

	snprintf(path, PATH_MAX, "/sys/devices/system/cpu/online");
	if (!file_int_extract(path, cpu_arr, NCPUS_MAX, &num)) {
		return (-1);
	}

	return (num);
}

static boolean_t execute_command(const char *command, const char *type)
{
	FILE *fp;

	fp = popen(command, type);
	if (fp == NULL) {
		debug_print(NULL, 2, "Execute '%s' failed (errno = %d)\n",
			    command, errno);
		return B_FALSE;
	}

	pclose(fp);
	debug_print(NULL, 2, "Execute '%s' ok\n", command);

	return B_TRUE;
}
