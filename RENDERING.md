# Optional map renderer

The renderer is an optional `libduke-render` library. The initial backend is
OpenGL 4.1 through sokol_gfx. `duke-view` uses sokol_app for its window and input;
SDL, Qt, Rust and native WebGPU are not build dependencies.

Sokol's unmodified headers are vendored in `third_party/sokol`; `REVISION` records
the exact upstream revision, and `LICENSE` contains its zlib license. No build-time
network access is needed. Only enabled builds compile Sokol or discover graphics
libraries. GLSL shaders are embedded in renderer.c; other backends need shader
variants before they can be enabled.

## Build and run

After the usual Conan dependency setup:

```sh
meson setup build-render --native-file build/conan_meson_native.ini \
  -Drenderer=enabled -Dviewer=enabled
meson compile -C build-render
./build-render/duke-view path/to/level.map path/to/DUKE3D.GRP
```

Linux requires OpenGL and the X11, Xi and Xcursor development libraries. The
viewer currently uses Sokol's X11/GLX path (XWayland on Wayland desktops).
Windows links the system OpenGL/window libraries; macOS uses OpenGL and Cocoa.
Linux is the currently tested platform; Windows and macOS still need validation.
The default build leaves both options disabled. Renderer-only builds do not
require window/input dependencies.

Controls: WASD moves horizontally, Q/E moves down/up, Shift increases speed.
Click to capture the mouse for look; Escape releases it, then exits. Losing focus
releases input. Navigation starts at the map's player position and angle and is
free-flight: no collision, gravity, game simulation or sprite interactions.
`--frames N` exits after N frames for automated graphics smoke tests.

## Embedding

1. Create/make current an OpenGL context and call `sg_setup` yourself. Use the
   supplied Sokol header and implementation exported by libduke-render; do not
   compile a second Sokol implementation. Provision image/view pools for the
   number of distinct map and sprite tiles (the viewer uses 8192 each).
2. Load the map and GRP directory with libduke. Call `duke_renderer_create` with
   the color/depth formats and sample count of your intended render target.
   It snapshots geometry and textures; the map and archive can then be freed.
3. Begin your Sokol pass, apply a viewport if necessary, call
   `duke_renderer_draw` with a column-major OpenGL view-projection matrix, then
   end/commit the pass and present. This renderer does not begin/end passes or
   present buffers. The host owns resizing, event dispatch and timing.
4. Destroy the renderer while its graphics context remains current, before
   `sg_shutdown`. Recreate it to reflect map edits or context loss.

World coordinates are `(map.x / 1024, -map.z / 16384, map.y / 1024)`.
The differing Z scale follows Build's fixed-point vertical coordinates. A Build
angle of zero points along +X; increasing angles turn toward +Z. No window types
appear in the public renderer API.

For Qt, a future QOpenGLWidget adapter can call this API with its context current
and use its `defaultFramebufferObject()` as the Sokol swapchain framebuffer,
including the matching physical pixel dimensions and attachment formats. The
renderer resets Sokol's cached GL state before drawing; Qt integration must also
respect Qt's context lifetime. All graphics calls are on the graphics thread;
this is not an independent Sokol device per renderer.

## Current rendering scope

- Textured static walls, ceilings and floors with nearest-neighbor sampling.
- Concave sector outlines and hole loops, triangulated by horizontal bands.
- Slopes, portal upper/lower wall bands, and alpha-tested masked walls.
- Face billboards and angle-oriented wall/floor sprites with repeats, ART and
  sprite offsets, centering, flips, invisibility and one-sided rendering.
- Translucent sprites use approximate 2/3 or 1/3 alpha, sorted back to front;
  intersecting sprites can still exhibit sorting artifacts.
- Basic UV scaling, panning, flips and approximate shade brightness.
- Missing tiles use a magenta checkerboard; missing/invalid PALETTE.DAT fails.

This is an inspection renderer, not an EDuke replacement. It does not yet render
animated/directional actor frames, parallax skies, wall translucency, palette lookup variants,
first-wall-relative floor UVs or exact wall alignment rules. It draws all sectors
with a depth buffer rather than reproducing Build portal visibility, so overlapping
rooms/effect sectors may differ from the game. Self-intersecting sector outlines
are not supported. Crossing sloped portal boundaries use endpoint approximation.

Tests cover concave/hole triangulation area and containment, slope heights,
invalid renderer arguments and Sokol resource setup using a dummy backend. The
viewer can additionally be smoke-tested against a real map/GRP with `--frames`.
