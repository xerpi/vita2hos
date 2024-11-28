#include "compiler_iface.h"
#include <getopt.h>

static int usage(const char* prog)
{
	fprintf(stderr,
		"Usage: %s [options] file\n"
		"Options:\n"
		"  -o, --out=<file>   Specifies the output deko3d shader module file (.dksh)\n"
		"  -r, --raw=<file>   Specifies the file to which output raw Maxwell bytecode\n"
		"  -t, --tgsi=<file>  Specifies the file to which output intermediary TGSI code\n"
		"  -s, --stage=<name> Specifies the pipeline stage of the shader\n"
		"                     (vert, tess_ctrl, tess_eval, geom, frag, comp)\n"
		"  -v, --version      Displays version information\n"
		, prog);
	return EXIT_FAILURE;
}

int main(int argc, char* argv[])
{
	const char *inFile = nullptr, *outFile = nullptr, *rawFile = nullptr, *tgsiFile = nullptr, *stageName = nullptr;

	static struct option long_options[] =
	{
		{ "out",     required_argument, NULL, 'o' },
		{ "raw",     required_argument, NULL, 'r' },
		{ "tgsi",    required_argument, NULL, 't' },
		{ "stage",   required_argument, NULL, 's' },
		{ "help",    no_argument,       NULL, '?' },
		{ "version", no_argument,       NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};

	int opt, optidx = 0;
	while ((opt = getopt_long(argc, argv, "o:r:t:s:?v", long_options, &optidx)) != -1)
	{
		switch (opt)
		{
			case 'o': outFile = optarg; break;
			case 'r': rawFile = optarg; break;
			case 't': tgsiFile = optarg; break;
			case 's': stageName = optarg; break;
			case '?': usage(argv[0]); return EXIT_SUCCESS;
			case 'v': printf("%s - Built on %s %s\n", PACKAGE_STRING, __DATE__, __TIME__); return EXIT_SUCCESS;
			default:  return usage(argv[0]);
		}
	}

	if ((argc-optind) != 1)
		return usage(argv[0]);
	inFile = argv[optind];

	if (!stageName)
	{
		fprintf(stderr, "Missing pipeline stage argument (--stage)\n");
		return EXIT_FAILURE;
	}

	if (!outFile && !rawFile && !tgsiFile)
	{
		fprintf(stderr, "No output file specified\n");
		return EXIT_FAILURE;
	}

	pipeline_stage stage;
	if (0) ((void)0);
#define TEST_STAGE(_str,_val) else if (strcmp(stageName,(_str))==0) stage = (_val)
	TEST_STAGE("vert", pipeline_stage_vertex);
	TEST_STAGE("tess_ctrl", pipeline_stage_tess_ctrl);
	TEST_STAGE("tess_eval", pipeline_stage_tess_eval);
	TEST_STAGE("geom", pipeline_stage_geometry);
	TEST_STAGE("frag", pipeline_stage_fragment);
	TEST_STAGE("comp", pipeline_stage_compute);
#undef TEST_STAGE
	else
	{
		fprintf(stderr, "Unrecognized pipeline stage: `%s'\n", stageName);
		return EXIT_FAILURE;
	}

	FILE* fin = fopen(inFile, "rb");
	if (!fin)
	{
		fprintf(stderr, "Could not open input file: %s\n", inFile);
		return EXIT_FAILURE;
	}

	fseek(fin, 0, SEEK_END);
	long fsize = ftell(fin);
	rewind(fin);

	char* glsl_source = new char[fsize+1];
	fread(glsl_source, 1, fsize, fin);
	fclose(fin);
	glsl_source[fsize] = 0;

	DekoCompiler compiler{stage};
	bool rc = compiler.CompileGlsl(glsl_source);
	delete[] glsl_source;

	if (!rc)
		return EXIT_FAILURE;

	if (outFile)
		compiler.OutputDksh(outFile);

	if (rawFile)
		compiler.OutputRawCode(rawFile);

	if (tgsiFile)
		compiler.OutputTgsi(tgsiFile);

	return EXIT_SUCCESS;
}
