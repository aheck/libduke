#include "libduke/renderer.h"
#include "libduke/art.h"
#include "libduke/palette.h"
#include "libduke/palette_lookup.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Geometry is built once. Face-sprite offsets are oriented by the vertex
 * shader; each frame only sorts blended draw ranges and binds cached resources.
 */
#define TILE_COUNT 6144
#define VERTEX_LIMIT 8000000

typedef struct Vertex {
    float p[3], uv[2], light;
    float billboard[2], alpha;
} Vertex;
typedef struct Texture {
    sg_image image;
    sg_view view;
    int width, height;
    int xoffset, yoffset;
    uint8_t *alpha;
} Texture;
typedef struct Sky {
    Texture texture;
    int picnum;
    float vertical_scale;
} Sky;
typedef struct TextureVariant {
    int tile, palette;
    Texture texture;
} TextureVariant;
typedef struct Draw {
    int first, count, tile;
    bool sprite, translucent, one_sided;
    float center[3], depth;
    DukeSurfaceKind surface;
    int sector, wall, sprite_index;
    int sky; /* One-based sky atlas index; zero is an ordinary surface. */
    float sky_pan[2];
} Draw;
typedef struct PickFace {
    Draw draw;
    double key;
} PickFace;
typedef struct PickNode {
    double lo[3], hi[3];
    int first, count, left, right;
} PickNode;
struct DukeRenderer {
    PickFace *faces;
    PickNode *nodes;
    int face_count, node_count;
    bool hover_enabled, pointer_valid, sprite_picking;
    float pointer[2];
    DukeSurfaceHit hit, selection;
    sg_shader shader;
    sg_pipeline pipeline;
    sg_pipeline sprite_pipelines[4];
    sg_sampler sampler;
    sg_sampler sky_sampler;
    sg_buffer buffer;
    /* The extra slot holds the checkerboard used for missing/invalid tiles. */
    Texture textures[TILE_COUNT + 1];
    TextureVariant *variants;
    size_t variant_count;
    Sky *skies;
    size_t sky_count;
    Vertex *vertices;
    size_t count, capacity;
    Draw *draws;
    size_t draw_count, draw_capacity;
};
typedef struct Source {
    DukeArtFile **art;
    size_t count;
    DukePaletteFile *palette;
    DukePaletteLookupFile *lookup;
} Source;

static bool upload(Texture *t, int width, int height, const void *rgba);

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
    duke_palette_lookup_free(s->lookup);
}
/* Keep ART sources alive while geometry requests tiles lazily. On failure,
 * the caller releases any partially loaded source with source_free(). */
static bool source_load(Source *s, DukeGrpFile *grp) {
    s->palette = duke_palette_new();
    s->lookup = duke_palette_lookup_new();
    if (!s->palette || !s->lookup) {
        return false;
    }
    bool found = false, found_lookup = false;
    for (uint32_t i = 0; i < grp->header.entry_count; i++) {
        DukeGrpFileEntry *e = duke_grp_get_entry_by_index(grp, i);
        if (!e) {
            return false;
        }
        size_t len = strlen(e->filename);
        bool palette = named(e->filename, "PALETTE.DAT");
        bool lookup = named(e->filename, "LOOKUP.DAT");
        if (!palette && !lookup && (len < 4 || !named(e->filename + len - 4, ".ART"))) {
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
        } else if (lookup) {
            if (!duke_palette_lookup_read_from_memory(s->lookup, data, size)) {
                return false;
            }
            found_lookup = true;
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
            DukeArtFile **next =
                realloc(s->art, (s->count + 1) * sizeof(*next));
            if (!next) {
                duke_art_free(art);
                return false;
            }
            s->art = next;
            s->art[s->count++] = art;
        }
    }
    return found && found_lookup;
}

static Texture *texture_at(DukeRenderer *r, int id) {
    return id <= TILE_COUNT ? &r->textures[id]
                            : &r->variants[id - TILE_COUNT - 1].texture;
}

static int texture_variant(DukeRenderer *r, Source *s, int tile, int palette) {
    for (size_t i = 0; i < r->variant_count; i++) {
        if (r->variants[i].tile == tile && r->variants[i].palette == palette) {
            return TILE_COUNT + 1 + (int)i;
        }
    }
    if (palette == 0) return tile;
    for (size_t i = s->count; i > 0; i--) {
        DukeArtTile *a = duke_art_get_tile_by_number(s->art[i - 1], tile);
        if (!a || a->width <= 0 || a->height <= 0) continue;
        void *pixels = NULL;
        size_t size = duke_art_get_tile_data_by_number(s->art[i - 1], tile, &pixels);
        if (size == (size_t)-1) return -1;
        size_t count = (size_t)a->width * a->height;
        uint8_t *mapped = malloc(count), *rgba = malloc(count * 4);
        if (!mapped || !rgba) { free(mapped); free(rgba); return -1; }
        for (size_t p = 0; p < count; p++) {
            if (!duke_palette_lookup_get_index(s->lookup, palette,
                                                ((uint8_t *)pixels)[p], &mapped[p])) {
                free(mapped); free(rgba); return -1;
            }
        }
        bool ok = duke_art_tile_to_rgba(a, mapped, count, s->palette,
                                        rgba, count * 4);
        if (!ok) { free(mapped); free(rgba); return -1; }
        TextureVariant *next = realloc(r->variants,
            (r->variant_count + 1) * sizeof(*next));
        if (!next) { free(mapped); free(rgba); return -1; }
        r->variants = next;
        TextureVariant *v = &r->variants[r->variant_count++];
        v->tile = tile; v->palette = palette;
        if (!upload(&v->texture, a->width, a->height, rgba)) {
            free(mapped); free(rgba); return -1;
        }
        v->texture.xoffset = (int8_t)(a->picanm >> 8);
        v->texture.yoffset = (int8_t)(a->picanm >> 16);
        free(mapped); free(rgba);
        return TILE_COUNT + (int)r->variant_count;
    }
    return TILE_COUNT;
}

