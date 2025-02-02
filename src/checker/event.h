/**
 * @file event.h
 * @brief Event definition.
 * @date 12 sep 2017
 * @author Sami Evangelista
 */

#ifndef LIB_EVENT
#define LIB_EVENT

#include "model.h"
#include "heap.h"
#include "prop.h"
#include "state.h"
#include "config.h"

typedef list_t mevent_list_t;
typedef list_t event_list_t;

void mevent_free_void
(void * data);

uint32_t mevent_list_char_size
(mevent_list_t l);

void mevent_list_serialise
(mevent_list_t l,
 char * v);

mevent_list_t mevent_list_unserialise
(char * v,
 heap_t heap);


#if CFG_ACTION_CHECK_LTL == 1

/**
 *  event definition when doing LTL model checking
 */

typedef struct {
  bool_t dummy;
  uint8_t m;
  uint8_t b;
} event_id_t;

typedef struct {
  bool_t dummy;
  mevent_t m;
  bevent_t b;
} event_t;

bool_t event_is_dummy(event_t e);
void event_free(event_t e);
void event_free_void(void * e);
event_t event_copy(event_t e, heap_t h);
event_id_t event_id(event_t e);
unsigned int event_tid(event_t e);
void event_exec(event_t e, state_t s);
void event_undo(event_t e, state_t s);
void event_to_xml(event_t e, FILE * f);
order_t event_cmp(event_t e, event_t f);
bool_t event_are_independent(event_t e, event_t f);
unsigned int event_char_size(event_t e);
void event_serialise(event_t e, char * v);
event_t event_unserialise(char * v, heap_t heap);

event_list_t state_events(state_t s, heap_t heap);
event_t state_event(state_t s, event_id_t id, heap_t heap);
event_list_t state_events_reduced(state_t s, bool_t * red, heap_t heap);
state_t state_succ(state_t s, event_t e, heap_t heap);
state_t state_pred(state_t s, event_t e, heap_t heap);

unsigned int event_list_char_size(event_list_t l);
void event_list_serialise(event_list_t l, char * v);
event_list_t event_list_unserialise(char * v, heap_t heap);

#else

/**
 *  event definition when not doing LTL model checking: an event
 *  is simply an mevent
 */


typedef mevent_t event_t;
typedef mevent_id_t event_id_t;

#define event_is_dummy(e) FALSE
#define event_free mevent_free
#define event_free_void mevent_free_void
#define event_copy mevent_copy
#define event_id mevent_id
#define event_tid mevent_tid
#define event_exec mevent_exec
#define event_undo mevent_undo
#define event_to_xml mevent_to_xml
#define event_cmp mevent_cmp
#define event_are_independent mevent_are_independent
#define event_char_size mevent_char_size
#define event_serialise mevent_serialise
#define event_unserialise mevent_unserialise

#define state_events mstate_events
#define state_event mstate_event
#define state_events_reduced mstate_events_reduced
#define state_succ mstate_succ
#define state_pred mstate_pred

#define event_list_char_size mevent_list_char_size
#define event_list_serialise mevent_list_serialise
#define event_list_unserialise mevent_list_unserialise

#endif

#endif
