#include "bfs.h"
#include "bfs_queue.h"
#include "config.h"
#include "context.h"
#include "dbfs_comm.h"
#include "prop.h"
#include "reduction.h"
#include "workers.h"

#if CFG_ALGO_BFS == 0 && CFG_ALGO_DBFS == 0 && CFG_ALGO_FRONTIER == 0

void bfs() {}

#else

struct timespec BFS_WAIT_TIME[CFG_NO_WORKERS];

storage_t S;
bfs_queue_t Q;
pthread_barrier_t BFS_BARRIER;


worker_id_t bfs_thread_owner
(hash_key_t h) {
  uint8_t result = 0;
  int i;
  
  for(i = 0; i < sizeof(hash_key_t); i++) {
    result += (h >> (i * 8)) & 0xff;
  }
  return result % CFG_NO_WORKERS;
}

void bfs_wait_barrier
() {
  if(CFG_PARALLEL) {
    context_barrier_wait(&BFS_BARRIER);
  }
}

void bfs_init_queue
() {
  bool_t levels = CFG_ALGO_DBFS ? 1 : 2;
  bool_t events_in_queue = CFG_EDGE_LEAN;
  uint16_t no_workers =
    CFG_NO_WORKERS + (CFG_ALGO_DBFS ? CFG_NO_COMM_WORKERS : 0);
  bool_t states_in_queue = CFG_HASH_COMPACTION;
  
  Q = bfs_queue_new(no_workers, CFG_BFS_QUEUE_BLOCK_SIZE,
                    states_in_queue, events_in_queue, levels);
}

bool_t bfs_check_termination
(worker_id_t w) {
  bool_t result = FALSE;
  uint16_t trials = 0;
  
  if(CFG_ALGO_DBFS) {
    dbfs_comm_send_all_pending_states(w);
    while(!(result = dbfs_comm_termination())
          && bfs_queue_local_is_empty(Q, w)) {
      context_sleep(BFS_WAIT_TIME[w]);
      trials ++;
    }
    if(trials == 1) {
      BFS_WAIT_TIME[w].tv_nsec /= 2;
    }    
  } else {
    bfs_wait_barrier();
    bfs_queue_switch_level(Q, w);
    bfs_wait_barrier();
    result = !context_keep_searching() || bfs_queue_is_empty(Q);
    bfs_wait_barrier();
  }
  return result;
}

void bfs_report_trace
(storage_id_t id) {
  list_t trace = list_new(SYSTEM_HEAP, sizeof(event_t), event_free_void);
  list_t trace_id = storage_get_trace(S, id);
  list_iter_t it;
  state_t s = state_initial();
  event_t e;
  
  for(it = list_get_iter(trace_id);
      !list_iter_at_end(it);
      it = list_iter_next(it)) {
    e = state_event(s, * ((event_id_t *) list_iter_item(it)));
    event_exec(e, s);
    list_append(trace, &e);
  }
  state_free(s);
  list_free(trace_id);
  context_set_trace(trace);
}

#if CFG_EVENT_UNDOABLE == 1
#define bfs_back_to_s() {event_undo(e, s);}
#else
#define bfs_back_to_s() {state_free(succ);}
#endif

