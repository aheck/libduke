project(
  'libduke',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    # Sanitizers are opt-in; release artifacts must not require their runtimes.
    'b_sanitize=none',
  ],
)

inc = include_directories('include')
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

libduke = both_libraries(
  'duke',
  'src/lib/art.c',
  'src/lib/camera.c',
  'src/lib/glist.c',
  'src/lib/grp.c',
  'src/lib/input.c',
  'src/lib/map.c',
  'src/lib/palette.c',
  include_directories: inc,
  dependencies: math_dep,
  install: true,
  version: meson.project_version(),
)

libduke_dep = declare_dependency(
  link_with: libduke,
  dependencies: math_dep,
  include_directories: inc,
)

libduke_static_dep = declare_dependency(
  link_with: libduke.get_static_lib(),
  dependencies: math_dep,
  include_directories: inc,
)

install_headers(
  'include/libduke/art.h',
  'include/libduke/camera.h',
  'include/libduke/grp.h',
  'include/libduke/map.h',
  'include/libduke/palette.h',
  subdir: 'libduke',
)

executable(
  'duke-map',
  'src/tools/duke-map.c',
  dependencies: libduke_static_dep,
  install: true,
)

duke_grp_tool = executable(
  'duke-grp',
  ['src/tools/duke-grp.c', 'src/tools/platform.c'],
  dependencies: libduke_static_dep,
  install: true,
)

duke_art_tool = executable(
  'duke-art',
  ['src/tools/duke-art.c', 'src/tools/platform.c'],
  dependencies: libduke_static_dep,
  install: true,
)

# Disabled builds do not search for Check or build/register its test executables.
if get_option('check_tests')
  # Conan's PkgConfigDeps generates check.pc for us.
  check_dep = dependency('check')

  test_grp = executable(
    'test-grp',
    'tests/test_grp.c',
    dependencies: [
      libduke_dep,
      check_dep,
    ],
  )

  test('grp', test_grp)

  test_art = executable(
    'test-art',
    'tests/test_art.c',
    dependencies: [
      libduke_dep,
      check_dep,
    ],
  )

  test('art', test_art)

  test_palette = executable(
    'test-palette',
    'tests/test_palette.c',
    dependencies: [
      libduke_dep,
      check_dep,
    ],
  )

  test('palette', test_palette)

  test_map = executable(
    'test-map',
    'tests/test_map.c',
    dependencies: [
      libduke_dep,
      check_dep,
    ],
  )

  test('map', test_map)

endif

# Sokol is vendored at a fixed revision; disabled builds have no GPU dependencies.
if not get_option('renderer').disabled()
  sokol_inc = include_directories('third_party/sokol')
  renderer_deps = [libduke_static_dep, math_dep]
  backend_args = ['-DSOKOL_GLCORE']
  if host_machine.system() == 'linux'
    renderer_deps += dependency('gl')
    renderer_deps += dependency('threads')
    renderer_deps += cc.find_library('dl')
  elif host_machine.system() == 'windows'
    renderer_deps += cc.find_library('opengl32')
  elif host_machine.system() == 'darwin'
    renderer_deps += dependency('appleframeworks', modules: ['OpenGL'])
  else
    error('The initial renderer supports desktop OpenGL on Linux, Windows and macOS')
  endif
  renderer_lib = both_libraries('duke-render', ['src/lib/renderer.c', 'src/lib/sokol.c'],
    include_directories: [inc, sokol_inc], dependencies: renderer_deps,
    c_args: backend_args, install: true)
  renderer_dep = declare_dependency(link_with: renderer_lib,
    include_directories: [inc, sokol_inc], dependencies: renderer_deps)
  install_headers('include/libduke/renderer.h', 'third_party/sokol/sokol_gfx.h', subdir: 'libduke')
  if not get_option('viewer').disabled()
    viewer_deps = [renderer_dep]
    viewer_args = backend_args
    if host_machine.system() == 'linux'
      foreach name : ['x11', 'xi', 'xcursor']
        viewer_deps += dependency(name)
      endforeach
    elif host_machine.system() == 'windows'
      foreach name : ['user32', 'gdi32', 'shell32']
        viewer_deps += cc.find_library(name)
      endforeach
    elif host_machine.system() == 'darwin'
      viewer_args += ['-x', 'objective-c']
      viewer_deps += dependency('appleframeworks', modules: ['Cocoa', 'QuartzCore'])
    endif
    executable('duke-view', 'src/tools/duke-view.c', c_args: viewer_args,
      include_directories: [inc, sokol_inc], dependencies: viewer_deps, install: true)
  endif
elif get_option('viewer').enabled()
  error('Enable the renderer with -Drenderer=enabled when enabling the viewer')
endif

if not get_option('renderer').disabled()
  renderer_test = executable('test-renderer', ['tests/test_renderer.c', 'src/lib/sokol.c'],
    include_directories: [inc, sokol_inc], dependencies: [libduke_static_dep, math_dep],
    c_args: ['-DSOKOL_DUMMY_BACKEND', '-UNDEBUG'])
  test('renderer', renderer_test)
  # Optional surfaceless OpenGL regression test; no window system or game data.
  if host_machine.system() == 'linux'
    egl_dep = dependency('egl', version: '>=1.5', required: false)
    if egl_dep.found()
      renderer_gl_test = executable('test-renderer-gl',
        ['tests/test_renderer_gl.c', 'src/lib/sokol.c'],
        include_directories: [inc, sokol_inc],
        dependencies: renderer_deps + [egl_dep],
        c_args: ['-DSOKOL_GLCORE', '-UNDEBUG'])
      test('renderer-gl', renderer_gl_test)
    endif
  endif
endif

# Camera math is available even when the renderer is disabled.
camera_test = executable('test-camera', 'tests/test_camera.c',
  dependencies: [libduke_static_dep, math_dep], c_args: ['-UNDEBUG'])
test('camera', camera_test)

# No Check dependency: exercise the OS-specific archive publication layer.
tool_platform_test = executable('test-tool-platform',
  ['tests/test_tool_platform.c', 'src/tools/platform.c'])
test('tool-platform', tool_platform_test)
python = find_program('python3', 'python', required: false)
if python.found()
  test('archive-tools', python,
    args: [files('tests/test_tools.py'), duke_art_tool, duke_grp_tool])
endif
