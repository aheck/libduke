/* Geometry invariants are tested without a window using Sokol's dummy backend.
 */
#include "../src/lib/renderer.c"
#include <assert.h>

static DukeMapFile *fixture(const int xy[][2], int n, int hole) {
    DukeMapFile *m = duke_map_file_new();
    assert(m);
    m->mapversion = 7;
    m->numsectors = 1;
    m->numwalls = n;
    m->sectors = calloc(1, sizeof(*m->sectors));
    m->walls = calloc(n, sizeof(*m->walls));
    assert(m->sectors && m->walls);
    m->sectors[0] = duke_map_sector_new();
    assert(m->sectors[0]);
    m->sectors[0]->wallptr = 0;
    m->sectors[0]->wallnum = n;
    m->sectors[0]->ceilingz = -16384;
    m->sectors[0]->floorz = 0;
    for (int i = 0; i < n; i++) {
        m->walls[i] = duke_map_wall_new();
        assert(m->walls[i]);
        m->walls[i]->x = xy[i][0];
        m->walls[i]->y = xy[i][1];
        m->walls[i]->point2 = i + 1;
    }
    m->walls[n - 1]->point2 = hole ? hole : 0;
    if (hole) {
        m->walls[hole - 1]->point2 = 0;
    }
    return m;
}
static void check_area(DukeMapFile *m, double expected) {
    DukeRenderer *r = calloc(1, sizeof(*r));
    assert(r);
    Source s = {0};
    r->textures[TILE_COUNT].width = r->textures[TILE_COUNT].height = 2;
    assert(floors(r, &s, m, 0));
    double area = 0;
    for (size_t i = 0; i < r->count; i += 3) {
        const Vertex *a = &r->vertices[i], *b = a + 1, *c = a + 2;
        double cross = (b->p[0] - a->p[0]) * (c->p[2] - a->p[2]) -
                       (b->p[2] - a->p[2]) * (c->p[0] - a->p[0]);
        area += fabs(cross) * 0.5;
        /* A triangle's centroid must not fall in the hole or outside the
         * outline.
         */
        int x = lround((a->p[0] + b->p[0] + c->p[0]) * 1024 / 3);
        int y = lround((a->p[2] + b->p[2] + c->p[2]) * 1024 / 3);
        assert(duke_map_sector_classify_point(m, 0, x, y) >=
               DUKE_MAP_POINT_INSIDE);
    }
    assert(fabs(area - expected) < 0.0001);
    duke_renderer_destroy(r);
}
static void sprite_tests(void) {
    DukeRenderer *r = calloc(1, sizeof(*r));
    assert(r);
    r->textures[0].width = 64;
    r->textures[0].height = 128;
    DukeMapSprite s = {.xrepeat = 64, .yrepeat = 64};
    assert(sprite_quad(r, &s, 0));
    float left = 100, right = -100, top = -100, bottom = 100;
    for (int i = 0; i < 6; i++) {
        Vertex v = r->vertices[i];
        left = fminf(left, v.billboard[0]);
        right = fmaxf(right, v.billboard[0]);
        top = fmaxf(top, v.billboard[1]);
        bottom = fminf(bottom, v.billboard[1]);
        assert(v.p[0] == 0 && v.p[1] == 0 && v.p[2] == 0 && v.alpha == 1);
    }
    assert(left == -0.5f && right == 0.5f && top == 2 && bottom == 0);
    s.cstat = 128 | 4 | 8 | 2 | 512;
    s.xoffset = 4;
    s.yoffset = 8;
    r->textures[0].xoffset = 2;
    r->textures[0].yoffset = -4;
    assert(sprite_quad(r, &s, 0));
    assert(r->draws[1].translucent && !r->draws[1].one_sided);
    /* Flips invert both UVs and pivot offsets, independently of centering. */
    bool found = false;
    for (int i = 6; i < 12; i++) {
        const Vertex *v = &r->vertices[i];
        assert(fabs(v->alpha - 1.0f / 3.0f) < 0.00001);
        if (v->uv[0] == 1 && v->uv[1] == 1) {
            assert(v->billboard[0] == -0.40625f && v->billboard[1] == 0.9375f);
            found = true;
        }
    }
    assert(found);
    s.cstat = 16 | 64;
    s.xoffset = s.yoffset = 0;
    r->textures[0].xoffset = r->textures[0].yoffset = 0;
    assert(sprite_quad(r, &s, 0));
    assert(r->draws[2].one_sided);
    for (int i = 12; i < 18; i++) {
        assert(r->vertices[i].p[0] == 0 && r->vertices[i].billboard[0] == 0);
    }
    s.cstat = 32 | 64;
    s.z = -16384;
    assert(sprite_quad(r, &s, 0));
    for (int i = 18; i < 24; i++) {
        assert(r->vertices[i].p[1] == 1);
    }
    /* Floor winding faces upward; Y flip selects the opposite side. */
    Vertex a = r->vertices[18], b = r->vertices[19], c = r->vertices[20];
    assert((b.p[2] - a.p[2]) * (c.p[0] - a.p[0]) -
               (b.p[0] - a.p[0]) * (c.p[2] - a.p[2]) >
           0);
    DukeMapSprite hidden = s;
    hidden.cstat = INT16_MIN;
    DukeMapSprite zero = s;
    zero.xrepeat = 0;
    DukeMapSprite invalid = s;
    invalid.sectnum = -1;
    DukeMapSprite *list[] = {&hidden, &zero, &invalid};
    DukeMapFile m = {.numsprites = 3, .numsectors = 1, .sprites = list};
    Source src = {0};
    assert(sprites(r, &src, &m) && r->draw_count == 4);
    Draw order[3] = {{.first = 0, .translucent = true, .depth = 2},
                     {.first = 6},
                     {.first = 12, .translucent = true, .depth = 8}};
    qsort(order, 3, sizeof(*order), compare_draw);
    assert(order[0].first == 6 && order[1].first == 12 && order[2].first == 0);
    duke_renderer_destroy(r);
}

