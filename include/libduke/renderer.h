#ifndef LIBDUKE_RENDERER_H
#define LIBDUKE_RENDERER_H
#include "grp.h"
#include "map.h"
#include "sokol_gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DukeRenderer DukeRenderer;
/** @brief Type of hovered map element; sprite selection is opt-in.
 */
typedef enum DukeSurfaceKind {
    DUKE_SURFACE_NONE,
    DUKE_SURFACE_WALL,
    DUKE_SURFACE_FLOOR,
    DUKE_SURFACE_CEILING,
    DUKE_SURFACE_SPRITE
} DukeSurfaceKind;
/** @brief Surface identity in the renderer's map snapshot, plus world-space
 * hit. */
typedef struct DukeSurfaceHit {
    DukeSurfaceKind kind;
    int sector_index;
    int wall_index; /* Sector-facing Build wall index, or -1 for
                       floors/ceilings. */
    float position[3];
    float distance; /* World units from the pointer ray's near-plane origin. */
    int sprite_index; /* Snapshot sprite index, or -1 for map surfaces. */
} DukeSurfaceHit;
/**
 * @brief Enable or disable pointer picking and surface tinting (default
 * disabled). Disabling immediately clears the queried hit. NULL is allowed.
 * Call on the graphics thread between draw calls; this does not change map
 * data.
 */
void duke_renderer_set_hover_enabled(DukeRenderer *renderer, bool enabled);
/**
 * @brief Include visible sprites in hover picking and highlighting.
 * Disabled by default: opaque sprites then occlude surface hits and translucent
 * sprites are skipped. When enabled, both opaque and translucent sprites can
 * return DUKE_SURFACE_SPRITE with sprite_index and sector_index; wall_index is
 * -1. Transparent texels and backfaces of one-sided sprites remain unpickable.
 * Requires hover to be enabled. Call between draws on the graphics thread.
 * Changing this option clears the previous hit. NULL is a no-op.
 */
void duke_renderer_set_sprite_picking_enabled(DukeRenderer *renderer,
                                              bool enabled);
/**
 * @brief Set pointer coordinates relative to the host's rendering viewport.
 * X spans -1 (left) to +1 (right); Y spans -1 (bottom) to +1 (top).
 * Outside/nonfinite coordinates clear the hit and suppress picking. Each call
 * clears the previous result; the next draw computes a fresh hit. NULL is
 * allowed.
 */
void duke_renderer_set_pointer(DukeRenderer *renderer, float x, float y);
/**
 * @brief Copy the hovered surface from the most recent draw, if any.
 * Returns false and writes NONE/-1 IDs when disabled, no hit, or no draw since
 * the pointer changed. NULL renderer/output are accepted and return false.
 * IDs refer to the immutable map snapshot supplied at renderer creation.
 */
bool duke_renderer_get_hovered_surface(const DukeRenderer *renderer,
                                       DukeSurfaceHit *hit);

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
