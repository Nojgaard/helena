#include "config.h"
#include "delta_ddd.h"
#include "prop.h"
#include "graph.h"
#include "workers.h"
#include "context.h"

#if CFG_ALGO_DELTA_DDD != 1

void delta_ddd() { assert(0); }

#else

typedef uint32_t delta_ddd_storage_id_t;

typedef struct struct_delta_ddd_storage_t * delta_ddd_storage_t;

typedef struct {
  delta_ddd_storage_id_t fst_child;
  delta_ddd_storage_id_t next;
  bool_t father;
  bool_t dd;
  bool_t dd_visit;
  bool_t recons[2];
  event_id_t e;
  hkey_t h;
#if CFG_ACTION_BUILD_GRAPH == 1
  uint32_t num;
#endif
} delta_ddd_state_t;

typedef struct {
  unsigned char content;
  delta_ddd_storage_id_t id;
  delta_ddd_storage_id_t pred;
  event_id_t e;
  char * s;
  hkey_t h;
  uint16_t width;
} delta_ddd_candidate_t;

struct struct_delta_ddd_storage_t {
  delta_ddd_storage_id_t root;
  delta_ddd_state_t ST[CFG_HASH_SIZE];
  int32_t size[CFG_NO_WORKERS];
  uint64_t dd_time;
};

typedef struct struct_delta_ddd_storage_t struct_delta_ddd_storage_t;

delta_ddd_storage_t S;
uint32_t next_num;

/*  mail boxes used during expansion  */
delta_ddd_candidate_t * BOX[CFG_NO_WORKERS][CFG_NO_WORKERS];
uint32_t BOX_size[CFG_NO_WORKERS][CFG_NO_WORKERS];
uint32_t BOX_tot_size[CFG_NO_WORKERS];
uint32_t BOX_max_size;

/*  candidate set (obtained from boxes after merging)  */
delta_ddd_candidate_t * CS[CFG_NO_WORKERS];
uint32_t CS_size[CFG_NO_WORKERS];
uint32_t * NCS[CFG_NO_WORKERS];
uint32_t CS_max_size;

/*  state table  */
delta_ddd_state_t * ST;

/*  heaps used to store candidates, to reconstruct states and
 *  perform duplicate detection  */
heap_t candidates_heaps[CFG_NO_WORKERS];
heap_t expand_heaps[CFG_NO_WORKERS];
heap_t detect_heaps[CFG_NO_WORKERS];
heap_t expand_evts_heaps[CFG_NO_WORKERS];
heap_t detect_evts_heaps[CFG_NO_WORKERS];

/*  random seeds  */
rseed_t seeds[CFG_NO_WORKERS];

/*  synchronisation variables  */
bool_t LVL_TERMINATED[CFG_NO_WORKERS];
pthread_barrier_t BARRIER;
uint32_t NEXT_LVL;
uint32_t NEXT_LVLS[CFG_NO_WORKERS];
bool_t error_reported;

/*  alternating bit to know which states to expand  */
uint8_t RECONS_ID;

#define DELTA_DDD_CAND_NEW  1
#define DELTA_DDD_CAND_DEL  2
#define DELTA_DDD_CAND_NONE 3

#define DELTA_DDD_OWNER(h) (((h) & CFG_HASH_SIZE_M) % CFG_NO_WORKERS)

#if defined(MODEL_HAS_EVENT_UNDOABLE)
#define DELTA_DDD_VISIT_PRE_HEAP_PROCESS() {            \
    if(heap_size(heap) > CFG_DELTA_DDD_MAX_HEAP_SIZE) { \
      state_t copy = state_copy(s, SYSTEM_HEAP);        \
      heap_reset(heap);                                 \
      s = state_copy(copy, heap);                       \
      state_free(copy);                                 \
    }                                                   \
    heap_pos = heap_get_position(heap_evts);	\
  }
#define DELTA_DDD_VISIT_POST_HEAP_PROCESS() {   \
    heap_set_position(heap_evts, heap_pos);	\
  }
#define DELTA_DDD_VISIT_HANDLE_EVENT(func) {    \
    e = state_event(s, ST[curr].e, heap_evts);	\
    event_exec(e, s);                           \
    s = func(w, curr, s, depth - 1);            \
    event_undo(e, s);                           \
  }
