#include "libduke/renderer.h"
#include "libduke/art.h"
#include "libduke/palette.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Geometry is built once at creation; drawing only binds cached textures and
 * submits ranges from a single immutable vertex buffer. */
#define TILE_COUNT 6144
#define VERTEX_LIMIT 8000000

typedef struct Vertex {
    float p[3], uv[2], light;
} Vertex;
typedef struct Texture {
    sg_image image;
    sg_view view;
    int width, height;
} Texture;
typedef struct Draw {
    int first, count, tile;
} Draw;
struct DukeRenderer {
    sg_shader shader;
    sg_pipeline pipeline;
    sg_sampler sampler;
    sg_buffer buffer;
    /* The extra slot holds the checkerboard used for missing/invalid tiles. */
    Texture textures[TILE_COUNT + 1];
    Vertex *vertices;
    size_t count, capacity;
    Draw *draws;
    size_t draw_count, draw_capacity;
};
typedef struct Source {
    DukeArtFile **art;
    size_t count;
    DukePaletteFile *palette;
} Source;

static bool named(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) {
            return false;
        }
    }
    return *a == *b;
}
static void source_free(Source *s) {
    for (size_t i = 0; i < s->count; i++) {
        duke_art_free(s->art[i]);
    }
    free(s->art);
    duke_palette_free(s->palette);
}
/* Keep ART sources alive while geometry requests tiles lazily. On failure,
 * the caller releases any partially loaded source with source_free(). */
static bool source_load(Source *s, DukeGrpFile *grp) {
    s->palette = duke_palette_new();
    if (!s->palette) {
        return false;
    }
    bool found = false;
    for (uint32_t i = 0; i < grp->header.entry_count; i++) {
        DukeGrpFileEntry *e = duke_grp_get_entry_by_index(grp, i);
        if (!e) {
            return false;
        }
        size_t len = strlen(e->filename);
        bool palette = named(e->filename, "PALETTE.DAT");
        if (!palette && (len < 4 || !named(e->filename + len - 4, ".ART"))) {
            continue;
        }
        void *data = NULL;
        size_t size = duke_grp_get_file_data_by_index(grp, i, &data);
        if (size == (size_t)-1) {
            return false;
        }
        if (palette) {
            if (!duke_palette_read_from_memory(s->palette, data, size)) {
                return false;
            }
            found = true;
        } else {
            DukeArtFile *art = duke_art_new();
            if (!art) {
                return false;
            }
            if (!duke_art_open_memory(art, data, size) ||
                    !duke_art_read_tiles_sparse(art)) {
                duke_art_free(art);
                return false;
            }
            DukeArtFile **next = realloc(s->art, (s->count + 1) * sizeof(*next));
            if (!next) {
                duke_art_free(art);
                return false;
            }
            s->art = next;
            s->art[s->count++] = art;
        }
    }
    return found;
}

static bool upload(Texture *t, int width, int height, const void *rgba) {
    t->width = width;
    t->height = height;
    t->image = sg_make_image(&(sg_image_desc){
            .width = width,
            .height = height,
            .pixel_format = SG_PIXELFORMAT_RGBA8,
            .data.mip_levels[0] = {rgba, (size_t)width * height * 4}});
    if (sg_query_image_state(t->image) != SG_RESOURCESTATE_VALID) {
        return false;
    }
    t->view = sg_make_view(&(sg_view_desc){.texture.image = t->image});
    return sg_query_view_state(t->view) == SG_RESOURCESTATE_VALID;
}

/* Return a cached/uploaded tile slot, TILE_COUNT for a missing tile, or -1
 * for a read/allocation/upload failure that must abort renderer creation. */
static int texture(DukeRenderer *r, Source *s, int tile) {
    if (tile < 0 || tile >= TILE_COUNT) {
        return TILE_COUNT;
    }

    Texture *t = &r->textures[tile];
    if (t->view.id) {
        return tile;
    }

    /* Later ART entries take precedence when tile ranges overlap. */
    for (size_t i = s->count; i > 0; i--) {
        DukeArtTile *a = duke_art_get_tile_by_number(s->art[i - 1], tile);
        if (!a || a->width <= 0 || a->height <= 0) {
            continue;
        }

        void *pixels = NULL;
        size_t size =
            duke_art_get_tile_data_by_number(s->art[i - 1], tile, &pixels);
        if (size == (size_t)-1) {
            return -1;
        }

        size_t bytes = (size_t)a->width * a->height * 4;
        uint8_t *rgba = malloc(bytes);
        if (!rgba) {
            return -1;
        }

        bool ok = duke_art_tile_to_rgba(a, pixels, size, s->palette, rgba, bytes) &&
            upload(t, a->width, a->height, rgba);
        free(rgba);

        return ok ? tile : -1;
    }

    return TILE_COUNT;
}

