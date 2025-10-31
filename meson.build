project('dasynq', 'cpp',
  default_options: [
    'b_ndebug=if-release',
    'buildtype=debugoptimized',
    'c_std=c++11',
    'warning_level=1',
  ],
  version: '2.1.3',
)

dasynq_deps = []
if target_machine.system() == 'freebsd'
  dasynq_deps += meson.get_compiler('cpp').find_library('rt')
endif

dasynq_dep = declare_dependency(
  dependencies: dasynq_deps,
  include_directories: include_directories('include'),
)

if meson.is_subproject()
    subdir_done()
endif

install_subdir('include',
  install_dir: get_option('includedir'),
  strip_directory: true,
)

pc = import('pkgconfig')
pc.generate(
  description: 'Thread-safe cross-platform event loop library in C++',
  filebase: 'dasynq',
  libraries: dasynq_deps,
  name: 'Dasynq',
  url: 'https://davmac.org/projects/dasynq/',
)

threads_dep = dependency('threads')

dasynq_test = executable('dasynq_test', 'tests/dasynq-tests.cc',
  dependencies: [dasynq_dep, threads_dep],
)

test('dasynq_test', dasynq_test, verbose: true)
