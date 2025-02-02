#include "config.h"
#include "darray.h"
#include "dfs.h"
#include "model.h"
#include "dfs_stack.h"
#include "state.h"
#include "prop.h"
#include "reduction.h"
#include "workers.h"
#include "por_analysis.h"

#define MOD_BUILD_GRAPH 1

#if CFG_ALGO_DFS == 0 && CFG_ALGO_TARJAN == 0

void dfs() { assert(0); }

#else

const struct timespec DFS_WAIT_RED_SLEEP_TIME = { 0, 10 };

typedef struct {
  bool_t accepting;
  htbl_id_t id;  
} red_processed_t;

htbl_t H = NULL;

#if defined(MODEL_HAS_EVENT_UNDOABLE)
#define dfs_recover_state() {                   \
    dfs_stack_event_undo(stack, now);           \
  }
#else
#define dfs_recover_state() {                           \
    if(CFG_HASH_COMPACTION) {                           \
      now = dfs_stack_top_state(stack, heap);           \
    } else {                                            \
      const htbl_id_t top_id = dfs_stack_top(stack);    \
      now = htbl_get(H, top_id, heap);                  \
    }                                                   \
  }
#endif

#define dfs_push_new_state(id, is_s0) {                                 \
    dfs_stack_push(stack, id, now);                                     \
    en = dfs_stack_compute_events(stack, now, por);			\
    if(blue) {                                                          \
      htbl_set_worker_attr(H, id, ATTR_CYAN, w, TRUE);                  \
    } else {                                                            \
      htbl_set_worker_attr(H, id, ATTR_PINK, w, TRUE);                  \
      red_stack_size ++;                                                \
    }                                                                   \
    if(htbl_has_attr(H, ATTR_SAFE) &&                                   \
       dfs_stack_fully_expanded(stack)) {                               \
      htbl_set_attr(H, id, ATTR_SAFE, TRUE);                            \
    }                                                                   \
    if(tarjan) {                                                        \
      darray_push(scc_stack, &id);                                      \
      htbl_set_attr(H, id, ATTR_INDEX, index);                          \
      htbl_set_attr(H, id, ATTR_LOWLINK, index);                        \
      htbl_set_attr(H, id, ATTR_LIVE, TRUE);                            \
      index ++;                                                         \
    }                                                                   \
    if(is_new) {                                                        \
      context_incr_stat(STAT_STATES_STORED, w, 1);                      \
	    FILE * gf = context_graph_file();								\
		if (id != id_init) {											\
			fprintf(gf, ",");												\
		}																\
	    fprintf(gf, "{\"id\": %u, \"state\":", id);\
		state_to_json(now, gf);		\
	    fprintf(gf, "}");\
    }                                                                   \
    if(!dfs_stack_fully_expanded(stack)) {                              \
      context_incr_stat(STAT_STATES_REDUCED, w, 1);                     \
    }                                                                   \
    if(is_new && blue && (0 == list_size(en))) {                        \
      context_incr_stat(STAT_STATES_DEADLOCK, w, 1);                    \
    }                                                                   \
    if(check_safety && state_check_property(now, en)) {                 \
      context_faulty_state(now);                                        \
      dfs_stack_create_trace(stack);                                    \
    }                                                                   \
    if(is_new && blue && check_ltl && state_accepting(now)) {           \
      context_incr_stat(STAT_STATES_ACCEPTING, w, 1);                   \
    }                                                                   \
  }
    

