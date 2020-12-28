project('dasynq', 'cpp',
  default_options: [
    'b_lto=true',
    'b_ndebug=if-release',
    'buildtype=debugoptimized',
    'c_std=c++11',
    'warning_level=3',
  # 'werror=true',
  ],
  license: 'Apache-2.0',
  version: '1.1.7-development',
)

dasynq_dep = declare_dependency(
  include_directories: include_directories('include'),
)

if meson.is_subproject()
    subdir_done()
endif

install_subdir('include',
  install_dir: 'include',
  strip_directory: true,
)

pc = import('pkgconfig')
pc.generate(
  description: 'Thread-safe cross-platform event loop library in C++',
  name: meson.project_name(),
)

executable('chatserver', 'examples/chatserver/chatserver.cc',
  dependencies: dasynq_dep,
)

threads_dep = dependency('threads')

executable('chatserver_mt', 'examples/chatserver/chatserver-mt.cc',
  dependencies: [dasynq_dep, threads_dep],
)

dasynq_test = executable('dasynq_test', 'tests/dasynq-tests.cc',
  dependencies: [dasynq_dep, threads_dep],
)

test('dasynq_test', dasynq_test)