#else  /*  !defined(MODEL_HAS_EVENT_UNDOABLE)  */
#define DELTA_DDD_VISIT_PRE_HEAP_PROCESS() {    \
    heap_pos = heap_get_position(heap);         \
  }
#define DELTA_DDD_VISIT_POST_HEAP_PROCESS() {   \
    heap_set_position(heap, heap_pos);		\
  }
#define DELTA_DDD_VISIT_HANDLE_EVENT(func) {            \
    e = state_event(s, ST[curr].e, SYSTEM_HEAP);        \
    t = state_succ(s, e, heap);                         \
    func(w, curr, t, depth - 1);                        \
  }
#endif

#define DELTA_DDD_PICK_RANDOM_NODE(w, now, start)	{       \
    unsigned int rnd;						\
    delta_ddd_storage_id_t fst = ST[now].fst_child;             \
    start = fst;						\
    rnd = random_int(&seeds[w]) % (ST[now].father >> 1);	\
    while(rnd --) {						\
      if((start = ST[start].next) == now) {			\
	start = fst;						\
      }								\
    }								\
  }

void delta_ddd_barrier
(worker_id_t w) {
  if(CFG_PARALLEL) {
    context_barrier_wait(&BARRIER);
  }
}



/*****
 *
 *  Function: delta_ddd_storage_new
 *
 *****/
delta_ddd_storage_t delta_ddd_storage_new
() {
  worker_id_t w;
  unsigned int i;
  delta_ddd_storage_t result;

  result = mem_alloc(SYSTEM_HEAP, sizeof(struct_delta_ddd_storage_t));
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    result->size[w] = 0;
  }

  /*
   *  initialisation of the state table
   */
  for(i = 0; i < CFG_HASH_SIZE; i ++) {
    result->ST[i].fst_child = UINT_MAX;
    result->ST[i].recons[0] = FALSE;
    result->ST[i].recons[1] = FALSE;
  }

  result->dd_time = 0;
  return result;
}

void delta_ddd_storage_free
(delta_ddd_storage_t storage) {
  mem_free(SYSTEM_HEAP, storage);
}

uint64_t delta_ddd_storage_size
(delta_ddd_storage_t storage) {
  uint64_t result = 0;
  worker_id_t w;
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    result += storage->size[w];
  }
  return result;
}



/*****
 *
 *  Function: delta_ddd_create_trace
 *
 *****/
void delta_ddd_create_trace
(delta_ddd_storage_id_t id) {
  event_list_t trace = list_new(SYSTEM_HEAP, sizeof(event_t), event_free_void);
  state_t s = state_initial(SYSTEM_HEAP);
  list_t trace_ids = list_new(SYSTEM_HEAP, sizeof(event_id_t), NULL);
  event_id_t eid;
  event_t e;
  delta_ddd_storage_id_t curr;

  curr = id;
  while(curr != S->root) {
    list_append(trace_ids, &ST[curr].e);
    while(!(ST[curr].father & 1)) { curr = ST[curr].next; }
    curr = ST[curr].next;
  }
  while(!list_is_empty(trace_ids)) {
    list_pick_last(trace_ids, &eid);
    e = state_event(s, eid, SYSTEM_HEAP);
    event_exec(e, s);
    list_append(trace, &e);
  }
  list_free(trace_ids);
  state_free(s);
  context_set_trace(trace);
}



/*****
 *
 *  Function: delta_ddd_send_candidate
 *
 *****/
bool_t delta_ddd_send_candidate
(worker_id_t w,
 delta_ddd_storage_id_t pred,
 event_id_t e,
 state_t s) {
  delta_ddd_candidate_t c;
  worker_id_t x;
  uint16_t size;
  
  c.content = DELTA_DDD_CAND_NEW;
  c.pred = pred;
  c.e = e;
  c.width = state_char_size(s);
  c.s = mem_alloc0(candidates_heaps[w], c.width);
  state_serialise(s, c.s, &size);
  c.h = state_hash(s);
  x = DELTA_DDD_OWNER(c.h);
  BOX[w][x][BOX_size[w][x]] = c;  
  BOX_size[w][x] ++;
  BOX_tot_size[w] ++;
  return BOX_size[w][x] == BOX_max_size;
}



