#ifndef HMALLOC_H
#define HMALLOC_H
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>


//#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)
#endif

#define PAGE_SZ         4096
#define MAX_ALLOCS      500

/* 
 * Small buckets: 16, 32, 64, 128, 256, 512, 1024
 * Large buckets (not YET supported) : >= 2048
 */

#define SMALL_BUCKET_PAGES    1
#define SMALL_BUCKET_MIN_SZ   16
#define SMALL_BUCKET_MAX_SZ   1024
#define SMALL_BUCKET_MASK     ~0xFFF // 1 PAGE

#define LARGE_BUCKET_MIN_SZ   2048
#define LARGE_BUCKET_PAGES    4             
//#define LARGE_BUCKET_MASK     0xFFFFFFFFC000 // 4 PAGES
//#define LARGE_BUCKET_MASK     TODO

#define FREELIST_REPLACE_BUCKET_THRESHOLD   3

/* 10 seconds */
#define FREELIST_QUARANTINE
#define FREELIST_QUARANTINE_WAIT            10


#define BUCKET_MAX_SZ   2048

typedef uint64_t bool;
#define true 1
#define false 0

struct list_head{
  struct list_head* next;
  struct list_head* prev;
};

struct bucket{
  struct list_head  buckets;
  /* Offset of the available alloc inside the bucket */
  uint16_t  offset;
  /* How many allocs are freed */
  uint8_t   freelist_count;
  uint8_t   freelist[MAX_ALLOCS];
  void*     allocs[];
};

/* Single allocations just contain the size of the alloc as metadata */
struct alloc{
  uint16_t size;
  void*   user[];
} __attribute__((packed));

void* malloc(size_t size);
void free(void* ptr);
bool __hmalloc_init();
void* __malloc(size_t size);
size_t __round_up_size(size_t sz);
size_t __bucket_max_allocations(size_t alloc_size);
struct bucket* __get_bucket(size_t alloc_size);
struct bucket* __get_bucket_from_alloc(struct alloc* target_alloc);
static inline void __update_master_ctrl_bucket(size_t alloc_size, struct bucket* b);
void* __fastpath_new_bucket_alloc(struct bucket* bucket, size_t alloc_size);
bool __is_freelist_available();
void __dump_bucket(struct bucket* b);
void __dump_alloc(struct alloc* a);

#endif // HMALLOC_H
