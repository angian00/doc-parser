#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <iconv.h>


//----------------------------------------------------------------------

typedef void (*parse_cbk)(struct doc_file *doc, char *buffer, unsigned int buffer_size);

//----------------------------------------------------------------------
// local function declaration

void parse_difat(FILE *fp, struct doc_file *doc);
void parse_fat(FILE *fp, struct doc_file *doc);
void parse_fat_sector(FILE *fp, struct doc_file *doc, uint32_t i_sector);

void parse_chain(FILE *fp, struct doc_file *doc, unsigned int start_sector, parse_cbk parse_chain_cbk);
void parse_dir_chain(struct doc_file *doc, char *buffer, unsigned int buffer_size);


void utf162ascii(char *str_to, char *str_from, int len);

//----------------------------------------------------------------------
// implementation

struct doc_file *parse_doc(char *filename) {
	struct doc_file *doc = (struct doc_file *)malloc(sizeof(struct doc_file));

	errno = 0;
	FILE *fp = fopen(filename, "rb");
    if (!fp) {
            sprintf(parser_err_msg, "could not open file: %s; errno: %d", filename, errno);
            free(doc);
            return NULL;
    }

	size_t n_read = fread(&doc->header, sizeof(struct header), 1, fp);
	if (!n_read) {
            sprintf(parser_err_msg, "could not read from file: %s", filename);
            free(doc);
		    fclose(fp);
            return NULL;
    }

	doc->sector_size = 1 << doc->header.sector_shift;

    parse_fat(fp, doc);
	parse_chain(fp, doc, doc->header.dir_sector_start, parse_dir_chain);

    fclose(fp);

	return doc;
}



bool validate_doc(struct doc_file *doc) {
	char DOC_SIGNATURE[] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

	struct header *h = &doc->header;
	if (memcmp(h->signature, DOC_SIGNATURE, sizeof(doc->header.signature))) {
        sprintf(parser_err_msg, "invalid file signature");
        return false;
	}

	if (h->minor_version != 0x003E) {
        sprintf(parser_err_msg, "invalid minor version");
        return false;
	}

	if ((h->major_version != 0x0003) && (h->major_version != 0x0004) ) {
        sprintf(parser_err_msg, "invalid major version");
        return false;
	}

	if (h->byte_order != 0xFFFE) {
        sprintf(parser_err_msg, "invalid byte order");
        return false;
	}

	if ( ((h->major_version == 0x0003) && (h->sector_shift != 0x0009)) ||
		 ((h->major_version == 0x0004) && (h->sector_shift != 0x000C)) ) {
        sprintf(parser_err_msg, "invalid sector shift");
        return false;
	}

	if (h->minisector_shift != 0x0006) {
        sprintf(parser_err_msg, "invalid minisector shift");
        return false;
	}

	if ( (h->major_version == 0x0003) && (h->num_dir_sectors != 0x0000) ) {
        sprintf(parser_err_msg, "invalid number of directory sectors");
        return false;
	}

	if (h->ministream_cutoff_size != 0x00001000) {
        sprintf(parser_err_msg, "invalid ministream cutoff size");
        return false;
	}

	

	return true;
}


void print_header(struct doc_file *doc) {
    struct header *h = &doc->header;

	printf("-- Header \n");

    printf("  major version: %"PRIu16" \n", h->major_version);
    printf("  sector shift: %"PRIu16" \n", h->sector_shift);
    printf("  num_fat_sectors: %"PRIu32" \n", h->num_fat_sectors);
    printf("  num_dir_sectors: %"PRIu32" \n", h->num_dir_sectors);
    //if (h->num_dir_sectors)
	    printf("  dir_sector_start: %"PRIu32" \n", h->dir_sector_start);
    printf("  num_minifat_sectors: %"PRIu32" \n", h->num_minifat_sectors);
    if (h->num_minifat_sectors)
	    printf("  minifat_sector_start: %"PRIu32" \n", h->minifat_sector_start);
    printf("  num_difat_sectors: %"PRIu32" \n", h->num_difat_sectors);
    if (h->num_difat_sectors)
    	printf("  difat_sector_start: %"PRIu32" \n", h->difat_sector_start);
}




void parse_fat(FILE *fp, struct doc_file *doc) {
	unsigned int header_difat_size = 109; //fixed, independent from major version
	uint32_t *difat = doc->header.difat;

	doc->fat_entries = malloc(0x01);
	for (int i=0; i < header_difat_size; i++) {
		if (difat[i] != FREESECT) {
			//printf("DDD difat[%u]: %"PRIu32"\n", i, difat[i]);
			parse_fat_sector(fp, doc, difat[i]);
		}
	}

	//TODO: support also external difat sectors
}


