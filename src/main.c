#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "pool-buffer.h"
#include "xbm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define VERSION "0.1.0"

// Built-in gray pattern (2x2 checkerboard, same as X11's gray_bits)
static const unsigned char gray_bits[] = { 0x01, 0x02 };
#define GRAY_WIDTH 2
#define GRAY_HEIGHT 2

// Pattern type enum
enum pattern_type {
    PATTERN_NONE,
    PATTERN_XBM,
    PATTERN_GRAY,
    PATTERN_MOD,
};

// Global state
struct wlrsetroot_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    
    struct wl_list outputs;  // list of wlrsetroot_output
    
    enum pattern_type pattern;
    struct xbm_image *xbm;
    int mod_x;  // modula pattern x spacing
    int mod_y;  // modula pattern y spacing
    uint32_t fg_color;  // ARGB format
    uint32_t bg_color;  // ARGB format
    float pattern_scale;  // Scale factor for XBM pattern (default 1.0)
    bool reverse;  // swap fg/bg colors
    
    bool running;
};

struct wlrsetroot_output {
    struct wl_list link;
    struct wlrsetroot_state *state;
    
    struct wl_output *wl_output;
    uint32_t wl_name;
    
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct pool_buffer buffer;
    
    uint32_t width;
    uint32_t height;
    int32_t scale;
    
    bool configured;
    uint32_t configure_serial;
};

// Parse color string like "#rrggbb" or "rrggbb"
static bool parse_color(const char *str, uint32_t *color) {
    if (!str || !color) {
        return false;
    }
    
    if (str[0] == '#') {
        str++;
    }
    
    size_t len = strlen(str);
    if (len != 6) {
        return false;
    }
    
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit(str[i])) {
            return false;
        }
    }
    
    uint32_t rgb = (uint32_t)strtoul(str, NULL, 16);
    // Convert RGB to ARGB (fully opaque)
    *color = 0xFF000000 | rgb;
    return true;
}

// Get pixel from built-in gray pattern (2x2 checkerboard)
static int gray_get_pixel(unsigned int x, unsigned int y) {
    size_t byte_index = y * ((GRAY_WIDTH + 7) / 8) + x / 8;
    unsigned int bit_index = x % 8;
    return (gray_bits[byte_index] >> bit_index) & 1;
}

// Get pixel from modula pattern (like xsetroot's MakeModulaBitmap)
// Creates a 16x16 grid pattern based on mod_x and mod_y spacing
static int mod_get_pixel(int mod_x, int mod_y, unsigned int x, unsigned int y) {
    // Wrap to 16x16 tile
    x = x % 16;
    y = y % 16;
    
    // Every mod_y'th row is fully lit
    if ((y % mod_y) == 0) {
        return 1;
    }
    // Every mod_x'th column is lit
    if ((x % mod_x) == 0) {
        return 1;
    }
    return 0;
}

// Render the pattern tiled across the buffer
static void render_tiled_pattern(struct wlrsetroot_output *output) {
    struct wlrsetroot_state *state = output->state;
    uint32_t *pixels = output->buffer.data;
    uint32_t buf_width = output->buffer.width;
    uint32_t buf_height = output->buffer.height;
    
    // Apply reverse if set
    uint32_t fg = state->reverse ? state->bg_color : state->fg_color;
    uint32_t bg = state->reverse ? state->fg_color : state->bg_color;
    
    if (state->pattern == PATTERN_NONE) {
        // Solid background color
        for (uint32_t i = 0; i < buf_width * buf_height; i++) {
            pixels[i] = bg;
        }
        return;
    }
    
    float scale = state->pattern_scale;
    
    for (uint32_t y = 0; y < buf_height; y++) {
        for (uint32_t x = 0; x < buf_width; x++) {
            int pixel = 0;
            
            switch (state->pattern) {
            case PATTERN_XBM: {
                struct xbm_image *xbm = state->xbm;
                float xbm_width_f = (float)xbm->width;
                float xbm_height_f = (float)xbm->height;
                unsigned int xbm_x = (unsigned int)fmodf(x / scale, xbm_width_f);
                unsigned int xbm_y = (unsigned int)fmodf(y / scale, xbm_height_f);
                pixel = xbm_get_pixel(xbm, xbm_x, xbm_y);
                break;
            }
            case PATTERN_GRAY: {
                unsigned int gray_x = (unsigned int)fmodf(x / scale, (float)GRAY_WIDTH);
                unsigned int gray_y = (unsigned int)fmodf(y / scale, (float)GRAY_HEIGHT);
                pixel = gray_get_pixel(gray_x, gray_y);
                break;
            }
            case PATTERN_MOD: {
                unsigned int mod_px = (unsigned int)(x / scale);
                unsigned int mod_py = (unsigned int)(y / scale);
                pixel = mod_get_pixel(state->mod_x, state->mod_y, mod_px, mod_py);
                break;
            }
            default:
                break;
            }
            
            // XBM convention: 1 = background, 0 = foreground (matches xsetroot)
            pixels[y * buf_width + x] = pixel ? bg : fg;
        }
    }
}