/*****
 *
 *  Function: delta_ddd_merge_candidate_set
 *
 *  merge the mailboxes of workers into the candidate set before
 *  performing duplicate detection
 *
 *****/
bool_t delta_ddd_cmp_string
(uint16_t width,
 char * v,
 char * w) {
  return (0 == memcmp(v, w, width)) ? TRUE : FALSE;
}

bool_t delta_ddd_merge_candidate_set
(worker_id_t w) {
  delta_ddd_candidate_t * C = CS[w];
  unsigned int i, pos, fst, slot;
  delta_ddd_storage_id_t id;
  worker_id_t x;
  bool_t loop;

  CS_size[w] = 0;
  for(x = 0; x < CFG_NO_WORKERS; x ++) {
    for(i = 0; i < BOX_size[x][w]; i ++) {
      delta_ddd_candidate_t c = BOX[x][w][i];
      fst = pos = c.h % CS_max_size;
      loop = TRUE;
      while(loop) {
	switch(C[pos].content) {
	case DELTA_DDD_CAND_NONE :
	  C[pos] = c;
	  NCS[w][CS_size[w] ++] = pos;
	  loop = FALSE;
	  slot = C[pos].h & CFG_HASH_SIZE_M;

	  /*  mark for reconstruction states in conflict with the candidate  */
	  while(ST[slot].fst_child != UINT_MAX) {
            if(ST[slot].h == c.h) {
              ST[slot].dd = TRUE;
              id = slot;
              while(id != S->root) {
                while(!(ST[id].father & 1)) {
                  id = ST[id].next;
                }
                id = ST[id].next;
                if(ST[id].dd_visit) {
                  break;
                } else {
                  ST[id].dd_visit = TRUE;
                }
              }
            }
            slot = (slot + CFG_NO_WORKERS) & CFG_HASH_SIZE_M;
	  }
	  break;
	case DELTA_DDD_CAND_NEW :
#if CFG_ACTION_BUILD_GRAPH == 1
	  pos = (pos + 1) % CS_max_size;
	  assert (pos != fst);
#else
	  if(c.h == C[pos].h
             && c.width == C[pos].width
             && delta_ddd_cmp_string(c.width, c.s, C[pos].s)) {
	    loop = FALSE;
	  } else {
	    pos = (pos + 1) % CS_max_size;
	    assert (pos != fst);
	  }
#endif
	  break;
	case DELTA_DDD_CAND_DEL :
	  assert(FALSE);
	}
      }
    }
  }
}



/*****
 *
 *  Function: delta_ddd_duplicate_detection_dfs
 *
 *****/
void delta_ddd_storage_delete_candidate
(worker_id_t w,
 state_t s,
 delta_ddd_storage_id_t id) {
  hkey_t h = state_hash(s);
  unsigned int fst, i = h % CS_max_size;
  worker_id_t x = DELTA_DDD_OWNER(h);
  
  fst = i;
  do {
    switch(CS[x][i].content) {
    case DELTA_DDD_CAND_NONE : return;
    case DELTA_DDD_CAND_NEW  :
      if(state_cmp_string(s, CS[x][i].s)) {
	CS[x][i].content = DELTA_DDD_CAND_DEL;
#if CFG_ACTION_BUILD_GRAPH == 1
	CS[x][i].id = id;
	break;
#else
	return;
#endif
      }
    }
    i = (i + 1) % CS_max_size;
  } while(fst != i);
}

