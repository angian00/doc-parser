#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <iconv.h>
#include <time.h>


//----------------------------------------------------------------------
// typedefs

typedef int (*parse_cbk)(struct doc_file *doc, char *buffer, unsigned int buffer_size);

//----------------------------------------------------------------------
// local function declaration

int parse_difat(struct doc_file *doc);
int parse_fat(struct doc_file *doc);
int parse_fat_sector(struct doc_file *doc, uint32_t i_sector);

int parse_chain(struct doc_file *doc, unsigned int start_sector, parse_cbk parse_chain_cbk);
int parse_stream(struct doc_file *doc, char *chain_name, parse_cbk parse_stream_cbk);

int parse_dir(struct doc_file *doc, char *buffer, unsigned int buffer_size);
int parse_propertyset_stream(struct doc_file *doc, char *buffer, unsigned int buffer_size);
void parse_property(struct doc_file *doc, uint32_t pid, struct property *p);


void utf16_to_ascii(char *str_to, char *str_from, int len);
void decode_str(char *str_to, char *str_from, uint16_t codepage);
void propid_to_str(char *str_to, uint32_t propid);
time_t filetime_to_unix(FILETIME filetime);
char *filetime_to_str(FILETIME filetime);

//----------------------------------------------------------------------
// implementation

struct doc_file *parse_doc(char *filename) {
	struct doc_file *doc = (struct doc_file *)malloc(sizeof(struct doc_file));

	errno = 0;
	doc->fp = fopen(filename, "rb");
    if (!doc->fp) {
            sprintf(parser_err_msg, "could not open file: %s; errno: %d", filename, errno);
            free(doc);
            return NULL;
    }

	size_t n_read = fread(&doc->header, sizeof(struct header), 1, doc->fp);
	if (!n_read) {
            sprintf(parser_err_msg, "could not read from file: %s", filename);
		    fclose(doc->fp);
            free(doc);
            return NULL;
    }

	doc->sector_size = 1 << doc->header.sector_shift;

    if (parse_fat(doc))
    	return NULL;

	if (parse_chain(doc, doc->header.dir_sector_start, parse_dir))
		return NULL;

