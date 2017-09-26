#include "dfs.h"
#include "model.h"
#include "storage.h"
#include "dfs_stack.h"
#include "ddfs_comm.h"
#include "prop.h"
#include "workers.h"

#if defined(CFG_ALGO_DDFS) || defined(CFG_ALGO_DFS)

storage_t S;

state_t dfs_recover_state
(dfs_stack_t stack,
 state_t now,
 worker_id_t w,
 heap_t heap) {
  storage_id_t id;
#if defined(CFG_EVENT_UNDOABLE)
  dfs_stack_event_undo(stack, now);
#else
#if defined(STORAGE_STATE_RECOVERABLE)
  state_free(now);
  id = dfs_stack_top(stack);
  now = storage_get_mem(S, id, w, heap);
#else
  state_free(now);
  now = dfs_stack_top_state(stack, heap);
#endif
#endif
  return now;
}

state_t dfs_check_state
(state_t now,
 event_list_t en,
 dfs_stack_t blue_stack,
 dfs_stack_t red_stack) { 
#if defined(CFG_ACTION_CHECK_SAFETY)
  if(state_check_property(now, en)) {
    context_faulty_state(now);
    dfs_stack_create_trace(blue_stack, red_stack);
  }
#endif
}

state_t dfs_main
(worker_id_t w,
 state_t now,
 storage_id_t id,
 heap_t heap,
 bool_t blue,
 dfs_stack_t blue_stack,
 dfs_stack_t red_stack) {
  storage_id_t id_seed;
  bool_t push;
  dfs_stack_t stack = blue ? blue_stack : red_stack;
  state_t copy;
  event_t e;
  bool_t is_new;
  event_list_t en;
  storage_id_t id_top;
  hash_key_t h;

  /*
   *  push the root state on the stack
   */
  storage_ref(S, w, id);
  dfs_stack_push(stack, id, now);
  en = dfs_stack_compute_events(stack, now, TRUE);
  if(blue) {
    storage_set_cyan(S, id, w, TRUE);
  } else {
    id_seed = id;
    storage_set_pink(S, id, w, TRUE);
  }
  dfs_check_state(now, en, blue_stack, red_stack);

  /*
   *  search loop
   */
  while(dfs_stack_size(stack) && context_keep_searching()) {
  loop_start:

    /*
     *  launch garbage collection on the storage, if necessary
     */
    storage_gc(S, w);
    
    /*
     *  reinitialise the heap if we do not have enough space
     */
    if(heap_space_left(heap) <= 1024) {
      copy = state_copy(now);
      heap_reset(heap);
      now = state_copy_mem(copy, heap);
      state_free(copy);
    }

    /**
     *  1st case: all events of the top state have been executed => the state
     *  has been expanded and we must pop it
     **/
    if(dfs_stack_top_expanded(stack)) {

      /*
       *  check if proviso is verified.  if not we reexpand the state
       */
#if defined(CFG_POR) && defined(CFG_PROVISO)
      if(!dfs_stack_proviso(stack)) {
        dfs_stack_compute_events(stack, now, FALSE);
        goto loop_start;
      }
#endif

      /*
       *  we check an ltl property => launch the red search if the
       *  state is accepting and halt after if an accepting cycle has
       *  been found
       */
#if defined(CFG_ACTION_CHECK_LTL)
      if(blue && state_accepting(now)) {
        context_incr_accepting(w, 1);
	dfs_main(w, now, dfs_stack_top(stack), heap,
                 FALSE, blue_stack, red_stack);
	if(!context_keep_searching()) {
          break;
	}
      }
#endif

      /* 
       * put new colors on the popped state as it leaves the stack
       */
      id_top = dfs_stack_top(stack);
      if(blue) {
	storage_set_cyan(S, id_top, w, FALSE);
        storage_set_blue(S, id_top, TRUE);
      } else {
        storage_set_red(S, id_top, TRUE);
      }

      /*
       *  in distributed DFS we process the state to be later sent
       */
#if defined(CFG_ALGO_DDFS)
      en = dfs_stack_top_events(stack);
      ddfs_comm_process_explored_state(w, id_top, en);
#endif

      /*
       *  and finally pop the state
       */
      context_incr_visited(w, 1);
      dfs_stack_pop(stack);
      if(dfs_stack_size(stack)) {
        now = dfs_recover_state(stack, now, w, heap);
      }

      /*
       *  the state leaves the stack => we decrease its reference
       *  counter
       */
      storage_unref(S, w, id_top);
    }

    /**
     *  2nd case: some events of the top state remain to be executed
     *  => we execute the next one
     **/
    else {
      
      /*
       *  get the next event to process on the top state and excute it
       */
      dfs_stack_pick_event(stack, &e);
      event_exec(e, now);
      context_incr_evts_exec(w, 1);

      /*
       *  try to insert the successor
       */
      id_top = dfs_stack_top(stack);
      storage_insert(S, now, w, &is_new, &id, &h);

      /*
       *  if we check an LTL property, test whether the state reached
       *  is the seed.  exit the loop if this is the case
       */
#if defined(CFG_ACTION_CHECK_LTL)
      if(!blue && (id == id_seed)) {
        context_stop_search();
        context_set_termination_state(FAILURE);
        dfs_stack_create_trace(blue_stack, red_stack);
        break;
      }
#endif
      
      /*
       *  see if it must be pushed on the stack to be processed
       */
      if(blue) {
	context_incr_arcs(w, 1);
        push = is_new || ((!storage_get_blue(S, id)) &&
                          (!storage_get_cyan(S, id, w)));
      } else {
        push = is_new || ((!storage_get_red(S, id)) &&
                          (!storage_get_pink(S, id, w)));
      }

      /*
       *  if the successor state must not be explored we recover the
       *  state on top of the stack.  otherwise we push it on the
       *  stack.  if the successor is on the stack the proviso is not
       *  verified for the current state.
       */
      if(!push) {
        now = dfs_recover_state(stack, now, w, heap);
#if defined(CFG_POR) && defined(CFG_PROVISO)
        if(storage_get_cyan(S, id, w)) {
          dfs_stack_unset_proviso(stack);
        }
#endif
      } else {

        /*
         *  the state enters the stack => we increase its reference
         *  counter
         */
        storage_ref(S, w, id);

        /*
         *  push the successor state on the stack and then set some
         *  color on it
         */
        dfs_stack_push(stack, id, now);
    	en = dfs_stack_compute_events(stack, now, TRUE);
        if(blue) {
          storage_set_cyan(S, id, w, TRUE);
        } else {
          storage_set_pink(S, id, w, TRUE);
        }

        /*
         *  update some statistics and check the state
         */
	if(blue && (0 == event_list_size(en))) {
	  context_incr_dead(w, 1);
	}
        dfs_check_state(now, en, blue_stack, red_stack);
      }
    }
  }
  return now;
}

