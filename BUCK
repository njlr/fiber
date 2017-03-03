include_defs('//BUCKAROO_DEPS')

cxx_library(
  name = 'boost-fiber',
  header_namespace = 'boost/fiber',
  exported_headers = subdir_glob([
    ('include/boost/fiber', '**/*.hpp'),
  ]),
  srcs = glob([
    'src/**/*.cpp',
  ]),
  compiler_flags = [
    '-std=c++14',
  ],
  visibility = [
    'PUBLIC',
  ],
  deps = BUCKAROO_DEPS,
)
