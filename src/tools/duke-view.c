#define SOKOL_APP_IMPL
#define SOKOL_LOG_IMPL
#define SOKOL_GLUE_IMPL
#include "libduke/renderer.h"
#include "libduke/camera.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    DukeMapFile *map;
    DukeGrpFile *grp;
    DukeRenderer *renderer;
    DukeCamera camera;
    bool keys[SAPP_MAX_KEYCODES];
    bool hover_enabled, pointer_valid;
    float mouse_x, mouse_y;
    int frames, frame_limit;
} state;

static void fail(const char *message) {
    fprintf(stderr, "duke-view: %s\n", message);
    exit(EXIT_FAILURE);
}
static void init(void) {
    sg_setup(&(sg_desc){.environment = sglue_environment(),
            .logger.func = slog_func,
            .image_pool_size = 8192,
            .view_pool_size = 8192});
    if (!sg_isvalid()) {
        fail("Graphics initialization failed");
    }
    const sg_environment env = sglue_environment();
    DukeRendererDesc desc = {env.defaults.color_format, env.defaults.depth_format,
        env.defaults.sample_count};
    char error[256];
    state.renderer =
        duke_renderer_create(state.map, state.grp, &desc, error, sizeof(error));
    if (!state.renderer) {
        fail(error);
    }
    duke_camera_init_from_map(&state.camera, state.map);
    duke_map_file_free(state.map);
    state.map = NULL;
    duke_grp_free(state.grp);
    state.grp = NULL;
}
static void frame(void) {
    float dt = fmin(sapp_frame_duration(), 0.1);
    float speed = (state.keys[SAPP_KEYCODE_LEFT_SHIFT] ? 8.0f : 2.0f) * dt;
    float forward =
        (float)state.keys[SAPP_KEYCODE_W] - state.keys[SAPP_KEYCODE_S];
    float right = (float)state.keys[SAPP_KEYCODE_D] - state.keys[SAPP_KEYCODE_A];
    float length = hypotf(forward, right);
    if (length > 1) {
        forward /= length;
        right /= length;
    }
    duke_camera_move(&state.camera, speed * forward, speed * right);
    if (sapp_width() > 0 && sapp_height() > 0) {
        float mvp[16];
        if (!duke_camera_view_projection(&state.camera,
                                         sapp_widthf() / sapp_heightf(), mvp)) {
            fail("Invalid camera projection");
        }
        /* Captured mouse look targets the center; otherwise follow the cursor. */
        if (sapp_mouse_locked()) {
            duke_renderer_set_pointer(state.renderer, 0, 0);
        } else if (state.pointer_valid) {
            duke_renderer_set_pointer(state.renderer,
                                      2 * state.mouse_x / sapp_widthf() - 1,
                                      1 - 2 * state.mouse_y / sapp_heightf());
        } else {
            duke_renderer_set_pointer(state.renderer, 2, 2);
        }
        sg_begin_pass(&(sg_pass){
                .swapchain = sglue_swapchain(),
                .action.colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                .clear_value = {0.04f, 0.05f, 0.07f, 1}}});
        duke_renderer_draw(state.renderer, mvp);
        sg_end_pass();
        sg_commit();
    }
    if (state.frame_limit > 0 && ++state.frames >= state.frame_limit) {
        sapp_request_quit();
    }
}
static void event(const sapp_event *e) {
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN || e->type == SAPP_EVENTTYPE_KEY_UP) {
        if (e->key_code > 0 && e->key_code < SAPP_MAX_KEYCODES) {
            state.keys[e->key_code] = e->type == SAPP_EVENTTYPE_KEY_DOWN;
        }
        if (e->type == SAPP_EVENTTYPE_KEY_DOWN && !e->key_repeat &&
            e->key_code == SAPP_KEYCODE_H) {
            state.hover_enabled = !state.hover_enabled;
            duke_renderer_set_hover_enabled(state.renderer, state.hover_enabled);
            sapp_set_window_title(state.hover_enabled
                ? "Duke map viewer — hover ON (H to toggle)"
                : "Duke map viewer — hover OFF (H to toggle)");
        }
        if (e->type == SAPP_EVENTTYPE_KEY_DOWN &&
                e->key_code == SAPP_KEYCODE_ESCAPE) {
            if (sapp_mouse_locked()) {
                sapp_lock_mouse(false);
            } else {
                sapp_request_quit();
            }
        }
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_DOWN &&
            e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
        sapp_lock_mouse(true);
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE ||
        e->type == SAPP_EVENTTYPE_MOUSE_ENTER ||
        e->type == SAPP_EVENTTYPE_MOUSE_DOWN) {
        state.mouse_x = e->mouse_x;
        state.mouse_y = e->mouse_y;
        state.pointer_valid = true;
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_LEAVE) {
        state.pointer_valid = false;
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE && sapp_mouse_locked()) {
        duke_camera_rotate(&state.camera, e->mouse_dx * 0.003f,
                           -e->mouse_dy * 0.003f);
    }
    if (e->type == SAPP_EVENTTYPE_UNFOCUSED) {
        state.pointer_valid = false;
        memset(state.keys, 0, sizeof(state.keys));
        sapp_lock_mouse(false);
    }
}
static void cleanup(void) {
    duke_renderer_destroy(state.renderer);
    sg_shutdown();
    duke_map_file_free(state.map);
    duke_grp_free(state.grp);
}

sapp_desc sokol_main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        puts("Usage: duke-view MAP GRP [--frames N]\nW/S: fly along view | A/D: strafe | "
                "Shift: faster | H: toggle surface highlight | Click: mouse look | Esc: release mouse/quit\nStatic "
                "free-flight viewer; no collision or game simulation.");
        exit(EXIT_SUCCESS);
    }
    if (argc != 3 && argc != 5) {
        fail("Usage: duke-view MAP GRP [--frames N]");
    }
    if (argc == 5) {
        char *end = NULL;
        errno = 0;
        long count = strtol(argv[4], &end, 10);
        if (errno || strcmp(argv[3], "--frames") || !end || end == argv[4] ||
                *end || count < 1 || count > 1000000) {
            fail("Invalid --frames count");
        }
        state.frame_limit = (int)count;
    }
    state.map = duke_map_file_new();
    state.grp = duke_grp_new();
    if (!state.map || !state.grp) {
        fail("Allocation failed");
    }
    if (!duke_map_file_read_from_filename(state.map, argv[1])) {
        fail(state.map->last_error);
    }
    if (!duke_grp_open_filename(state.grp, argv[2]) ||
            !duke_grp_read_entries_sparse(state.grp)) {
        fail(state.grp->last_error);
    }

    return (sapp_desc){.init_cb = init,
        .frame_cb = frame,
        .event_cb = event,
        .cleanup_cb = cleanup,
        .width = 1280,
        .height = 720,
        .window_title = "Duke map viewer — hover OFF (H to toggle)",
        .logger.func = slog_func,
        .gl = {.major_version = 4, .minor_version = 1}};
}