static bool upload(Texture *t, int width, int height, const void *rgba) {
    t->alpha = malloc((size_t)width * height);
    if (!t->alpha) {
        return false;
    }
    for (size_t i = 0; i < (size_t)width * height; i++) {
        t->alpha[i] = ((const uint8_t *)rgba)[i * 4 + 3];
    }
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

        bool ok =
            duke_art_tile_to_rgba(a, pixels, size, s->palette, rgba, bytes) &&
            upload(t, a->width, a->height, rgba);
        free(rgba);

        t->xoffset = (int8_t)(a->picanm >> 8);
        t->yoffset = (int8_t)(a->picanm >> 16);
        return ok ? tile : -1;
    }

    return TILE_COUNT;
}

/* Duke's backdrop consists of eight 45-degree panels. Ordinary tiles repeat;
 * the three stock panoramas select additional ART tiles in this order.
 * See setupbackdrop in https://github.com/icculus/duke3d/blob/main/source/premap.c.
 */
static int sky_panel(int picnum, int panel) {
    static const int moon[8] = {0, 2, 3, 0, 2, 0, 1, 0};
    static const int orbit[8] = {0, 0, 4, 0, 0, 1, 2, 3};
    static const int city[8] = {1, 2, 1, 3, 4, 0, 2, 3};
    const int *offsets = picnum == 80 ? moon : picnum == 84 ? orbit :
                         picnum == 89 ? city : NULL;
    return picnum + (offsets ? offsets[panel] : 0);
}

/* Cache a horizontal panorama independently of the ordinary tile texture.
 * Missing panels retain the checkerboard and mismatched replacement ART is
 * sampled at the base tile's size. No GRP data is needed after creation. */
static int sky_texture(DukeRenderer *r, Source *src, int picnum, int tile) {
    for (size_t i = 0; i < r->sky_count; i++) {
        if (r->skies[i].picnum == picnum) {
            return (int)i + 1;
        }
    }
    int width = r->textures[tile].width, height = r->textures[tile].height;
    int limit = sg_query_limits().max_image_size_2d;
    if (width <= 0 || height <= 0 ||
        (limit > 0 && width > limit / 8)) {
        return -1;
    }
    size_t stride = (size_t)width * 8 * 4;
    if ((size_t)height > SIZE_MAX / stride) {
        return -1;
    }
    uint8_t *rgba = malloc(stride * height);
    if (!rgba) {
        return -1;
    }
    for (int panel = 0; panel < 8; panel++) {
        int number = sky_panel(picnum, panel);
        DukeArtTile *art = NULL;
        DukeArtFile *owner = NULL;
        for (size_t i = src->count; i > 0 && number >= 0 && number < TILE_COUNT; i--) {
            art = duke_art_get_tile_by_number(src->art[i - 1], number);
            if (art && art->width > 0 && art->height > 0) {
                owner = src->art[i - 1];
                break;
            }
        }
        void *data = NULL;
        if (owner && duke_art_get_tile_data_by_number(owner, number, &data) ==
                         (size_t)-1) {
            free(rgba);
            return -1;
        }
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t *pixel = rgba + y * stride + (panel * width + x) * 4;
                if (owner) {
                    int ax = (int)((int64_t)x * art->width / width);
                    int ay = (int)((int64_t)y * art->height / height);
                    uint8_t index = ((uint8_t *)data)[(size_t)ax * art->height + ay];
                    const DukePaletteColor *color = &src->palette->colors[index];
                    pixel[0] = (color->red << 2) | (color->red >> 4);
                    pixel[1] = (color->green << 2) | (color->green >> 4);
                    pixel[2] = (color->blue << 2) | (color->blue >> 4);
                } else {
                    bool dark = ((x * 2 / width) ^ (y * 2 / height)) != 0;
                    pixel[0] = pixel[2] = dark ? 20 : 255;
                    pixel[1] = dark ? 20 : 0;
                }
                /* The sky is opaque, including palette index 255. */
                pixel[3] = 255;
            }
        }
    }
    Sky *skies = realloc(r->skies, (r->sky_count + 1) * sizeof(*skies));
    if (!skies) {
        free(rgba);
        return -1;
    }
    r->skies = skies;
    Sky *sky = &r->skies[r->sky_count++];
    *sky = (Sky){.picnum = picnum,
                 .vertical_scale = (picnum == 89 ? 0.265625f :
                                    picnum == 78 ? 1.0f : 0.5f) * 256 / height};
    bool ok = upload(&sky->texture, width * 8, height, rgba);
    free(rgba);
    return ok ? (int)r->sky_count : -1;
}