state_t delta_ddd_duplicate_detection_dfs
(worker_id_t w,
 delta_ddd_storage_id_t now,
 state_t s,
 unsigned int depth) {
  heap_t heap = detect_heaps[w];
  heap_t heap_evts = detect_evts_heaps[w];
  state_t t;
  event_t e;
  mem_size_t heap_pos;
  delta_ddd_storage_id_t start, curr;

  DELTA_DDD_VISIT_PRE_HEAP_PROCESS();

  /*
   *  remove state now from the candidate set
   */
  if(ST[now].dd) {
    ST[now].dd = FALSE;
    delta_ddd_storage_delete_candidate(w, s, now);
  }

  /*
   *  state now must be visited by the duplicate detection procedure
   */
  if(ST[now].dd_visit) {
    
    /*
     *  we start expanding now from a randomly picked successor
     */
    DELTA_DDD_PICK_RANDOM_NODE(w, now, start);
    curr = start;
    do {
      if(ST[curr].dd || ST[curr].dd_visit) {
        context_incr_stat(STAT_EVENT_EXEC, w, 1);
	DELTA_DDD_VISIT_HANDLE_EVENT(delta_ddd_duplicate_detection_dfs);
      }
      if(ST[curr].father & 1) {
	curr = ST[ST[curr].next].fst_child;
      } else {
	curr = ST[curr].next;
      }
    } while(curr != start);
  }
  ST[now].dd = FALSE;
  ST[now].dd_visit = FALSE;
  DELTA_DDD_VISIT_POST_HEAP_PROCESS();
  return s;
}


#if CFG_ACTION_BUILD_GRAPH == 1
void delta_ddd_remove_duplicates_around
(delta_ddd_candidate_t * C,
 unsigned int i) {
  int j, k, m, moves[2] = { 1, -1};
  for(k = 0; k < 2; k ++) {
    j = i;
    m = moves[k];
    while(TRUE) {
      j += m;
      if(j == -1) { j = CS_max_size - 1; }
      if(j == CS_max_size) { j = 0; }
      if(C[j].content == DELTA_DDD_CAND_NONE) { break; }
      if(C[j].width == C[i].width
         && delta_ddd_cmp_string(C[j].width, C[j].s, C[i].s)) {
	C[j].content = DELTA_DDD_CAND_DEL;
	C[j].id = C[i].id;
      }
    }
  }
}

void delta_ddd_write_nodes_graph
(worker_id_t w) {
  delta_ddd_candidate_t * C = CS[w];
  unsigned int i = 0;
  uint8_t t, succs = 0;
  FILE * gf = context_graph_file();
  for(i = 0; i < CS_max_size; i ++) {
    if(DELTA_DDD_CAND_NEW == C[i].content) {
      t = GT_NODE;
      fwrite(&t, sizeof(uint8_t), 1, gf);
      fwrite(&ST[C[i].id].num, sizeof(node_t), 1, gf);
      fwrite(&succs, sizeof(uint8_t), 1, gf);
    }
  }
  for(i = 0; i < CS_max_size; i ++) {
    if(DELTA_DDD_CAND_NONE != C[i].content) {
      t = GT_EDGE;
      fwrite(&t, sizeof(uint8_t), 1, gf);
      fwrite(&ST[C[i].pred].num, sizeof(node_t), 1, gf);
      fwrite(&C[i].e, sizeof(edge_num_t), 1, gf);
      fwrite(&ST[C[i].id].num, sizeof(node_t), 1, gf);
    }
  }
}
#endif



/*****
 *
 *  Function: delta_ddd_insert_new_states
 *
 *  insert in the state tree states that are still in the candidate
 *  set after duplicate detection
 *
 *****/
delta_ddd_storage_id_t delta_ddd_insert_new_state
(worker_id_t w,
 hkey_t h,
 delta_ddd_state_t s,
 delta_ddd_storage_id_t pred) {
  uint8_t r = (RECONS_ID + 1) & 1;
  unsigned int fst = h & CFG_HASH_SIZE_M, slot = fst;

  context_incr_stat(STAT_STATES_STORED, w, 1);
  
  while(ST[slot].fst_child != UINT_MAX) {
    assert((slot = (slot + CFG_NO_WORKERS) & CFG_HASH_SIZE_M) != fst);
  }
  s.next = s.fst_child = slot;
#if CFG_ACTION_BUILD_GRAPH == 1
  s.num = next_num ++;
#endif
  ST[slot] = s;

  /*  mark the state for the next expansion step  */
  do {
    ST[pred].recons[r] = TRUE;
    while(!(ST[pred].father & 1)) {
      pred = ST[pred].next;
    }
    pred = ST[pred].next;
  } while(pred != S->root && !ST[pred].recons[r]);
  return slot;
}

