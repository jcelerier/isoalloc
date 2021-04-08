/* iso_alloc_internal.h - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */
#pragma once

#ifndef CPP_SUPPORT
#define _GNU_SOURCE
#endif

#if !__x86_64__
#pragma message "IsoAlloc is untested and unsupported on 32 bit platforms"
#endif

#if __linux__
#include <byteswap.h>
#define ENVIRON environ
#elif __APPLE__
#include <libkern/OSByteOrder.h>
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#define ENVIRON NULL
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#if THREAD_SUPPORT
#include <pthread.h>
#include <stdatomic.h>
#endif

#if UNINIT_READ_SANITY
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

#if defined(CPU_PIN) && defined(_GNU_SOURCE) && defined(__linux__)
#include <sched.h>
#endif

#if HEAP_PROFILER
#include <fcntl.h>
#endif

#ifndef MADV_DONTNEED
#define MADV_DONTNEED POSIX_MADV_DONTNEED
#endif

#if ENABLE_ASAN
#include <sanitizer/asan_interface.h>

#define POISON_ZONE(zone)                                                     \
    if(IS_POISONED_RANGE(zone->user_pages_start, ZONE_USER_SIZE) == 0) {      \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, ZONE_USER_SIZE);    \
    }                                                                         \
    if(IS_POISONED_RANGE(zone->bitmap_start, zone->bitmap_size) == 0) {       \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, zone->bitmap_size); \
    }

#define UNPOISON_ZONE(zone)                                                  \
    if(IS_POISONED_RANGE(zone->user_pages_start, ZONE_USER_SIZE) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(zone->user_pages_start, ZONE_USER_SIZE); \
    }                                                                        \
    if(IS_POISONED_RANGE(zone->bitmap_start, zone->bitmap_size) != 0) {      \
        ASAN_UNPOISON_MEMORY_REGION(zone->bitmap_start, zone->bitmap_size);  \
    }

#define POISON_ZONE_CHUNK(zone, ptr)                      \
    if(IS_POISONED_RANGE(ptr, zone->chunk_size) == 0) {   \
        ASAN_POISON_MEMORY_REGION(ptr, zone->chunk_size); \
    }

#define UNPOISON_ZONE_CHUNK(zone, ptr)                      \
    if(IS_POISONED_RANGE(ptr, zone->chunk_size) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(ptr, zone->chunk_size); \
    }

#define POISON_BIG_ZONE(zone)                                          \
    if(IS_POISONED_RANGE(zone->user_pages_start, zone->size) == 0) {   \
        ASAN_POISON_MEMORY_REGION(zone->user_pages_start, zone->size); \
    }

#define UNPOISON_BIG_ZONE(zone)                                          \
    if(IS_POISONED_RANGE(zone->user_pages_start, zone->size) != 0) {     \
        ASAN_UNPOISON_MEMORY_REGION(zone->user_pages_start, zone->size); \
    }

#define IS_POISONED_RANGE(ptr, size) \
    __asan_region_is_poisoned(ptr, size)
#else
#define POISON_ZONE(zone)
#define UNPOISON_ZONE(zone)
#define POISON_ZONE_CHUNK(ptr, zone)
#define UNPOISON_ZONE_CHUNK(ptr, zone)
#define POISON_BIG_ZONE(zone)
#define UNPOISON_BIG_ZONE(zone)
#define IS_POISONED_RANGE(ptr, size) 0
#endif

#define OK 0
#define ERR -1

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/* GCC complains if your constructor priority is
 * 0-100 but Clang does not. We need the lowest
 * priority constructor for MALLOC_HOOK */
#define FIRST_CTOR 101
#define LAST_DTOR 65535

#define INTERNAL_HIDDEN __attribute__((visibility("hidden")))

#if UNIT_TESTING
#define EXTERNAL_API __attribute__((visibility("default")))
#endif

#if PERF_TEST_BUILD
#define INLINE
#define FLATTEN
#else
#define INLINE __attribute__((always_inline))
#define FLATTEN __attribute__((flatten))
#endif

