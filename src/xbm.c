#define _POSIX_C_SOURCE 200809L

#include "xbm.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Skip whitespace and comments in the file
static void skip_whitespace_and_comments(FILE *fp) {
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (isspace(c)) {
            continue;
        }
        if (c == '/') {
            int next = fgetc(fp);
            if (next == '*') {
                // Block comment
                while ((c = fgetc(fp)) != EOF) {
                    if (c == '*') {
                        if ((c = fgetc(fp)) == '/') {
                            break;
                        }
                        ungetc(c, fp);
                    }
                }
            } else if (next == '/') {
                // Line comment
                while ((c = fgetc(fp)) != EOF && c != '\n');
            } else {
                ungetc(next, fp);
                ungetc(c, fp);
                return;
            }
        } else {
            ungetc(c, fp);
            return;
        }
    }
}

// Read a #define directive and extract name and value
static bool read_define(FILE *fp, char *name, size_t name_size, int *value) {
    skip_whitespace_and_comments(fp);
    
    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        return false;
    }
    
    // Check for #define
    char *p = line;
    while (isspace(*p)) p++;
    
    if (strncmp(p, "#define", 7) != 0) {
        return false;
    }
    p += 7;
    
    // Skip whitespace
    while (isspace(*p)) p++;
    
    // Read name
    char *name_start = p;
    while (*p && !isspace(*p)) p++;
    
    size_t len = p - name_start;
    if (len >= name_size) {
        return false;
    }
    strncpy(name, name_start, len);
    name[len] = '\0';
    
    // Skip whitespace
    while (isspace(*p)) p++;
    
    // Read value
    *value = (int)strtol(p, NULL, 0);
    
    return true;
}

// Find the data array and parse it
static unsigned char *parse_data_array(FILE *fp, size_t expected_size, size_t *actual_size) {
    // Look for the opening brace
    int c;
    while ((c = fgetc(fp)) != EOF && c != '{');
    
    if (c == EOF) {
        return NULL;
    }
    
    // Allocate buffer
    unsigned char *data = malloc(expected_size);
    if (!data) {
        return NULL;
    }
    
    size_t count = 0;
    char hex_buf[16];
    int hex_idx = 0;
    
    while ((c = fgetc(fp)) != EOF && c != '}') {
        if (isspace(c) || c == ',') {
            if (hex_idx > 0) {
                hex_buf[hex_idx] = '\0';
                
                // Parse the hex value
                char *endptr;
                unsigned long val = strtoul(hex_buf, &endptr, 0);
                
                if (endptr != hex_buf && count < expected_size) {
                    data[count++] = (unsigned char)val;
                }
                hex_idx = 0;
            }
            continue;
        }
        
        if (hex_idx < (int)(sizeof(hex_buf) - 1)) {
            hex_buf[hex_idx++] = c;
        }
    }
    
    // Handle last value if no trailing comma
    if (hex_idx > 0 && count < expected_size) {
        hex_buf[hex_idx] = '\0';
        char *endptr;
        unsigned long val = strtoul(hex_buf, &endptr, 0);
        if (endptr != hex_buf) {
            data[count++] = (unsigned char)val;
        }
    }
    
    *actual_size = count;
    return data;
}

struct xbm_image *xbm_load(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open XBM file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }
    
    struct xbm_image *image = calloc(1, sizeof(struct xbm_image));
    if (!image) {
        fclose(fp);
        return NULL;
    }
    
    image->hotspot_x = -1;
    image->hotspot_y = -1;
    
    // Read all #defines
    char name[256];
    int value;
    bool got_width = false, got_height = false;
    
    long pos = ftell(fp);
    while (read_define(fp, name, sizeof(name), &value)) {
        // Check for width
        size_t len = strlen(name);
        if (len >= 6 && strcmp(name + len - 6, "_width") == 0) {
            image->width = value;
            got_width = true;
        } else if (len >= 7 && strcmp(name + len - 7, "_height") == 0) {
            image->height = value;
            got_height = true;
        } else if (len >= 6 && strcmp(name + len - 6, "_x_hot") == 0) {
            image->hotspot_x = value;
        } else if (len >= 6 && strcmp(name + len - 6, "_y_hot") == 0) {
            image->hotspot_y = value;
        }
        pos = ftell(fp);
    }
    
    if (!got_width || !got_height) {
        fprintf(stderr, "XBM file missing width or height definition\n");
        free(image);
        fclose(fp);
        return NULL;
    }
    
    if (image->width == 0 || image->height == 0) {
        fprintf(stderr, "Invalid XBM dimensions: %ux%u\n", image->width, image->height);
        free(image);
        fclose(fp);
        return NULL;
    }
    
    // Calculate expected data size
    // Each row is padded to byte boundary
    size_t bytes_per_row = (image->width + 7) / 8;
    size_t expected_size = bytes_per_row * image->height;
    
    // Seek back to find the data array
    fseek(fp, pos, SEEK_SET);
    
    size_t actual_size;
    image->bits = parse_data_array(fp, expected_size, &actual_size);
    
    if (!image->bits) {
        fprintf(stderr, "Failed to parse XBM data array\n");
        free(image);
        fclose(fp);
        return NULL;
    }
    
    if (actual_size < expected_size) {
        fprintf(stderr, "Warning: XBM data array smaller than expected (%zu < %zu)\n",
                actual_size, expected_size);
    }
    
    fclose(fp);
    return image;
}

void xbm_free(struct xbm_image *image) {
    if (image) {
        free(image->bits);
        free(image);
    }
}

int xbm_get_pixel(const struct xbm_image *image, unsigned int x, unsigned int y) {
    if (x >= image->width || y >= image->height) {
        return 0;
    }
    
    // XBM format: each row is padded to byte boundary
    // Bits are stored LSB first within each byte
    size_t bytes_per_row = (image->width + 7) / 8;
    size_t byte_index = y * bytes_per_row + x / 8;
    unsigned int bit_index = x % 8;
    
    return (image->bits[byte_index] >> bit_index) & 1;
}
