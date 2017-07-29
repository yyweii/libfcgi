env = Environment(CPPPATH = '#/include', CCFLAGS = '-Wall -Wextra -Werror -g')

if ARGUMENTS.get('release', 0):
  env['CCFLAGS'] += ' -O2'

src_files = Split("""
  src/fcgi_app.cpp
  src/fcgi_connection.cpp
  src/fcgi_record.cpp
  src/fcgi_request.cpp
""")

env.SharedLibrary(target = 'lib/fcgi', source = src_files)

link_libs = Split("""
  fcgi
  boost_system-mt
  boost_thread-mt
""")

env.Program(target = 'demo/demo',
            source = 'example/demo.cpp',
            LIBS = link_libs,
            LIBPATH = 'lib',
            RPATH = '../lib')
