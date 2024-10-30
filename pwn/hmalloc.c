#include "hmalloc.h"

/* 
 * TODO: Single buckets are never unmmaped
 */

void** bucket_master_ctrl = NULL;
time_t freelist_quarantine_time = 0;

size_t __round_up_size(size_t size) {
  int power = 32;
  while(power < size)
    power*=2;
  return power;
}

void __dump_bucket(struct bucket* b){
  DEBUG_PRINT("[DEBUG]\tstruct bucket {\n");
  DEBUG_PRINT("\t\tnext: %p\n", b->buckets.next);
  DEBUG_PRINT("\t\tprev: %p\n", b->buckets.prev);
  DEBUG_PRINT("\t\toffset: %d\n", b->offset);
  DEBUG_PRINT("\t\tfreelist_count: %d\n", b->freelist_count);
  for(int i=0; i < b->freelist_count; i++)
    DEBUG_PRINT("\t\tfreelist[%d]: %d\n", i, b->freelist[i]);
  DEBUG_PRINT("\t\tallocs: %p\n", b->allocs);
  DEBUG_PRINT("\t}\n[/DEBUG]\n");
}

void __dump_alloc(struct alloc* a){
  DEBUG_PRINT("[DEBUG]\tstruct alloc {\n");
  DEBUG_PRINT("\t\tsize: %d\n", a->size);
  DEBUG_PRINT("\t\tuser: %p\n", a->user);
  DEBUG_PRINT("\t}\n[/DEBUG]\n");
}

bool __hmalloc_init(){
  bucket_master_ctrl = mmap(NULL, PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0);
  if(bucket_master_ctrl == MAP_FAILED){
    perror("mmap");
    return false;
  }
  memset(bucket_master_ctrl, 0x0, PAGE_SZ);
  return true;
}

struct bucket* __init_bucket(size_t size){
  struct bucket* bucket;
  DEBUG_PRINT("Initializing small bucket with %d pages\n", SMALL_BUCKET_PAGES);
  bucket = mmap(NULL, PAGE_SZ * SMALL_BUCKET_PAGES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0);
  memset(bucket, 0x0, PAGE_SZ * SMALL_BUCKET_PAGES);
  return bucket;
}

size_t __bucket_max_allocations(size_t alloc_size){
  return ((PAGE_SZ * SMALL_BUCKET_PAGES) - sizeof(struct bucket)) / alloc_size;
}

struct bucket* __get_bucket(size_t alloc_size){
  off_t off = alloc_size - sizeof(void*);
  return *(void**)((void*) bucket_master_ctrl + off);
}

struct bucket* __get_bucket_from_alloc(struct alloc* target_alloc){
  return (struct bucket*) ((uint64_t) target_alloc & SMALL_BUCKET_MASK);
}

static inline void __update_master_ctrl_bucket(size_t alloc_size, struct bucket* bck){
  off_t off = alloc_size - sizeof(void*);
  *(void**) ((void*) bucket_master_ctrl + off) = bck;
}

void* __fastpath_new_bucket_alloc(struct bucket* b, size_t alloc_size){
  struct alloc* alloc = (struct alloc*) b->allocs;
  alloc->size = alloc_size;
  b->offset++;
  return alloc->user;
}

bool __is_freelist_available(){
#ifdef FREELIST_QUARANTINE
  if((time(NULL) - freelist_quarantine_time) < FREELIST_QUARANTINE_WAIT){
    return false;
  }
  return true;
#endif
  return true;
}

void* malloc(size_t size){
  void* alloc;
  if(!bucket_master_ctrl){
    if(!__hmalloc_init())
      return NULL;
  }
  alloc = __malloc(size);
  DEBUG_PRINT("malloc(%zu) = %p\n", size, alloc);
  return alloc;
}