	if (parse_stream(doc, "\005SummaryInformation", parse_propertyset_stream))
		return NULL;

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


void close_doc(struct doc_file *doc) {
	fclose(doc->fp);
}


int parse_fat(struct doc_file *doc) {
	unsigned int header_difat_size = 109; //fixed, independent from major version
	uint32_t *difat = doc->header.difat;

	doc->fat_entries = malloc(0x01);
	for (int i=0; i < header_difat_size; i++) {
		if (difat[i] != FREESECT) {
			if (parse_fat_sector(doc, difat[i]))
				return -1;
		}
	}

	//TODO: support also external difat sectors
	return 0;
}


int parse_fat_sector(struct doc_file *doc, uint32_t i_sector) {
	unsigned int n_new_entries = doc->sector_size / sizeof(uint32_t);
	
	doc->fat_entries = realloc(doc->fat_entries, (doc->n_fat_entries +n_new_entries) * doc->sector_size);
	fseek(doc->fp, (i_sector + 1) * doc->sector_size, SEEK_SET);
	size_t n_read = fread(&doc->fat_entries[doc->n_fat_entries], doc->sector_size, 1, doc->fp);
	if (n_read != 1) {
		sprintf(parser_err_msg, "Could not read FAT sector #%"PRIu32"; n_read=%lu", i_sector, n_read);
		return -1;
	}

	doc->n_fat_entries += n_new_entries;
	return 0;
}



int parse_stream(struct doc_file *doc, char *stream_name, parse_cbk parse_stream_cbk) {
	//TODO: support nested dirs
	printf(" DDD parsing stream \n");
	for (uint32_t i=0; i < doc->n_dir_entries; i++) {
		char ascii_name[100];
		utf16_to_ascii(ascii_name, doc->dir_entries[i].name, doc->dir_entries[i].name_len);

		// int len = strlen(stream_name);
		// printf(" DDD [%s] <--> [%s] \n", ascii_name, stream_name);
		// printf(" DDD [0x%x...0x%x] <--> [0x%x...0x%x] \n", ascii_name[0], ascii_name[len], 
		// 	stream_name[0], stream_name[len]);

		if (!strcmp(ascii_name, stream_name)) {
			//printf(" DDD stream found starting on sector #%"PRIu32" \n", i);
			return parse_chain(doc, doc->dir_entries[i].start_sector, parse_stream_cbk);
		}
	}

	sprintf(parser_err_msg, "Could not find stream: %s", stream_name);
	return -1;
}


int parse_chain(struct doc_file *doc, unsigned int start_sector, parse_cbk parse_chain_cbk) {
	unsigned int chain_size = 0;
	void *chain_buffer = malloc(doc->sector_size);

	unsigned int curr_sector = start_sector;

	while (true) {
		chain_buffer = realloc(chain_buffer, (++chain_size)*doc->sector_size);
		fseek(doc->fp, (curr_sector + 1) * doc->sector_size, SEEK_SET);
		size_t n_read = fread(&chain_buffer[(chain_size-1)*doc->sector_size], doc->sector_size, 1, doc->fp);
		if (n_read != 1) {
			sprintf(parser_err_msg, "Could not read sector #%"PRIu32"; n_read=%lu", curr_sector, n_read);
			return -1;
		}

		if (doc->fat_entries[curr_sector] == (uint32_t)0xFFFFFFFE) {
			//ENDCHAIN
			return parse_chain_cbk(doc, chain_buffer, chain_size*doc->sector_size);

		} else {
			curr_sector = doc->fat_entries[curr_sector];
		}
	}
}

int parse_dir(struct doc_file *doc, char *buffer, unsigned int buffer_size) {
	doc->n_dir_entries = buffer_size / sizeof(struct dir_entry);
	doc->dir_entries = (struct dir_entry *)buffer;

	return 0;
}


int parse_propertyset_stream(struct doc_file *doc, char *buffer, unsigned int buffer_size) {
	printf(" DDD parsing propertyset stream \n");
	struct property_set_stream *ps_stream = (struct property_set_stream *)buffer;
    printf(" DDD   byte_order %"PRIu16" \n", ps_stream->byte_order);
    printf(" DDD   version %"PRIu16" \n", ps_stream->version);
    printf(" DDD   sys_id %"PRIu32" \n", ps_stream->sys_id);
    //printf(" DDD   clsid %"PRIu16" \n", ps_stream->clsid);

    printf(" DDD num_property_sets: %"PRIu32" \n", ps_stream->num_property_sets);

	for (uint32_t i_ps=0; i_ps < ps_stream->num_property_sets; i_ps++) {
		struct property_set_header *ps_header = (struct property_set_header *)(buffer + sizeof(struct property_set_stream));
		//printf(" DDD header: %"PRIxx" \n", ps_header->fmtid);
			
		struct property_set *ps = (struct property_set *)(buffer + ps_header->offset);
		printf(" DDD num_props: %"PRIu32" \n", ps->num_props);

		for (uint32_t i_p=0; i_p < ps->num_props; i_p++) {
			//struct propid_offset *pid_offset = (struct propid_offset *)(ps + sizeof(struct property_set) 
			struct propid_offset *pid_offset = (struct propid_offset *)((void *)ps + sizeof(struct property_set) 
				+ i_p * sizeof(struct propid_offset));
			if (pid_offset->propid) {
				struct property *p = (struct property *)((void *)ps + pid_offset->offset);
				// printf("   DDD %"PRIu32" pid: %"PRIu32" offset: %"PRIu32" \n", 
				// 	i_p, pid_offset->propid, pid_offset->offset);
				parse_property(doc, pid_offset->propid, p);
			}
		}
	}

	return 0;
}


void parse_property(struct doc_file *doc, uint32_t propid, struct property *p) {
	//printf(" DDD pid: %"PRIu32" type: %"PRIu16" \n", propid, p->type);
	char prop_name[100];
	propid_to_str(prop_name, propid);
	void *p_val = ((void *)p + sizeof(struct property));
	char str_val[1000];


	uint16_t str_encoding = -1;
	if (propid == PIDSI_CodePage) {
		str_encoding = *((uint16_t *)p_val);
	}


	switch (p->type) {
		case VT_I2:
			//uint16_t *p_val = ((void *)p + sizeof(struct property));
			//printf("   DDD %s %"PRIu16" \n", prop_name, p, *p_val);
			printf("   DDD %s = %"PRIu16" \n", prop_name, *((uint16_t *)p_val));
			break;
		case VT_I4:
			//uint32_t *p_val = ((void *)p + sizeof(struct property));
			//printf("   DDD %s %"PRIu32" \n", prop_name, p, *p_val);
			printf("   DDD %s = %"PRIu32" \n", prop_name, (*((uint32_t *)p + sizeof(struct property))));
			break;
		case VT_LPSTR:
			//skip size field
			decode_str(str_val, p_val + 4, str_encoding);
			printf("   DDD %s = %s \n", prop_name, str_val);
			break;
		case VT_FILETIME:
			printf("   DDD %s = %s", prop_name, filetime_to_str(*(FILETIME *)p_val));
			break;

		default:
			printf("   DDD Unknown property type: %"PRIu16" \n", p->type);
			//ignore;
	}
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
		utf16_to_ascii(out_name_str, d.name, d.name_len);
		if (d.obj_type != 0x00)  {
			printf("  %s \n", out_name_str);
			printf("    obj_type %u \n", d.obj_type);
			if (d.left_id != NOSTREAM)
				printf("    left %"PRIu32" \n", d.left_id);
			if (d.right_id != NOSTREAM)
				printf("    right %"PRIu32" \n", d.right_id);
			if (d.child_id != NOSTREAM)
				printf("    child %"PRIu32" \n", d.child_id);

			printf("    creat_time %s", filetime_to_str(d.creat_time));
			printf("    mod_time %s", filetime_to_str(d.mod_time));
			printf("    start sector %"PRIu32" \n", d.start_sector);
			printf("    stream size %llu \n", d.stream_size);
		}
	}
}