void delta_ddd_insert_new_states
(worker_id_t w) {
  worker_id_t x;
  unsigned int i = 0;
  delta_ddd_candidate_t c, * C = CS[w];
  delta_ddd_state_t ns;
  unsigned int no_new = 0;

  ns.dd = ns.dd_visit = ns.recons[RECONS_ID] = FALSE;
  ns.recons[(RECONS_ID + 1) & 1] = TRUE;
  for(i = 0; i < CS_size[w]; i ++) {
    c = C[NCS[w][i]];
    if(DELTA_DDD_CAND_NEW == c.content) {
      no_new ++;
      ns.e = c.e;
      ns.father = 0;
      ns.h = c.h;
      C[NCS[w][i]].id = delta_ddd_insert_new_state(w, c.h, ns, c.pred);
#if CFG_ACTION_BUILD_GRAPH == 1
      delta_ddd_remove_duplicates_around(C, NCS[w][i]);
#endif
    }
  }
  S->size[w] += no_new;
  NEXT_LVLS[w] += no_new;
#if CFG_ACTION_BUILD_GRAPH == 1
  delta_ddd_write_nodes_graph(w);
#endif

  delta_ddd_barrier(w);

  if(0 == w) {
    for(x = 0; x < CFG_NO_WORKERS; x ++) {
      for(i = 0; i < CS_size[x]; i ++) {
	c = CS[x][NCS[x][i]];
	if(DELTA_DDD_CAND_NEW == c.content) {
	  if(ST[c.pred].fst_child == c.pred) {
	    ST[c.id].next = c.pred;
	    ST[c.id].father += 1;
	  } else {
	    ST[c.id].next = ST[c.pred].fst_child;
	  }
	  ST[c.pred].fst_child = c.id;
	  ST[c.pred].father += 2;
	}
	CS[x][NCS[x][i]].content = DELTA_DDD_CAND_NONE;
      }
    }
  }
}



/*****
 *
 *  Function: delta_ddd_duplicate_detection
 *
 *****/
bool_t delta_ddd_duplicate_detection
(worker_id_t w) {
  state_t s;
  worker_id_t x;
  bool_t all_terminated = TRUE;
  lna_timer_t t;

  if(0 == w) {
    lna_timer_init(&t);
    lna_timer_start(&t);
  }

  /*
   *  initialize heaps for duplicate detection
   */
#if defined(MODEL_HAS_EVENT_UNDOABLE)
  heap_reset(detect_evts_heaps[w]);
#endif
  heap_reset(detect_heaps[w]);
  s = state_initial(detect_heaps[w]);

  /*
   *  merge the candidate set and mark states to reconstruct
   */
  delta_ddd_barrier(w);
  if(delta_ddd_storage_size(S) >= 0.9 * CFG_HASH_SIZE) {
    context_error("state table too small (increase --hash-size and rerun)");
  }
  if(!context_keep_searching()) {
    pthread_exit(NULL);
  }
  delta_ddd_merge_candidate_set(w);

  /*
   *  reconstruct states and perform duplicate detection
   */
  delta_ddd_barrier(w);
  delta_ddd_duplicate_detection_dfs(w, S->root, s, 0);

  /*
   *  insert these new states in the tree
   */
  delta_ddd_barrier(w);
  delta_ddd_insert_new_states(w);

  /*
   *  reinitialise my mail boxes and candidate heaps
   */
  heap_reset(candidates_heaps[w]);
  BOX_tot_size[w] = 0;
  for(x = 0; x < CFG_NO_WORKERS; x ++) {
    BOX_size[w][x] = 0;
    all_terminated = all_terminated && LVL_TERMINATED[x];
  }
  if(0 == w) {
    lna_timer_stop(&t);
    S->dd_time += lna_timer_value(t);
  }
  delta_ddd_barrier(w);
  return all_terminated ? FALSE : TRUE;
}



/*****
 *
 *  Function: delta_ddd_expand_dfs
 *
 *  recursive function called by delta_ddd_expand to explore state now
 *
 *****/