static bool sector_sky(DukeRenderer *r, Source *src, const DukeMapSector *s,
                       bool floor, Draw *d) {
    d->sky = sky_texture(r, src, floor ? s->floorpicnum : s->ceilingpicnum,
                         d->tile);
    d->sky_pan[0] = (floor ? s->floorxpanning : s->ceilingxpanning) / 256.0f;
    d->sky_pan[1] = (floor ? s->floorypanning : s->ceilingypanning) / 256.0f;
    return d->sky > 0;
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
    r->draws[r->draw_count++] =
        (Draw){.first = (int)r->count, .count = 6, .tile = tile};
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
    return (Vertex){.p = {x / 1024.0, -z / 16384.0, y / 1024.0},
                    .uv = {u, v},
                    .light = light,
                    .alpha = 1};
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
                int tile =
                    texture(r, src, floor ? s->floorpicnum : s->ceilingpicnum);
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
                    /* Build's world mapping runs V against world Y. Relative
                     * mapping instead measures along/across the first wall. */
                    double u = x[j], v = -y[j];
                    if (flags & 64) {
                        const DukeMapWall *first = m->walls[s->wallptr];
                        const DukeMapWall *next = m->walls[first->point2];
                        double dx = (double)next->x - first->x;
                        double dy = (double)next->y - first->y;
                        double length = hypot(dx, dy);
                        double px = x[j] - first->x, py = y[j] - first->y;
                        if (length > 0) {
                            u = (px * dx + py * dy) / length;
                            v = (py * dx - px * dy) / length;
                        } else {
                            /* Degenerate effect sectors must not produce NaNs. */
                            u = v = 0;
                        }
                        if (flags & 2) {
                            double gradient = (floor ? s->floorheinum : s->ceilingheinum) / 4096.0;
                            v *= hypot(1.0, gradient);
                        }
                    }
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
                    quad[j] =
                        vertex(x[j], y[j], surface(m, id, floor, x[j], y[j]), u,
                               v, floor ? s->floorshade : s->ceilingshade);
                }
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int j = 0; j < 6; j++) {
                    triangles[j] = quad[order[j]];
                }
                ok = emit(r, triangles, tile);
                if (ok) {
                    Draw *d = &r->draws[r->draw_count - 1];
                    d->surface =
                        floor ? DUKE_SURFACE_FLOOR : DUKE_SURFACE_CEILING;
                    d->sector = id;
                    d->wall = -1;
                    if (flags & 1) {
                        ok = sector_sky(r, src, s, floor, d);
                    }
                }
            }
        }
    }
    free(ys);
    free(edges);

    return ok;
}

/* A wall's UV anchor is a base sector height, never the sloping edge's
 * height at an endpoint. Masked openings have their own shared-span anchors. */
typedef enum { WALL_SOLID, WALL_UPPER, WALL_LOWER, WALL_MASKED, WALL_ONE_WAY } WallBand;

static bool wall_quad(DukeRenderer *r, Source *src, const DukeMapFile *m,
                      int sector, int w, double top[2], double bottom[2],
                      int pic, WallBand band) {
    /* Build heights increase downward. Empty spans need no geometry; if
     * only one end is closed, collapse that end rather than invert it. */
    if (bottom[0] <= top[0] && bottom[1] <= top[1]) {
        return true;
    }

    const DukeMapWall *a = m->walls[w], *b = m->walls[a->point2];
    const DukeMapSector *own = m->sectors[sector];
    const DukeMapSector *neighbor = a->nextsector >= 0 ? m->sectors[a->nextsector] : own;
    bool floor = band == WALL_LOWER;
    bool sky = (band == WALL_UPPER || floor) && a->nextsector >= 0 &&
               ((floor ? own->floorstat : own->ceilingstat) & 1) &&
               ((floor ? neighbor->floorstat : neighbor->ceilingstat) & 1);
    if (sky) {
        pic = floor ? own->floorpicnum : own->ceilingpicnum;
    }
    /* Bottom swap borrows appearance, not geometry or repeat values. Build's
     * horizontal flip also remains on the visible wall. */
    const DukeMapWall *material = a;
    if (!sky && band == WALL_LOWER && (a->cstat & 2) && a->nextwall >= 0 &&
        a->nextwall < m->numwalls) {
        material = m->walls[a->nextwall];
        pic = material->picnum;
    }
    int tile = texture(r, src, pic);
    if (tile < 0) {
        return false;
    }

    const Texture *t = texture_at(r, tile);
    Vertex q[4], v[6];
    for (int j = 0; j < 4; j++) {
        int end = (j == 1 || j == 2);
        double z = j < 2 ? top[end] : fmax(top[end], bottom[end]);
        /* X-flip reverses the repeat span but retains the panning origin. */
        double u = ((end != !!(a->cstat & 8) ? a->xrepeat * 8.0 : 0.0) +
                    material->xpanning) / t->width;
        bool aligned = (material->cstat & 4) != 0;
        double origin;
        if (band == WALL_LOWER) {
            origin = aligned ? own->ceilingz : neighbor->floorz;
        } else if (band == WALL_UPPER || band == WALL_ONE_WAY) {
            origin = aligned ? own->ceilingz : neighbor->ceilingz;
        } else if (band == WALL_MASKED) {
            origin = aligned ? fmin(own->floorz, neighbor->floorz)
                             : fmax(own->ceilingz, neighbor->ceilingz);
        } else {
            origin = aligned ? own->floorz : own->ceilingz;
        }
        double texv = (z - origin) * a->yrepeat / (2048.0 * t->height) +
                      material->ypanning / 256.0;
        if (material->cstat & 256) {
            texv = -texv;
        }
        q[j] =
            vertex(end ? b->x : a->x, end ? b->y : a->y, z, u, texv,
                   sky ? (floor ? own->floorshade : own->ceilingshade)
                       : material->shade);
    }

    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int j = 0; j < 6; j++) {
        v[j] = q[order[j]];
    }

    if (!emit(r, v, tile)) {
        return false;
    }
    Draw *d = &r->draws[r->draw_count - 1];
    d->surface = DUKE_SURFACE_WALL;
    d->sector = sector;
    d->wall = w;
    if (sky) {
        /* Unequal sky surfaces meet at a sky curtain, not a textured wall.
         * Keep this boundary filled so the host's clear color cannot leak in. */
        d->surface = floor ? DUKE_SURFACE_FLOOR : DUKE_SURFACE_CEILING;
        d->wall = -1;
        return sector_sky(r, src, own, floor, d);
    }
    return true;
}