/* Append one quad as two triangles and retain its texture binding separately
 * from the vertex data. Draw offsets are vertex indices, not byte offsets. */
static bool emit(DukeRenderer *r, const Vertex v[6], int tile) {
    if (r->count + 6 > VERTEX_LIMIT) {
        return false;
    }

    if (r->count + 6 > r->capacity) {
        size_t cap = r->capacity ? r->capacity * 2 : 4096;
        Vertex *p = realloc(r->vertices, cap * sizeof(*p));
        if (!p) {
            return false;
        }
        r->vertices = p;
        r->capacity = cap;
    }

    if (r->draw_count == r->draw_capacity) {
        size_t cap = r->draw_capacity ? r->draw_capacity * 2 : 256;
        Draw *p = realloc(r->draws, cap * sizeof(*p));
        if (!p) {
            return false;
        }
        r->draws = p;
        r->draw_capacity = cap;
    }
    r->draws[r->draw_count++] = (Draw){(int)r->count, 6, tile};
    memcpy(r->vertices + r->count, v, 6 * sizeof(*v));
    r->count += 6;

    return true;
}
static double surface(const DukeMapFile *m, int sector, bool floor, double x,
        double y) {
    const DukeMapSector *s = m->sectors[sector];
    double z = floor ? s->floorz : s->ceilingz;
    /* Slope bit 2 makes heinum a gradient perpendicular to the sector's
     * first wall. Dividing the cross product by length gives signed distance;
     * 256 converts the stored slope to Build Z units. */
    if ((floor ? s->floorstat : s->ceilingstat) & 2) {
        const DukeMapWall *a = m->walls[s->wallptr], *b = m->walls[a->point2];
        double dx = (double)b->x - a->x, dy = (double)b->y - a->y;
        double length = hypot(dx, dy);
        if (length > 0) {
            z += (floor ? s->floorheinum : s->ceilingheinum) *
                (dx * (y - a->y) - dy * (x - a->x)) / (length * 256.0);
        }
    }

    return z;
}

static Vertex vertex(double x, double y, double z, double u, double v,
        int shade) {
    /* Build Z points down and uses 16 times as many units as X/Y. Map it
     * to renderer Y-up, with 1024 horizontal map units per world unit.
     * Shade is approximated as brightness rather than a palette lookup. */
    float light = fmaxf(0.15f, fminf(1.0f, 1.0f - shade / 32.0f));
    return (Vertex){{x / 1024.0, -z / 16384.0, y / 1024.0}, {u, v}, light};
}

static double edge_x(const DukeMapFile *m, int wall, double y) {
    const DukeMapWall *a = m->walls[wall], *b = m->walls[a->point2];
    return a->x + (y - a->y) * ((double)b->x - a->x) / ((double)b->y - a->y);
}

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef struct Crossing {
    double x;
    int wall;
} Crossing;

static int compare_crossing(const void *a, const void *b) {
    return compare_double(&((const Crossing *)a)->x, &((const Crossing *)b)->x);
}

/* Horizontal bands have fixed edge order for simple loops. Pair crossings by
 * parity to tessellate concave outlines and holes without a triangle fan. */
