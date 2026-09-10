#include "libduke/camera.h"
#include <math.h>

void duke_camera_init(DukeCamera *c) {
    if (c) {
        *c = (DukeCamera){.vertical_fov = 1.2217304764f,
                          .near_plane = 0.01f,
                          .far_plane = 512.0f};
    }
}
void duke_camera_init_from_map(DukeCamera *c, const DukeMapFile *map) {
    duke_camera_init(c);
    if (c && map) {
        c->position[0] = map->posx / 1024.0f;
        c->position[1] = -map->posz / 16384.0f;
        c->position[2] = map->posy / 1024.0f;
        c->yaw = map->ang * (6.28318530718f / 2048.0f);
    }
}
void duke_camera_rotate(DukeCamera *c, float yaw_delta, float pitch_delta) {
    if (c) {
        c->yaw += yaw_delta;
        c->pitch = fmaxf(-1.5f, fminf(1.5f, c->pitch + pitch_delta));
    }
}
void duke_camera_move(DukeCamera *c, float forward, float right) {
    if (!c) {
        return;
    }
    float cy = cosf(c->yaw), sy = sinf(c->yaw);
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    c->position[0] += forward * cy * cp - right * sy;
    c->position[1] += forward * sp;
    c->position[2] += forward * sy * cp + right * cy;
}
/* Use the same basis for flight and view so Build orientation is preserved. */
bool duke_camera_view_projection(const DukeCamera *c, float aspect,
                                 float out[16]) {
    if (!c || !out || !isfinite(aspect) || aspect <= 0 || !isfinite(c->yaw) ||
        !isfinite(c->pitch) || !isfinite(c->vertical_fov) ||
        c->vertical_fov <= 0 || c->vertical_fov >= 3.14159265358979323846 ||
        !isfinite(c->near_plane) || !isfinite(c->far_plane) ||
        c->near_plane <= 0 || c->far_plane <= c->near_plane) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(c->position[i])) {
            return false;
        }
    }
    float cy = cosf(c->yaw), sy = sinf(c->yaw), cp = cosf(c->pitch),
          sp = sinf(c->pitch);
    float f[3] = {cy * cp, sp, sy * cp}, right[3] = {-sy, 0, cy},
          up[3] = {-cy * sp, cp, -sy * sp};
    float view[16] = {right[0], up[0], -f[0], 0, right[1], up[1], -f[1], 0,
                      right[2], up[2], -f[2], 0, 0,        0,     0,     1};
    for (int i = 0; i < 3; i++) {
        view[12] -= right[i] * c->position[i];
        view[13] -= up[i] * c->position[i];
        view[14] += f[i] * c->position[i];
    }
    float near = c->near_plane, far = c->far_plane;
    float focal = 1.0f / tanf(c->vertical_fov * 0.5f);
    float projection[16] = {0};
    projection[0] = focal / aspect;
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
    return true;
}
