project(
  'libPorpoise',
  'c',
  version: '0.0.0-test',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

libporpoise_stub_inc = include_directories('include')

libporpoise_stub_lib = static_library(
  'porpoise_stub',
  [
    'src/host_main.c',
    'src/os_stub.c',
  ],
  include_directories: libporpoise_stub_inc,
  install: false,
)

libporpoise_dep = declare_dependency(
  compile_args: ['-DLIBPORPOISE_PORT=1'],
  include_directories: libporpoise_stub_inc,
  link_with: libporpoise_stub_lib,
)

meson.override_dependency('libPorpoise', libporpoise_dep)

porpoise_title_host_dep = declare_dependency(
  include_directories: libporpoise_stub_inc,
  link_with: libporpoise_stub_lib,
)

meson.override_dependency('porpoise-title-host', porpoise_title_host_dep)