static bool floors(DukeRenderer *r, Source *src, const DukeMapFile *m, int id) {
    const DukeMapSector *s = m->sectors[id];
    double *ys = malloc(s->wallnum * sizeof(*ys));
    Crossing *edges = malloc(s->wallnum * sizeof(*edges));
    if (!ys || !edges) {
        free(ys);
        free(edges);
        return false;
    }

    for (int i = 0; i < s->wallnum; i++) {
        ys[i] = m->walls[s->wallptr + i]->y;
    }

    /* Split at every vertex height so no boundary edge starts or ends
     * inside a band. Repeated heights produce empty bands and are skipped. */
    qsort(ys, s->wallnum, sizeof(*ys), compare_double);
    bool ok = true;
    for (int band = 0; band + 1 < s->wallnum && ok; band++) {
        double lo = ys[band], hi = ys[band + 1], mid = (lo + hi) * 0.5;
        if (lo == hi) {
            continue;
        }

        /* Sampling strictly inside the band avoids vertex ties and excludes
         * horizontal edges, keeping edge_x() denominators nonzero. */
        int n = 0;
        for (int i = s->wallptr; i < s->wallptr + s->wallnum; i++) {
            const DukeMapWall *a = m->walls[i], *b = m->walls[a->point2];
            if ((a->y > mid) != (b->y > mid)) {
                edges[n++] = (Crossing){edge_x(m, i, mid), i};
            }
        }

        qsort(edges, n, sizeof(*edges), compare_crossing);
        /* A closed outline must enter and leave the filled region in pairs. */
        if (n % 2) {
            ok = false;
            break;
        }

        /* Each filled interval becomes a trapezoid bounded by the same two
         * edges at both band limits. Evaluate slopes at all four corners. */
        for (int k = 0; k < n && ok; k += 2) {
            double x[4] = {
                edge_x(m, edges[k].wall, lo), edge_x(m, edges[k + 1].wall, lo),
                edge_x(m, edges[k + 1].wall, hi), edge_x(m, edges[k].wall, hi)};
            double y[4] = {lo, lo, hi, hi};
            for (int floor = 0; floor < 2 && ok; floor++) {
                int tile = texture(r, src, floor ? s->floorpicnum : s->ceilingpicnum);
                if (tile < 0) {
                    ok = false;
                    break;
                }
                const Texture *t = &r->textures[tile];
                int flags = floor ? s->floorstat : s->ceilingstat;
                /* Bit 8 doubles texture density; bits 4, 16 and 32 swap
                 * axes and flip U/V. Panning is a fraction of a full repeat. */
                double scale = (flags & 8) ? 8.0 : 16.0;
                Vertex quad[4], triangles[6];
                for (int j = 0; j < 4; j++) {
                    double u = x[j], v = y[j];
                    if (flags & 4) {
                        double tmp = u;
                        u = v;
                        v = tmp;
                    }
                    if (flags & 16) {
                        u = -u;
                    }
                    if (flags & 32) {
                        v = -v;
                    }
                    u = u / (scale * t->width) +
                        (floor ? s->floorxpanning : s->ceilingxpanning) / 256.0;
                    v = v / (scale * t->height) +
                        (floor ? s->floorypanning : s->ceilingypanning) / 256.0;
                    quad[j] = vertex(x[j], y[j], surface(m, id, floor, x[j], y[j]), u, v,
                            floor ? s->floorshade : s->ceilingshade);
                }
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int j = 0; j < 6; j++) {
                    triangles[j] = quad[order[j]];
                }
                ok = emit(r, triangles, tile);
            }
        }
    }
    free(ys);
    free(edges);

    return ok;
}

static bool wall_quad(DukeRenderer *r, Source *src, const DukeMapFile *m, int w,
        double top[2], double bottom[2], int pic) {
    /* Build heights increase downward. Empty spans need no geometry; if
     * only one end is closed, collapse that end rather than invert it. */
    if (bottom[0] <= top[0] && bottom[1] <= top[1]) {
        return true;
    }

    int tile = texture(r, src, pic);
    if (tile < 0) {
        return false;
    }

    const Texture *t = &r->textures[tile];
    const DukeMapWall *a = m->walls[w], *b = m->walls[a->point2];
    Vertex q[4], v[6];
    for (int j = 0; j < 4; j++) {
        int end = (j == 1 || j == 2);
        double z = j < 2 ? top[end] : fmax(top[end], bottom[end]);
        /* Horizontal repeat spans the whole wall regardless of its length.
         * Vertical mapping is relative to this span's top at each endpoint. */
        double u = (end ? a->xrepeat * 8.0 : 0.0) / t->width +
            a->xpanning / (double)t->width;
        double texv = (z - top[end]) * a->yrepeat / (2048.0 * t->height) +
            a->ypanning / 256.0;
        if (a->cstat & 8) {
            u = -u;
        }
        if (a->cstat & 256) {
            texv = -texv;
        }
        q[j] = vertex(end ? b->x : a->x, end ? b->y : a->y, z, u, texv, a->shade);
    }

    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int j = 0; j < 6; j++) {
        v[j] = q[order[j]];
    }

    return emit(r, v, tile);
}