void * dfs_worker
(void * arg) {
  const worker_id_t w = (worker_id_t) (unsigned long int) arg;
  const bool_t check_ltl = CFG_ACTION_CHECK_LTL;
  const bool_t check_safety = CFG_ACTION_CHECK_SAFETY;
  const bool_t por = CFG_POR;
  const bool_t proviso = CFG_POR && CFG_PROVISO;
  const bool_t shuffle = CFG_PARALLEL || CFG_RANDOM_SUCCS;
  const bool_t cndfs = check_ltl && CFG_ALGO_DFS && CFG_PARALLEL;
  const bool_t tarjan = CFG_ALGO_TARJAN;
  const bool_t states_stored = 
#if defined(MODEL_HAS_EVENT_UNDOABLE)
    FALSE
#else
    CFG_HASH_COMPACTION
#endif
    ;
  htbl_meta_data_t mdata;
  uint32_t i;
  heap_t heap = local_heap_new();
  FILE * gf = context_open_graph_file();
  state_t copy, now = state_initial(heap);
  dfs_stack_t stack = dfs_stack_new(CFG_DFS_STACK_BLOCK_SIZE,
                                    shuffle, states_stored);
  htbl_id_t id, id_seed, id_succ, id_popped, id_init;
  bool_t push, blue = TRUE, is_new, state_popped = FALSE, on_stack;
  event_t e;
  event_list_t en;
  uint64_t red_stack_size = 0, index = 0, index_other, lowlink, lowlink_popped;
  red_processed_t proc;
  darray_t red_states = cndfs ?
    darray_new(SYSTEM_HEAP, sizeof(red_processed_t)) : NULL;
  darray_t scc_stack = tarjan ?
    darray_new(SYSTEM_HEAP, sizeof(htbl_id_t)) : NULL;
  darray_t scc = tarjan ?
    darray_new(SYSTEM_HEAP, sizeof(htbl_id_t)) : NULL;

  fprintf(gf, "{\n");
  fprintf(gf, "\"states\": [");
  list_t state_edges = list_new(SYSTEM_HEAP, sizeof(htbl_id_t), NULL);
  /* list_t event_edges = list_new(SYSTEM_HEAP, sizeof(event_t), NULL); */
  
  /*
   * insert the initial state and push it on the stack
   */
  htbl_meta_data_init(mdata, now);
  stbl_insert(H, mdata, is_new);
  id = mdata.id;
  id_init = id;
  dfs_push_new_state(id, TRUE);

  /*
   * search loop
   */
  while(dfs_stack_size(stack) && context_keep_searching()) {
  loop_start:

    /*
     * reinitialise the heap if its current size exceeds
     * CFG_DFS_MAX_HEAP_SIZE
     */
    if(heap_size(heap) >= CFG_DFS_MAX_HEAP_SIZE) {
      copy = state_copy(now, SYSTEM_HEAP);
      heap_reset(heap);
      now = state_copy(copy, heap);
      state_free(copy);
    }

    id = dfs_stack_top(stack);

    /**
     * before handling the state on top of the stack we look at the
     * state that has just been popped, if any
     */
    if(state_popped) {
      state_popped = FALSE;

      /*
       * in tarjan we update the lowlink of the current state
       */
      if(tarjan) {
        lowlink_popped = htbl_get_attr(H, id_popped, ATTR_LOWLINK);
        if(lowlink_popped < htbl_get_attr(H, id, ATTR_LOWLINK)) {
          htbl_set_attr(H, id, ATTR_LOWLINK, lowlink_popped);
        }
      }

      /*
       * proviso is on: if the current state and its popped successor
       * are unsafe, we set the unsafe-sucessor bit of the current
       * state
       */
      if(proviso &&
         !htbl_get_attr(H, id, ATTR_SAFE) &&
         !htbl_get_attr(H, id_popped, ATTR_SAFE)) {
        htbl_set_attr(H, id, ATTR_UNSAFE_SUCC, TRUE);
      }
    }

    /**
     * 1st case for the state on top of the stack: all its events have
     * been executed => it must be popped
     **/
    if(dfs_stack_top_expanded(stack)) {

      /*
       * proviso is on: if the state to pop does not have unsafe
       * successors, it become safe.  else if it is marked for revisit
       * it also become safe and we reexpand it
       */
      if(proviso && !htbl_get_attr(H, id, ATTR_SAFE)) {
        if(!htbl_get_attr(H, id, ATTR_UNSAFE_SUCC)) {
          htbl_set_attr(H, id, ATTR_SAFE, TRUE);
        } else if(htbl_get_attr(H, id, ATTR_TO_REVISIT)) {
          htbl_set_attr(H, id, ATTR_SAFE, TRUE);
          dfs_stack_compute_events(stack, now, FALSE);
          context_incr_stat(STAT_STATES_REDUCED, w, - 1);
          goto loop_start;
        }
      }

      /*
       * we check an ltl property => launch the red search if the
       * state is accepting
       */
      if(check_ltl && blue && state_accepting(now)) {
        id_seed = dfs_stack_top(stack);
        blue = FALSE;
        red_stack_size = 1;
        dfs_stack_compute_events(stack, now, por);
        if(cndfs) {
          darray_reset(red_states);
        }
        goto loop_start;
      }

      /* 
       * put new colors on the popped state as it leaves the stack
       */
    pop_blue:
      if(blue) {
	htbl_set_worker_attr(H, id, ATTR_CYAN, w, FALSE);
        htbl_set_attr(H, id, ATTR_BLUE, TRUE);
      } else {  /* nested search of ndfs or cndfs */
        
        /*
         * in cdnfs we put the popped state in red_states.  in
         * sequential ndfs we can directly mark it as red.
         */
        if(cndfs) {
          proc.accepting = state_accepting(now);
          proc.id = id;
          darray_push(red_states, &proc);
        } else {
          htbl_set_attr(H, id, ATTR_RED, TRUE);
        }
        red_stack_size --;
        
        /*
         * termination of the red DFS.  in cndfs we wait for all
         * accepting states of the red_states set to become red and
         * then mark all states of this set as red
         */
        if(0 == red_stack_size) {
          blue = TRUE;
          if(cndfs) {
            for(i = 0; i < darray_size(red_states); i ++) {
              proc = * ((red_processed_t *) darray_get(red_states, i));
              if(proc.accepting && proc.id != id) {
                while(!htbl_get_attr(H, proc.id, ATTR_RED)
                      && context_keep_searching()) {
                  context_sleep(DFS_WAIT_RED_SLEEP_TIME);
                }
              }
            }
            for(i = 0; i < darray_size(red_states); i ++) {
              proc = * ((red_processed_t *) darray_get(red_states, i));
              htbl_set_attr(H, proc.id, ATTR_RED, TRUE);
            }
          }
          goto pop_blue;
        }
      }

      /*
       * in tarjan we check the state popped if the root of an SCC in
       * which case we pop this SCC
       */
      if(tarjan) {
        if(htbl_get_attr(H, id, ATTR_INDEX) ==
           htbl_get_attr(H, id, ATTR_LOWLINK)) {
          darray_reset(scc);
          do {
            id_succ = * ((htbl_id_t *) darray_pop(scc_stack));
            htbl_set_attr(H, id_succ, ATTR_LIVE, FALSE);
            darray_push(scc, &id_succ);
          } while (id_succ != id);
          if(!proviso) {
            por_analysis_scc(H, scc);
          }
        }
      }

      /*
       * and finally pop the state
       */
      context_incr_stat(STAT_STATES_PROCESSED, w, 1);
      dfs_stack_pop(stack);
      if(dfs_stack_size(stack)) {
        dfs_recover_state();
      }
      state_popped = TRUE;
      id_popped = id;
    }
        
    /**
     * 2nd case for the state on top of the stack: some of its events
     * remain to be executed => we execute the next one
     **/
    else {
      
      /*
       * get the next event to process on the top state and execute it
       */
      dfs_stack_pick_event(stack, &e);
      event_exec(e, now);
      context_incr_stat(STAT_EVENT_EXEC, w, 1);

      /*
       * try to insert the successor
       */
      htbl_meta_data_init(mdata, now);
      stbl_insert(H, mdata, is_new);
      id_succ = mdata.id;
      
      /*
       * if we check an LTL property and are in the red search, test
       * whether the state reached is the seed.  exit the loop if this
       * is the case
       */
      if(check_ltl && !blue && (id_succ == id_seed)) {
        dfs_stack_create_trace(stack);
        break;
      }
      
      /*
       * see if it must be pushed on the stack to be processed
       */
      if(blue) {
        context_incr_stat(STAT_ARCS, w, 1);
        push = is_new
          || (!(on_stack = htbl_get_worker_attr(H, id_succ, ATTR_CYAN, w)) &&
              !htbl_get_attr(H, id_succ, ATTR_BLUE));
      } else {
        push = is_new
          || (!(on_stack = htbl_get_worker_attr(H, id_succ, ATTR_PINK, w)) &&
              !htbl_get_attr(H, id_succ, ATTR_RED));
      }

	printf("%u -> %u (%u)\n", id, id_succ, event_tid(e));
	unsigned int etid = event_tid(e);
	list_append(state_edges, &id);
	list_append(state_edges, &id_succ);
	list_append(state_edges, &etid);
	/* event_to_xml(e, gf); */
      if(push) { /* successor state must be explored */
        dfs_push_new_state(id_succ, FALSE);
      } else {
        dfs_recover_state();

        /*
         * tarjan: we reach a live state.
         */
        if(tarjan && htbl_get_attr(H, id_succ, ATTR_LIVE)) {
          index_other = htbl_get_attr(H, id_succ, ATTR_INDEX);
          lowlink = htbl_get_attr(H, id, ATTR_LOWLINK);
          if(lowlink > index_other) {
            htbl_set_attr(H, id, ATTR_LOWLINK, index_other);
          }
        }

        /*
         * proviso is on: if the state is on the stack and the current
         * state is unsafe, we set the unsafe-successor flag of the
         * current state and the to-revisit flag of the sucessor
         */
        if(proviso && on_stack && !htbl_get_attr(H, id, ATTR_SAFE)) {
          htbl_set_attr(H, id, ATTR_UNSAFE_SUCC, TRUE);
          htbl_set_attr(H, id_succ, ATTR_TO_REVISIT, TRUE);          
        }
      }
    }
  }

  fprintf(gf, "],\n");
  printf("--------\n");
  fprintf(gf, "\"edges\": [");
  for (i = 0; i < list_size(state_edges); i = i + 3) {
	  id = *((htbl_id_t*)list_nth(state_edges, i));
	  id_succ = *((htbl_id_t*)list_nth(state_edges, i + 1));
	  unsigned int tid = *((htbl_id_t*)list_nth(state_edges, i + 2));
	fprintf(gf, "[%u, %u, %u]\n", id, id_succ, tid);
	if (i + 3 < list_size(state_edges)) {
		fprintf(gf, ",");
	}
  }
  fprintf(gf, "]");
  fprintf(gf, "}\n");

  /*
   * free everything
   */
  dfs_stack_free(stack);
  heap_free(heap);
  if(cndfs) {
    darray_free(red_states);
  }
  if(tarjan) {
    darray_free(scc);
    darray_free(scc_stack);
  }
  context_close_graph_file();

  return NULL;
}

void dfs
() {
  H = stbl_default_new();
  launch_and_wait_workers(&dfs_worker);
  htbl_free(H);
}

#endif