state_t delta_ddd_expand_dfs
(worker_id_t w,
 delta_ddd_storage_id_t now,
 state_t s,
 unsigned int depth) {
  heap_t heap = expand_heaps[w];
  heap_t heap_evts = expand_evts_heaps[w];
  mem_size_t heap_pos;
  delta_ddd_storage_id_t start, curr;
  state_t t;
  event_t e;
  event_id_t e_id;
  event_list_t en;
  unsigned int en_size, i;
  uint32_t size;
  worker_id_t x;

  DELTA_DDD_VISIT_PRE_HEAP_PROCESS();

  if(0 == depth) {
    
    /*
     *  we have reached a leaf => we expand it
     */
    en = state_events(s, heap);
    if(CFG_ACTION_CHECK_SAFETY && state_check_property(s, en)) {
      if(!error_reported) {
	error_reported = TRUE;
	context_faulty_state(s);
	delta_ddd_create_trace(now);
      }
    }
    en_size = list_size(en);
    if(0 == en_size) {
      context_incr_stat(STAT_STATES_DEADLOCK, w, 1);
    }
    for(i = 0; i < en_size; i ++) {
      e = * ((event_t *) list_nth(en, i));
      e_id = event_id(e);
      t = state_succ(s, e, heap);
      if(delta_ddd_send_candidate(w, now, e_id, t)) {
	delta_ddd_duplicate_detection(w);
      }
    }
    context_incr_stat(STAT_ARCS, w, en_size);
    context_incr_stat(STAT_EVENT_EXEC, w, en_size);
    context_incr_stat(STAT_STATES_PROCESSED, w, 1);

    /*
     *  perform duplicate detection if the candidate set is full
     */
    size = 0;
    for(x = 0; x < CFG_NO_WORKERS; x ++) {
      size += BOX_tot_size[x];
    }
    if(size >= CFG_DELTA_DDD_CAND_SET_SIZE) {
      delta_ddd_duplicate_detection(w);
    }
  } else {

    /*
     *  we start expanding now from a randomly picked successor
     */
    DELTA_DDD_PICK_RANDOM_NODE(w, now, start);
    curr = start;
    do {
      if(ST[curr].recons[RECONS_ID]) {
        context_incr_stat(STAT_EVENT_EXEC, w, 1);
	DELTA_DDD_VISIT_HANDLE_EVENT(delta_ddd_expand_dfs);
      }
      if(ST[curr].father & 1) {
	curr = ST[ST[curr].next].fst_child;
      } else {
	curr = ST[curr].next;
      }
    } while(curr != start);
  }

  ST[now].recons[RECONS_ID] = FALSE;
  DELTA_DDD_VISIT_POST_HEAP_PROCESS();
  return s;
}



/*****
 *
 *  Function: delta_ddd_expand
 *
 *****/
void delta_ddd_expand
(worker_id_t w,
 unsigned int depth) {
  state_t s;

#if defined(MODEL_HAS_EVENT_UNDOABLE)
  heap_reset(expand_evts_heaps[w]);
#endif
  heap_reset(expand_heaps[w]);
  s = state_initial(expand_heaps[w]);
  delta_ddd_expand_dfs(w, S->root, s, depth);
  LVL_TERMINATED[w] = TRUE;
  while(delta_ddd_duplicate_detection(w));
}



/*****
 *
 *  Function: delta_ddd_worker
 *
 *****/
