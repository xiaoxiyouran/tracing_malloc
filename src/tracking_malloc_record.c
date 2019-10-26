#include "tracking_malloc.h"
#include "tracking_malloc_record.h"
#include "tracking_malloc_hashmap.h"
#include "tracking_malloc_stacktrace.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>

struct record_data {
    struct hashmap* hashmap;
    pid_t pid;
};

struct record_data g_record = {NULL, 0};
__thread int record_disable_flag = 0;

__attribute__((always_inline)) 
static inline int _record_init() 
{
    pid_t pid =  getpid();
    char file_name[FILENAME_MAX];
    sprintf(file_name, "/tmp/%s.%d", "tracking.malloc", pid);
    struct hashmap* hashmap = hashmap_create(file_name, 1024 * 1024);

    if (!hashmap)
        return -1;

    g_record.hashmap = hashmap;
    g_record.pid = pid;
    return 0;
}

int record_init()
{
    record_disable_flag = 1;
    int ret = _record_init();
    record_disable_flag = 0;
    return ret;
}

static void _backup_proc_maps()
{
    pid_t pid = getpid();
    FILE* src = NULL;
    FILE* dst = NULL;
    char file_name_buffer[FILENAME_MAX];

    sprintf(file_name_buffer, "/proc/%d/maps", pid);
    src = fopen(file_name_buffer, "rb");

    sprintf(file_name_buffer, "/tmp/%s.%d.maps", "tracking.malloc", pid);
    dst = fopen(file_name_buffer, "wb");

    if (!src || !dst) 
        goto cleanup;

    while (1)
	{
        int bytes_read_len = fread(file_name_buffer, 1, sizeof(file_name_buffer), src);
        if (bytes_read_len <= 0)
            break;

        int bytes_write_len = fwrite(file_name_buffer, bytes_read_len, 1, dst);
        if (bytes_read_len != 1)
            break;
	}
    
cleanup:
    if (src)
        fclose(src);
    if (dst)
        fclose(dst);
}

static int _record_debug(struct hashmap_value* hashmap_value) 
{
    printf("ptr:%p, size:%lld\n", (void*)hashmap_value->pointer, hashmap_value->alloc_size);
    return 0;
}

__attribute__((always_inline)) 
static inline void _record_uninit() 
{
    /* 
    hashmap_traverse(g_record.hashmap, _record_debug); 
    */
    _backup_proc_maps();
    hashmap_destory(g_record.hashmap);
    g_record.hashmap = NULL;
    g_record.pid = 0;
}

void record_uninit()
{
    record_disable_flag = 1;
    _record_uninit();
    record_disable_flag = 0;
}

__attribute__((always_inline)) 
static inline int _record_alloc(int add_flag, void* ptr, size_t size) 
{
    void* buffer[STACK_TRACE_DEPTH + STACK_TRACE_SKIP];
    memset(buffer, 0, sizeof(buffer));
    int count = stack_backtrace(buffer, sizeof(buffer) / sizeof(buffer[0]), STACK_TRACE_SKIP); 
    if (count < STACK_TRACE_SKIP)
        return -1;

    struct hashmap_value* hashmap_value;
    if (add_flag)
        hashmap_value = hashmap_add(g_record.hashmap, (intptr_t)ptr);
    else
        hashmap_value = hashmap_get(g_record.hashmap, (intptr_t)ptr);

    if (!hashmap_value)
        return -2;

    hashmap_value->alloc_time = (int64_t)time(NULL);
    hashmap_value->alloc_size = size;
    for (int i = 0; i < STACK_TRACE_DEPTH; ++i) {
        hashmap_value->address[i] = (int64_t)buffer[i + STACK_TRACE_SKIP];
    }
}

int record_alloc(void* ptr, size_t size)
{
    if (!ptr || record_disable_flag)
        return -3;

    record_disable_flag = 1;
    int ret = _record_alloc(1, ptr, size);
    record_disable_flag = 0;
    return ret;
}

int record_free(void* ptr)
{
    if (!ptr || record_disable_flag)
        return -3;

    record_disable_flag = 1;
    hashmap_remove(g_record.hashmap, (intptr_t)ptr);
    record_disable_flag = 0;
    return 0;
}

int record_update(void* ptr, size_t new_size)
{
    record_disable_flag = 1;
    int ret = _record_alloc(0, ptr, new_size);
    record_disable_flag = 0;
    return ret;
}