static bool geometry(DukeRenderer *r, Source *src, const DukeMapFile *m) {
    for (int s = 0; s < m->numsectors; s++) {
        if (!floors(r, src, m, s)) {
            return false;
        }
        const DukeMapSector *sector = m->sectors[s];
        for (int w = sector->wallptr; w < sector->wallptr + sector->wallnum; w++) {
            const DukeMapWall *a = m->walls[w], *b = m->walls[a->point2];
            /* Sample both sectors at each endpoint to preserve sloped
             * ceilings/floors along the shared wall. nt/nb belong to the
             * neighboring sector and are only used for portal walls. */
            double top[2], bottom[2], nt[2], nb[2];
            for (int j = 0; j < 2; j++) {
                double x = j ? b->x : a->x, y = j ? b->y : a->y;
                top[j] = surface(m, s, false, x, y);
                bottom[j] = surface(m, s, true, x, y);
                if (a->nextsector >= 0) {
                    nt[j] = surface(m, a->nextsector, false, x, y);
                    nb[j] = surface(m, a->nextsector, true, x, y);
                }
            }
            /* Solid boundaries and one-way walls (bit 32) cover the full
             * sector height. Portals leave the overlapping opening clear. */
            if (a->nextsector < 0 || (a->cstat & 32)) {
                if (!wall_quad(r, src, m, w, top, bottom, a->picnum)) {
                    return false;
                }
            } else {
                /* Clamp neighboring heights to this sector: the strips
                 * above and below the opening use the main wall texture. */
                double upper[2], lower[2];
                for (int j = 0; j < 2; j++) {
                    upper[j] = fmin(bottom[j], fmax(top[j], nt[j]));
                    lower[j] = fmax(top[j], fmin(bottom[j], nb[j]));
                }
                if (!wall_quad(r, src, m, w, top, upper, a->picnum) ||
                        !wall_quad(r, src, m, w, lower, bottom, a->picnum)) {
                    return false;
                }
                /* Masked walls (bit 16) add an overlay across the opening.
                 * Transparent texels are discarded by the fragment shader. */
                if (a->cstat & 16) {
                    if (!wall_quad(r, src, m, w, upper, lower, a->overpicnum)) {
                        return false;
                    }
                }
            }
        }
    }

    return r->count > 0;
}

