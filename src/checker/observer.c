#include "config.h"
#include "observer.h"
#include "context.h"
#include "storage.h"

void * observer_worker
(void * arg) {
  float time = 0;
  struct timeval now;
  float mem, cpu, cpu_avg = 0;
  uint64_t processed;
  uint64_t stored;
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
    stored = storage_size(context_storage());
    mem = mem_usage();
    cpu = context_cpu_usage();
    if(context_keep_searching()) {
      cpu_avg = (cpu + (n - 1) * cpu_avg) / n;
    }
    processed = context_processed();
    context_update_max_states_stored(stored);
    context_update_max_mem_used(mem);
    time = ((float) duration(context_start_time(), now)) / 1000000.0;
    if(CFG_WITH_OBSERVER) {
      printf("\n%sTime elapsed    :   %8.2f s.\n", pref, time);
      printf("%sStates stored   :%'11llu\n", pref, stored);
      printf("%sStates processed:%'11llu\n", pref, processed);
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
    context_set_avg_cpu_usage(cpu_avg);
  }
  if(CFG_WITH_OBSERVER) {
    printf("\n%sdone.\n", pref);
  }
  return NULL;
}