//iconv implementation gives error 22
void utf16_to_ascii(char *str_to, char *str_from, int len) {
	for (int i=0; i < len/2; i++) {
		str_to[i] = str_from[i*2];
	}

	str_to[len/2] = 0x00;
}

void decode_str(char *str_to, char *str_from, uint16_t codepage) {
	//TODO: decode CP_WINUNICODE
	strcpy(str_to, str_from);
}


void propid_to_str(char *str_to, uint32_t propid) {
	switch (propid) {
	case PIDSI_CodePage:
		strcpy(str_to, "CodePage");
		return;
	case PIDSI_TITLE:
		strcpy(str_to, "Title");
		return;
	case PIDSI_SUBJECT:
		strcpy(str_to, "Subject");
		return;
	case PIDSI_AUTHOR:
		strcpy(str_to, "Author");
		return;
	case PIDSI_KEYWORDS:
		strcpy(str_to, "Keywords");
		return;
	case PIDSI_COMMENTS:
		strcpy(str_to, "Comments");
		return;
	case PIDSI_TEMPLATE:
		strcpy(str_to, "Template");
		return;
	case PIDSI_LASTAUTHOR:
		strcpy(str_to, "LastAuthor");
		return;
	case PIDSI_REVNUMBER:
		strcpy(str_to, "RevNumber");
		return;
	case PIDSI_EDITTIME:
		strcpy(str_to, "EditTime");
		return;
	case PIDSI_LASTPRINTED:
		strcpy(str_to, "LastPrinted");
		return;
	case PIDSI_CREATE_DTM:
		strcpy(str_to, "CreateDTM");
		return;
	case PIDSI_LASTSAVE_DTM:
		strcpy(str_to, "LastSaveDTM");
		return;
	case PIDSI_PAGECOUNT:
		strcpy(str_to, "PageCount");
		return;
	case PIDSI_WORDCOUNT:
		strcpy(str_to, "WordCount");
		return;
	case PIDSI_CHARCOUNT:
		strcpy(str_to, "CharCount");
		return;
	case PIDSI_THUMBNAIL:
		strcpy(str_to, "Thumbnail");
		return;
	case PIDSI_APPNAME:
		strcpy(str_to, "AppName");
		return;
	case PIDSI_DOC_SECURITY:
		strcpy(str_to, "DocSecurity");
		return;

	default:
		strcpy(str_to, "<UNKNOWN>");
		return;

	}
}


#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL

time_t filetime_to_unix(FILETIME filetime) {
	long long ll_filetime = filetime.low_datetime + (((long long)filetime.high_datetime) << 32);
	return (time_t)(ll_filetime / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
}

char *filetime_to_str(FILETIME filetime) {
	time_t ts = filetime_to_unix(filetime);
	return ctime(&ts);
}
