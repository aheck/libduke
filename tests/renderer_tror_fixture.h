/* Shared synthetic TROR geometry; no game assets or window system required. */
#ifndef RENDERER_TROR_FIXTURE_H
#define RENDERER_TROR_FIXTURE_H

static DukeMapFile *tror_fixture(bool hole, bool slope) {
    static const int xy[8][2] = {
        {-32768,-32768}, {32768,-32768}, {32768,32768}, {-32768,32768},
        {-2048,-2048}, {-2048,2048}, {2048,2048}, {2048,-2048}
    };
    int n = hole ? 8 : 4;
    DukeMapFile *m = duke_map_file_new();
    assert(m);
    m->mapversion = 9;
    m->numsectors = 3;
    m->numwalls = 3 * n;
    m->sectors = calloc(3, sizeof(*m->sectors));
    m->walls = calloc(3 * n, sizeof(*m->walls));
    assert(m->sectors && m->walls);
    for (int s = 0; s < 3; ++s) {
        DukeMapSector *sector = m->sectors[s] = duke_map_sector_new();
        assert(sector);
        sector->wallptr = s * n;
        sector->wallnum = n;
        sector->ceilingz = (s - 2) * 16384;
        sector->floorz = (s - 1) * 16384;
        sector->ceilingpicnum = s == 0 ? 2 : 3;
        sector->floorpicnum = s == 2 ? 1 : 3;
        sector->ceilingstat = (s > 0 ? 1024 : 0) | (slope ? 2 : 0);
        sector->floorstat = (s < 2 ? 1024 : 0) | (slope ? 2 : 0);
        sector->ceilingheinum = sector->floorheinum = slope ? 64 : 0;
        sector->ceilingxpanning = s == 2 ? 255 : 0;
        sector->floorxpanning = s == 1 ? 255 : 0;
        for (int v = 0; v < n; ++v) {
            int w = s * n + v;
            DukeMapWall *wall = m->walls[w] = duke_map_wall_new();
            assert(wall);
            wall->x = xy[v][0];
            wall->y = xy[v][1];
            wall->point2 = s * n + v / 4 * 4 + (v + 1) % 4;
            wall->cstat = (s > 0 ? 1024 : 0) | (s < 2 ? 2048 : 0);
            wall->lotag = s > 0 ? w - n : 0;
            wall->extra = s < 2 ? w + n : -1;
        }
    }
    assert(duke_map_file_validate_tror(m));
    return m;
}

#endif
