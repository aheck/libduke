#include "libduke/camera.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static void close_to(float a, float b) { assert(fabsf(a - b) < 0.0001f); }
static void project(const float m[16], const float p[3], float out[3]) {
    float q[4];
    for (int i = 0; i < 4; i++) {
        q[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i];
    }
    for (int i = 0; i < 3; i++) {
        out[i] = q[i] / q[3];
    }
}
int main(void) {
    DukeCamera c;
    DukeMapFile map = {0};
    map.posx = 1024;
    map.posy = 2048;
    map.posz = -16384;
    map.ang = 512;
    duke_camera_init_from_map(&c, &map);
    close_to(c.position[0], 1);
    close_to(c.position[1], 1);
    close_to(c.position[2], 2);
    duke_camera_move(&c, 2, 1);
    close_to(c.position[0], 0);
    close_to(c.position[1], 1);
    close_to(c.position[2], 4);
    duke_camera_init(&c);
    duke_camera_rotate(&c, 0, 0.5235987756f);
    duke_camera_move(&c, 2, 1);
    close_to(c.position[0], sqrtf(3));
    close_to(c.position[1], 1);
    close_to(c.position[2], 1);
    duke_camera_rotate(&c, 0, 100);
    close_to(c.pitch, 1.5f);
    duke_camera_rotate(&c, 0, -100);
    close_to(c.pitch, -1.5f);

    /* Known points verify camera handedness, vertical FOV, aspect and clipping.
     */
    duke_camera_init(&c);
    c.vertical_fov = 1.5707963268f;
    c.near_plane = 1;
    c.far_plane = 10;
    float m[16], p[3], ndc[3];
    assert(duke_camera_view_projection(&c, 2, m));
    p[0] = 1;
    p[1] = 0;
    p[2] = 0;
    project(m, p, ndc);
    close_to(ndc[2], -1);
    p[0] = 10;
    project(m, p, ndc);
    close_to(ndc[2], 1);
    p[0] = 2;
    p[1] = 2;
    p[2] = 4;
    project(m, p, ndc);
    close_to(ndc[0], 1);
    close_to(ndc[1], 1);
    c.position[0] = 4;
    c.position[1] = 5;
    c.position[2] = 6;
    c.yaw = 1.5707963268f;
    c.pitch = 0.5235987756f;
    assert(duke_camera_view_projection(&c, 1, m));
    p[0] = 4;
    p[1] = 6;
    p[2] = 6 + sqrtf(3);
    project(m, p, ndc);
    close_to(ndc[0], 0);
    close_to(ndc[1], 0);
    float saved[16];
    memcpy(saved, m, sizeof(m));
    assert(!duke_camera_view_projection(&c, 0, m));
    assert(!duke_camera_view_projection(NULL, 1, m));
    assert(!duke_camera_view_projection(&c, 1, NULL));
    c.near_plane = c.far_plane;
    assert(!duke_camera_view_projection(&c, 1, m));
    duke_camera_init(&c);
    c.vertical_fov = NAN;
    assert(!duke_camera_view_projection(&c, 1, m));
    assert(memcmp(saved, m, sizeof(m)) == 0);
    duke_camera_init_from_map(&c, NULL);
    close_to(c.position[0], 0);
    duke_camera_init(NULL);
    duke_camera_init_from_map(NULL, &map);
    duke_camera_move(NULL, 1, 1);
    duke_camera_rotate(NULL, 1, 1);
    return 0;
}
