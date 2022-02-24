
project('uam', ['c', 'cpp'],
	version: '1.1.0',
	default_options: [ 'buildtype=release', 'strip=true', 'b_ndebug=if-release', 'c_std=c99', 'cpp_std=c++11' ],
)

prog_python = import('python3').find_python()
has_mako = run_command(
	prog_python, '-c',
	'''
from distutils.version import StrictVersion
import mako
assert StrictVersion(mako.__version__) > StrictVersion("0.8.0")
	''')
if has_mako.returncode() != 0
	error('Python (3.x) mako module >= 0.8.0 required to build uam.')
endif

compiler_c = meson.get_compiler('c')
compiler_cpp = meson.get_compiler('cpp')

prog_bison = find_program('bison', native: true)
prog_flex = find_program('flex', native: true)

pre_args = [
	'-DPACKAGE_STRING="@0@ @1@"'.format(meson.project_name(), meson.project_version()),
	'-DDESKTOP',
	'-D_USE_MATH_DEFINES',
]

if host_machine.system() != 'windows'
	pre_args += '-D_GNU_SOURCE'
	foreach f : [ 'posix_memalign' ]
		if compiler_c.has_function(f)
			pre_args += '-DHAVE_@0@'.format(f.to_upper())
		endif
	endforeach
else
	pre_args += [
		'-mno-ms-bitfields',
		'-Werror=format-extra-args',
		'-D__STDC_FORMAT_MACROS',
		'-D__USE_MINGW_ANSI_STDIO=1',
	]
endif

foreach a : [ '-ffunction-sections' ]
	if compiler_c.has_argument(a)
		pre_args += a
	endif
endforeach

foreach a : pre_args
	add_project_arguments(a, language : ['c', 'cpp'])
endforeach

foreach a : [ '-Werror=implicit-function-declaration' ]
	if compiler_c.has_argument(a)
		add_project_arguments(a, language : 'c')
	endif
endforeach

foreach a : [ '-Wno-class-memaccess', '-Wno-non-virtual-dtor' ]
	if compiler_cpp.has_argument(a)
		add_project_arguments(a, language : 'cpp')
	endif
endforeach

foreach a : [ '-Wl,--gc-sections' ]
	if compiler_c.has_link_argument(a)
		add_project_link_arguments(a, language : [ 'c', 'cpp' ])
	endif
endforeach

uam_files = []
uam_incs = []

subdir('source')
subdir('mesa-imported')

uam = executable(
	'uam',
	uam_files,
	include_directories: uam_incs,
	install: true,
)
