#include "bfs.h"
#include "bfs_queue.h"
#include "report.h"
#include "dbfs_comm.h"
#include "prop.h"

#if defined(CFG_ALGO_BFS) || defined(CFG_ALGO_DBFS) || \
  defined(CFG_ALGO_FRONTIER)

static report_t R;
static storage_t S;
static bfs_queue_t Q;
static pthread_barrier_t B;
static bool_t TERM;

worker_id_t bfs_thread_owner
(hash_key_t h) {
  uint8_t result = 0;
  int i;
  uint16_t move = 0;
  
  for(i = 0; i < sizeof(hash_key_t); i++) {
    result += (h >> (i * 8)) & 0xff;
  }
  return result % CFG_NO_WORKERS;
}

void bfs_wait_barrier
() {
#if defined(CFG_PARALLEL)
  pthread_barrier_wait(&B);
#endif
}

void bfs_terminate_level
(worker_id_t w) {
#if defined(CFG_ALGO_DBFS)
  
  /**
   *  level termination in distributed mode
   *
   *  first send pending states then wait that all threads have done
   *  so.  then wait that other threads have done so.  worker 0
   *  notifies the communicator thread that the level has been
   *  processed.  then all wait that the communicator thread has
   *  exchanged termination information with remote processes.
   *  finally move to the next level.  this is the communicator thread
   *  that tells us whether the search is finished or not.
   */
  dbfs_comm_send_all_pending_states(w);
  bfs_wait_barrier();
  if(0 == w) {
    dbfs_comm_notify_level_termination();
  }
  dbfs_comm_local_barrier();
  bfs_queue_switch_level(Q, w);
  bfs_wait_barrier();
  TERM = dbfs_comm_global_termination();
#else
  
  /**
   *  level termination in non distributed mode
   *
   *  move to the next level of the queue and check if the queue is
   *  empty in order to terminate.  this must be made by all threads
   *  simultaneously.  hence the barriers
   */
  bfs_wait_barrier();
  bfs_queue_switch_level(Q, w);
  bfs_wait_barrier();
  if(0 == w) {
    TERM = (!R->keep_searching || bfs_queue_is_empty(Q)) ? TRUE : FALSE;
  }
  bfs_wait_barrier();
#endif

  /**
   *  with FRONTIER algorithm we delete all states of the previous
   *  level that were marked as garbage by the storage_remove calls
   */
#if defined(CFG_ALGO_FRONTIER)
  hash_tbl_gc_all(S, w);
#endif
}

