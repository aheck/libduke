A valid Build map is primarily a consistent collection of closed sector polygons, reciprocal portals, and correctly assigned sprites.

  The hard invariants are:

  1. File structure and limits
      - The version must be supported: 7, 8, or 9.
      - Every declared sector, wall, and sprite record must be present.
      - Counts must not exceed the version limits:

         Version    Sectors    Walls    Sprites
        ━━━━━━━━━  ━━━━━━━━━  ━━━━━━━  ━━━━━━━━━
         7             1024     8192       4096
        ─────────  ─────────  ───────  ─────────
         8/9           4096    16384      16384

     These are the checks currently enforced by src/lib/map.c:125, using limits from include/libduke/map.h:13.

  2. Sector wall ownership

     For every sector s:
      - wallptr >= 0
      - wallnum >= 3
      - wallptr + wallnum <= numwalls
      - Its walls are exactly the contiguous range:

        [wallptr, wallptr + wallnum)

      - Sector ranges should be disjoint and collectively account for every wall. In canonical maps, they appear consecutively in sector order.

  3. Closed wall loops

     Each wall’s point2 must:
      - Reference a valid wall.
      - Reference a wall owned by the same sector.
      - Eventually return to the starting wall.
      - Give every wall exactly one predecessor.

     Thus, point2 is a permutation of that sector’s walls partitioned into closed loops. A sector may contain multiple loops, for example an outer boundary plus holes.

  4. Valid polygon geometry

     Within a loop:
      - An edge runs from wall[i].(x,y) to wall[wall[i].point2].(x,y).
      - Edges must have nonzero length.
      - Non-adjacent edges must not cross or overlap.
      - Loops must contain at least three distinct vertices and have nonzero signed area.
      - The outer boundary and hole boundaries must have the orientation expected by Build—holes use the opposite winding from their containing boundary.
      - Distinct sectors may share an edge only through a valid portal pair; arbitrary overlapping sector interiors are unsafe.

  5. Portal reciprocity

     A solid wall has:

     nextwall == -1
     nextsector == -1

     Otherwise, if wall w has nextwall = q and nextsector = t:
      - q is a valid wall index.
      - t is a valid sector index.
      - Wall q belongs to sector t.
      - wall[q].nextwall == w.
      - wall[q].nextsector is the sector containing w.
      - The paired edges have reversed endpoints:

        start(w) == end(q)
        end(w)   == start(q)

     A half-connected portal—only one of nextwall or nextsector being -1—is invalid.

  6. Vertical sector validity

     Build’s Z axis increases downward, so throughout the usable sector area:

     ceiling_z(x, y) <= floor_z(x, y)

     For flat sectors, this reduces to ceilingz <= floorz. For sloped sectors, it must remain true after applying ceilingheinum and floorheinum, especially at vertices and portals.

     If a surface is marked sloped, the sector’s first wall must also be a usable, nonzero-length reference edge.

  7. Sprite references

     For each serialized sprite:
      - 0 <= sectnum < numsectors.
      - Its (x,y) position should lie inside that sector, respecting holes.
      - statnum must be in the engine/game’s supported status range.
      - Angle and resource fields should be legal for the target engine: normally 0 <= ang < 2048, valid tile numbers, and sensible nonzero repeats for visible sprites.

     The structural sectnum requirement is the important safety invariant; several other sprite constraints are game- or port-specific.

  8. Starting position
      - 0 <= cursectnum < numsectors.
      - (posx,posy) lies inside cursectnum.
      - posz lies between that sector’s ceiling and floor at the starting point.
      - Normally 0 <= ang < 2048.

  The key distinction is that libduke currently establishes only “parsable and within array limits.” It reads fields such as wallptr, point2, nextwall, and sectnum but does not validate their relationships
  (include/libduke/map.h:42). A file can therefore load successfully while still containing out-of-range references, open wall loops, mismatched portals, or invalid geometry.

  These invariants follow the data relationships used directly by the original Build engine source and modernized JFBuild source.