static void hover_plane(DukeRenderer *r, float z, int wall, int tile) {
    Vertex a = {.p = {-0.8f, -0.8f, z}, .uv = {0, 0}, .alpha = 1}, b = a, c = a,
           d = a;
    b.p[0] = 0.8f;
    b.uv[0] = 1;
    c.p[0] = 0.8f;
    c.p[1] = 0.8f;
    c.uv[0] = c.uv[1] = 1;
    d.p[1] = 0.8f;
    d.uv[1] = 1;
    Vertex v[6] = {a, b, c, a, c, d};
    assert(emit(r, v, tile));
    Draw *draw = &r->draws[r->draw_count - 1];
    draw->surface = DUKE_SURFACE_WALL;
    draw->sector = 3;
    draw->wall = wall;
}
static void hover_tests(void) {
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    DukeRenderer *r = calloc(1, sizeof(*r));
    assert(r);
    /* Enough planes to exercise internal hierarchy nodes, all behind the first.
     */
    for (int i = 0; i < 24; i++) {
        hover_plane(r, i / 30.0f, 100 + i, 0);
    }
    assert(build_picking(r));
    assert(r->node_count > 1);
    DukeSurfaceHit hit;
    duke_renderer_set_pointer(r, 0, 0);
    update_hover(r, identity);
    assert(!duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == -1);
    duke_renderer_set_hover_enabled(r, true);
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit));
    assert(hit.kind == DUKE_SURFACE_WALL && hit.wall_index == 100 &&
           hit.sector_index == 3);
    assert(fabs(hit.distance - 1) < 1e-6 && fabs(hit.position[2]) < 1e-6);
    assert(highlighted(r, &r->draws[0]) && !highlighted(r, &r->draws[1]));
    duke_renderer_set_pointer(r, 0.99f, 0);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    update_hover(r, identity);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    duke_renderer_set_pointer(r, 2, 0);
    update_hover(r, identity);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    duke_renderer_set_pointer(r, NAN, 0);
    assert(!r->pointer_valid);
    duke_renderer_set_pointer(r, 0, 0);
    update_hover(r, identity);
    duke_renderer_set_hover_enabled(r, false);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    duke_renderer_set_hover_enabled(r, true);
    float singular[16] = {0};
    update_hover(r, singular);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    duke_renderer_destroy(r);

    r = calloc(1, sizeof(*r));
    assert(r);
    hover_plane(r, 0, 1, 0);
    hover_plane(r, 0.5f, 2, 1);
    r->textures[0].width = 2;
    r->textures[0].height = 2;
    r->textures[0].alpha = calloc(4, 1);
    assert(r->textures[0].alpha);
    assert(build_picking(r));
    duke_renderer_set_hover_enabled(r, true);
    duke_renderer_set_pointer(r, 0, 0);
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 2);
    memset(r->textures[0].alpha, 255, 4);
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 1);
    /* Replace the first picking face by a sprite without touching the rear
     * wall. */
    for (int i = 0; i < r->face_count; i++) {
        if (r->faces[i].draw.wall == 1) {
            r->faces[i].draw.sprite = true;
        }
    }
    r->draws[0].sprite = true;
    update_hover(r, identity);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    memset(r->textures[0].alpha, 0, 4);
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 2);
    memset(r->textures[0].alpha, 255, 4);
    r->draws[0].translucent = true;
    for (int i = 0; i < r->face_count; i++) {
        if (r->faces[i].draw.wall == 1) {
            r->faces[i].draw.translucent = true;
        }
    }
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 2);
    duke_renderer_set_sprite_picking_enabled(r, true);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    r->draws[0].sprite_index = 37;
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit));
    assert(hit.kind == DUKE_SURFACE_SPRITE && hit.sprite_index == 37 &&
           hit.wall_index == -1);
    assert(highlighted(r, &r->draws[0]) && !highlighted(r, &r->draws[1]));
    memset(r->textures[0].alpha, 0, 4);
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 2 &&
           hit.sprite_index == -1);
    memset(r->textures[0].alpha, 255, 4);
    duke_renderer_set_sprite_picking_enabled(r, false);
    assert(!duke_renderer_get_hovered_surface(r, &hit));
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 2);
    duke_renderer_set_sprite_picking_enabled(r, true);
    /* One-sided CCW plane viewed from behind should not occlude. */
    r->draws[0].translucent = false;
    r->draws[0].one_sided = true;
    for (int i = 0; i < r->face_count; i++) {
        if (r->faces[i].draw.wall == 1) {
            r->faces[i].draw.translucent = false;
            r->faces[i].draw.one_sided = true;
        }
    }
    update_hover(r, identity);
    assert(duke_renderer_get_hovered_surface(r, &hit) && hit.wall_index == 2);
    duke_renderer_destroy(r);

    /* Real sector surfaces: a downward ray hits the floor inside the ring,
     * while a ray through its hole must miss both floor and ceiling. */
    const int xy[][2] = {{0, 0},       {4096, 0},    {4096, 4096},
                         {0, 4096},    {1024, 1024}, {1024, 3072},
                         {3072, 3072}, {3072, 1024}};
    DukeMapFile *m = fixture(xy, 8, 4);
    m->sectors[0]->floorstat = 2;
    m->sectors[0]->floorheinum = 256;
    r = calloc(1, sizeof(*r));
    assert(r);
    Source src = {0};
    r->textures[TILE_COUNT].width = r->textures[TILE_COUNT].height = 2;
    assert(floors(r, &src, m, 0));
    assert(build_picking(r));
    double o[3] = {0.5, 0.5, 0.5}, dir[3] = {0, -1, 0}, limit = 10;
    r->hit = no_hit();
    pick_node(r, 0, identity, o, dir, &limit);
    assert(r->hit.kind == DUKE_SURFACE_FLOOR && r->hit.sector_index == 0 &&
           r->hit.wall_index == -1);
    assert(fabs(r->hit.position[1] + 0.03125) < 1e-6);
    dir[1] = 1;
    limit = 10;
    r->hit = no_hit();
    pick_node(r, 0, identity, o, dir, &limit);
    assert(r->hit.kind == DUKE_SURFACE_CEILING);
    o[0] = o[2] = 2;
    limit = 10;
    r->hit = no_hit();
    pick_node(r, 0, identity, o, dir, &limit);
    assert(r->hit.kind == DUKE_SURFACE_NONE);
    duke_map_file_free(m);
    duke_renderer_destroy(r);
    /* A rotated/translated orthographic view unprojects into the expected axis.
     */
    float rotated[16] = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, -2, 1};
    double ray_o[3], ray_d[3], far;
    float pointer[2] = {0, 0};
    assert(pointer_ray(rotated, pointer, ray_o, ray_d, &far));
    assert(fabs(ray_o[0] - 1) < 1e-8 && fabs(ray_d[0] - 1) < 1e-8);
    const float perspective[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -10.1f / 9.9f, -1, 0, 0, -2.0f / 9.9f, 0};
    assert(pointer_ray(perspective, pointer, ray_o, ray_d, &far));
    assert(fabs(ray_o[2] + 0.1) < 1e-6 && fabs(ray_d[2] + 1) < 1e-6);
}

