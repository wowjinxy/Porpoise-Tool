project(
  'porpoise-real-lib-build-run-smoke',
  ['c', 'cpp'],
  default_options: ['c_std=c99', 'warning_level=3', 'werror=true'],
)

libporpoise_dep = dependency(
  'libPorpoise',
  fallback: ['libPorpoise', 'libporpoise_dep'],
)
title_host_dep = dependency(
  'porpoise-title-host',
  fallback: ['porpoise-title-host', 'porpoise_title_host_dep'],
)

executable(
  'porpoise_title',
  ['main.c', 'mixed_cpp.cpp'],
  dependencies: [libporpoise_dep, title_host_dep],
  install: false,
)
