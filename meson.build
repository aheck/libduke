project(
  'libduke',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

inc = include_directories('include')
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

libduke = library(
  'duke',
  'src/lib/grp.c',
  'src/lib/map.c',
  include_directories: inc,
  dependencies: math_dep,
  install: true,
  version: meson.project_version(),
)

libduke_dep = declare_dependency(
  link_with: libduke,
  include_directories: inc,
)

install_headers(
  'include/libduke/grp.h',
  'include/libduke/map.h',
  subdir: 'libduke',
)

executable(
  'duke-map',
  'src/tools/duke-map.c',
  dependencies: libduke_dep,
  install: true,
)

executable(
  'duke-grp',
  'src/tools/duke-grp.c',
  dependencies: libduke_dep,
  install: true,
)

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

test_map = executable(
  'test-map',
  'tests/test_map.c',
  dependencies: [
    libduke_dep,
    check_dep,
  ],
)

test('map', test_map)
