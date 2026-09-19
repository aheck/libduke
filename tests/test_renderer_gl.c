/* Exercise the actual GLSL and texture bindings without a window or game data.
 * Linux builds with EGL register this test; unavailable contexts skip it. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include "../src/lib/renderer.c"
#include "libduke/camera.h"
#define SOKOL_LOG_IMPL
#include "sokol_log.h"
#include <assert.h>

#define SIZE 32

static DukeRenderer *scene(bool sky, bool floor) {
    DukeMapSector sector = {.wallnum = 4, .ceilingz = -16384,
                            .ceilingpicnum = 89, .floorpicnum = 89,
                            .ceilingstat = sky && !floor ? 1 : 0,
                            .floorstat = sky && floor ? 1 : 0};
    DukeMapSector *sectors[] = {&sector};
    DukeMapWall walls[4] = {{.x = -32768, .y = -32768, .point2 = 1},
                            {.x = 32768, .y = -32768, .point2 = 2},
                            {.x = 32768, .y = 32768, .point2 = 3},
                            {.x = -32768, .y = 32768, .point2 = 0}};
    DukeMapWall *wp[] = {walls, walls + 1, walls + 2, walls + 3};
    DukeMapFile map = {.numsectors = 1, .numwalls = 4, .sectors = sectors, .walls = wp};
    DukeArtFile *art = duke_art_new();
    assert(art);
    DukePaletteFile palette = {0};
    for (int panel = 0; panel < 5; panel++) {
        uint8_t data[16 * 128];
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 128; y++) {
                int index = panel * 32 + y / 4;
                data[x * 128 + y] = index;
                palette.colors[index] = (DukePaletteColor){
                    .red = 10 + panel * 10, .green = (y / 4) * 2, .blue = 5};
            }
        }
        assert(duke_art_set_tile(art, 89 + panel, 16, 128, 0, data, sizeof(data)));
    }
    Source src = {.art = &art, .count = 1, .palette = &palette};
    DukeRenderer *r = calloc(1, sizeof(*r));
    assert(r && floors(r, &src, &map, 0));
    const DukeRendererDesc desc = {.color_format = SG_PIXELFORMAT_RGBA8,
                                  .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                                  .sample_count = 1};
    assert(pipeline(r, &desc));
    r->buffer = sg_make_buffer(&(sg_buffer_desc){
        .data = {r->vertices, r->count * sizeof(Vertex)}});
    assert(sg_query_buffer_state(r->buffer) == SG_RESOURCESTATE_VALID);
    assert(build_picking(r));
    duke_art_free(art);
    return r;
}

static void frame(DukeRenderer *r, const DukeCamera *c, uint8_t *pixels) {
    float mvp[16];
    assert(duke_camera_view_projection(c, 1, mvp));
    sg_begin_pass(&(sg_pass){
        .action.colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                             .clear_value = {1, 0, 1, 1}},
        .swapchain = {.width = SIZE, .height = SIZE, .sample_count = 1,
                      .color_format = SG_PIXELFORMAT_RGBA8,
                      .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL}});
    duke_renderer_draw(r, mvp);
    sg_end_pass();
    sg_commit();
    glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(glGetError() == GL_NO_ERROR);
}

static void sky_pixels(bool floor) {
    float direction = floor ? -1.0f : 1.0f;
    DukeCamera c;
    duke_camera_init(&c);
    c.position[1] = 0.5f;
    c.pitch = direction * 0.3f;
    c.vertical_fov = 0.25f;
    DukeRenderer *r = scene(true, floor);
    uint8_t before[SIZE * SIZE * 4], after[sizeof(before)];
    const int panels[] = {1, 2, 1, 3, 4, 0, 2, 3};
    int center = (SIZE / 2 * SIZE + SIZE / 2) * 4;
    for (int panel = 0; panel < 8; panel++) {
        c.yaw = (panel + 0.5f) * (6.28318530718f / 8);
        frame(r, &c, before);
        int red = 10 + panels[panel] * 10;
        assert(before[center] == ((red << 2) | (red >> 4)));
    }
    /* Translation must preserve the whole panorama, including vertical UVs. */
    c.yaw = 0.2f;
    frame(r, &c, before);
    c.position[0] += 3;
    c.position[1] -= 0.25f;
    c.position[2] += 7;
    frame(r, &c, after);
    /* Raster interpolation can choose either neighbor exactly on a texel
     * boundary. Allow at most eight one-row differences in 1024 pixels. */
    int differences = 0;
    for (size_t i = 0; i < sizeof(before); i++) {
        if (before[i] != after[i]) {
            assert(i % 4 == 1 && abs(before[i] - after[i]) <= 9);
            differences++;
        }
    }
    assert(differences <= 8);
    c.pitch += direction * 0.2f;
    frame(r, &c, after);
    if (floor) {
        assert(after[center + 1] > before[center + 1]);
    } else {
        assert(after[center + 1] < before[center + 1]);
    }
    /* Looking up/down clamps to the edge row without repeating the horizon. */
    c.pitch = direction * 1.3f;
    frame(r, &c, after);
    assert(after[center + 1] == (floor ? 251 : 0));

    c.pitch = direction * 0.3f;
    Draw *sky = &r->draws[floor ? 1 : 0];
    sky->sky_pan[0] = 1;
    frame(r, &c, after);
    assert(after[center] != before[center]);
    sky->sky_pan[0] = 0;
    sky->sky_pan[1] = 0.25f;
    frame(r, &c, after);
    assert(after[center + 1] > before[center + 1]);
    sky->sky_pan[1] = 0;

    duke_renderer_set_hover_enabled(r, true);
    duke_renderer_set_pointer(r, 0, 0);
    frame(r, &c, after);
    DukeSurfaceHit hit;
    assert(duke_renderer_get_hovered_surface(r, &hit));
    assert(hit.kind == (floor ? DUKE_SURFACE_FLOOR : DUKE_SURFACE_CEILING));
    assert(hit.sector_index == 0);
    assert(memcmp(before + center, after + center, 3) != 0);
    duke_renderer_destroy(r);

    /* Without the flag, the surface still has world-space texture mapping. */
    r = scene(false, floor);
    frame(r, &c, before);
    c.position[2] += 1;
    frame(r, &c, after);
    assert(memcmp(before, after, sizeof(before)) != 0);
    duke_renderer_destroy(r);
}

int main(void) {
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                               EGL_DEFAULT_DISPLAY, NULL);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
        !eglBindAPI(EGL_OPENGL_API)) {
        return 77;
    }
    const EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(display, config_attrs, &config, 1, &count) || !count) {
        eglTerminate(display);
        return 77;
    }
    const EGLint context_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 1, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    const EGLint surface_attrs[] = {EGL_WIDTH, SIZE, EGL_HEIGHT, SIZE, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        eglTerminate(display);
        return 77;
    }
    sg_setup(&(sg_desc){.environment.defaults = {
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1},
        .logger.func = slog_func});
    sky_pixels(false);
    sky_pixels(true);
    sg_shutdown();
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
