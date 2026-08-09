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
math_dep = cc.find_library('m', required: false)
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
  'src/raw_word.c',
  'src/report.c',
  'src/system_lower.c',
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
  dependencies: math_dep,
)
test('lifted-runtime', runtime_test)

scalar_fp_runtime_test = executable(
  'test_scalar_fp_runtime',
  [
    'tests/test_scalar_fp_runtime.c',
    'runtime/src/porpoise_lifted.c',
  ],
  include_directories: include_directories('runtime/include'),
  dependencies: math_dep,
)
test('scalar-fp-runtime', scalar_fp_runtime_test)

psq_runtime_test = executable(
  'test_psq_runtime',
  [
    'tests/test_psq_runtime.c',
    'runtime/src/porpoise_lifted.c',
  ],
  include_directories: include_directories('runtime/include'),
  dependencies: math_dep,
)
test('psq-runtime', psq_runtime_test)

system_runtime_test = executable(
  'test_system_runtime',
  [
    'tests/test_system_runtime.c',
    'runtime/src/porpoise_lifted.c',
  ],
  include_directories: include_directories('runtime/include'),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test('system-runtime', system_runtime_test)

libporpoise_adapter_test = executable(
  'test_libporpoise_adapter',
  [
    'tests/test_libporpoise_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'tests/fixtures/libporpoise_stub/include',
  ),
  dependencies: math_dep,
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test('libporpoise-adapter', libporpoise_adapter_test)

system_lower_test = executable(
  'test_system_lower',
  [
    'tests/test_system_lower.c',
    'src/system_lower.c',
  ],
  include_directories: porpoise_inc,
  c_args: ['-Werror'],
)
test('system-lowering', system_lower_test)

raw_word_test = executable(
  'test_raw_word',
  [
    'tests/test_raw_word.c',
    'src/raw_word.c',
  ],
  include_directories: porpoise_inc,
  c_args: ['-Werror'],
)
test('raw-word-lowering', raw_word_test)

program_test = executable(
  'test_program',
  'tests/test_program.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
)
test(
  'program-parser',
  program_test,
  args: [meson.project_source_root()],
)

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

test(
  'system-cli-contract',
  python,
  args: [
    files('tests/test_system_cli.py'),
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
