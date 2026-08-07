project(
  'Porpoise-Tool',
  'c',
  version: '0.2.0',
  meson_version: '>=1.2.0',
  default_options: [
    'c_std=c99',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('c')
python = import('python').find_installation()

porpoise_inc = include_directories('include', 'third_party/jsmn')
runtime_path = meson.project_source_root() / 'runtime'
installed_runtime_path = get_option('prefix') / get_option('datadir') / 'porpoise' / 'runtime'
porpoise_compile_args = [
  '-DPORPOISE_RUNTIME_DIR="' + runtime_path + '"',
  '-DPORPOISE_INSTALLED_RUNTIME_DIR="' + installed_runtime_path + '"',
  '-DPORPOISE_TOOL_VERSION="' + meson.project_version() + '"',
]

porpoise_core_sources = files(
  'src/analysis.c',
  'src/abi.c',
  'src/lower.c',
  'src/options.c',
  'src/program.c',
  'src/project.c',
  'src/report.c',
  'src/util.c',
  'third_party/jsmn/jsmn.c',
)

porpoise_core = static_library(
  'porpoise_core',
  porpoise_core_sources,
  include_directories: porpoise_inc,
  c_args: porpoise_compile_args,
  install: false,
)

porpoise = executable(
  'porpoise',
  'src/main.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
  install: true,
)

install_data(
  'runtime/include/porpoise_lifted.h',
  'runtime/include/porpoise_libporpoise_adapter.h',
  install_dir: get_option('datadir') / 'porpoise' / 'runtime' / 'include',
)
install_data(
  'runtime/src/porpoise_lifted.c',
  'runtime/src/porpoise_libporpoise_adapter.c',
  install_dir: get_option('datadir') / 'porpoise' / 'runtime' / 'src',
)

runtime_test = executable(
  'test_runtime',
  [
    'tests/test_runtime.c',
    'runtime/src/porpoise_lifted.c',
  ],
  include_directories: include_directories('runtime/include'),
)
test('lifted-runtime', runtime_test)

test(
  'cli-contract',
  python,
  args: [
    files('tests/test_cli.py'),
    porpoise,
    meson.project_source_root(),
  ],
  timeout: 180,
)

compat_libporpoise = get_option('libporpoise_compat_path')
if compat_libporpoise != ''
  test(
    'libporpoise-compatibility-smoke',
    python,
    args: [
      files('tests/test_compat.py'),
      porpoise,
      meson.project_source_root(),
      compat_libporpoise,
    ],
    timeout: 180,
  )
endif
