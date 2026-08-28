#ifndef LIBDUKE_MAP_H
#define LIBDUKE_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAPV7_MAXSECTORS 1024
#define MAPV7_MAXWALLS 8192
#define MAPV7_MAXSPRITES 4096

#define MAPV8_MAXSECTORS 4096
#define MAPV8_MAXWALLS 16384
#define MAPV8_MAXSPRITES 16384
#define MAP_MAXSTATUS 1024

struct DukeMapSector;
struct DukeMapWall;
struct DukeMapSprite;

typedef struct DukeMapFile {
    int32_t mapversion; // Version of the map file format 7, 8, and 9 are supported
    int32_t posx; // Player starting pos x
    int32_t posy; // Player starting pos y
    int32_t posz; // Player starting pos z
    int16_t ang;  // Player starting angle
    int16_t cursectnum; // Starting sector
    int16_t numsectors; // Number of sectors in the map
    struct DukeMapSector **sectors; // List of all sectors in the map
    uint16_t numwalls; // Number of walls in the map
    struct DukeMapWall **walls; // List of all walls in the map
    uint16_t numsprites; // Number of sprites in the map
    struct DukeMapSprite **sprites; // List of all sprites in the map

    char last_error[256];
} DukeMapFile;

typedef struct DukeMapSector {
    int16_t wallptr; // Index of the first wall in the sector
    int16_t wallnum; // Number of walls in the sector
    int32_t ceilingz; // Z-coordinate of the sector ceiling
    int32_t floorz; // Z-coordinate of the sector floor

    int16_t ceilingstat; // Flags:
    int16_t floorstat;   //
                         //        bit 0: 1 = parallaxing, 0 = not
                         // bit 1: 1 = sloped, 0 = not
                         // bit 2: 1 = swap x&y, 0 = not
                         // bit 3: 1 = double smooshiness
                         // bit 4: 1 = x-flip
                         // bit 5: 1 = y-flip
                         // bit 6: 1 = Align texture to first wall of sector
                         // bits 7-15: reserved

    int16_t ceilingpicnum; // Ceiling texture
    int16_t ceilingheinum; // Ceiling slope (0 = no slope, 4096 = 45 degrees)
    int8_t ceilingshade; // Shade offset
    uint8_t ceilingpal; // Ceiling palette number
    uint8_t ceilingxpanning; // Ceiling texture offset x-coordinate
    uint8_t ceilingypanning; // Ceiling texture offset x-coordinate
    int16_t floorpicnum; // Floor texture
    int16_t floorheinum; // Floor slope (0 = no slope, 4096 = 45 degrees)
    int8_t floorshade; // Shade offset
    uint8_t floorpal; // Floor palette number
    uint8_t floorxpanning; // Floor texture x-offset
    uint8_t floorypanning; // Floor texture y-offset
    uint8_t visibility; // How fast an area changes shade relative to distance
    uint8_t filler; // Padding byte

    int16_t lotag; // Game-specific data field
    int16_t hitag; // Game-specific data field
    int16_t extra; // Game-specific data field
} DukeMapSector;

typedef struct DukeMapWall {
    int32_t x; // X-coordinate of the left side of the wall
    int32_t y; // Y-coordinate of the left side of the wall
    int16_t point2; // Index to the next wall to the right in the same sector
    int16_t nextwall; // Index to the wall on the other side of the wall (-1 if there is no wall on the other side)
    int16_t nextsector; // Index to the sector on the other side of the wall (-1 if there is no wall on the other side)

    int16_t cstat; // Flags:
                   //        bit 0: 1 = Blocking wall (use with clipmove, getzrange)
                   // bit 1: 1 = bottoms of invisible walls swapped, 0 = not
                   // bit 2: 1 = align picture on bottom (for doors), 0 = top
                   // bit 3: 1 = x-flipped, 0 = normal
                   // bit 4: 1 = masking wall, 0 = not
                   // bit 5: 1 = 1-way wall, 0 = not
                   // bit 6: 1 = Blocking wall (use with hitscan / cliptype 1)
                   // bit 7: 1 = Transluscence, 0 = not
                   // bit 8: 1 = y-flipped, 0 = normal
                   // bit 9: 1 = Transluscence reversing, 0 = normal
                   // bits 10-15: reserved

    int16_t picnum; // Wall texture
    int16_t overpicnum; // Texture for masked/one-way walls
    int8_t shade; // Shade offset
    uint8_t pal; // Wall palette
    uint8_t xrepeat; // Stretch/shrink texture on x-axis
    uint8_t yrepeat; // Stretch/shrink texture on y-axis
    uint8_t xpanning; // Texture alignment x-offset
    uint8_t ypanning; // Texture alignment x-offset

    int16_t lotag; // Game-specific data field
    int16_t hitag; // Game-specific data field
    int16_t extra; // Game-specific data field
} DukeMapWall;