static bool pipeline(DukeRenderer *r, const DukeRendererDesc *desc) {
    sg_shader_desc sh = {0};
    sh.vertex_func.source =
        "#version 410\nlayout(location=0) in vec3 position;layout(location=1) in "
        "vec2 texcoord;layout(location=2) in float light;uniform mat4 mvp;out "
        "vec2 uv;out float brightness;void "
        "main(){gl_Position=mvp*vec4(position,1);uv=texcoord;brightness=light;}";
    /* Alpha is a binary cutout here: surviving fragments are opaque and
     * write depth. This pipeline does not blend translucent surfaces. */
    sh.fragment_func.source =
        "#version 410\nuniform sampler2D tex;in vec2 uv;in float brightness;out "
        "vec4 frag;void main(){vec4 "
        "c=texture(tex,uv);if(c.a<0.5)discard;frag=vec4(c.rgb*brightness,1);}";
    sh.uniform_blocks[0] = (sg_shader_uniform_block){
        .stage = SG_SHADERSTAGE_VERTEX,
        .size = 64,
        .glsl_uniforms[0] = {.type = SG_UNIFORMTYPE_MAT4, .glsl_name = "mvp"}};
    sh.views[0].texture =
        (sg_shader_texture_view){.stage = SG_SHADERSTAGE_FRAGMENT,
            .image_type = SG_IMAGETYPE_2D,
            .sample_type = SG_IMAGESAMPLETYPE_FLOAT};
    sh.samplers[0] =
        (sg_shader_sampler){.stage = SG_SHADERSTAGE_FRAGMENT,
            .sampler_type = SG_SAMPLERTYPE_FILTERING};
    sh.texture_sampler_pairs[0] = (sg_shader_texture_sampler_pair){
        .stage = SG_SHADERSTAGE_FRAGMENT, .glsl_name = "tex"};
    r->shader = sg_make_shader(&sh);
    if (sg_query_shader_state(r->shader) != SG_RESOURCESTATE_VALID) {
        return false;
    }
    sg_pipeline_desc p = {.shader = r->shader,
        .depth = {.pixel_format = desc->depth_format,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true},
        .sample_count = desc->sample_count};
    p.colors[0].pixel_format = desc->color_format;
    p.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;
    r->pipeline = sg_make_pipeline(&p);
    /* Nearest sampling preserves ART pixels; repeats support UVs outside
     * [0, 1] from wall repeats and world-aligned floor/ceiling coordinates. */
    r->sampler =
        sg_make_sampler(&(sg_sampler_desc){.min_filter = SG_FILTER_NEAREST,
                .mag_filter = SG_FILTER_NEAREST,
                .wrap_u = SG_WRAP_REPEAT,
                .wrap_v = SG_WRAP_REPEAT});

    return sg_query_pipeline_state(r->pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(r->sampler) == SG_RESOURCESTATE_VALID;
}

DukeRenderer *duke_renderer_create(DukeMapFile *map, DukeGrpFile *grp,
        const DukeRendererDesc *desc, char *error,
        size_t error_size) {
    const char *message = "Invalid renderer arguments or graphics context";
    DukeRenderer *r = NULL;
    Source src = {0};
    if (error && error_size) {
        error[0] = 0;
    }
    if (!map || !grp || !desc || !sg_isvalid()) {
        goto fail;
    }
    if (sg_query_backend() != SG_BACKEND_GLCORE &&
            sg_query_backend() != SG_BACKEND_DUMMY) {
        message = "Renderer currently requires the OpenGL backend";
        goto fail;
    }
    if (!duke_map_file_validate_references(map) || !map->numsectors) {
        message = "Map has invalid references or no sectors";
        goto fail;
    }
    r = calloc(1, sizeof(*r));
    if (!r) {
        message = "Unable to allocate renderer";
        goto fail;
    }
    message = "Unable to load GRP ART files and PALETTE.DAT";
    if (!source_load(&src, grp)) {
        goto fail;
    }
    /* Create the fallback before tessellation, which may reference it and
     * lazily upload every real tile needed by the generated surfaces. */
    const uint8_t checker[16] = {255, 0,  255, 255, 20,  20, 20,  255,
        20,  20, 20,  255, 255, 0,  255, 255};
    message = "Unable to create textures or sector geometry (check Sokol "
        "resource pools)";
    if (!upload(&r->textures[TILE_COUNT], 2, 2, checker) ||
            !geometry(r, &src, map)) {
        goto fail;
    }
    message = "Unable to create renderer GPU pipeline/buffer";
    if (!pipeline(r, desc)) {
        goto fail;
    }
    r->buffer = sg_make_buffer(
            &(sg_buffer_desc){.data = {r->vertices, r->count * sizeof(Vertex)}});
    if (sg_query_buffer_state(r->buffer) != SG_RESOURCESTATE_VALID) {
        goto fail;
    }
    /* GPU uploads own their data now; retain only draw ranges and resource
     * handles for subsequent frames. */
    free(r->vertices);
    r->vertices = NULL;
    source_free(&src);
    return r;
    /* Both cleanup routines accept partially initialized, zero-filled state. */
fail:
    if (error && error_size) {
        snprintf(error, error_size, "%s", message);
    }
    source_free(&src);
    duke_renderer_destroy(r);

    return NULL;
}

void duke_renderer_draw(DukeRenderer *r, const float mvp[16]) {
    if (!r || !mvp) {
        return;
    }

    /* The host may issue graphics calls between frames. Invalidate Sokol
     * cached bindings so this draw reapplies the state it depends on. */
    sg_reset_state_cache();
    sg_apply_pipeline(r->pipeline);
    sg_apply_uniforms(0, &(sg_range){mvp, 16 * sizeof(float)});
    for (size_t i = 0; i < r->draw_count; i++) {
        Draw d = r->draws[i];
        sg_bindings b = {.vertex_buffers[0] = r->buffer,
            .views[0] = r->textures[d.tile].view,
            .samplers[0] = r->sampler};
        sg_apply_bindings(&b);
        sg_draw(d.first, d.count, 1);
    }
}

void duke_renderer_destroy(DukeRenderer *r) {
    if (!r) {
        return;
    }
    if (r->buffer.id) {
        sg_destroy_buffer(r->buffer);
    }
    if (r->pipeline.id) {
        sg_destroy_pipeline(r->pipeline);
    }
    if (r->shader.id) {
        sg_destroy_shader(r->shader);
    }
    if (r->sampler.id) {
        sg_destroy_sampler(r->sampler);
    }

    /* Include the fallback slot and destroy views before their images. */
    for (int i = 0; i <= TILE_COUNT; i++) {
        if (r->textures[i].view.id) {
            sg_destroy_view(r->textures[i].view);
        }
        if (r->textures[i].image.id) {
            sg_destroy_image(r->textures[i].image);
        }
    }
    free(r->draws);
    free(r->vertices);
    free(r);
}
