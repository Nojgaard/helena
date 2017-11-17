/**
 * @file htbl.h
 * @brief Implementation of an hash table supporting concurrent accesses.
 * @date 12 sep 2017
 * @author Sami Evangelista
 *
 * The hash table is a small variation of the structure presented in:
 *
 * Boosting Multi-Core Reachability Performance with Shared Hash Tables
 * by Alfons Laarman, Jaco van de Pol, Michael Weber.
 * in Formal Methods in Computer-Aided Design.
 *
 */

#ifndef LIB_HTBL
#define LIB_HTBL

#include "state.h"
#include "event.h"
#include "heap.h"
#include "config.h"


/**
 * state attribute definitions
 */
#define ATTR_CYAN     0
#define ATTR_BLUE     1
#define ATTR_PINK     2
#define ATTR_RED      3
#define ATTR_PRED     4
#define ATTR_EVT      5
#define ATTR_INDEX    6
#define ATTR_LOWLINK  7
#define ATTR_LIVE     8
#define ATTR_SAFE     9

typedef struct struct_htbl_t * htbl_t;

typedef uint64_t htbl_id_t;

typedef void (* htbl_fold_func_t)
(state_t, htbl_id_t, void *);


/**
 * @brief htbl_new
 */
htbl_t htbl_new
(bool_t use_system_heap,
 uint64_t hash_size,
 uint16_t no_workers,
 bool_t hash_compaction,
 uint32_t attrs);


/**
 * @brief htbl_default_new
 */
htbl_t htbl_default_new
();


/**
 * @brief htbl_free
 */
void htbl_free
(htbl_t tbl);


/**
 * @brief htbl_size
 */
uint64_t htbl_size
(htbl_t tbl);


/**
 * @brief htbl_contains
 */
bool_t htbl_contains
(htbl_t tbl,
 state_t s,
 htbl_id_t * id,
 hash_key_t * h);


/**
 * @brief htbl_insert
 */
void htbl_insert
(htbl_t tbl,
 state_t s,
 worker_id_t w,
 bool_t * is_new,
 htbl_id_t * id,
 hash_key_t * h);


/**
 * @brief htbl_insert_hashed
 */
void htbl_insert_hashed
(htbl_t tbl,
 state_t s,
 worker_id_t w,
 hash_key_t h,
 bool_t * is_new,
 htbl_id_t * id);


/**
 * @brief htbl_insert_serialised
 */
void htbl_insert_serialised
(htbl_t tbl,
 bit_vector_t s,
 uint16_t s_char_len,
 hash_key_t h,
 worker_id_t w,
 bool_t * is_new,
 htbl_id_t * id);


/**
 * @brief htbl_erase
 */
void htbl_erase
(htbl_t tbl,
 worker_id_t w,
 htbl_id_t id);


/**
 * @brief htbl_get
 */
state_t htbl_get
(htbl_t tbl,
 htbl_id_t id);


/**
 * @brief htbl_get_mem
 */
state_t htbl_get_mem
(htbl_t tbl,
 htbl_id_t id,
 heap_t heap);


/**
 * @brief htbl_get_serialised
 */
void htbl_get_serialised
(htbl_t tbl,
 htbl_id_t id,
 bit_vector_t * s,
 uint16_t * size,
 hash_key_t * h);


/**
 * @brief htbl_get_hash
 */
hash_key_t htbl_get_hash
(htbl_t tbl,
 htbl_id_t id);


/**
 * @brief htbl_get_attr
 */
uint64_t htbl_get_attr
(htbl_t tbl,
 htbl_id_t id,
 uint32_t attr);


/**
 * @brief htbl_set_attr
 */
void htbl_set_attr
(htbl_t tbl,
 htbl_id_t id,
 uint32_t attr,
 uint64_t val);


/**
 * @brief htbl_get_worker_attr
 */
uint64_t htbl_get_worker_attr
(htbl_t tbl,
 htbl_id_t id,
 uint32_t attr,
 worker_id_t w);


/**
 * @brief htbl_set_worker_attr
 */
void htbl_set_worker_attr
(htbl_t tbl,
 htbl_id_t id,
 uint32_t attr,
 worker_id_t w,
 uint64_t val);


/**
 * @brief htbl_get_any_cyan
 */
bool_t htbl_get_any_cyan
(htbl_t tbl,
 htbl_id_t id);


/**
 * @brief htbl_fold
 */
void htbl_fold
(htbl_t tbl,
 htbl_fold_func_t f,
 void * data);


/**
 * @brief htbl_reset
 */
void htbl_reset
(htbl_t tbl);


/**
 * @brief htbl_has_attr
 */
bool_t htbl_has_attr
(htbl_t tbl,
 uint32_t attr);


/**
 * @brief htbl_get_trace
 */
list_t htbl_get_trace
(htbl_t tbl,
 htbl_id_t id);

#endif