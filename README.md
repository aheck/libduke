# libduke

`libduke` is a C library and command-line toolkit for working with classic
Duke Nukem 3D data files. It provides APIs for reading and manipulating GRP
archives, ART tile sets, and Build engine map files without depending on the
original game executable.

## Features

- Read GRP archive metadata and entry data, either lazily or eagerly
- Inspect, extract, create, append to, and update GRP archives
- Read, validate, create, and modify ART tile sets
- Read, validate, inspect, and write complete Build palette files
- Access ART pixel data lazily or load complete tile sets
- Read and write Build map versions 7, 8, and 9
- Query/edit version 9 TROR bunches and vertical wall links, with validation
- Add and remove map sprites
- Validate map structure, geometry, portals, slopes, sprites, and player starts
- Explicit little-endian handling in the library's binary format readers
- Build static and shared libraries

The repository also builds three command-line programs: `duke-grp`,
`duke-art`, and `duke-map`.

## Version 9 maps (binary TROR)

Use `duke_map_sector_get_bunch` / `duke_map_sector_set_bunch` for ceiling and
floor bunch IDs, and `duke_map_wall_get_vertical_link` /
`duke_map_wall_set_vertical_link` for upper and lower wall connections.
Directions are `DUKE_MAP_CEILING` (up) and `DUKE_MAP_FLOOR` (down). Bunch IDs
range from 0 to 255; -1 disconnects a surface or wall. A bunch can contain
multiple sectors on either side. Wall links are reciprocal and connect edges
with the **same** XY direction, unlike ordinary red-wall portals.

The structs retain the binary representation for compatibility and lossless
round trips: surface stat bit 10 marks a bunch ID in X panning; wall cstat bits
10/11 mark upper/lower links in lotag/extra. Those marked fields are **not**
ordinary texture panning or game tags. Use the accessors instead of directly
editing them, and preserve these bits when changing other surface/wall flags.
Accessors do not maintain a second copy of the data or renumber bunch IDs.

Setters are low-level operations: assemble both sides of each connection,
then call `duke_map_file_validate_tror`. It checks ordinary references, bunch
membership on both sides, valid reciprocal vertical links and matching XY
endpoints. It does not prove matching Z/slopes, full surface coverage, or
engine-specific gameplay constraints. Version 9 loading and all map saving
perform these checks automatically; failed loading preserves the old map, and
validation failure on saving leaves the destination unopened.

Use `duke_map_file_set_version` to change format while checking its limits.
TROR connections require version 9 regardless of map size. Saving as 7/8 is
rejected while TROR marker bits remain, even after a direct assignment to
`mapversion`. This conservative check also rejects old maps using those bits
as otherwise-unused flags. Downgrading requires explicitly calling
`duke_map_file_clear_tror` first. This discards the connections and resets only
their occupied fields (X panning and upper-wall lotag to 0, lower-wall extra to
-1); it cannot restore values that were overwritten when TROR was created.
The function does not change geometry or the map version.

This is format/API support only. The renderer does not yet render through
TROR floors and ceilings, and editors must update vertical references when
renumbering walls or altering connected geometry. Text-based map formats are
not included.

`meson test -C build map-tror` runs standalone synthetic regression tests,
including three stacked levels, one-to-many bunches, malformed references,
non-destructive failures, version limits and v7/v8/v9 round trips. To also test
real Mapster32 maps without redistributing external assets:

```sh
./build/test-map-tror /path/to/eduke32/package/sdk/samples/trueror1.map
```

Each external fixture must be a v9 TROR map and is checked for byte-identical
round trips without modifying it. Compatibility was checked against the
`trueror1.map` SDK sample shipped in EDuke32 `20260203-10664-ba6b7bb1d`
(96 sectors, 997 walls; SHA-256
`8dcfb7db66e22cfc51f7cdf6c03ee192df3432200e4916e1616d5da73aa7a66a`).
The encoding follows `source/build/include/build.h` (binary-v9 compatibility
helpers) and `source/build/src/engine.cpp` (YAX accessors) from that source
distribution; upstream is https://voidpoint.io/terminx/eduke32.

## Building

