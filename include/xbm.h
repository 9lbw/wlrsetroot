#ifndef XBM_H
#define XBM_H

#include <stdbool.h>
#include <stdint.h>

struct xbm_image {
    unsigned int width;
    unsigned int height;
    unsigned char *bits;
    int hotspot_x;  // -1 if not defined
    int hotspot_y;  // -1 if not defined
};

// Parse an XBM file and return an xbm_image structure
// Returns NULL on failure
struct xbm_image *xbm_load(const char *filename);

// Free an xbm_image structure
void xbm_free(struct xbm_image *image);

// Get pixel value at (x, y) - returns 1 for foreground, 0 for background
int xbm_get_pixel(const struct xbm_image *image, unsigned int x, unsigned int y);

#endif // XBM_H
