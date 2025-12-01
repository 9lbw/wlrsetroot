#define _POSIX_C_SOURCE 200809L

#include "pool-buffer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int create_shm_file(void) {
    int retries = 100;
    
    do {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        
        char name[64];
        snprintf(name, sizeof(name), "/wlrsetroot-%x-%x",
                 (unsigned int)getpid(),
                 (unsigned int)ts.tv_nsec);
        
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
        
        retries--;
    } while (retries > 0 && errno == EEXIST);
    
    return -1;
}

bool pool_buffer_create(struct pool_buffer *buf, struct wl_shm *shm,
                        uint32_t width, uint32_t height, uint32_t format) {
    uint32_t stride = width * 4;  // 4 bytes per pixel (ARGB8888)
    size_t size = (size_t)stride * height;
    
    int fd = create_shm_file();
    if (fd < 0) {
        fprintf(stderr, "Failed to create shm file: %s\n", strerror(errno));
        return false;
    }
    
    if (ftruncate(fd, size) < 0) {
        fprintf(stderr, "Failed to set shm file size: %s\n", strerror(errno));
        close(fd);
        return false;
    }
    
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap shm file: %s\n", strerror(errno));
        close(fd);
        return false;
    }
    
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    buf->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);
    
    buf->data = data;
    buf->width = width;
    buf->height = height;
    buf->size = size;
    
    return true;
}

void pool_buffer_destroy(struct pool_buffer *buf) {
    if (buf->buffer) {
        wl_buffer_destroy(buf->buffer);
        buf->buffer = NULL;
    }
    if (buf->data) {
        munmap(buf->data, buf->size);
        buf->data = NULL;
    }
}
