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

int main(void) {
    sg_setup(&(sg_desc){0});
    sprite_tests();
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
