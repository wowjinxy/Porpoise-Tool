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
threads_dep = dependency('threads')
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
  'src/asm_data.c',
  'src/lower.c',
  'src/operation.c',
  'src/options.c',
  'src/program.c',
  'src/project.c',
  'src/raw_word.c',
  'src/relocation.c',
  'src/report.c',
  'src/session.c',
  'src/sha256.c',
  'src/sdk_contract.c',
  'src/sdk_catalog.c',
  'src/signature.c',
  'src/symbol_map.c',
  'src/plan.c',
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
  'tools/porpoise_sdk_catalog.py',
  install_dir: get_option('bindir'),
  rename: 'porpoise-sdk-catalog',
  install_mode: 'rwxr-xr-x',
)

install_data(
  'runtime/include/porpoise_lifted.h',
  'runtime/include/porpoise_libporpoise_adapter.h',
  'runtime/include/porpoise_title_host.h',
  install_dir: get_option('datadir') / 'porpoise' / 'runtime' / 'include',
)
install_data(
  'runtime/src/porpoise_lifted.c',
  'runtime/src/porpoise_libporpoise_adapter.c',
  'runtime/src/porpoise_libporpoise_ai.c',
  'runtime/src/porpoise_libporpoise_ar.c',
  'runtime/src/porpoise_libporpoise_arena.c',
  'runtime/src/porpoise_libporpoise_card.c',
  'runtime/src/porpoise_libporpoise_gx.c',
  'runtime/src/porpoise_libporpoise_gx_headers.h',
  'runtime/src/porpoise_libporpoise_gx_objects.c',
  'runtime/src/porpoise_libporpoise_gx_values.c',
  'runtime/src/porpoise_libporpoise_guest_os.c',
  'runtime/src/porpoise_libporpoise_message_queue.c',
  'runtime/src/porpoise_libporpoise_os_report.c',
  'runtime/src/porpoise_libporpoise_builtins_private.h',
  'runtime/src/porpoise_libporpoise_private.h',
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
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test('libporpoise-adapter', libporpoise_adapter_test)

libporpoise_vi_framebuffer_adapter_sources = [
  'tests/test_libporpoise_vi_framebuffer_adapter.c',
  'runtime/src/porpoise_lifted.c',
  'runtime/src/porpoise_libporpoise_adapter.c',
  'runtime/src/porpoise_libporpoise_guest_os.c',
  'runtime/src/porpoise_libporpoise_ar.c',
  'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
  'tests/fixtures/libporpoise_stub/src/os_stub.c',
  'tests/fixtures/libporpoise_stub/src/gx_stub.c',
  'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
]

libporpoise_vi_framebuffer_adapter_test = executable(
  'test_libporpoise_vi_framebuffer_adapter',
  libporpoise_vi_framebuffer_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test(
  'libporpoise-vi-framebuffer-adapter',
  libporpoise_vi_framebuffer_adapter_test,
)

libporpoise_vi_missing_framebuffer_contract_adapter_test = executable(
  'test_libporpoise_vi_missing_framebuffer_contract_adapter',
  libporpoise_vi_framebuffer_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_STUB_DISABLE_VI_NEXT_FRAMEBUFFER_GUEST_ADDRESS_CONTRACT',
  ],
)
test(
  'libporpoise-vi-missing-framebuffer-contract-adapter',
  libporpoise_vi_missing_framebuffer_contract_adapter_test,
)

libporpoise_xfb_vi_ordering_test = executable(
  'test_libporpoise_xfb_vi_ordering',
  [
    'tests/test_libporpoise_xfb_vi_ordering.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'runtime/src/porpoise_libporpoise_gx.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-pedantic-errors',
    '-DLIBPORPOISE_PORT',
  ],
  override_options: ['c_std=c99'],
)
test('libporpoise-xfb-vi-ordering', libporpoise_xfb_vi_ordering_test)

libporpoise_pointer_tokens_test = executable(
  'test_libporpoise_pointer_tokens',
  [
    'tests/test_libporpoise_pointer_tokens.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DOS_HOST_ADDRESS_TOKEN_SLOT_BITS=2U',
  ],
)
test('libporpoise-pointer-tokens', libporpoise_pointer_tokens_test)

libporpoise_ai_init_adapter_test = executable(
  'test_libporpoise_ai_init_adapter',
  [
    'tests/test_libporpoise_ai_init_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_ai.c',
    'tests/fixtures/libporpoise_stub/src/ai_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
  ),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test(
  'libporpoise-ai-init-adapter',
  libporpoise_ai_init_adapter_test,
)

libporpoise_ar_adapter_test = executable(
  'test_libporpoise_ar_adapter',
  [
    'tests/test_libporpoise_ar_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
  ),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test('libporpoise-ar-adapter', libporpoise_ar_adapter_test)

libporpoise_gx_init_adapter_test = executable(
  'test_libporpoise_gx_init_adapter',
  [
    'tests/test_libporpoise_gx_init_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'runtime/src/porpoise_libporpoise_gx.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test(
  'libporpoise-gx-init-adapter',
  libporpoise_gx_init_adapter_test,
)

libporpoise_gx_boundary_adapter_sources = [
  'tests/test_libporpoise_gx_boundary_adapter.c',
  'runtime/src/porpoise_lifted.c',
  'runtime/src/porpoise_libporpoise_adapter.c',
  'runtime/src/porpoise_libporpoise_guest_os.c',
  'runtime/src/porpoise_libporpoise_ar.c',
  'runtime/src/porpoise_libporpoise_gx.c',
  'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
  'tests/fixtures/libporpoise_stub/src/os_stub.c',
  'tests/fixtures/libporpoise_stub/src/gx_stub.c',
  'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
]

libporpoise_gx_boundary_adapter_test = executable(
  'test_libporpoise_gx_boundary_adapter',
  libporpoise_gx_boundary_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test(
  'libporpoise-gx-boundary-adapter',
  libporpoise_gx_boundary_adapter_test,
)

libporpoise_gx_copy_contract_adapter_test = executable(
  'test_libporpoise_gx_copy_contract_adapter',
  libporpoise_gx_boundary_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT',
    '-DPORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT',
  ],
)
test(
  'libporpoise-gx-copy-contract-adapter',
  libporpoise_gx_copy_contract_adapter_test,
  args: ['no-copy-contract'],
)

libporpoise_gx_missing_display_contract_adapter_test = executable(
  'test_libporpoise_gx_missing_display_contract_adapter',
  libporpoise_gx_boundary_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT',
  ],
)
test(
  'libporpoise-gx-missing-display-contract-adapter',
  libporpoise_gx_missing_display_contract_adapter_test,
  args: ['missing-display-contract'],
)

libporpoise_gx_missing_texture_contract_adapter_test = executable(
  'test_libporpoise_gx_missing_texture_contract_adapter',
  libporpoise_gx_boundary_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT',
  ],
)
test(
  'libporpoise-gx-missing-texture-contract-adapter',
  libporpoise_gx_missing_texture_contract_adapter_test,
  args: ['missing-texture-contract'],
)

libporpoise_gx_guest_address_adapter_test = executable(
  'test_libporpoise_gx_guest_address_adapter',
  libporpoise_gx_boundary_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
  ],
)
test(
  'libporpoise-gx-guest-address-adapter',
  libporpoise_gx_guest_address_adapter_test,
  args: ['guest-address-contract'],
)

libporpoise_gx_shifted_format_adapter_test = executable(
  'test_libporpoise_gx_shifted_format_adapter',
  libporpoise_gx_boundary_adapter_sources,
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_STUB_SHIFTED_GX_COPY_FORMATS',
  ],
)
test(
  'libporpoise-gx-shifted-format-adapter',
  libporpoise_gx_shifted_format_adapter_test,
  args: ['shifted-formats'],
)

libporpoise_gx_values_adapter_test = executable(
  'test_libporpoise_gx_values_adapter',
  [
    'tests/test_libporpoise_gx_values_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'runtime/src/porpoise_libporpoise_gx.c',
    'runtime/src/porpoise_libporpoise_gx_values.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_values_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test(
  'libporpoise-gx-values-adapter',
  libporpoise_gx_values_adapter_test,
)

libporpoise_gx_objects_adapter_test = executable(
  'test_libporpoise_gx_objects_adapter',
  [
    'tests/test_libporpoise_gx_objects_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'runtime/src/porpoise_libporpoise_gx.c',
    'runtime/src/porpoise_libporpoise_gx_objects.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_gx_objects_stub/src/gx_objects_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'tests/fixtures/libporpoise_gx_objects_stub/include',
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_LIBPORPOISE_FORCE_UMBRELLA_GX_HEADERS=1',
  ],
)
test(
  'libporpoise-gx-objects-adapter',
  libporpoise_gx_objects_adapter_test,
)
test(
  'libporpoise-gx-init-null-poison',
  libporpoise_gx_init_adapter_test,
  args: ['null'],
)
test(
  'libporpoise-gx-init-unencodable-poison',
  libporpoise_gx_init_adapter_test,
  args: ['unencodable'],
)
test(
  'libporpoise-gx-init-mapped-poison',
  libporpoise_gx_init_adapter_test,
  args: ['mapped'],
)

libporpoise_arena_adapter_test = executable(
  'test_libporpoise_arena_adapter',
  [
    'tests/test_libporpoise_arena_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'runtime/src/porpoise_libporpoise_arena.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test('libporpoise-arena-adapter', libporpoise_arena_adapter_test)

libporpoise_arq_adapter_test = executable(
  'test_libporpoise_arq_adapter',
  [
    'tests/test_libporpoise_arq_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test('libporpoise-arq-adapter', libporpoise_arq_adapter_test)

libporpoise_card_adapter_test = executable(
  'test_libporpoise_card_adapter',
  [
    'tests/test_libporpoise_card_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_card.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
  ),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test('libporpoise-card-adapter', libporpoise_card_adapter_test)

libporpoise_dsp_adapter_test = executable(
  'test_libporpoise_dsp_adapter',
  [
    'tests/test_libporpoise_dsp_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: ['-Werror', '-DLIBPORPOISE_PORT'],
)
test('libporpoise-dsp-adapter', libporpoise_dsp_adapter_test)

libporpoise_thread_queue_adapter_test = executable(
  'test_libporpoise_thread_queue_adapter',
  [
    'tests/test_libporpoise_thread_queue_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'tests/fixtures/libporpoise_stub/src/thread_registry_link_stub.c',
  ],
  include_directories: include_directories('runtime/include', 'runtime/src'),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test(
  'libporpoise-thread-queue-adapter',
  libporpoise_thread_queue_adapter_test,
)

libporpoise_thread_carrier_adapter_test = executable(
  'test_libporpoise_thread_carrier_adapter',
  [
    'tests/test_libporpoise_thread_carrier_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_adapter.c',
    'runtime/src/porpoise_libporpoise_guest_os.c',
    'runtime/src/porpoise_libporpoise_ar.c',
    'tests/fixtures/libporpoise_stub/src/ar_allocator_stub.c',
    'tests/fixtures/libporpoise_stub/src/os_stub.c',
    'tests/fixtures/libporpoise_stub/src/gx_stub.c',
    'tests/fixtures/libporpoise_stub/src/dispatch_stub.c',
    'tests/fixtures/libporpoise_stub/src/thread_carrier_stub.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
    'tests/fixtures/libporpoise_stub/private',
  ),
  dependencies: [math_dep, threads_dep],
  c_args: [
    '-Werror',
    '-DLIBPORPOISE_PORT',
    '-DPORPOISE_HAVE_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1',
  ],
)
test(
  'libporpoise-thread-carrier-adapter',
  libporpoise_thread_carrier_adapter_test,
)

libporpoise_message_queue_adapter_test = executable(
  'test_libporpoise_message_queue_adapter',
  [
    'tests/test_libporpoise_message_queue_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_message_queue.c',
  ],
  include_directories: include_directories('runtime/include', 'runtime/src'),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test(
  'libporpoise-message-queue-adapter',
  libporpoise_message_queue_adapter_test,
)

libporpoise_os_report_adapter_test = executable(
  'test_libporpoise_os_report_adapter',
  [
    'tests/test_libporpoise_os_report_adapter.c',
    'runtime/src/porpoise_lifted.c',
    'runtime/src/porpoise_libporpoise_os_report.c',
  ],
  include_directories: include_directories(
    'runtime/include',
    'runtime/src',
    'tests/fixtures/libporpoise_stub/include',
  ),
  dependencies: math_dep,
  c_args: ['-Werror'],
)
test(
  'libporpoise-os-report-adapter',
  libporpoise_os_report_adapter_test,
)

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

relocation_test = executable(
  'test_relocation',
  [
    'tests/test_relocation.c',
    'src/relocation.c',
  ],
  include_directories: porpoise_inc,
  c_args: ['-Werror'],
)
test('relocation', relocation_test)

sha256_test = executable(
  'test_sha256',
  [
    'tests/test_sha256.c',
    'src/sha256.c',
  ],
  include_directories: porpoise_inc,
  c_args: ['-Werror'],
)
test('sha256', sha256_test)

signature_test = executable(
  'test_signature',
  [
    'tests/test_signature.c',
    'src/signature.c',
    'src/sha256.c',
    'src/relocation.c',
  ],
  include_directories: porpoise_inc,
  c_args: ['-Werror'],
)
test('function-signature', signature_test)

sdk_contract_test = executable(
  'test_sdk_contract',
  'tests/test_sdk_contract.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
)
test('sdk-contract', sdk_contract_test)

sdk_catalog_test = executable(
  'test_sdk_catalog',
  'tests/test_sdk_catalog.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
)
test(
  'sdk-catalog',
  sdk_catalog_test,
  args: [meson.project_source_root()],
)

test(
  'sdk-catalog-tool',
  python,
  args: [files('tests/test_sdk_catalog_tool.py')],
  timeout: 180,
)

sdk_policy_test = executable(
  'test_sdk_policy',
  'tests/test_sdk_policy.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
)
test(
  'sdk-policy',
  sdk_policy_test,
  args: [meson.project_source_root()],
)

symbol_map_test = executable(
  'test_symbol_map',
  'tests/test_symbol_map.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
)
test(
  'symbol-map',
  symbol_map_test,
  args: [meson.project_source_root()],
)

session_plan_test = executable(
  'test_session_plan',
  'tests/test_session_plan.c',
  include_directories: porpoise_inc,
  link_with: porpoise_core,
  c_args: porpoise_compile_args,
)
test(
  'session-plan',
  session_plan_test,
  args: [meson.project_source_root()],
)

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
  'assembly-data-generation',
  python,
  args: [
    files('tests/test_asm_data_generation.py'),
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

test(
  'libporpoise-compatibility-checker',
  python,
  args: [
    files('tests/test_compat.py'),
    meson.project_source_root(),
    '--cc',
  ] + cc.cmd_array(),
  timeout: 180,
)

compat_libporpoise = get_option('libporpoise_compat_path')
if compat_libporpoise != ''
  test(
    'libporpoise-compatibility-smoke',
    python,
    args: [
      files('tools/check_libporpoise_compat.py'),
      compat_libporpoise,
      '--source-root',
      meson.project_source_root(),
      '--target',
      'auto',
      '--cc',
    ] + cc.cmd_array(),
    timeout: 180,
  )
endif