static void bottom_swap_tests(void) {
    const int xy[][2] = {{0, 0}, {1024, 0}, {1024, 1024}, {0, 1024}};
    DukeMapFile *m = fixture(xy, 4, 0);
    DukeMapWall *a = m->walls[0], *other = m->walls[2];
    a->nextwall = 2;
    a->nextsector = 0;
    a->picnum = 10;
    a->xrepeat = 8;
    a->yrepeat = 16;
    other->picnum = 11;
    other->xrepeat = 99;
    other->yrepeat = 99;
    other->xpanning = 16;
    other->ypanning = 64;
    other->shade = 12;
    other->cstat = 4 | 8 | 256;
    DukeRenderer *r = calloc(1, sizeof(*r));
    assert(r);
    Source src = {0};
    uint32_t pixels[64 * 64] = {0};
    assert(upload(&r->textures[10], 64, 64, pixels));
    assert(upload(&r->textures[11], 64, 64, pixels));
    double top[2] = {-8192, -4096}, bottom[2] = {0, 0};
    /* Toggle the flag on identical geometry, including a sloping top edge.
     * Upper/masked/solid spans must never borrow the opposite material. */
    assert(wall_quad(r, &src, m, 0, 0, top, bottom, 10, true));
    assert(r->draws[0].tile == 10);
    a->cstat = 2;
    assert(wall_quad(r, &src, m, 0, 0, top, bottom, 10, true));
    assert(r->draws[1].tile == 11 && r->draws[1].wall == 0 && r->draws[1].sector == 0);
    for (int i = 0; i < 6; i++) {
        assert(memcmp(r->vertices[i].p, r->vertices[i + 6].p, sizeof(r->vertices[i].p)) == 0);
    }
    assert(fabs(r->vertices[6].uv[0] - 0.25) < 1e-6);
    assert(fabs(r->vertices[7].uv[0] - 1.25) < 1e-6);
    assert(fabs(r->vertices[6].uv[1] + 1.25) < 1e-6);
    assert(r->vertices[6].light < r->vertices[0].light);
    assert(wall_quad(r, &src, m, 0, 0, top, bottom, 10, false));
    assert(r->draws[2].tile == 10);
    a->cstat |= 8;
    assert(wall_quad(r, &src, m, 0, 0, top, bottom, 10, true));
    assert(fabs(r->vertices[19].uv[0] + 1.25) < 1e-6);
    a->nextwall = -1;
    assert(wall_quad(r, &src, m, 0, 0, top, bottom, 10, true));
    assert(r->draws[4].tile == 10);
    duke_renderer_destroy(r);
    duke_map_file_free(m);
}