void * dfs_worker
(void * arg) {
  worker_id_t w = (worker_id_t) (unsigned long int) arg;
  uint32_t wid = worker_global_id(w);
  hash_key_t h;
  bool_t dummy;
  storage_id_t id;
  heap_t heap = bounded_heap_new("state heap", 1024 * 100);
  state_t now = state_initial_mem(heap);
#if defined(CFG_PARALLEL) || defined(CFG_DISTRIBUTED)
  bool_t shuffle = TRUE;
#else
  bool_t shuffle = FALSE;
#endif
#if defined(CFG_EVENT_UNDOABLE) || defined(STORAGE_STATE_RECOVERABLE)
  bool_t states_stored = FALSE;
#else
  bool_t states_stored = TRUE;
#endif
  dfs_stack_t blue_stack = dfs_stack_new(wid * 2, CFG_DFS_STACK_BLOCK_SIZE,
                                         shuffle, states_stored);
#if defined(CFG_ACTION_CHECK_LTL)
  dfs_stack_t red_stack = dfs_stack_new(wid * 2 + 1, CFG_DFS_STACK_BLOCK_SIZE,
                                        shuffle, states_stored);
#else
  dfs_stack_t red_stack = NULL;
#endif

  storage_insert(S, now, w, &dummy, &id, &h);
  storage_set_cyan(S, id, w, TRUE);
  now = dfs_main(w, now, id, heap, TRUE, blue_stack, red_stack);

  /**
   *  if state caching is on we keep waiting on the storage barrier
   *  since some other threads may still launch garbage collection
   */
#if defined(CFG_PARALLEL) && defined(CFG_STATE_CACHING)
  storage_gc_barrier(S, w);
#endif

  dfs_stack_free(blue_stack);
  dfs_stack_free(red_stack);
  state_free(now);
  heap_free(heap);
}

void dfs
() {
  worker_id_t w;
  void * dummy;

  S = context_storage();

#if defined(CFG_ALGO_DDFS)
  ddfs_comm_start();
#endif
  
  launch_and_wait_workers(&dfs_worker);

#if defined(CFG_ALGO_DDFS)
  context_stop_search();
  ddfs_comm_end();
#endif
}

#endif  /*  defined(CFG_ALGO_DDFS) || defined(CFG_ALGO_DFS)  */