typedef struct DukeMapSprite {
    int32_t x; // X-coordinate of the sprite
    int32_t y; // Y-coordinate of the sprite
    int32_t z; // Z-coordinate of the sprite

    int16_t cstat; // Flags:
                   //        bit 0: 1 = Blocking sprite (use with clipmove, getzrange)
                   //        bit 1: 1 = transluscence, 0 = normal
                   //        bit 2: 1 = x-flipped, 0 = normal
                   //        bit 3: 1 = y-flipped, 0 = normal
                   //        bits 5-4: 00 = FACE sprite (default)
                   //        01 = WALL sprite (like masked walls)
                   //        10 = FLOOR sprite (parallel to ceilings&floors)
                   //        bit 6: 1 = 1-sided sprite, 0 = normal
                   //        bit 7: 1 = Real centered centering, 0 = foot center
                   //        bit 8: 1 = Blocking sprite (use with hitscan / cliptype 1)
                   //        bit 9: 1 = Transluscence reversing, 0 = normal
                   //        bits 10-14: reserved
                   //        bit 15: 1 = Invisible sprite, 0 = not invisible

    int16_t picnum; // Texture of the sprite
    int8_t shade; // Shade offset
    uint8_t pal; // Sprite palette
    uint8_t clipdist; // Size of movement clipping square
    uint8_t filler; // Padding byte
    uint8_t xrepeat; // Stretch/shrink texture on x-axis
    uint8_t yrepeat; // Stretch/shrink texture on y-axis
    int8_t xoffset; // Center sprite animations
    int8_t yoffset; // Center sprite animations

    int16_t sectnum; // Sector where the sprite is placed
    int16_t statnum; // Status of sprite (inactive, monster, etc.)
    int16_t ang; // Angle of the sprite
    int16_t owner;
    int16_t xvel;
    int16_t yvel;
    int16_t zvel;

    int16_t lotag; // Game-specific data field
    int16_t hitag; // Game-specific data field
    int16_t extra; // Game-specific data field
} DukeMapSprite;

DukeMapFile* duke_map_file_new(void);
bool duke_map_file_read_from_filename(DukeMapFile *map, const char *filename);
/* Validate all structural, topological, geometric, and placement invariants. */
bool duke_map_file_validate(DukeMapFile *map);
/* The validators below may also be called independently. On failure, the first
 * error is written to map->last_error. */
bool duke_map_file_validate_structure(DukeMapFile *map);
bool duke_map_file_validate_sector_wall_ownership(DukeMapFile *map);
bool duke_map_file_validate_wall_loops(DukeMapFile *map);
bool duke_map_file_validate_geometry(DukeMapFile *map);
bool duke_map_file_validate_portals(DukeMapFile *map);
bool duke_map_file_validate_vertical_sectors(DukeMapFile *map);
bool duke_map_file_validate_sprites(DukeMapFile *map);
bool duke_map_file_validate_start_position(DukeMapFile *map);
void duke_map_file_reset_last_error(DukeMapFile *map);
void duke_map_file_free(DukeMapFile *map);

DukeMapSector* duke_map_sector_new(void);
bool duke_map_sector_read_from_file(DukeMapSector *sector, FILE *fp);
void duke_map_sector_free(DukeMapSector *sector);

DukeMapWall* duke_map_wall_new(void);
bool duke_map_wall_read_from_file(DukeMapWall *wall, FILE *fp);
void duke_map_wall_free(DukeMapWall *wall);

DukeMapSprite* duke_map_sprite_new(void);
bool duke_map_sprite_read_from_file(DukeMapSprite *sprite, FILE *fp);
void duke_map_sprite_free(DukeMapSprite *sprite);

#ifdef __cplusplus
}
#endif

#endif /* LIBDUKE_GRP_H */
