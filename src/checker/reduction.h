/**
 * @file reduction.h
 * @brief Implementation of various reduction methods.
 * @date 12 sep 2017
 * @author Sami Evangelista
 */

#ifndef LIB_REDUCTION
#define LIB_REDUCTION

#include "includes.h"
#include "context.h"
#include "event.h"


/**
 * @brief edge_lean_reduction
 */
void edge_lean_reduction
(event_list_t en,
 event_t e);


/**
 * @brief mstate_events_reduced_mem
 */
list_t mstate_events_reduced_mem
(mstate_t s,
 bool_t * reduced,
 heap_t heap);


/**
 * @brief mstate_events_reduced
 */
list_t mstate_events_reduced
(mstate_t s,
 bool_t * reduced);

#endif
