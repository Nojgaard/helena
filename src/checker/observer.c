#include "config.h"
#include "observer.h"
#include "context.h"
#include "bfs.h"
#include "delta_ddd.h"
#include "dfs.h"
#include "rwalk.h"

void * observer_worker
(void * arg) {
  float time = 0;
  struct timeval now;
  float mem, cpu, cpu_avg = 0;
  double processed;
  double stored;
  char name[100], pref[100];
  int n = 0;
 
  pref[0] = 0;  
  if(CFG_WITH_OBSERVER) {
    if(CFG_DISTRIBUTED) {
      gethostname(name, 1024);
      sprintf(pref, "[%s:%d] ", name, getpid());
    }
    printf("%sRunning...\n", pref);
  }
  while(context_keep_searching()) {
    n ++;
    sleep(1);
    gettimeofday(&now, NULL);
    mem = mem_usage();
    cpu = context_cpu_usage();
    if(context_keep_searching()) {
      cpu_avg = (cpu + (n - 1) * cpu_avg) / n;
    }
    processed = context_get_stat(STAT_STATES_PROCESSED);
    stored = context_get_stat(STAT_STATES_STORED);
    context_set_stat(STAT_MAX_MEM_USED, 0, mem);
    time = ((float) duration(context_start_time(), now)) / 1000000.0;
    if(CFG_WITH_OBSERVER) {
      printf("\n%sTime elapsed    :   %8.2f s.\n", pref, time);
      printf("%sStates stored   :%'11llu\n", pref, (uint64_t) stored);
      printf("%sStates processed:%'11llu\n", pref, (uint64_t) processed);
      printf("%sMemory usage    :   %8.1f MB.\n", pref, mem);
      printf("%sCPU usage       :   %8.2f %c\n", pref, cpu, '%');
    }
    
    /*
     *  check for limits
     */
    if(CFG_MEMORY_LIMITED && mem > CFG_MAX_MEMORY) {
      context_set_termination_state(MEMORY_EXHAUSTED);
    }
    if(CFG_TIME_LIMITED && time > (float) CFG_MAX_TIME) {
      context_set_termination_state(TIME_ELAPSED);
    }
    if(CFG_STATE_LIMITED && processed > CFG_MAX_STATE) {
      context_set_termination_state(STATE_LIMIT_REACHED);
    }
  }
  if(cpu_avg != 0) {
    context_set_stat(STAT_AVG_CPU_USAGE, 0, cpu_avg);
  }

  if(CFG_WITH_OBSERVER) {
    printf("\n%sdone.\n", pref);
  }
  return NULL;
}