#if DEBUG
#define LOG(msg, ...) \
    _iso_alloc_printf(STDOUT_FILENO, "[LOG][%d](%s:%d %s()) " msg "\n", getpid(), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);
#else
#define LOG(msg, ...)
#endif

#define LOG_AND_ABORT(msg, ...)                                                                                                          \
    _iso_alloc_printf(STDOUT_FILENO, "[ABORTING][%d](%s:%d %s()) " msg "\n", getpid(), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    abort();

/* The number of bits in the bitmap that correspond
 * to a user chunk. We use 2 bits:
 *  00 free, never used
 *  10 currently in use
 *  01 was used, now free
 *  11 canary chunk / permanently free'd */
#define BITS_PER_CHUNK 2
#define BITS_PER_CHUNK_SHIFT 1

#define BITS_PER_BYTE 8
#define BITS_PER_BYTE_SHIFT 3

#define BITS_PER_QWORD 64
#define BITS_PER_QWORD_SHIFT 6

#define CANARY_SIZE 8

#define CANARY_COUNT_DIV 100

#define ALIGNMENT 8

#define WHICH_BIT(bit_slot) \
    (bit_slot & (BITS_PER_QWORD - 1))

#define IS_ALIGNED(v) \
    (v & (ALIGNMENT - 1))

#define GET_BIT(n, k) \
    (n >> k) & 1UL

#define SET_BIT(n, k) \
    n |= 1UL << k;

#define UNSET_BIT(n, k) \
    n &= ~(1UL << k);

#define ALIGN_SZ_UP(n) \
    ((((n) + (ALIGNMENT) -1) / (ALIGNMENT)) * (ALIGNMENT))

#define ALIGN_SZ_DOWN(n) \
    ((((n) + (ALIGNMENT) -1) / (ALIGNMENT)) * (ALIGNMENT)) - ALIGNMENT

#define ROUND_UP_PAGE(n) \
    ((((n) + (g_page_size) -1) / (g_page_size)) * (g_page_size))

#define ROUND_DOWN_PAGE(n) \
    (ROUND_UP_PAGE(n) - g_page_size)

#define GET_MAX_BITMASK_INDEX(zone) \
    (zone->bitmap_size >> 3)

#define MASK_ZONE_PTRS(zone) \
    MASK_BITMAP_PTRS(zone);  \
    MASK_USER_PTRS(zone);

#define UNMASK_ZONE_PTRS(zone) \
    MASK_ZONE_PTRS(zone);

#define MASK_BITMAP_PTRS(zone) \
    zone->bitmap_start = (void *) ((uintptr_t) zone->bitmap_start ^ (uintptr_t) zone->pointer_mask);

#define MASK_USER_PTRS(zone) \
    zone->user_pages_start = (void *) ((uintptr_t) zone->user_pages_start ^ (uintptr_t) zone->pointer_mask);

#define MASK_BIG_ZONE_NEXT(bnp) \
    UNMASK_BIG_ZONE_NEXT(bnp)

#define UNMASK_BIG_ZONE_NEXT(bnp) \
    ((iso_alloc_big_zone *) ((uintptr_t) _root->big_zone_next_mask ^ (uintptr_t) bnp))

#define GET_CHUNK_COUNT(zone) \
    (ZONE_USER_SIZE / zone->chunk_size)

/* This is the maximum number of zones iso_alloc can
 * create. This is a completely arbitrary number but
 * it does correspond to the size of the _root.zones
 * array that lives in global memory. Currently the
 * iso_alloc_zone structure is roughly 1096 bytes so
 * this allocates 4489216 bytes (~4.4 MB) */
#define MAX_ZONES 4096

/* Each user allocation zone we make is 8mb in size.
 * With MAX_ZONES at 4096 this means we top out at
 * about 32 gb of heap. We don't allocate that many
 * zones by default but its the maximum we could in
 * any one process runtime */
#define ZONE_USER_SIZE 8388608

/* This is the largest divisor of ZONE_USER_SIZE we can
 * get from (BITS_PER_QWORD/BITS_PER_CHUNK). Anything
 * above this size will need to go through the big
 * mapping code path */
#define SMALL_SZ_MAX 262144

/* Cap our big zones at 4GB of memory */
#define BIG_SZ_MAX 4294967296

#define WASTED_SZ_MULTIPLIER 8
#define WASTED_SZ_MULTIPLIER_SHIFT 3

#define BIG_ZONE_META_DATA_PAGE_COUNT 3
#define BIG_ZONE_USER_PAGE_COUNT 2
#define BIG_ZONE_USER_PAGE_COUNT_SHIFT 1

/* We allocate (1) zone at startup for common sizes.
 * Each of these default zones is ZONE_USER_SIZE bytes
 * so ZONE_8192 holds less chunks than ZONE_128 for
 * example. These are inexpensive for us to create */
#define ZONE_16 16
#define ZONE_32 32
#define ZONE_64 64
#define ZONE_128 128
#define ZONE_256 256
#define ZONE_512 512
#define ZONE_1024 1024
#define ZONE_2048 2048
#define ZONE_4096 4096
#define ZONE_8192 8192

#define MAX_DEFAULT_ZONE_SZ ZONE_8192

/* The size of our bit slot freelist */
#define BIT_SLOT_CACHE_SZ 128

/* The size of the thread cache */
#define THREAD_ZONE_CACHE_SZ 8

#define MEGABYTE_SIZE 1000000

/* This byte value will overwrite the contents
 * of all free'd user chunks */
#define POISON_BYTE 0xde

#define CANARY_VALIDATE_MASK 0xffffffffffffff00

#define BAD_BIT_SLOT -1

/* Calculate the user pointer given a zone and a bit slot */
#define POINTER_FROM_BITSLOT(zone, bit_slot) \
    ((void *) zone->user_pages_start + ((bit_slot / BITS_PER_CHUNK) * zone->chunk_size));

#if THREAD_SUPPORT
extern atomic_flag root_busy_flag;
extern atomic_flag big_zone_busy_flag;

#define LOCK_ROOT() \
    do {            \
    } while(atomic_flag_test_and_set(&root_busy_flag));

#define UNLOCK_ROOT() \
    atomic_flag_clear(&root_busy_flag);

#define LOCK_BIG_ZONE() \
    do {                \
    } while(atomic_flag_test_and_set(&big_zone_busy_flag));

#define UNLOCK_BIG_ZONE() \
    atomic_flag_clear(&big_zone_busy_flag);
#else
#define LOCK_ROOT()
#define UNLOCK_ROOT()
#define LOCK_BIG_ZONE()
#define UNLOCK_BIG_ZONE()
#endif

/* This global is used by the page rounding macros.
 * The value stored in _root->system_page_size is
 * preferred but we need this to setup the root. */
extern uint32_t g_page_size;

/* iso_alloc makes a number of default zones for common
 * allocation sizes. Allocations are 'first fit' up until
 * ZONE_1024 at which point a new zone is created for that
 * specific size request. You can create additional startup
 * profile by adjusting the next few lines below. */
extern uint32_t _default_zone_count;

#if SMALL_MEM_STARTUP
/* ZONE_USER_SIZE * sizeof(default_zones) = ~32 mb */
#define SMALLEST_ZONE ZONE_64
static uint64_t default_zones[] = {ZONE_64, ZONE_256, ZONE_512, ZONE_1024};
#else
/* ZONE_USER_SIZE * sizeof(default_zones) = ~80 mb */
#define SMALLEST_ZONE ZONE_16
static uint64_t default_zones[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128, ZONE_256, ZONE_512,
                                   ZONE_1024, ZONE_2048, ZONE_4096, ZONE_8192};
#endif

/* If you have specific allocation pattern requirements
 * then you want a custom set of default zones. These
 * example are provided to get you started. Zone creation
 * is not limited to these sizes, this array just specifies
 * the default zones that will be created at startup time.
 * Each of these examples is 4 default zones which will
 * consume 32mb of memory in total */
#if 0
#define SMALLEST_ZONE ZONE_16
static uint64_t default_zones[] = {ZONE_16, ZONE_16, ZONE_16, ZONE_16};
#endif

#if 0
#define SMALLEST_ZONE ZONE_16
static uint64_t default_zones[] = {ZONE_16, ZONE_32, ZONE_64, ZONE_128};
#endif

#if 0
#define SMALLEST_ZONE ZONE_256
static uint64_t default_zones[] = {ZONE_256, ZONE_256, ZONE_512, ZONE_512};
#endif

#if 0
#define SMALLEST_ZONE ZONE_512
static uint64_t default_zones[] = {ZONE_512, ZONE_512, ZONE_512, ZONE_1024};
#endif

typedef uint64_t bit_slot_t;
typedef int64_t bitmap_index_t;

/* The API allows for consumers of the library to
 * create their own zones for unique data/object
 * types. This structure allows the caller to define
 * which security mitigations should be applied to
 * all allocations within the zone */
typedef struct {
    bool random_allocation_pattern;
    bool adjacent_canary_verification_on_alloc;
    bool adjacent_canary_verification_on_free;
    bool clear_chunk_on_free;
    bool double_free_detection;
} iso_alloc_zone_configuration;

typedef struct {
    void *user_pages_start;     /* Start of the pages backing this zone */
    void *bitmap_start;         /* Start of the bitmap */
    int64_t next_free_bit_slot; /* The last bit slot returned by get_next_free_bit_slot */
    uint64_t canary_secret;     /* Each zone has its own canary secret */
    uint64_t pointer_mask;      /* Each zone has its own pointer protection secret */
    uint32_t chunk_size;        /* Size of chunks managed by this zone */
    uint32_t bitmap_size;       /* Size of the bitmap in bytes */
    bool internally_managed;    /* Zones can be managed by iso_alloc or custom */
    bool is_full;               /* Indicates whether this zone is full to avoid expensive free bit slot searches */
    uint16_t index;             /* Zone index */
#if CPU_PIN
    uint8_t cpu_core; /* What CPU core this zone is pinned to */
#endif
    /* These indexes must be bumped to uint16_t if BIT_SLOT_CACHE_SZ >= MAX_UINT8 */
    uint8_t free_bit_slot_cache_index;                     /* Tracks how many entries in the cache are filled */
    uint8_t free_bit_slot_cache_usable;                    /* The oldest members of the free cache are served first */
    bit_slot_t free_bit_slot_cache[BIT_SLOT_CACHE_SZ + 1]; /* A cache of bit slots that point to freed chunks */
} __attribute__((aligned(sizeof(int64_t)))) iso_alloc_zone;

#if THREAD_SUPPORT && THREAD_ZONE_CACHE
/* Each thread gets a local cache of the most recently
 * used zones. This can greatly speed up allocations
 * if your threads are reusing the same zones. This
 * cache is first in last out, and is populated during
 * both alloc and free operations */
typedef struct {
    size_t chunk_size;
    iso_alloc_zone *zone;
} __attribute__((aligned(sizeof(int64_t)))) _tzc;

static __thread _tzc thread_zone_cache[THREAD_ZONE_CACHE_SZ];
static __thread size_t thread_zone_cache_count;
#endif

/* Meta data for big allocations are allocated near the
 * user pages themselves but separated via guard pages.
 * This meta data is stored at a random offset from the
 * beginning of the page it resides on */
typedef struct iso_alloc_big_zone {
    uint64_t canary_a;
    bool free;
    uint64_t size;
    void *user_pages_start;
    struct iso_alloc_big_zone *next;
    uint64_t canary_b;
} __attribute__((aligned(sizeof(int64_t)))) iso_alloc_big_zone;

/* There is only one iso_alloc root per-process.
 * It contains an array of zone structures. Each
 * Zone represents a number of contiguous pages
 * that hold chunks containing caller data */
typedef struct {
    uint32_t zones_used;
    uint32_t system_page_size;
    void *guard_below;
    void *guard_above;
    uint64_t zone_handle_mask;
    uint64_t big_zone_next_mask;
    uint64_t big_zone_canary_secret;
    iso_alloc_big_zone *big_zone_head;
    iso_alloc_zone zones[MAX_ZONES];
} __attribute__((aligned(sizeof(int64_t)))) iso_alloc_root;

#if HEAP_PROFILER
#define PROFILER_ODDS 10000
#define HG_SIZE 65535
#define CHUNK_USAGE_THRESHOLD 75
#define PROFILER_ENV_STR "ISO_ALLOC_PROFILER_FILE_PATH"
#define PROFILER_FILE_PATH "iso_alloc_profiler.data"
#define PROFILER_STACK_DEPTH 2

uint64_t _allocation_count;
uint64_t _sampled_count;

int32_t profiler_fd;
uint32_t caller_hg[HG_SIZE];

typedef struct {
    uint64_t total;
    uint64_t count;
} zone_profiler_map_t;

zone_profiler_map_t _zone_profiler_map[SMALL_SZ_MAX];
#endif

#if ALLOC_SANITY
#define SANITY_SAMPLE_ODDS 10000
#define MAX_SANE_SAMPLES 1024
#define SANE_CACHE_SIZE 65535
#define SANE_CACHE_IDX(p) (((uint64_t) p >> 8) & 0xffff)

#if THREAD_SUPPORT
extern atomic_flag sane_cache_flag;

#define LOCK_SANITY_CACHE() \
    do {                    \
    } while(atomic_flag_test_and_set(&sane_cache_flag));

#define UNLOCK_SANITY_CACHE() \
    atomic_flag_clear(&sane_cache_flag);
#else
#define LOCK_SANITY_CACHE()
#define UNLOCK_SANITY_CACHE()
#endif

#if UNINIT_READ_SANITY
extern pthread_t _page_fault_thread;
extern struct uffdio_api _uffd_api;
extern int64_t _uf_fd;
#endif

extern int32_t _sane_sampled;
extern uint8_t _sane_cache[SANE_CACHE_SIZE];

typedef struct {
    void *guard_below;
    void *guard_above;
    void *address;
    size_t size;
    size_t orig_size;
} _sane_allocation_t;

extern _sane_allocation_t _sane_allocations[MAX_SANE_SAMPLES];

#endif

/* The global root */
extern iso_alloc_root *_root;

INTERNAL_HIDDEN INLINE void check_big_canary(iso_alloc_big_zone *big);
INTERNAL_HIDDEN INLINE void check_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE void iso_clear_user_chunk(uint8_t *p, size_t size);
INTERNAL_HIDDEN INLINE void fill_free_bit_slot_cache(iso_alloc_zone *zone);
INTERNAL_HIDDEN INLINE void insert_free_bit_slot(iso_alloc_zone *zone, int64_t bit_slot);
INTERNAL_HIDDEN INLINE void write_canary(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE int64_t check_canary_no_abort(iso_alloc_zone *zone, void *p);
INTERNAL_HIDDEN INLINE uint64_t _get_backtrace_hash(uint32_t frames);
INTERNAL_HIDDEN INLINE size_t next_pow2(size_t sz);
INTERNAL_HIDDEN INLINE void flush_thread_zone_cache(void);
INTERNAL_HIDDEN FLATTEN void iso_free_chunk_from_zone(iso_alloc_zone *zone, void *p, bool permanent);
INTERNAL_HIDDEN iso_alloc_zone *is_zone_usable(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_fit(size_t size);
INTERNAL_HIDDEN iso_alloc_zone *iso_new_zone(size_t size, bool internal);
INTERNAL_HIDDEN iso_alloc_zone *_iso_new_zone(size_t size, bool internal);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_bitmap_range(void *p);
INTERNAL_HIDDEN iso_alloc_zone *iso_find_zone_range(void *p);
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot_slow(iso_alloc_zone *zone);
INTERNAL_HIDDEN bit_slot_t iso_scan_zone_free_slot(iso_alloc_zone *zone);
INTERNAL_HIDDEN bit_slot_t get_next_free_bit_slot(iso_alloc_zone *zone);
INTERNAL_HIDDEN iso_alloc_root *iso_alloc_new_root(void);
INTERNAL_HIDDEN bool iso_does_zone_fit(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN void create_canary_chunks(iso_alloc_zone *zone);
INTERNAL_HIDDEN void iso_alloc_initialize_global_root(void);
INTERNAL_HIDDEN void mprotect_pages(void *p, size_t size, int32_t protection);
INTERNAL_HIDDEN void create_guard_page(void *p);
INTERNAL_HIDDEN void *mmap_rw_pages(size_t size, bool populate);
INTERNAL_HIDDEN void _iso_alloc_destroy_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void _verify_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void _verify_all_zones(void);
INTERNAL_HIDDEN void verify_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void verify_all_zones(void);
INTERNAL_HIDDEN void _iso_free(void *p, bool permanent);
INTERNAL_HIDDEN void iso_free_big_zone(iso_alloc_big_zone *big_zone, bool permanent);
INTERNAL_HIDDEN void _iso_alloc_protect_root(void);
INTERNAL_HIDDEN void _iso_alloc_unprotect_root(void);
INTERNAL_HIDDEN void _unmap_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN void *_iso_big_alloc(size_t size);
INTERNAL_HIDDEN void *_iso_alloc(iso_alloc_zone *zone, size_t size);
INTERNAL_HIDDEN void *_iso_alloc_bitslot_from_zone(bit_slot_t bitslot, iso_alloc_zone *zone);
INTERNAL_HIDDEN void *_iso_calloc(size_t nmemb, size_t size);
INTERNAL_HIDDEN void *_iso_alloc_ptr_search(void *n);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_leak_detector(iso_alloc_zone *zone, bool profile);
INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks_in_zone(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_detect_leaks(void);
INTERNAL_HIDDEN uint64_t _iso_alloc_zone_mem_usage(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t __iso_alloc_zone_mem_usage(iso_alloc_zone *zone);
INTERNAL_HIDDEN uint64_t _iso_alloc_big_zone_mem_usage();
INTERNAL_HIDDEN uint64_t __iso_alloc_big_zone_mem_usage();
INTERNAL_HIDDEN uint64_t _iso_alloc_mem_usage(void);
INTERNAL_HIDDEN uint64_t __iso_alloc_mem_usage(void);
INTERNAL_HIDDEN uint64_t rand_uint64(void);
INTERNAL_HIDDEN size_t _iso_chunk_size(void *p);
INTERNAL_HIDDEN int8_t *_fmt(uint64_t n, uint32_t base);
INTERNAL_HIDDEN void _iso_alloc_printf(int32_t fd, const char *f, ...);
INTERNAL_HIDDEN void _initialize_profiler(void);
INTERNAL_HIDDEN void _iso_alloc_profile(void);

#if ALLOC_SANITY
#if UNINIT_READ_SANITY
INTERNAL_HIDDEN void *_page_fault_thread_handler(void *uf_fd);
#endif
INTERNAL_HIDDEN void *_iso_alloc_sample(size_t size);
INTERNAL_HIDDEN int32_t _iso_alloc_free_sane_sample(void *p);
INTERNAL_HIDDEN int32_t _remove_from_sane_trace(void *p);
INTERNAL_HIDDEN _sane_allocation_t *_get_sane_alloc(void *p);
#endif

#if EXPERIMENTAL
INTERNAL_HIDDEN void _iso_alloc_search_stack(uint8_t *stack_start);
#endif

#if UNIT_TESTING
EXTERNAL_API iso_alloc_root *_get_root(void);
#endif