void * bfs_worker
(void * arg) {
  const worker_id_t w = (worker_id_t) (unsigned long int) arg;
  const bool_t states_in_queue = bfs_queue_states_stored(Q);
  const bool_t por = CFG_POR;
  const bool_t proviso = CFG_PROVISO;
  const bool_t edge_lean = CFG_EDGE_LEAN;
  const bool_t with_trace = CFG_ACTION_CHECK_SAFETY && CFG_ALGO_BFS;
  uint32_t levels = 0;
  state_t s, succ;
  storage_id_t id_succ;
  event_list_t en;
  worker_id_t x, y;
  unsigned int arcs;
  heap_t heap = local_heap_new();
  hash_key_t h;
  bfs_queue_item_t item, succ_item;
  event_t e;
  bool_t is_new, reduced, termination = FALSE;
  
  while(!termination) {
    for(x = 0; x < bfs_queue_no_workers(Q); x ++) {
      while(!bfs_queue_slot_is_empty(Q, x, w)) {
 
        /**
         *  get the next state sent by thread x, get its successors
         *  and a valid reduced set.  if the states are not stored in
         *  the queue we get it from the storage
         */
        item = bfs_queue_next(Q, x, w);
        heap_reset(heap);
        if(states_in_queue) {
          s = item.s;
        } else {
          s = storage_get_mem(S, item.id, w, heap);
        }

        /**
         *  compute enabled events and apply POR
         */
        if(por) {
          en = state_events_reduced_mem(s, &reduced, heap);
        } else {
          en = state_events_mem(s, heap);
        }


        /**
         *  check the state property
         */
#if CFG_ACTION_CHECK_SAFETY == 1
        if(state_check_property(s, en)) {
          if(with_trace) {
            bfs_report_trace(item.id);
          } else {
            context_faulty_state(s);
          }
        }
#endif
        
        /**
         *  apply edge lean reduction after checking state property
         *  (EDGE-LEAN may remove all enabled events)
         */
        if(edge_lean && item.e_set) {
          edge_lean_reduction(en, item.e);
        }

        /**
         *  expand the current state and put its unprocessed
         *  successors in the queue
         */
      state_expansion:
        arcs = 0;
        while(!list_is_empty(en)) {
          list_pick_first(en, &e);
          arcs ++;
          if(CFG_EVENT_UNDOABLE) {
            event_exec(e, s);
            succ = s;
          } else {
            succ = state_succ_mem(s, e, heap);
          }
          if(!CFG_ALGO_DBFS) {
            storage_insert(S, succ, w, &is_new, &id_succ, &h);
          } else {
            h = state_hash(succ);
            if(!dbfs_comm_state_owned(h)) {
              dbfs_comm_process_state(w, succ, h);
              bfs_back_to_s();
              continue;
            }
            storage_insert_hashed(S, succ, w, h, &is_new, &id_succ);
          }

          /**
           *  if new, enqueue the successor
           */
          if(is_new) {
            y = bfs_thread_owner(h);
            storage_set_cyan(S, id_succ, y, TRUE);
            succ_item.id = id_succ;
            succ_item.s = succ;
            succ_item.e_set = TRUE;
            succ_item.e = e;
            bfs_queue_enqueue(Q, succ_item, w, y);
            if(with_trace) {
              storage_set_pred(S, succ_item.id, item.id, event_id(e));
            }
          } else {

            /**
             *  if the successor state is not new and if the current
             *  state is reduced then the successor must be in the
             *  queue (i.e., cyan for some worker)
             */
            if(por && proviso && reduced &&
               !storage_get_any_cyan(S, id_succ)) {
              reduced = FALSE;
              list_free(en);
              bfs_back_to_s();
              en = state_events_mem(s, heap);
              goto state_expansion;
            }
          }
          bfs_back_to_s();
        }
        state_free(s);
        list_free(en);

        /**
         *  update some statistics
         */
        if(0 == arcs) {
          context_incr_dead(w, 1);
        }
        context_incr_arcs(w, arcs);
        context_incr_processed(w, 1);
        context_incr_evts_exec(w, arcs);

        /**
         *  the state leaves the queue => we unset its cyan bit and
         *  delete it from storage if algo is FRONTIER.
         */
        bfs_queue_dequeue(Q, x, w);
        storage_set_cyan(S, item.id, w, FALSE);
        if(CFG_ALGO_FRONTIER) {
          storage_remove(S, w, item.id);
        }
      }
    }
    
    /**
     *  with FRONTIER algorithm we delete all states of the previous
     *  level that were marked as garbage by the storage_remove calls
     */
    if(CFG_ALGO_FRONTIER) {
      storage_gc_all(S, w);
    }

    /**
     *  all states in the queue have been processed => check for termination
     */
    termination = bfs_check_termination(w);
    levels ++;
  }
  heap_free(heap);
  context_update_bfs_levels(levels);
}

void bfs
() {
  const bool_t with_trace = CFG_ACTION_CHECK_SAFETY && CFG_ALGO_BFS;
  state_t s = state_initial();
  bool_t is_new;
  storage_id_t id;
  worker_id_t w;
  hash_key_t h;
  bool_t enqueue = TRUE;
  bfs_queue_item_t item;
  
  S = context_storage();
  bfs_init_queue();
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    BFS_WAIT_TIME[w].tv_sec = 0;
    BFS_WAIT_TIME[w].tv_nsec = 1000;
  }

  if(CFG_ALGO_DBFS) {
    dbfs_comm_start(Q);
  }

  pthread_barrier_init(&BFS_BARRIER, NULL, CFG_NO_WORKERS);
  
  if(CFG_ALGO_DBFS) {
    h = state_hash(s);
    enqueue = dbfs_comm_state_owned(h);
  }
  
  if(enqueue) {
    storage_insert(S, s, 0, &is_new, &id, &h);
    w = h % CFG_NO_WORKERS;
    item.id = id;
    item.s = s;
    item.e_set = FALSE;
    if(with_trace) {
      storage_set_pred(S, id, id, 0);
    }
    bfs_queue_enqueue(Q, item, w, w);
    bfs_queue_switch_level(Q, w);
  }
  state_free(s);

  launch_and_wait_workers(&bfs_worker);

  bfs_queue_free(Q);

  if(CFG_ALGO_DBFS) {
    context_stop_search();
    dbfs_comm_end();
  }
}

#endif