int main(void) {
    sg_setup(&(sg_desc){0});
    bottom_swap_tests();
    sprite_tests();
    hover_tests();
    const int ring[][2] = {{0, 0},       {4096, 0},    {4096, 4096},
                           {0, 4096},    {1024, 1024}, {1024, 3072},
                           {3072, 3072}, {3072, 1024}};
    DukeMapFile *m = fixture(ring, 8, 4);
    assert(duke_map_file_validate_references(m));
    check_area(m, 24);
    m->sectors[0]->floorstat = 2;
    m->sectors[0]->floorheinum = 256;
    assert(fabs(surface(m, 0, true, 0, 1024) - 1024) < 0.0001);
    check_area(m, 24);
    duke_map_file_free(m);
    const int concave[][2] = {{0, 0},       {3072, 0},    {3072, 1024},
                              {1024, 1024}, {1024, 3072}, {0, 3072}};
    m = fixture(concave, 6, 0);
    check_area(m, 10);
    char error[64];
    DukeRendererDesc desc = {0};
    assert(!duke_renderer_create(m, NULL, &desc, error, sizeof(error)) &&
           error[0]);
    DukeRenderer *r = calloc(1, sizeof(*r));
    assert(r);
    assert(pipeline(r, &desc));
    duke_renderer_destroy(r);
    duke_map_file_free(m);
    sg_shutdown();
    return 0;
}