// Layer surface configure handler
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial,
                                    uint32_t width,
                                    uint32_t height) {
    (void)surface;
    struct wlrsetroot_output *output = data;
    
    output->width = width;
    output->height = height;
    output->configure_serial = serial;
    output->configured = true;
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {
    (void)surface;
    struct wlrsetroot_output *output = data;
    
    if (output->layer_surface) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
        output->layer_surface = NULL;
    }
    if (output->surface) {
        wl_surface_destroy(output->surface);
        output->surface = NULL;
    }
    
    pool_buffer_destroy(&output->buffer);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

// Create layer surface for an output
static void create_layer_surface(struct wlrsetroot_output *output) {
    struct wlrsetroot_state *state = output->state;
    
    output->surface = wl_compositor_create_surface(state->compositor);
    if (!output->surface) {
        fprintf(stderr, "Failed to create surface\n");
        return;
    }
    
    // Create empty input region (we don't want input events)
    struct wl_region *input_region = wl_compositor_create_region(state->compositor);
    wl_surface_set_input_region(output->surface, input_region);
    wl_region_destroy(input_region);
    
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state->layer_shell,
        output->surface,
        output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        "wallpaper"
    );
    
    if (!output->layer_surface) {
        fprintf(stderr, "Failed to create layer surface\n");
        wl_surface_destroy(output->surface);
        output->surface = NULL;
        return;
    }
    
    // Configure the layer surface
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    
    zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                       &layer_surface_listener, output);
    
    // Initial commit to get configure event
    wl_surface_commit(output->surface);
}

// Render and display the wallpaper on an output
static void render_output(struct wlrsetroot_output *output) {
    struct wlrsetroot_state *state = output->state;
    
    if (!output->configured || output->width == 0 || output->height == 0) {
        return;
    }
    
    uint32_t buffer_width = output->width * output->scale;
    uint32_t buffer_height = output->height * output->scale;
    
    // Create buffer if needed
    if (output->buffer.buffer == NULL ||
        output->buffer.width != buffer_width ||
        output->buffer.height != buffer_height) {
        
        pool_buffer_destroy(&output->buffer);
        
        if (!pool_buffer_create(&output->buffer, state->shm,
                                buffer_width, buffer_height,
                                WL_SHM_FORMAT_ARGB8888)) {
            fprintf(stderr, "Failed to create buffer\n");
            return;
        }
    }
    
    // Render the pattern
    render_tiled_pattern(output);
    
    // Ack configure
    zwlr_layer_surface_v1_ack_configure(output->layer_surface,
                                        output->configure_serial);
    
    // Attach and commit
    wl_surface_set_buffer_scale(output->surface, output->scale);
    wl_surface_attach(output->surface, output->buffer.buffer, 0, 0);
    wl_surface_damage_buffer(output->surface, 0, 0, buffer_width, buffer_height);
    wl_surface_commit(output->surface);
}