void * bfs_worker
(void * arg) {
  uint32_t levels = 0;
  char t;
  unsigned int l, en_size;
  state_t s, succ;
  storage_id_t id, id_new;
  event_set_t en;
  worker_id_t x;
  int i, k, no;
  unsigned char * tr;
  worker_id_t w = (worker_id_t) (unsigned long int) arg;
  char heap_name[100];
  bool_t fully_expanded;
  unsigned int arcs;
  heap_t heap;
  hash_key_t h;
  
  sprintf(heap_name, "bfs heap of worker %d", w);
  heap = bounded_heap_new(heap_name, 10 * 1024 * 1024);
  while(!TERM) {
    for(x = 0; x < NO_WORKERS_QUEUE && R->keep_searching; x ++) {
      while(!bfs_queue_slot_is_empty(Q, x, w) && R->keep_searching) {
        
        /**
         *  dequeue a state sent by thread x, get its successors and a
         *  valid stubborn set
         */
        id = bfs_queue_dequeue(Q, x, w);
        heap_reset(heap);
        s = storage_get_mem(S, id, w, heap);
        en = state_enabled_events_mem(s, heap);
        en_size = event_set_size(en);
        if(0 == en_size) {
          R->states_dead[w] ++;
        }
#if defined(CFG_POR)
        en_size = event_set_size(en);
        state_stubborn_set(s, en);
        fully_expanded = (en_size == event_set_size(en)) ? TRUE : FALSE;
#endif

        /**
         *  check the state property
         */
#ifdef CFG_ACTION_CHECK_SAFETY
        if(state_check_property(s, en)) {
          report_faulty_state(R, s);
        }
#endif
    
        /**
         *  expand the current state and put its unprocessed
         *  successors in the queue
         */
      state_expansion:
        arcs = 0;
        en_size = event_set_size(en);
        for(i = 0; i < en_size; i ++) {
          event_t e = event_set_nth(en, i);
          bool_t is_new;

          arcs ++;
          R->events_executed[w] ++;
          event_exec(e, s);
#if defined(CFG_ALGO_DBFS)
          h = state_hash(s);
          if(!dbfs_comm_state_owned(h)) {
            dbfs_comm_process_state(w, s, h);
            event_undo(e, s);
            continue;
          }
          storage_insert_hashed(S, s, w, h, &is_new, &id_new);
#else
          storage_insert(S, s, w, &is_new, &id_new, &h);
#endif

          /*
           *  if new, enqueue the successor after setting its trace and
           *  level
           */
          if(is_new) {
	    storage_set_cyan(S, id_new, w, TRUE);
            bfs_queue_enqueue(Q, id_new, w, bfs_thread_owner(h));
          } else {

            /*
             *  if the successor state is not new it must be in the
             *  queue for the proviso to be satisfied
             */
#if defined(CFG_POR) && defined(CFG_PROVISO)
            if(!fully_expanded && !storage_get_cyan(S, id_new, w)) {
              fully_expanded = TRUE;
              event_undo(e, s);
              event_set_free(en);
              en = state_enabled_events_mem(s, heap);
              goto state_expansion;
            }
#endif
          }
          event_undo(e, s);
        }
        state_free(s);
        event_set_free(en);

        /*
         *  the state leaves the queue
         */
        R->arcs[w] += arcs;
        R->states_visited[w] ++;
        storage_set_cyan(S, id, w, FALSE);
#ifdef CFG_ALGO_FRONTIER
        storage_remove(S, id);
#endif
      }
    }

    /*
     *  all states in the current queue has been processed => initiate
     *  level termination processing
     */
    bfs_terminate_level(w);
    levels ++;
  }
  heap_free(heap);
  report_update_bfs_levels(R, levels);
}

void bfs
(report_t r) {
  state_t s = state_initial();
  bool_t is_new;
  storage_id_t id;
  worker_id_t w;
  void * dummy;
  hash_key_t h;
  bool_t enqueue = TRUE;
  
  R = r;
  S = R->storage;
  Q = bfs_queue_new();

#if defined(CFG_ALGO_DBFS)
  dbfs_comm_start(R, Q);
#endif

  TERM = FALSE;
  pthread_barrier_init(&B, NULL, CFG_NO_WORKERS);
  
#if defined(CFG_ALGO_DBFS)
  h = state_hash(s);
  enqueue = dbfs_comm_state_owned(h);
#endif
  
  if(enqueue) {
    storage_insert(R->storage, s, 0, &is_new, &id, &h);
    w = h % CFG_NO_WORKERS;
    bfs_queue_enqueue(Q, id, w, w);
    bfs_queue_switch_level(Q, w);
  }
  state_free(s);

  /*
   *  start the threads and wait for their termination
   */
  for(w = 0; w < r->no_workers; w ++) {
    pthread_create(&(r->workers[w]), NULL, &bfs_worker, (void *) (long) w);
  }
  for(w = 0; w < r->no_workers; w ++) {
    pthread_join(r->workers[w], &dummy);
  }

  bfs_queue_free(Q);

#if defined(CFG_ALGO_DBFS)
  dbfs_comm_end();
#endif
}

#endif  /*  defined(CFG_ALGO_BFS)  || defined(CFG_ALGO_DBFS) ||
	    defined(CFG_ALGO_FRONTIER)  */
