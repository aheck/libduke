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

libduke = library(
  'duke',
  'src/lib/grp.c',
  'src/lib/map.c',
  include_directories: inc,
  install: true,
  version: meson.project_version(),
)

libduke_dep = declare_dependency(
  link_with: libduke,
  include_directories: inc,
)

install_headers(
  'include/libduke/grp.h',
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
