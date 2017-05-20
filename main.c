#define MAP_LOD "Games.lod"

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>


void usage_exit();



int main(int argc, char *argv[]) {
	if (argc <= 1) {
		fprintf(stderr, "!! Missing command \n");
		usage_exit(argv, 0);
	}

	char *filename = argv[1];
	printf ("-- Parsing file %s... \n", filename);
	struct doc_file *p_doc = parse_doc(filename);
	if (!p_doc) {
		fprintf(stderr, "!! Error parsing file %s \n", filename);
		fprintf(stderr, "!! %s \n", parser_err_msg);
		exit(-1);
	}

	bool is_valid = validate_doc(p_doc);
	printf ("File %s is %svalid \n", filename, (is_valid ? "" : "NOT "));
	if (!is_valid) {
		fprintf(stderr, "!! File %s is NOT valid \n", filename);
		fprintf(stderr, "!! %s \n", parser_err_msg);
		exit(-1);
	}

	print_header(p_doc);
	//print_dir(p_doc);
	//print_fat(p_doc);
	close_doc(p_doc);
}


void usage_exit(char *argv[], int rc) {
	printf("\n");
	printf("    Usage: %s   <filename.doc> \n", argv[0]);
	printf("\n");

	exit(rc);
}
