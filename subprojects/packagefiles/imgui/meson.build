project(
  'imgui',
  'cpp',
  version: '1.92.9',
  default_options: ['cpp_std=c++17'],
)

imgui_inc = include_directories('.')
imgui_lib = static_library(
  'imgui',
  files(
    'imgui.cpp',
    'imgui_draw.cpp',
    'imgui_tables.cpp',
    'imgui_widgets.cpp',
  ),
  include_directories: imgui_inc,
  install: false,
)

imgui_dep = declare_dependency(
  link_with: imgui_lib,
  include_directories: imgui_inc,
)
imgui_source_root = meson.current_source_dir()
imgui_sdlrenderer2_backend_sources = files(
  'backends/imgui_impl_sdl2.cpp',
  'backends/imgui_impl_sdlrenderer2.cpp',
)