// Output event handlers
static void output_geometry(void *data, struct wl_output *wl_output,
                           int32_t x, int32_t y, int32_t physical_width,
                           int32_t physical_height, int32_t subpixel,
                           const char *make, const char *model,
                           int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y;
    (void)physical_width; (void)physical_height; (void)subpixel;
    (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *wl_output,
                       uint32_t flags, int32_t width, int32_t height,
                       int32_t refresh) {
    (void)data; (void)wl_output; (void)flags;
    (void)width; (void)height; (void)refresh;
}

static void output_done(void *data, struct wl_output *wl_output) {
    (void)wl_output;
    struct wlrsetroot_output *output = data;
    
    if (!output->layer_surface) {
        create_layer_surface(output);
    }
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t scale) {
    (void)wl_output;
    struct wlrsetroot_output *output = data;
    output->scale = scale;
}

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)data; (void)wl_output; (void)name;
}

static void output_description(void *data, struct wl_output *wl_output,
                               const char *description) {
    (void)data; (void)wl_output; (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void destroy_output(struct wlrsetroot_output *output) {
    wl_list_remove(&output->link);
    
    if (output->layer_surface) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
    }
    if (output->surface) {
        wl_surface_destroy(output->surface);
    }
    if (output->wl_output) {
        wl_output_destroy(output->wl_output);
    }
    
    pool_buffer_destroy(&output->buffer);
    free(output);
}

// Registry handlers
static void registry_global(void *data, struct wl_registry *registry,
                           uint32_t name, const char *interface,
                           uint32_t version) {
    (void)version;
    struct wlrsetroot_state *state = data;
    
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name,
                                             &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name,
                                      &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wlrsetroot_output *output = calloc(1, sizeof(*output));
        if (!output) {
            fprintf(stderr, "Failed to allocate output\n");
            return;
        }
        
        output->state = state;
        output->wl_name = name;
        output->scale = 1;
        output->wl_output = wl_registry_bind(registry, name,
                                             &wl_output_interface, 4);
        
        wl_output_add_listener(output->wl_output, &output_listener, output);
        wl_list_insert(&state->outputs, &output->link);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->layer_shell = wl_registry_bind(registry, name,
                                              &zwlr_layer_shell_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
    (void)registry;
    struct wlrsetroot_state *state = data;
    
    struct wlrsetroot_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &state->outputs, link) {
        if (output->wl_name == name) {
            destroy_output(output);
            break;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n"
           "\n"
           "Options:\n"
           "  -bitmap <file>    XBM file to use as wallpaper pattern\n"
           "  -mod <x> <y>      Use a plaid-like grid pattern (16x16 tile)\n"
           "  -gray, -grey      Use a gray (checkerboard) pattern\n"
           "  -solid <color>    Solid background color (no pattern)\n"
           "  -bg <color>       Background color (hex: #rrggbb or rrggbb)\n"
           "  -fg <color>       Foreground color (hex: #rrggbb or rrggbb)\n"
           "  -rv, -reverse     Swap foreground and background colors\n"
           "  -scale <n>        Scale the pattern by factor n (0.1-32, default: 1)\n"
           "  -h, --help        Show this help message\n"
           "  -v, --version     Show version\n"
           "\n"
           "Examples:\n"
           "  %s -bitmap pattern.xbm -bg \"#1a1a2e\" -fg \"#e94560\"\n"
           "  %s -gray -bg \"#1a1a2e\" -fg \"#e94560\"\n"
           "  %s -mod 16 16 -bg \"#282a36\" -fg \"#44475a\"\n"
           "  %s -solid \"#282a36\"\n",
           prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[]) {
    struct wlrsetroot_state state = {0};
    wl_list_init(&state.outputs);
    
    // Default colors (similar to xsetroot defaults)
    state.bg_color = 0xFF000000;  // Black
    state.fg_color = 0xFFFFFFFF;  // White
    state.pattern_scale = 1.0f;   // No scaling by default
    state.pattern = PATTERN_NONE;
    state.reverse = false;
    
    const char *xbm_file = NULL;
    int excl = 0;  // Count of exclusive options (bitmap, gray, mod, solid)
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-bitmap") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument for -bitmap\n");
                return 1;
            }
            xbm_file = argv[i];
            state.pattern = PATTERN_XBM;
            excl++;
        } else if (strcmp(argv[i], "-gray") == 0 || strcmp(argv[i], "-grey") == 0) {
            state.pattern = PATTERN_GRAY;
            excl++;
        } else if (strcmp(argv[i], "-mod") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing x argument for -mod\n");
                return 1;
            }
            state.mod_x = atoi(argv[i]);
            if (state.mod_x <= 0) state.mod_x = 1;
            if (++i >= argc) {
                fprintf(stderr, "Missing y argument for -mod\n");
                return 1;
            }
            state.mod_y = atoi(argv[i]);
            if (state.mod_y <= 0) state.mod_y = 1;
            state.pattern = PATTERN_MOD;
            excl++;
        } else if (strcmp(argv[i], "-bg") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument for -bg\n");
                return 1;
            }
            if (!parse_color(argv[i], &state.bg_color)) {
                fprintf(stderr, "Invalid color: %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-fg") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument for -fg\n");
                return 1;
            }
            if (!parse_color(argv[i], &state.fg_color)) {
                fprintf(stderr, "Invalid color: %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-rv") == 0 || strcmp(argv[i], "-reverse") == 0) {
            state.reverse = true;
        } else if (strcmp(argv[i], "-scale") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument for -scale\n");
                return 1;
            }
            float scale = strtof(argv[i], NULL);
            if (scale < 0.1f || scale > 32.0f) {
                fprintf(stderr, "Scale must be between 0.1 and 32\n");
                return 1;
            }
            state.pattern_scale = scale;
        } else if (strcmp(argv[i], "-solid") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument for -solid\n");
                return 1;
            }
            if (!parse_color(argv[i], &state.bg_color)) {
                fprintf(stderr, "Invalid color: %s\n", argv[i]);
                return 1;
            }
            state.pattern = PATTERN_NONE;
            excl++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("wlrsetroot version %s\n", VERSION);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Check for multiple exclusive options
    if (excl > 1) {
        fprintf(stderr, "Error: choose only one of {-bitmap, -gray, -mod, -solid}\n");
        return 1;
    }
    
    // Load XBM file if specified
    if (state.pattern == PATTERN_XBM && xbm_file) {
        state.xbm = xbm_load(xbm_file);
        if (!state.xbm) {
            fprintf(stderr, "Failed to load XBM file: %s\n", xbm_file);
            return 1;
        }
    }
    
    // Connect to Wayland
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        xbm_free(state.xbm);
        return 1;
    }
    
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    
    // First roundtrip to get globals
    wl_display_roundtrip(state.display);
    
    if (!state.compositor) {
        fprintf(stderr, "Compositor does not support wl_compositor\n");
        goto cleanup;
    }
    if (!state.shm) {
        fprintf(stderr, "Compositor does not support wl_shm\n");
        goto cleanup;
    }
    if (!state.layer_shell) {
        fprintf(stderr, "Compositor does not support wlr-layer-shell\n");
        goto cleanup;
    }
    
    // Second roundtrip to get output info and create layer surfaces
    wl_display_roundtrip(state.display);
    
    // Main loop
    state.running = true;
    while (state.running && wl_display_dispatch(state.display) != -1) {
        // Check for outputs that need rendering
        struct wlrsetroot_output *output;
        wl_list_for_each(output, &state.outputs, link) {
            if (output->configured && output->buffer.buffer == NULL) {
                render_output(output);
            }
        }
    }
    
cleanup:
    // Cleanup outputs
    struct wlrsetroot_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &state.outputs, link) {
        destroy_output(output);
    }
    
    if (state.layer_shell) {
        zwlr_layer_shell_v1_destroy(state.layer_shell);
    }
    if (state.shm) {
        wl_shm_destroy(state.shm);
    }
    if (state.compositor) {
        wl_compositor_destroy(state.compositor);
    }
    if (state.registry) {
        wl_registry_destroy(state.registry);
    }
    if (state.display) {
        wl_display_disconnect(state.display);
    }
    
    xbm_free(state.xbm);
    
    return 0;
}