static bool geometry(DukeRenderer *r, Source *src, const DukeMapFile *m) {
    for (int s = 0; s < m->numsectors; s++) {
        if (!floors(r, src, m, s)) {
            return false;
        }
        const DukeMapSector *sector = m->sectors[s];
        for (int w = sector->wallptr; w < sector->wallptr + sector->wallnum;
             w++) {
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
                if (!wall_quad(r, src, m, s, w, top, bottom,
                               a->nextsector < 0 ? a->picnum : a->overpicnum,
                               a->nextsector < 0 ? WALL_SOLID : WALL_ONE_WAY)) {
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
                if (!wall_quad(r, src, m, s, w, top, upper, a->picnum, WALL_UPPER) ||
                    !wall_quad(r, src, m, s, w, lower, bottom, a->picnum, WALL_LOWER)) {
                    return false;
                }
                /* Masked walls (bit 16) add an overlay across the opening.
                 * Transparent texels are discarded by the fragment shader. */
                if (a->cstat & 16) {
                    if (!wall_quad(r, src, m, s, w, upper, lower,
                                   a->overpicnum, WALL_MASKED)) {
                        return false;
                    }
                }
            }
        }
    }

    return r->count > 0;
}

/* Sprite repeats scale ART pixels by 1/4 in horizontal Build coordinates.
 * Vertical coordinates have 16x precision. Offsets include the signed ART
 * animation pivot; face/wall sprites are bottom-anchored unless bit 128 is set.
 */
static bool sprite_quad(DukeRenderer *r, const DukeMapSprite *s, int tile) {
    const Texture *t = texture_at(r, tile);
    int alignment = s->cstat & 48;
    double angle = s->ang * (6.283185307179586 / 2048.0);
    double rx = sin(angle), ry = -cos(angle);
    double xoff = t->xoffset + s->xoffset, yoff = t->yoffset + s->yoffset;
    if (s->cstat & 4) {
        xoff = -xoff;
    }
    if (s->cstat & 8) {
        yoff = -yoff;
    }
    Vertex q[4], v[6];
    for (int j = 0; j < 4; j++) {
        bool right = j == 1 || j == 2, bottom = j >= 2;
        double u = right ? 1 : 0, texv = bottom ? 1 : 0;
        double x = (u * t->width - t->width * 0.5 - xoff) * s->xrepeat / 4.0;
        double y =
            (texv * t->height - t->height * 0.5 - yoff) * s->yrepeat / 4.0;
        double z = s->z;
        if (alignment != 32) {
            z += ((texv - 1) * t->height - yoff +
                  ((s->cstat & 128) ? t->height * 0.5 : 0)) *
                 s->yrepeat * 4.0;
        }
        if (s->cstat & 4) {
            u = 1 - u;
        }
        if (s->cstat & 8) {
            texv = 1 - texv;
        }
        if (alignment == 0) {
            q[j] = vertex(s->x, s->y, s->z, u, texv, s->shade);
            q[j].billboard[0] = x / 1024.0;
            q[j].billboard[1] = -(z - s->z) / 16384.0;
        } else if (alignment == 16) {
            q[j] = vertex(s->x + rx * x, s->y + ry * x, z, u, texv, s->shade);
        } else {
            q[j] = vertex(s->x + rx * x - ry * y, s->y + ry * x + rx * y, z, u,
                          texv, s->shade);
        }
        q[j].alpha = (s->cstat & 2)
                         ? ((s->cstat & 512) ? 1.0f / 3.0f : 2.0f / 3.0f)
                         : 1.0f;
    }
    /* Front faces: wall sprites face along their Build angle;
     * floor sprites face up, or down when Y-flipped. */
    bool reverse = alignment != 32 || !(s->cstat & 8);
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int j = 0; j < 6; j++) {
        v[j] = q[order[reverse ? (j / 3 * 3 + 2 - j % 3) : j]];
    }
    if (!emit(r, v, tile)) {
        return false;
    }
    Draw *d = &r->draws[r->draw_count - 1];
    d->sprite = true;
    d->translucent = (s->cstat & 2) != 0;
    d->one_sided = alignment != 0 && (s->cstat & 64) != 0;
    d->center[0] = s->x / 1024.0f;
    d->center[1] = -s->z / 16384.0f;
    d->center[2] = s->y / 1024.0f;
    return true;
}
static bool sprites(DukeRenderer *r, Source *src, const DukeMapFile *m) {
    for (int i = 0; i < m->numsprites; i++) {
        const DukeMapSprite *s = m->sprites[i];
        if ((s->cstat & 32768) || !s->xrepeat || !s->yrepeat ||
            (s->cstat & 48) == 48 || s->sectnum < 0 ||
            s->sectnum >= m->numsectors || s->statnum == MAP_MAXSTATUS) {
            continue;
        }
        int tile = texture(r, src, s->picnum);
        if (tile >= 0) tile = texture_variant(r, src, tile, s->pal);
        if (tile < 0 || !sprite_quad(r, s, tile)) {
            return false;
        }
        Draw *d = &r->draws[r->draw_count - 1];
        d->surface = DUKE_SURFACE_SPRITE;
        d->sector = s->sectnum;
        d->wall = -1;
        d->sprite_index = i;
    }
    return true;
}
static int compare_draw(const void *a, const void *b) {
    const Draw *x = a, *y = b;
    if (x->translucent != y->translucent) {
        return x->translucent ? 1 : -1;
    }
    if (x->translucent && x->depth != y->depth) {
        return x->depth < y->depth ? 1 : -1;
    }
    return (x->first > y->first) - (x->first < y->first);
}