The project uses [Meson](https://mesonbuild.com/) and
[Conan 2](https://conan.io/) and requires a C11 compiler.

```sh
git clone https://github.com/aheck/libduke.git
cd libduke

conan install . --output-folder=build --build=missing
meson setup build --native-file build/conan_meson_native.ini
meson compile -C build
```

The resulting library and tools are placed in `build/`.

For a release build, explicitly disable sanitizers and use release dependencies:

```sh
conan install . --output-folder=conan-release --build=missing -s build_type=Release
meson setup build-release --native-file=conan-release/conan_meson_native.ini --buildtype=release -Db_sanitize=none
meson compile -C build-release
```

Sanitizers are disabled by default. To opt into AddressSanitizer for a debug
build with a supported compiler, pass `-Db_sanitize=address` to `meson setup`.
Existing build directories retain their configured sanitizer settings; changing
only the build type does not reset them. Before rebuilding an existing release
directory, use:

```sh
meson configure build-release --buildtype=release -Db_sanitize=none
meson compile -C build-release
```

The ART and GRP command-line tools support POSIX systems and Windows. Their
private platform layer uses native directory enumeration, binary temporary
files, and replacement operations. `create` refuses to overwrite an existing
file even if that file appears while the archive is being written. Updates are
staged beside the destination and published only after writing succeeds; source
archive handles are closed before Windows replacement. Windows paths use the
same narrow-character encoding as the existing library file APIs.

The test suite includes archive-tool round trips and no-overwrite publication
checks. Install Python 3 to enable the command-line round-trip tests. These tests
can also exercise cross-compiled Windows tools under Wine:

```sh
python3 tests/test_tools.py /path/to/duke-art.exe /path/to/duke-grp.exe --runner wine
```

To run the test suite:

```sh
meson test -C build --print-errorlogs
```

The libcheck-based GRP, ART, palette, and map tests are enabled by default.
To omit them and avoid the Check dependency entirely:

```sh
meson setup build-no-check -Dcheck_tests=false
meson compile -C build-no-check
meson test -C build-no-check --print-errorlogs
```

For an existing build, use `meson configure build -Dcheck_tests=false` (or
`true` to re-enable them). Camera, platform, archive-tool, and enabled renderer
tests remain available. With Check disabled, the library has no Conan-provided
dependencies, so the `conan install` step can be skipped. The existing Conan
manifest still installs Check for the default test-enabled workflow.

To install the library, public headers, and tools using Meson:

```sh
meson install -C build
```

Use `--prefix` when running `meson setup` if you want to install somewhere
other than Meson's default prefix.

## Command-line tools

### GRP archives

```text
duke-grp info ARCHIVE.GRP
duke-grp list ARCHIVE.GRP
duke-grp validate ARCHIVE.GRP
duke-grp extract ARCHIVE.GRP
duke-grp get ARCHIVE.GRP FILENAME
duke-grp append ARCHIVE.GRP FILENAME
duke-grp replace ARCHIVE.GRP FILE_IN_GRP FILENAME
duke-grp create ARCHIVE.GRP DIRECTORY
```

Extraction writes files to the current directory. GRP member names are limited
to the format's 12-character filename field.

### ART tile sets

```text
duke-art info TILES000.ART
duke-art list TILES000.ART
duke-art validate TILES000.ART
duke-art extract TILES000.ART
duke-art get TILES000.ART TILE
duke-art append TILES000.ART WIDTH HEIGHT PIXELS.raw [PICANM]
duke-art replace TILES000.ART TILE WIDTH HEIGHT PIXELS.raw [PICANM]
duke-art create TILES999.ART TILE WIDTH HEIGHT PIXELS.raw [PICANM]
```

Pixel input and output is raw, column-major tile data. `PICANM` defaults to
zero and accepts decimal or `0x`-prefixed hexadecimal values.

### Build maps

```text
duke-map info E1L1.MAP
duke-map dump E1L1.MAP
duke-map validate E1L1.MAP
```

`dump` prints every sector, wall, and sprite field. The validator reports the
first structural or geometric problem it encounters.

## Library example

This example loads a map, adds a sprite, and saves the result:

```c
#include <stdio.h>

#include <libduke/map.h>

int main(void)
{
    DukeMapFile *map = duke_map_file_new();
    DukeMapSprite *sprite;

    if (map == NULL) {
        return 1;
    }

    if (!duke_map_file_read_from_filename(map, "input.map")) {
        fprintf(stderr, "Unable to load map: %s\n", map->last_error);
        duke_map_file_free(map);
        return 1;
    }

    sprite = duke_map_file_add_sprite(map);
    if (sprite == NULL) {
        fprintf(stderr, "Unable to add sprite: %s\n", map->last_error);
        duke_map_file_free(map);
        return 1;
    }

    sprite->x = map->posx;
    sprite->y = map->posy;
    sprite->z = map->posz;
    sprite->sectnum = map->cursectnum;
    sprite->statnum = 0;
    sprite->ang = 0;
    sprite->picnum = 0;

    if (!duke_map_file_write_to_filename(map, "output.map")) {
        fprintf(stderr, "Unable to save map: %s\n", map->last_error);
        duke_map_file_free(map);
        return 1;
    }

    duke_map_file_free(map);
    return 0;
}
```

Compile against an installed copy with your usual C compiler:

```sh
cc example.c -lduke -lm -o example
```

Public APIs are declared in:

- [`include/libduke/grp.h`](include/libduke/grp.h)
- [`include/libduke/art.h`](include/libduke/art.h)
- [`include/libduke/map.h`](include/libduke/map.h)
- [`include/libduke/palette.h`](include/libduke/palette.h)

Objects and returned buffers remain owned by their parent library object unless
the API documentation says otherwise. Inspect `last_error` after an operation
returns failure to obtain a diagnostic where supported.

## Project status

The formats implemented here are binary formats with fixed-size fields. Keep
backups of game data before modifying it, and validate generated files before
using them in a game or editor.

## License

libduke is available under the [MIT License](LICENSE).
