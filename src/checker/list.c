#include "list.h"

struct struct_list_node_t {
  void * item;
  struct struct_list_node_t * prev;
  struct struct_list_node_t * next;
};
typedef struct struct_list_node_t struct_list_node_t;
typedef struct_list_node_t * list_node_t;

struct struct_list_t {
  heap_t heap;
  list_size_t no_items;
  list_node_t first;
  list_node_t last;
  list_node_free_func_t free_func;
  uint32_t sizeof_item;
};

typedef struct struct_list_t struct_list_t;

list_t list_new
(heap_t heap,
 uint32_t sizeof_item,
 list_node_free_func_t free_func) {
  list_t result;

  result = mem_alloc(heap, sizeof(struct_list_t));
  result->heap = heap;
  result->no_items = 0;
  result->first = NULL;
  result->last = NULL;
  result->sizeof_item = sizeof_item;
  result->free_func = free_func;
  return result;
}

void list_free
(list_t list) {
  list_node_t ptr = list->first, next;

  while(ptr) {
    next = ptr->next;
    if(list->free_func) {
      list->free_func(ptr->item, list->heap);
    }
    mem_free(list->heap, ptr->item);
    mem_free(list->heap, ptr);
    ptr = next;
  }
  mem_free(list->heap, list);
}

char list_is_empty
(list_t list) {
  return 0 == list->no_items;
}

list_size_t list_size
(list_t list) {
  return list->no_items;
}

void * list_nth
(list_t list,
 list_index_t n) {
  list_node_t ptr = list->first, next;
  list_index_t i = n;
  
  while(i) {
    assert(ptr);
    ptr = ptr->next;
    i --;
  }
  assert(ptr);
  return ptr->item;
}

void list_app
(list_t list,
 list_node_app_func_t app_func,
 void * data) {
  list_node_t ptr = list->first, next;

  while(ptr) {
    app_func(ptr->item, data);
    ptr = ptr->next;
  }
}

void list_append
(list_t list,
 void * item) {
  list_node_t ptr = mem_alloc(list->heap, sizeof(struct_list_node_t));

  ptr->item = mem_alloc(list->heap, list->sizeof_item);
  memcpy(ptr->item, item, list->sizeof_item);
  ptr->next = NULL;
  if(!list->first) {
    ptr->prev = NULL;
    list->first = ptr;
    list->last = ptr;
  } else {
    ptr->prev = list->last;
    list->last->next = ptr;
    list->last = ptr;
  }
  list->no_items ++;
}

void list_pick_last
(list_t list,
 void * item) {
  assert(list->last);
  if(item) {
    memcpy(item, list->last->item, list->sizeof_item);
  }
  mem_free(list->heap, list->last->item);
  if(list->first == list->last) {
    mem_free(list->heap, list->first);
    list->first = NULL;
    list->last = NULL;
  } else {
    list->last = list->last->prev;
    mem_free(list->heap, list->last->next);
    list->last->next = NULL;
  }
  list->no_items --;  
}

void list_pick_first
(list_t list,
 void * item) {
  assert(list->first);
  if(item) {
    memcpy(item, list->first->item, list->sizeof_item);
  }
  mem_free(list->heap, list->first->item);
  if(list->first == list->last) {
    mem_free(list->heap, list->first);
    list->first = NULL;
    list->last = NULL;
  } else {
    list->first = list->first->next;
    mem_free(list->heap, list->first->prev);
    list->first->prev = NULL;
  }
  list->no_items --;
}

void list_pick_random
(list_t list,
 void * item,
 rseed_t * seed) {
  list_node_t ptr = list->first;
  uint32_t rnd = random_int(seed) % list->no_items;

  while(rnd) {
    ptr = ptr->next;
    rnd --;
  }
  if(item) {
    memcpy(item, ptr->item, list->sizeof_item);
  }
  mem_free(list->heap, ptr->item);
  if(ptr->prev) {
    ptr->prev->next = ptr->next;
  } else {
    list->first = ptr->next;
  }
  if(ptr->next) {
    ptr->next->prev = ptr->prev;
  } else {
    list->last = ptr->next;
  }
  mem_free(list->heap, ptr);
  list->no_items --;
}

list_iter_t list_get_iterator
(list_t l) {
  return l->first;
}

list_iter_t list_iterator_next
(list_iter_t it) {
  return it->next;
}

char list_iterator_at_end
(list_iter_t it) {
  if(it) {
    return FALSE;
  } else {
    return TRUE;
  }
}

void * list_iterator_item
(list_iter_t it) {
  return it->item;
}