static DukeSurfaceHit no_hit(void) {
    return (DukeSurfaceHit){.kind = DUKE_SURFACE_NONE,
                            .sector_index = -1,
                            .wall_index = -1,
                            .sprite_index = -1};
}
void duke_renderer_set_hover_enabled(DukeRenderer *r, bool enabled) {
    if (r) {
        r->hover_enabled = enabled;
        if (!enabled) {
            r->hit = no_hit();
        }
    }
}
void duke_renderer_set_sprite_picking_enabled(DukeRenderer *r, bool enabled) {
    if (r) {
        r->sprite_picking = enabled;
        r->hit = no_hit();
    }
}
void duke_renderer_set_pointer(DukeRenderer *r, float x, float y) {
    if (!r) {
        return;
    }
    r->pointer[0] = x;
    r->pointer[1] = y;
    r->pointer_valid =
        isfinite(x) && isfinite(y) && fabsf(x) <= 1 && fabsf(y) <= 1;
    r->hit = no_hit();
}
bool duke_renderer_get_hovered_surface(const DukeRenderer *r,
                                       DukeSurfaceHit *hit) {
    if (hit) {
        *hit = no_hit();
    }
    if (!r || !hit || !r->hover_enabled || r->hit.kind == DUKE_SURFACE_NONE) {
        return false;
    }
    *hit = r->hit;
    return true;
}
static bool matches_surface(const DukeSurfaceHit *hit, const Draw *d) {
    if (hit->kind == DUKE_SURFACE_NONE) {
        return false;
    }
    if (d->sprite) {
        return hit->kind == DUKE_SURFACE_SPRITE &&
               hit->sprite_index == d->sprite_index;
    }
    return hit->kind == d->surface && hit->sector_index == d->sector &&
           hit->wall_index == d->wall;
}
bool duke_renderer_set_selected_surface(DukeRenderer *r,
                                        const DukeSurfaceHit *hit) {
    if (!r) {
        return false;
    }
    r->selection = no_hit();
    if (!hit || hit->kind == DUKE_SURFACE_NONE) {
        return true;
    }
    for (size_t i = 0; i < r->draw_count; ++i) {
        const Draw *d = &r->draws[i];
        if (matches_surface(hit, d)) {
            r->selection = (DukeSurfaceHit){.kind = hit->kind,
                .sector_index = d->sector, .wall_index = d->wall,
                .sprite_index = d->sprite ? d->sprite_index : -1};
            return true;
        }
    }
    return false;
}
bool duke_renderer_get_selected_surface(const DukeRenderer *r,
                                        DukeSurfaceHit *hit) {
    if (hit) {
        *hit = no_hit();
    }
    if (!r || !hit || r->selection.kind == DUKE_SURFACE_NONE) {
        return false;
    }
    *hit = r->selection;
    return true;
}
static int pick_compare(const void *a, const void *b) {
    double x = ((const PickFace *)a)->key, y = ((const PickFace *)b)->key;
    return (x > y) - (x < y);
}
static int build_node(DukeRenderer *r, int first, int count, int depth) {
    int id = r->node_count++;
    PickNode *n = &r->nodes[id];
    n->first = first;
    n->count = count;
    n->left = n->right = -1;
    for (int k = 0; k < 3; k++) {
        n->lo[k] = INFINITY;
        n->hi[k] = -INFINITY;
    }
    for (int i = first; i < first + count; i++) {
        const Draw *d = &r->faces[i].draw;
        for (int v = 0; v < d->count; v++) {
            for (int k = 0; k < 3; k++) {
                double p = r->vertices[d->first + v].p[k];
                n->lo[k] = fmin(n->lo[k], p);
                n->hi[k] = fmax(n->hi[k], p);
            }
        }
    }
    if (count <= 8 || depth >= 32) {
        return id;
    }
    int axis = 0;
    for (int k = 1; k < 3; k++) {
        if (n->hi[k] - n->lo[k] > n->hi[axis] - n->lo[axis]) {
            axis = k;
        }
    }
    for (int i = first; i < first + count; i++) {
        const Draw *d = &r->faces[i].draw;
        double sum = 0;
        for (int v = 0; v < d->count; v++) {
            sum += r->vertices[d->first + v].p[axis];
        }
        r->faces[i].key = sum / d->count;
    }
    qsort(r->faces + first, count, sizeof(*r->faces), pick_compare);
    n->left = build_node(r, first, count / 2, depth + 1);
    n->right = build_node(r, first + count / 2, count - count / 2, depth + 1);
    return id;
}
static bool build_picking(DukeRenderer *r) {
    r->faces = calloc(r->draw_count, sizeof(*r->faces));
    r->nodes = calloc(r->draw_count * 2 + 1, sizeof(*r->nodes));
    if (!r->faces || !r->nodes) {
        return false;
    }
    for (size_t i = 0; i < r->draw_count; i++) {
        if (!r->draws[i].sprite) {
            r->faces[r->face_count++].draw = r->draws[i];
        }
    }
    if (r->face_count) {
        build_node(r, 0, r->face_count, 0);
    }
    return true;
}
/* Invert the host's matrix with pivoting; supports perspective and orthographic
 * projections and rejects singular/nonfinite matrices instead of stale hits. */
