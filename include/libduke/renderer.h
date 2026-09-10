#ifndef LIBDUKE_RENDERER_H
#define LIBDUKE_RENDERER_H
#include "grp.h"
#include "map.h"
#include "sokol_gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DukeRenderer DukeRenderer;
/** @brief Attachment formats of the host's render pass. Zero uses Sokol
 * defaults. */
typedef struct DukeRendererDesc {
    sg_pixel_format color_format;
    sg_pixel_format depth_format;
    int sample_count;
} DukeRendererDesc;
/**
 * @brief Snapshot a map and its GRP textures into GPU resources.
 * The host must first call sg_setup with the OpenGL backend and make its
 * context current. The renderer never creates a window, context, event loop or
 * pass. Map and archive are borrowed only for this call; the archive directory
 * must be loaded. Missing tiles use a checkerboard. PALETTE.DAT must be
 * present. Supports static sector surfaces, holes, slopes and portal wall
 * bands and face/wall/floor sprites. Game effects, palette lookup variants and
 * exact Build visibility are not yet implemented. Overlapping sectors are drawn
 * together with depth testing. Coordinates are (Build X / 1024, -Build Z /
 * 16384, Build Y / 1024).
 * @param map Map snapshot; references must be valid (checked by this function).
 * @param grp Open GRP with parsed directory.
 * @param desc Host attachment formats; must match the drawing pass.
 * @param error Optional caller-owned diagnostic buffer.
 * @param error_size Capacity of error, including the terminating NUL.
 * @return Owned renderer, or NULL on failure. Destroy before sg_shutdown.
 */
DukeRenderer *duke_renderer_create(DukeMapFile *map, DukeGrpFile *grp,
                                   const DukeRendererDesc *desc, char *error,
                                   size_t error_size);
/**
 * @brief Draw the snapshot within an active host-owned Sokol render pass.
 * @param renderer Renderer created in the current Sokol context.
 * @param view_projection Column-major OpenGL view-projection matrix (16
 * floats). The host handles sg_begin_pass/end_pass/commit, presentation and
 * resizing. Resets Sokol's native state cache, then applies its own pipeline
 * and bindings. Calls must occur on the graphics thread; Sokol's global context
 * is shared. Face sprites stay upright and use the horizontal camera-right
 * vector from a conventional, unskewed view-projection matrix. Sprite
 * translucency uses approximate alpha blending, sorted by center depth.
 */
void duke_renderer_draw(DukeRenderer *renderer,
                        const float view_projection[16]);
/** @brief Release CPU/GPU resources with the creating graphics context current;
 * NULL is allowed. */
void duke_renderer_destroy(DukeRenderer *renderer);
#ifdef __cplusplus
}
#endif
#endif
