#ifndef JEVENTS_H
#define JEVENTS_H 1

#include <sys/types.h>
#include <stdbool.h>
#include <glob.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int json_events(const char *fn,
		int (*func)(void *data, char *name, char *event, char *desc,
			    char *pmu),
		void *data);
char *get_cpu_str(void);
char *get_cpu_str_type(char *type, char **idstr_step);

struct perf_event_attr;

struct jevent_extra {
	char *name;			/* output name */
	char *decoded;			/* decoded name */
	bool multi_pmu;			/* needs multiple pmus */
	glob_t pmus;			/* glob_t with all pmus */
	int next_pmu;			/* next pmu number */
};

void jevent_free_extra(struct jevent_extra *extra);
void jevent_copy_extra(struct jevent_extra *dst, struct jevent_extra *src);
int jevent_next_pmu(struct jevent_extra *extra, struct perf_event_attr *attr);
int jevent_name_to_attr(const char *str, struct perf_event_attr *attr);
int jevent_name_to_attr_extra(const char *str, struct perf_event_attr *attr,
			      struct jevent_extra *extra);
int resolve_event(const char *name, struct perf_event_attr *attr);
int resolve_event_extra(const char *name, struct perf_event_attr *attr,
			struct jevent_extra *extra);
int read_events(const char *fn);
int jevents_update_qual(const char *qual, struct perf_event_attr *attr,
			const char *str);
int walk_events(int (*func)(void *data, char *name, char *event, char *desc),
		                void *data);
int walk_perf_events(int (*func)(void *data, char *name, char *event, char *desc),
		     void *data);
char *format_raw_event(struct perf_event_attr *attr, char *name);
int rmap_event(unsigned event, char **name, char **desc);

int perf_event_open(struct perf_event_attr *attr, pid_t pid,
		    int cpu, int group_fd, unsigned long flags);
char *resolve_pmu(int type);
bool jevent_pmu_uncore(const char *str);
int jevents_socket_cpus(int *lenp, int **socket_cpus);
void jevent_print_attr(FILE *f, struct perf_event_attr *attr);

enum jevents_error {
    JEV_GENERIC_ERROR = -1,
    JEV_NO_PMU_EVENTS_FILE = -2,

};

/*
 * Returns a string describing the given error_code. Any code in the jevents_error
 * enum is supported, in addition to 0, which returns "Success". Any other code
 * returns "Unknown error".
 */
const char* jevent_error_to_string(int error_code);

/*
 * When a function returns an error code, this function may return additional details.
 */
const char* jevent_get_error_details();
#ifdef __cplusplus
}
#endif

#endif
