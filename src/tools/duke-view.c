#define SOKOL_APP_IMPL
#define SOKOL_LOG_IMPL
#define SOKOL_GLUE_IMPL
#include "libduke/renderer.h"
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
    float position[3], yaw, pitch;
    bool keys[SAPP_MAX_KEYCODES];
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
    state.position[0] = state.map->posx / 1024.0f;
    state.position[1] = -state.map->posz / 16384.0f;
    state.position[2] = state.map->posy / 1024.0f;
    state.yaw = state.map->ang * (6.28318530718f / 2048.0f);
    duke_map_file_free(state.map);
    state.map = NULL;
    duke_grp_free(state.grp);
    state.grp = NULL;
}
/* Column-major perspective * view. Build angle zero looks along world +X;
 * positive Build Y is world +Z, so mouse-right agrees with the game. */
static void camera(float out[16]) {
    float cy = cosf(state.yaw), sy = sinf(state.yaw), cp = cosf(state.pitch),
          sp = sinf(state.pitch);
    float f[3] = {cy * cp, sp, sy * cp}, right[3] = {-sy, 0, cy},
          up[3] = {-cy * sp, cp, -sy * sp};
    float view[16] = {right[0], up[0], -f[0], 0, right[1], up[1], -f[1], 0,
        right[2], up[2], -f[2], 0, 0,        0,     0,     1};
    for (int i = 0; i < 3; i++) {
        view[12] -= right[i] * state.position[i];
        view[13] -= up[i] * state.position[i];
        view[14] += f[i] * state.position[i];
    }
    float near = 0.01f, far = 512.0f, focal = 1.428148f;
    float projection[16] = {0};
    projection[0] = focal * sapp_heightf() / sapp_widthf();
    projection[5] = focal;
    projection[10] = (far + near) / (near - far);
    projection[11] = -1;
    projection[14] = 2 * far * near / (near - far);
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            out[c * 4 + row] = 0;
            for (int k = 0; k < 4; k++) {
                out[c * 4 + row] += projection[k * 4 + row] * view[c * 4 + k];
            }
        }
    }
}
static void frame(void) {
    float dt = fmin(sapp_frame_duration(), 0.1);
    float speed = (state.keys[SAPP_KEYCODE_LEFT_SHIFT] ? 8.0f : 2.0f) * dt;
    float forward =
        (float)state.keys[SAPP_KEYCODE_W] - state.keys[SAPP_KEYCODE_S];
    float right = (float)state.keys[SAPP_KEYCODE_D] - state.keys[SAPP_KEYCODE_A];
    float cy = cosf(state.yaw), sy = sinf(state.yaw);
    float length = hypotf(forward, right);
    if (length > 1) {
        forward /= length;
        right /= length;
    }
    // Fly along the same forward vector used by the camera; strafing stays level.
    float cp = cosf(state.pitch), sp = sinf(state.pitch);
    state.position[0] += speed * (forward * cy * cp - right * sy);
    state.position[1] += speed * forward * sp;
    state.position[2] += speed * (forward * sy * cp + right * cy);
    if (sapp_width() > 0 && sapp_height() > 0) {
        float mvp[16];
        camera(mvp);
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
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE && sapp_mouse_locked()) {
        state.yaw += e->mouse_dx * 0.003f;
        state.pitch -= e->mouse_dy * 0.003f;
        state.pitch = fmaxf(-1.5f, fminf(1.5f, state.pitch));
    }
    if (e->type == SAPP_EVENTTYPE_UNFOCUSED) {
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
                "Shift: faster | Click: mouse look | Esc: release mouse/quit\nStatic "
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
        .window_title = "Duke map viewer — click for mouse look",
        .logger.func = slog_func,
        .gl = {.major_version = 4, .minor_version = 1}};
}
