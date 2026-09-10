#ifndef LIBDUKE_CAMERA_H
#define LIBDUKE_CAMERA_H
#include "map.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/** @brief Context-free perspective camera in renderer world coordinates.
 * Position is (Build X / 1024, -Build Z / 16384, Build Y / 1024).
 * Angles are radians: yaw zero looks along +X, positive yaw turns toward +Z,
 * and positive pitch looks upward. Projection uses OpenGL clip coordinates.
 */
typedef struct DukeCamera {
    float position[3];
    float yaw, pitch;
    float vertical_fov, near_plane, far_plane;
} DukeCamera;
/** @brief Initialize at the origin looking along +X, with 70-degree vertical
 * FOV and clipping distances 0.01 and 512 world units. NULL is a no-op. */
void duke_camera_init(DukeCamera *camera);
/** @brief Initialize defaults and place the camera at the map's player start.
 * The map is borrowed only for this call. NULL map uses the origin defaults;
 * NULL camera is a no-op. */
void duke_camera_init_from_map(DukeCamera *camera, const DukeMapFile *map);
/** @brief Apply look deltas in radians, clamping pitch to [-1.5, 1.5].
 * The host supplies sensitivity and input mapping. NULL is a no-op. */
void duke_camera_rotate(DukeCamera *camera, float yaw_delta, float pitch_delta);
/** @brief Move by forward and horizontal-right distances in world units.
 * Forward follows pitch; strafing stays level. The caller handles speed,
 * timing and diagonal input normalization. No collision is performed.
 * NULL is a no-op. */
void duke_camera_move(DukeCamera *camera, float forward, float right);
/** @brief Write a column-major perspective-times-view matrix (16 floats).
 * Aspect is viewport width divided by height. Requires finite camera values,
 * 0 < vertical_fov < pi, aspect > 0 and 0 < near_plane < far_plane.
 * Returns false for invalid arguments, leaving output unchanged. No graphics
 * context is needed. */
bool duke_camera_view_projection(const DukeCamera *camera, float aspect,
                                 float out[16]);
#ifdef __cplusplus
}
#endif
#endif