static bool inverse_matrix(const float m[16], double inverse[16]) {
    double a[4][8];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            a[row][col] = col < 4 ? m[col * 4 + row] : (col - 4 == row);
            if (!isfinite(a[row][col])) {
                return false;
            }
        }
    }
    for (int col = 0; col < 4; col++) {
        int pivot = col;
        for (int row = col + 1; row < 4; row++) {
            if (fabs(a[row][col]) > fabs(a[pivot][col])) {
                pivot = row;
            }
        }
        if (fabs(a[pivot][col]) < 1e-15) {
            return false;
        }
        for (int k = 0; k < 8; k++) {
            double temp = a[col][k];
            a[col][k] = a[pivot][k];
            a[pivot][k] = temp;
        }
        double scale = a[col][col];
        for (int k = 0; k < 8; k++) {
            a[col][k] /= scale;
        }
        for (int row = 0; row < 4; row++) {
            if (row != col) {
                double f = a[row][col];
                for (int k = 0; k < 8; k++) {
                    a[row][k] -= f * a[col][k];
                }
            }
        }
    }
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            inverse[col * 4 + row] = a[row][col + 4];
        }
    }
    return true;
}
static bool pointer_ray(const float m[16], const float pointer[2], double o[3],
                        double dir[3], double *limit) {
    double inverse[16];
    if (!inverse_matrix(m, inverse)) {
        return false;
    }
    double points[2][3];
    for (int end = 0; end < 2; end++) {
        /* An interior far sample also supports infinite-far projections. */
        double clip[4] = {pointer[0], pointer[1], end ? 0.999999 : -1, 1},
               p[4] = {0};
        for (int row = 0; row < 4; row++) {
            for (int k = 0; k < 4; k++) {
                p[row] += inverse[k * 4 + row] * clip[k];
            }
        }
        if (fabs(p[3]) < 1e-15) {
            return false;
        }
        for (int k = 0; k < 3; k++) {
            points[end][k] = p[k] / p[3];
        }
    }
    double length = 0;
    for (int k = 0; k < 3; k++) {
        o[k] = points[0][k];
        dir[k] = points[1][k] - o[k];
        length += dir[k] * dir[k];
    }
    length = sqrt(length);
    if (!isfinite(length) || length < 1e-12) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        dir[k] /= length;
    }
    *limit = length;
    return true;
}
static void cross3(const double a[3], const double b[3], double out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
static double dot3(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static bool ray_box(const PickNode *n, const double o[3], const double dir[3],
                    double limit) {
    double near = 0, far = limit;
    for (int k = 0; k < 3; k++) {
        if (fabs(dir[k]) < 1e-15) {
            if (o[k] < n->lo[k] - 1e-7 || o[k] > n->hi[k] + 1e-7) {
                return false;
            }
        } else {
            double a = (n->lo[k] - o[k]) / dir[k],
                   b = (n->hi[k] - o[k]) / dir[k];
            near = fmax(near, fmin(a, b));
            far = fmin(far, fmax(a, b));
            if (near > far + 1e-7) {
                return false;
            }
        }
    }
    return true;
}
static void pick_draw(DukeRenderer *r, const Draw *d, const float mvp[16],
                      const double o[3], const double dir[3], double *nearest) {
    if (d->sprite && d->translucent && !r->sprite_picking) {
        return;
    }
    double rx = mvp[0], rz = mvp[8], length = hypot(rx, rz);
    if (length > 0.00001) {
        rx /= length;
        rz /= length;
    } else {
        rx = 1;
        rz = 0;
    }
    for (int i = 0; i < d->count; i += 3) {
        const Vertex *v = &r->vertices[d->first + i];
        double p[3][3];
        for (int j = 0; j < 3; j++) {
            p[j][0] = v[j].p[0] + rx * v[j].billboard[0];
            p[j][1] = v[j].p[1] + v[j].billboard[1];
            p[j][2] = v[j].p[2] + rz * v[j].billboard[0];
        }
        double e1[3], e2[3], q[3], h[3], s[3];
        for (int k = 0; k < 3; k++) {
            e1[k] = p[1][k] - p[0][k];
            e2[k] = p[2][k] - p[0][k];
            s[k] = o[k] - p[0][k];
        }
        cross3(dir, e2, h);
        double det = dot3(e1, h);
        if (fabs(det) < 1e-12 || (d->one_sided && det <= 0)) {
            continue;
        }
        double u = dot3(s, h) / det;
        cross3(s, e1, q);
        double w = dot3(dir, q) / det;
        double distance = dot3(e2, q) / det;
        if (u < -1e-8 || w < -1e-8 || u + w > 1 + 1e-8 || distance < 0 ||
            distance > *nearest) {
            continue;
        }
        const Texture *t = texture_at(r, d->tile);
        if (t->alpha && !d->sky) {
            double tx =
                v[0].uv[0] * (1 - u - w) + v[1].uv[0] * u + v[2].uv[0] * w;
            double ty =
                v[0].uv[1] * (1 - u - w) + v[1].uv[1] * u + v[2].uv[1] * w;
            int x = (int)floor((tx - floor(tx)) * t->width),
                y = (int)floor((ty - floor(ty)) * t->height);
            if (t->alpha[(size_t)y * t->width + x] < 128) {
                continue;
            }
        }
        *nearest = distance;
        r->hit = no_hit();
        if (!d->sprite || r->sprite_picking) {
            r->hit.kind = d->sprite ? DUKE_SURFACE_SPRITE : d->surface;
            r->hit.sector_index = d->sector;
            r->hit.wall_index = d->sprite ? -1 : d->wall;
            r->hit.sprite_index = d->sprite ? d->sprite_index : -1;
            r->hit.distance = distance;
            for (int k = 0; k < 3; k++) {
                r->hit.position[k] = o[k] + dir[k] * distance;
            }
        }
    }
}
static void pick_node(DukeRenderer *r, int id, const float m[16],
                      const double o[3], const double dir[3], double *near) {
    const PickNode *n = &r->nodes[id];
    if (!ray_box(n, o, dir, *near)) {
        return;
    }
    if (n->left >= 0) {
        pick_node(r, n->left, m, o, dir, near);
        pick_node(r, n->right, m, o, dir, near);
    } else {
        for (int i = n->first; i < n->first + n->count; i++) {
            pick_draw(r, &r->faces[i].draw, m, o, dir, near);
        }
    }
}
static void update_hover(DukeRenderer *r, const float m[16]) {
    r->hit = no_hit();
    if (!r->hover_enabled || !r->pointer_valid) {
        return;
    }
    double o[3], dir[3], nearest;
    if (!pointer_ray(m, r->pointer, o, dir, &nearest)) {
        return;
    }
    if (r->face_count) {
        pick_node(r, 0, m, o, dir, &nearest);
    }
    for (size_t i = 0; i < r->draw_count; i++) {
        if (r->draws[i].sprite) {
            pick_draw(r, &r->draws[i], m, o, dir, &nearest);
        }
    }
}
static bool highlighted(const DukeRenderer *r, const Draw *d) {
    if (d->sprite) {
        return r->hover_enabled && r->sprite_picking &&
               r->hit.kind == DUKE_SURFACE_SPRITE &&
               r->hit.sprite_index == d->sprite_index;
    }
    return r->hover_enabled && r->hit.kind != DUKE_SURFACE_NONE &&
           d->surface == r->hit.kind && d->sector == r->hit.sector_index &&
           d->wall == r->hit.wall_index;
}

static bool pipeline(DukeRenderer *r, const DukeRendererDesc *desc) {
    sg_shader_desc sh = {0};
    sh.vertex_func.source =
        "#version 410\nlayout(location=0) in vec3 position;layout(location=1) "
        "in vec2 texcoord;"
        "layout(location=2) in float light;layout(location=3) in vec2 "
        "billboard;"
        "layout(location=4) in float opacity;uniform mat4 mvp;out vec2 uv;out "
        "float brightness;out float alpha;out vec4 clip_position;"
        "void main(){vec2 "
        "r=vec2(mvp[0][0],mvp[2][0]);r=length(r)>0.00001?normalize(r):vec2(1,0)"
        ";"
        "vec3 p=position+vec3(r.x*billboard.x,billboard.y,r.y*billboard.x);"
        "gl_Position=mvp*vec4(p,1);uv=texcoord;brightness=light;alpha=opacity;"
        "clip_position=gl_Position;"
        "}";
    sh.fragment_func.source =
        "#version 410\nuniform sampler2D tex;in vec2 uv;in float brightness;in "
        "float alpha;in vec4 clip_position;uniform vec4 hover_tint;"
        "uniform mat4 inverse_mvp;uniform vec4 sky;out vec4 frag;"
        "void main(){vec2 tc=uv;"
        "if(sky.x>0){"
        "vec2 ndc=clip_position.xy/clip_position.w;"
        "vec4 a=inverse_mvp*vec4(ndc,-1,1);"
        "vec4 b=inverse_mvp*vec4(ndc,1,1);"
        "vec3 ray=b.xyz*a.w-a.xyz*b.w;"
        "float horizontal=max(length(ray.xz),0.000001);"
        "float angle=length(ray.xz)>0.000001?atan(ray.z,ray.x):0;"
        "tc=vec2(angle/6.28318530718+sky.y/8,"
        "0.5-ray.y/horizontal*sky.w+sky.z);"
        "}"
        "vec4 c=texture(tex,tc);if(sky.x==0 && c.a<0.5)discard;"
        "frag=vec4(mix(c.rgb*brightness,"
        "hover_tint.rgb,hover_tint.a),alpha)"
        ";}";
    sh.uniform_blocks[0] = (sg_shader_uniform_block){
        .stage = SG_SHADERSTAGE_VERTEX,
        .size = 64,
        .glsl_uniforms[0] = {.type = SG_UNIFORMTYPE_MAT4, .glsl_name = "mvp"}};
    sh.uniform_blocks[1] = (sg_shader_uniform_block){
        .stage = SG_SHADERSTAGE_FRAGMENT,
        .size = 96,
        .glsl_uniforms[0] = {.type = SG_UNIFORMTYPE_FLOAT4,
                             .glsl_name = "hover_tint"},
        .glsl_uniforms[1] = {.type = SG_UNIFORMTYPE_MAT4,
                             .glsl_name = "inverse_mvp"},
        .glsl_uniforms[2] = {.type = SG_UNIFORMTYPE_FLOAT4,
                             .glsl_name = "sky"}};
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
    p.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT;
    r->pipeline = sg_make_pipeline(&p);
    for (int i = 0; i < 4; i++) {
        bool translucent = (i & 1) != 0;
        p.depth.write_enabled = !translucent;
        p.cull_mode = (i & 2) ? SG_CULLMODE_BACK : SG_CULLMODE_NONE;
        p.face_winding = SG_FACEWINDING_CCW;
        p.colors[0].blend = (sg_blend_state){
            .enabled = translucent,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA};
        r->sprite_pipelines[i] = sg_make_pipeline(&p);
        if (sg_query_pipeline_state(r->sprite_pipelines[i]) !=
            SG_RESOURCESTATE_VALID) {
            return false;
        }
    }
    /* Nearest sampling preserves ART pixels; repeats support UVs outside
     * [0, 1] from wall repeats and world-aligned floor/ceiling coordinates. */
    r->sampler =
        sg_make_sampler(&(sg_sampler_desc){.min_filter = SG_FILTER_NEAREST,
                                           .mag_filter = SG_FILTER_NEAREST,
                                           .wrap_u = SG_WRAP_REPEAT,
                                           .wrap_v = SG_WRAP_REPEAT});
    /* ART skies repeat vertically as well as around the panorama. Clamping
     * extrudes the first/last row into streaks when looking up or down,
     * especially with star fields such as tile 97. */
    r->sky_sampler =
        sg_make_sampler(&(sg_sampler_desc){.min_filter = SG_FILTER_NEAREST,
                                           .mag_filter = SG_FILTER_NEAREST,
                                           .wrap_u = SG_WRAP_REPEAT,
                                           .wrap_v = SG_WRAP_REPEAT});

    return sg_query_pipeline_state(r->pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_sampler_state(r->sky_sampler) == SG_RESOURCESTATE_VALID &&
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
        !geometry(r, &src, map) || !sprites(r, &src, map)) {
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
    /* Keep CPU vertices and alpha alongside GPU resources for exact picking. */
    message = "Unable to build surface picking hierarchy";
    if (!build_picking(r)) {
        goto fail;
    }
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
    update_hover(r, mvp);
    /* Reconstruct view rays from the host matrix so sky UVs are independent
     * of camera translation, sector height, and the host's viewport origin.
     * Homogeneous near/far points also handle infinite-far projections. */
    struct {
        float tint[4], inverse[16], sky[4];
    } fragment = {0};
    double inverse[16];
    if (r->sky_count && inverse_matrix(mvp, inverse)) {
        for (int i = 0; i < 16; i++) {
            fragment.inverse[i] = inverse[i];
        }
    }
    sg_reset_state_cache();
    /* Opaque geometry first; blended sprites back-to-front, with depth testing
     * but no depth writes. Sorting centers is an approximation for
     * intersections. */
    for (size_t i = 0; i < r->draw_count; i++) {
        Draw *d = &r->draws[i];
        d->depth = mvp[3] * d->center[0] + mvp[7] * d->center[1] +
                   mvp[11] * d->center[2] + mvp[15];
    }
    qsort(r->draws, r->draw_count, sizeof(*r->draws), compare_draw);
    sg_pipeline current = {0};
    for (size_t i = 0; i < r->draw_count; i++) {
        Draw d = r->draws[i];
        sg_pipeline selected =
            d.sprite ? r->sprite_pipelines[(d.translucent ? 1 : 0) |
                                           (d.one_sided ? 2 : 0)]
                     : r->pipeline;
        if (selected.id != current.id) {
            sg_apply_pipeline(selected);
            sg_apply_uniforms(0, &(sg_range){mvp, 16 * sizeof(float)});
            current = selected;
        }
        const bool is_selected = matches_surface(&r->selection, &d);
        fragment.tint[0] = 1.0f;
        fragment.tint[1] = is_selected ? 0.45f : 0.7f;
        fragment.tint[2] = is_selected ? 0.05f : 0.15f;
        fragment.tint[3] = (is_selected || highlighted(r, &d)) ? 0.4f : 0.0f;
        fragment.sky[0] = d.sky != 0;
        fragment.sky[1] = d.sky_pan[0];
        fragment.sky[2] = d.sky_pan[1];
        fragment.sky[3] = d.sky ? r->skies[d.sky - 1].vertical_scale : 0;
        sg_apply_uniforms(1, &(sg_range){&fragment, sizeof(fragment)});
        sg_bindings b = {.vertex_buffers[0] = r->buffer,
                         .views[0] = d.sky ? r->skies[d.sky - 1].texture.view
                                          : texture_at(r, d.tile)->view,
                         .samplers[0] = d.sky ? r->sky_sampler : r->sampler};
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
    for (int i = 0; i < 4; i++) {
        if (r->sprite_pipelines[i].id) {
            sg_destroy_pipeline(r->sprite_pipelines[i]);
        }
    }
    if (r->shader.id) {
        sg_destroy_shader(r->shader);
    }
    if (r->sampler.id) {
        sg_destroy_sampler(r->sampler);
    }
    if (r->sky_sampler.id) {
        sg_destroy_sampler(r->sky_sampler);
    }
    for (size_t i = 0; i < r->sky_count; i++) {
        Texture *t = &r->skies[i].texture;
        if (t->view.id) {
            sg_destroy_view(t->view);
        }
        if (t->image.id) {
            sg_destroy_image(t->image);
        }
        free(t->alpha);
    }
    free(r->skies);

    /* Include the fallback slot and destroy views before their images. */
    for (int i = 0; i <= TILE_COUNT; i++) {
        if (r->textures[i].view.id) {
            sg_destroy_view(r->textures[i].view);
        }
        if (r->textures[i].image.id) {
            sg_destroy_image(r->textures[i].image);
        }
    }
    for (int i = 0; i <= TILE_COUNT; i++) {
        free(r->textures[i].alpha);
    }
    for (size_t i = 0; i < r->variant_count; i++) {
        if (r->variants[i].texture.view.id) {
            sg_destroy_view(r->variants[i].texture.view);
        }
        if (r->variants[i].texture.image.id) {
            sg_destroy_image(r->variants[i].texture.image);
        }
        free(r->variants[i].texture.alpha);
    }
    free(r->variants);
    free(r->faces);
    free(r->nodes);
    free(r->draws);
    free(r->vertices);
    free(r);
}
