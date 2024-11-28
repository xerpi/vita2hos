
uam_files += custom_target(
	'glcpp-parse.[ch]',
	input : 'glcpp-parse.y',
	output : ['glcpp-parse.c', 'glcpp-parse.h'],
	command : [
		prog_bison, '-o', '@OUTPUT0@', '-p', 'glcpp_parser_',
		'--defines=@OUTPUT1@', '@INPUT@',
	],
)

uam_files += custom_target(
	'glcpp-lex.c',
	input : 'glcpp-lex.l',
	output : 'glcpp-lex.c',
	command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@'],
)

uam_files += files(
	'pp.c',
)
