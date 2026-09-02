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
- Add and remove map sprites
- Validate map structure, geometry, portals, slopes, sprites, and player starts
- Explicit little-endian handling in the library's binary format readers
- Build static and shared libraries

The repository also builds three command-line programs: `duke-grp`,
`duke-art`, and `duke-map`.

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

To run the test suite:

```sh
meson test -C build --print-errorlogs
```

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
