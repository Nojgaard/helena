#include "comm.h"
#include "config.h"
#include "context.h"
#include "common.h"


#if CFG_DISTRIBUTED == 1
#include "shmem.h"
#include "debug.h"
#endif

#define COMM_CHUNK_SIZE 65000

void * COMM_HEAP;
int COMM_ME;
int COMM_PES;

void init_comm
() {
#if CFG_DISTRIBUTED == 0
  assert(0);
#else
  debug("init_comm started\n");
  shmem_init();
  COMM_ME = shmem_my_pe();
  COMM_PES = shmem_n_pes();
  debug("init_comm done\n");
#endif
}

void finalise_comm
() {
#if CFG_DISTRIBUTED == 0
  assert(0);
#else
  debug("finalise_comm_comm started\n");
  shmem_free(COMM_HEAP);
  shmem_finalize();
  debug("finalise_comm done\n");
#endif
}

int comm_me
() {
#if CFG_DISTRIBUTED == 0
  return 0;
#else
  return COMM_ME;
#endif
}

int comm_pes
() {
#if CFG_DISTRIBUTED == 0
  return 1;
#else
  return COMM_PES;
#endif
}

void * comm_malloc
(size_t heap_size) {
#if CFG_DISTRIBUTED == 0
  return NULL;
#else
  COMM_HEAP = shmem_malloc(heap_size);
  memset(COMM_HEAP, 0, heap_size);
  comm_barrier();
  return COMM_HEAP;
#endif
}

void comm_barrier
() {
#if CFG_DISTRIBUTED == 0
  assert(0);
#else
  lna_timer_t t;

  lna_timer_init(&t);
  lna_timer_start(&t);
  shmem_barrier_all();
  lna_timer_stop(&t);
  context_incr_stat(STAT_TIME_BARRIER, 0, (double) lna_timer_value(t));
#endif
}

void comm_put
(uint32_t pos,
 void * src,
 int size,
 int pe) {
  
#if CFG_DISTRIBUTED == 0
  assert(0);
#else
  debug("put %d bytes at %d to %d\n", size, pos, pe);
  if(pe != COMM_ME) {
    context_incr_stat(STAT_SHMEM_COMMS, 0, 1);
  }
  while(size) {
    if(size < COMM_CHUNK_SIZE) {
      shmem_putmem(COMM_HEAP + pos, src, size, pe);
      size = 0;
    } else {
      shmem_putmem(COMM_HEAP + pos, src, COMM_CHUNK_SIZE, pe);
      size -= COMM_CHUNK_SIZE;
      pos += COMM_CHUNK_SIZE;
      src += COMM_CHUNK_SIZE;
    }
  }
  debug("put done\n");
#endif
}

void comm_get
(void * dst,
 uint32_t pos,
 int size,
 int pe) {
#if CFG_DISTRIBUTED == 0
  assert(0);
#else
  debug("get %d bytes at %d from %d\n", size, pos, pe);
  if(pe != COMM_ME) {
    context_incr_stat(STAT_SHMEM_COMMS, 0, 1);
  }
  while(size) {
    if(size < COMM_CHUNK_SIZE) {
      shmem_getmem(dst, COMM_HEAP + pos, size, pe);
      size = 0;
    } else {
      shmem_getmem(dst, COMM_HEAP + pos, COMM_CHUNK_SIZE, pe);
      size -= COMM_CHUNK_SIZE;
      pos += COMM_CHUNK_SIZE;
      dst += COMM_CHUNK_SIZE;
    }
  }
  debug("get done\n");
#endif
}

void * comm_heap() {
  return COMM_HEAP;
}