void* __malloc(size_t input_size){
  size_t  alloc_size;
  off_t   bucket_offset;
  off_t   offset;
  struct bucket* current_bucket;
  struct bucket* old_current_bucket;
  struct bucket* linked_bucket;
  struct alloc* alloc;
  uint8_t freelist_count = 0;
  size_t  bucket_max_allocs = 0;

  if(input_size == 0)
    return NULL;

  DEBUG_PRINT("\n+++++++++ MALLOC +++++++++\n");
  alloc_size = __round_up_size(input_size + sizeof(struct alloc));
  if(alloc_size > SMALL_BUCKET_MAX_SZ){
    /* TODO: Not implemented */
    return NULL;
  }

  current_bucket = __get_bucket(alloc_size);
  if(current_bucket == NULL){
    current_bucket = __init_bucket(alloc_size);
    if(current_bucket == NULL){
      perror("__init_bucket: mmap");
      return NULL;
    }
    DEBUG_PRINT("[DEBUG] Bucket %zu initialized: %p\n", alloc_size, current_bucket);
    __update_master_ctrl_bucket(alloc_size, current_bucket);
  }
  DEBUG_PRINT("[DEBUG] current_bucket (%zu) = %p\n", alloc_size, current_bucket);
  __dump_bucket(current_bucket);

  freelist_count = current_bucket->freelist_count;
  if(freelist_count && __is_freelist_available()){
    offset = current_bucket->freelist[(current_bucket->freelist_count - 1)];
    alloc = (void*) current_bucket->allocs + ( offset * alloc_size);
    current_bucket->freelist_count--;
    DEBUG_PRINT("[DEBUG] Alloc retrieved from the freelist with offset %ld: %p\n", offset, alloc);
    return alloc->user;
  }

  bucket_max_allocs = __bucket_max_allocations(alloc_size);
  bucket_offset = current_bucket->offset;
  if(bucket_offset != bucket_max_allocs){
    if(bucket_offset > bucket_max_allocs){
      DEBUG_PRINT("!! bucket_offset should never exceeds its maximum allocations!\n");
      return NULL;
    }
    alloc = (struct alloc*) ((void*) current_bucket->allocs + (bucket_offset * alloc_size));
    alloc->size = alloc_size;
    current_bucket->offset++;
    __dump_alloc(alloc);
    return alloc->user;
  }

  if(current_bucket->buckets.prev){
    linked_bucket = (struct bucket*) current_bucket->buckets.prev;
    while(linked_bucket != NULL){
      DEBUG_PRINT("[DEBUG] linked bucket %p\n", linked_bucket);
      __dump_bucket(linked_bucket);

      if(linked_bucket->freelist_count == 0){
        linked_bucket = (struct bucket*) linked_bucket->buckets.prev;
        continue;
      }
      
      offset = linked_bucket->freelist[(linked_bucket->freelist_count - 1)];
      alloc = (void*) linked_bucket->allocs + ( offset * alloc_size);
      linked_bucket->freelist_count--;

      if(linked_bucket->freelist_count >= FREELIST_REPLACE_BUCKET_THRESHOLD){
        __update_master_ctrl_bucket(alloc_size, linked_bucket);
      }

      DEBUG_PRINT("[DEBUG] Returing alloc %p from bucket %p with traversing\n", alloc, linked_bucket);
      return alloc->user;
    }
  }


  if(current_bucket->buckets.next){
    linked_bucket = (struct bucket*) current_bucket->buckets.next;
    while(linked_bucket != NULL){
      DEBUG_PRINT("[DEBUG] linked bucket %p\n", linked_bucket);
      __dump_bucket(linked_bucket);

      if(linked_bucket->freelist_count == 0){
        linked_bucket = (struct bucket*) linked_bucket->buckets.next;
        continue;
      }
      
      offset = linked_bucket->freelist[(linked_bucket->freelist_count - 1)];
      alloc = (void*) linked_bucket->allocs + ( offset * alloc_size);
      linked_bucket->freelist_count--;

      if(linked_bucket->freelist_count >= FREELIST_REPLACE_BUCKET_THRESHOLD){
        DEBUG_PRINT("[DEBUG] Master bucket updated with %p\n", linked_bucket);
        __update_master_ctrl_bucket(alloc_size, linked_bucket);
      }
      DEBUG_PRINT("[DEBUG] Returing alloc %p from bucket %p with traversing\n", alloc, linked_bucket);
      return alloc->user;
    }
  }

  old_current_bucket = current_bucket;
  current_bucket = __init_bucket(alloc_size);
  if(current_bucket == NULL){
      perror("__init_bucket: mmap");
      return NULL;
  }
  DEBUG_PRINT("[DEBUG] New bucket %zu initialized: %p\n", alloc_size, current_bucket);

  old_current_bucket->buckets.next = (struct list_head*) &current_bucket->buckets.next;
  current_bucket->buckets.prev = (struct list_head*) &old_current_bucket->buckets.next;

  __update_master_ctrl_bucket(alloc_size, current_bucket);

  alloc = __fastpath_new_bucket_alloc(current_bucket, alloc_size);
  __dump_alloc(alloc);
  __dump_bucket(current_bucket);
  return alloc;
}

void free(void* ptr){ 
  struct alloc* current_alloc;
  struct bucket* current_bucket;
  uint16_t offset;

  if(ptr == NULL){ return; }
  DEBUG_PRINT("\n+++++++++ FREE +++++++++\n");

  current_alloc = ptr - sizeof(struct alloc);
  DEBUG_PRINT("[DEBUG] Freeing alloc %p\n", current_alloc);

  if(current_alloc->size > SMALL_BUCKET_MAX_SZ) {
    return;
  }

  current_bucket = __get_bucket_from_alloc(current_alloc);
  DEBUG_PRINT("[DEBUG] Retrieved current bucket for %d is: %p\n", current_alloc->size, current_bucket);

  offset = ( (void*) current_alloc - (void*) current_bucket->allocs ) / current_alloc->size;
  current_bucket->freelist[current_bucket->freelist_count] = offset;
  current_bucket->freelist_count++;
  freelist_quarantine_time = time(NULL);
  __dump_bucket(current_bucket);
}
