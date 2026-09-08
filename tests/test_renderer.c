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
    /* A triangle's centroid must not fall in the hole or outside the outline.
     */
    int x = lround((a->p[0] + b->p[0] + c->p[0]) * 1024 / 3);
    int y = lround((a->p[2] + b->p[2] + c->p[2]) * 1024 / 3);
    assert(duke_map_sector_classify_point(m, 0, x, y) >= DUKE_MAP_POINT_INSIDE);
  }
  assert(fabs(area - expected) < 0.0001);
  duke_renderer_destroy(r);
}
int main(void) {
  sg_setup(&(sg_desc){0});
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