void parse_fat_sector(FILE *fp, struct doc_file *doc, uint32_t i_sector) {
	unsigned int n_new_entries = doc->sector_size / sizeof(uint32_t);
	
	doc->fat_entries = realloc(doc->fat_entries, (doc->n_fat_entries +n_new_entries) * doc->sector_size);
	fseek(fp, (i_sector + 1) * doc->sector_size, SEEK_SET);
	fread(&doc->fat_entries[doc->n_fat_entries], doc->sector_size, 1, fp);
	//TODO: error check

	doc->n_fat_entries += n_new_entries;
}


void parse_chain(FILE *fp, struct doc_file *doc, unsigned int start_sector, parse_cbk parse_chain_cbk) {
	unsigned int chain_size = 0;
	void *chain_buffer = malloc(doc->sector_size);
	bool chain_found = false;

	unsigned int curr_sector = start_sector;

	while (true) {
		chain_buffer = realloc(chain_buffer, (++chain_size)*doc->sector_size);
		fseek(fp, (curr_sector + 1) * doc->sector_size, SEEK_SET);
		fread(&chain_buffer[(chain_size-1)*doc->sector_size], doc->sector_size, 1, fp);
		//TODO: error check

		if (doc->fat_entries[curr_sector] == (uint32_t)0xFFFFFFFE) {
			//ENDCHAIN
			parse_chain_cbk(doc, chain_buffer, chain_size*doc->sector_size);
			return;

		} else {
			curr_sector = doc->fat_entries[curr_sector];
		}
	}
}

void parse_dir_chain(struct doc_file *doc, char *buffer, unsigned int buffer_size) {
	doc->n_dir_entries = buffer_size / sizeof(struct dir_entry);
	doc->dir_entries = (struct dir_entry *)buffer;
}

void print_fat(struct doc_file *doc) {
	printf("-- FAT \n");

	for (uint32_t i=0; i < doc->n_fat_entries; i++) {
		if (doc->fat_entries[i] == 0xFFFFFFFF) {
			//free sector
			break;
		}

		printf("  [%03"PRIu32"] --> ", i);

		switch (doc->fat_entries[i]) {
		
		case (uint32_t)0xFFFFFFFC:
			printf("<DIFAT>");
			break;
		case (uint32_t)0xFFFFFFFD:
			printf("<FAT>");
			break;
		case (uint32_t)0xFFFFFFFE:
			printf("<ENDOFCHAIN>");
			break;
		default:
			printf("[%03"PRIu32"]", doc->fat_entries[i]);
		}

		printf("\n");
	}
}


void print_dir(struct doc_file *doc) {

	printf("-- Directory \n");

	for (uint32_t i=0; i < doc->n_dir_entries; i++) {
		/*
		char *in_name = dir_entries[i].name;
		char *out_name = out_name_str;
		unsigned long n_char_in = sizeof(dir_entries[i].name);
		unsigned long n_char_out = sizeof(out_name_str);
		//iconv (iconv_desc, NULL, NULL, (char **)&out_name, &n_char_out);
		//iconv(iconv_desc, &in_name, &n_char_in, (char **)&out_name, &n_char_out);

		//wprintf(L" DDD %u %S \n", i, dir_entries[i].name);
		//wprintf(L" DDD %u %S \n", i, out_name);
		//wprintf(L" DDD %u %S \n", i, in_name);
		printf(" DDD %u %c \n", i, in_name[0]);
		*/

		struct dir_entry d = doc->dir_entries[i];
		char out_name_str[200];
		utf162ascii(out_name_str, d.name, d.name_len);
		if (d.obj_type != 0x00)  {
			printf("  %s \n", out_name_str);
			printf("    obj_type %u \n", d.obj_type);
			if (d.left_id != NOSTREAM)
				printf("    left %"PRIu32" \n", d.left_id);
			if (d.right_id != NOSTREAM)
				printf("    right %"PRIu32" \n", d.right_id);
			if (d.child_id != NOSTREAM)
				printf("    child %"PRIu32" \n", d.child_id);
			printf("    start sector %"PRIu32" \n", d.start_sector);
			printf("    stream size %llu \n", d.stream_size);
		}
	}
}


//iconv implementation gives error 22
void utf162ascii(char *str_to, char *str_from, int len) {  //TODO: \0005
	for (int i=0; i < len; i++) {
		str_to[i] = str_from[i*2];
	}

	str_to[len] = 0x00;
}
