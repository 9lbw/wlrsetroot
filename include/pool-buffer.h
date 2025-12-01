#ifndef POOL_BUFFER_H
#define POOL_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

struct pool_buffer {
    struct wl_buffer *buffer;
    void *data;
    uint32_t width;
    uint32_t height;
    size_t size;
};

// Create a shared memory buffer
bool pool_buffer_create(struct pool_buffer *buf, struct wl_shm *shm,
                        uint32_t width, uint32_t height, uint32_t format);

// Destroy a pool buffer
void pool_buffer_destroy(struct pool_buffer *buf);

#endif // POOL_BUFFER_H