void * delta_ddd_worker
(void * arg) {
  worker_id_t x, w = (worker_id_t) (unsigned long int) arg;
  unsigned int depth = 0;
  
  if(0 == w) {
    delta_ddd_state_t ns;
    state_t s = state_initial(SYSTEM_HEAP);
    hkey_t h = state_hash(s);
    delta_ddd_storage_id_t slot = h & CFG_HASH_SIZE_M;

    ns.dd = ns.dd_visit = ns.recons[0] = FALSE;
    ns.recons[1] = ns.father = 1;
    ns.next = ns.fst_child = slot;
    ns.h = h;
#if CFG_ACTION_BUILD_GRAPH == 1
    {
      FILE * gf;
      uint8_t t = GT_NODE, succs = 0;
      gf = context_graph_file();
      ns.num = next_num ++;
      fwrite(&t, sizeof(uint8_t), 1, gf);
      fwrite(&ns.num, sizeof(node_t), 1, gf);
      fwrite(&succs, sizeof(uint8_t), 1, gf);
    }
#endif
    ST[slot] = ns;
    S->root = slot;
    S->size[0] = 1;
    state_free(s);
    RECONS_ID = 0;
    NEXT_LVL = 1;
    context_incr_stat(STAT_STATES_STORED, w, 1);
  }
  delta_ddd_barrier(w);
  while(NEXT_LVL != 0) {

    /*
     *  initialise some data for the next level
     */
    delta_ddd_barrier(w);
    LVL_TERMINATED[w] = FALSE;
    NEXT_LVLS[w] = 0;
    if(0 == w) {
      NEXT_LVL = 0;
      RECONS_ID = (RECONS_ID + 1) & 1;
    }

    /*
     *  all workers expand the current level
     */
    delta_ddd_barrier(w);
    delta_ddd_expand(w, depth);
    depth ++;
    if(0 == w) {
      for(x = 0; x < CFG_NO_WORKERS; x ++) {
	NEXT_LVL += NEXT_LVLS[x];
      }
    }
    delta_ddd_barrier(w);
  }
  return NULL;
}



/*****
 *
 *  Function: delta_ddd
 *
 *****/
void delta_ddd
() {  
  worker_id_t w, x;
  unsigned int i, s;
  FILE * gf;

  S = delta_ddd_storage_new();
  ST = S->ST;
  pthread_barrier_init(&BARRIER, NULL, CFG_NO_WORKERS);
  error_reported = FALSE;
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    seeds[w] = random_seed(w);
  }
  next_num = 0;
  gf = context_open_graph_file();

  /*
   *  initialisation of the heaps
   */
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    expand_heaps[w] = local_heap_new();
    detect_heaps[w] = local_heap_new();
    candidates_heaps[w] = local_heap_new();
    expand_evts_heaps[w] = local_heap_new();
    detect_evts_heaps[w] = local_heap_new();
  }

  /*
   *  initialisation of the mailboxes of workers
   */
  BOX_max_size = (CFG_DELTA_DDD_CAND_SET_SIZE /
                  (CFG_NO_WORKERS * CFG_NO_WORKERS)) << 2;
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    for(x = 0; x < CFG_NO_WORKERS; x ++) {
      s = BOX_max_size * sizeof(delta_ddd_candidate_t);
      BOX[w][x] = mem_alloc(SYSTEM_HEAP, s);
      for(i = 0; i < BOX_max_size; i ++) {
	BOX[w][x][i].content = DELTA_DDD_CAND_NONE;
	BOX[w][x][i].h = 0;
	BOX_size[w][x] = 0;
      }
    }
    BOX_tot_size[w] = 0;
  }

  /*
   *  initialisation of the candidate set
   */
  CS_max_size = (CFG_DELTA_DDD_CAND_SET_SIZE / CFG_NO_WORKERS) << 1;
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    CS[w] = mem_alloc(SYSTEM_HEAP, CS_max_size *
                      sizeof(delta_ddd_candidate_t));
    NCS[w] = mem_alloc(SYSTEM_HEAP, CS_max_size * sizeof(uint32_t));
    for(i = 0; i < CS_max_size; i ++) {
      CS[w][i].content = DELTA_DDD_CAND_NONE;
    }
    CS_size[w] = 0;
  }

  launch_and_wait_workers(&delta_ddd_worker);

  /*
   * free everything
   */  
  for(w = 0; w < CFG_NO_WORKERS; w ++) {
    mem_free(SYSTEM_HEAP, CS[w]);
    mem_free(SYSTEM_HEAP, NCS[w]);
    for(x = 0; x < CFG_NO_WORKERS; x ++) {
      mem_free(SYSTEM_HEAP, BOX[w][x]);
    }
    heap_free(candidates_heaps[w]);
    heap_free(expand_heaps[w]);
    heap_free(detect_heaps[w]);
    heap_free(expand_evts_heaps[w]);
    heap_free(detect_evts_heaps[w]);
  }

  context_close_graph_file();
  
  delta_ddd_storage_free(S);
}

#endif
