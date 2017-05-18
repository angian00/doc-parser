#ifndef _PARSER_H
#define _PARSER_H


#include <inttypes.h>
#include <stdbool.h>


//--------------------------------------------------------------


//REGSECT 0x00000000 - 0xFFFFFFF9 //Regular sector number.
#define MAXREGSECT 0xFFFFFFFA //Maximum regular sector number.
//Not applicable    0xFFFFFFFB //Reserved for future use.
#define DIFSECT    0xFFFFFFFC //Specifies a DIFAT sector in the FAT.
#define FATSECT    0xFFFFFFFD //Specifies a FAT sector in the FAT.
#define ENDOFCHAIN 0xFFFFFFFE //End of a linked chain of sectors.
#define FREESECT   0xFFFFFFFF //Specifies an unallocated sector in the FAT, Mini FAT, or DIFAT.
 


#define MAXREGSID 0xFFFFFFFA // Maximum regular stream ID.
#define NOSTREAM  0xFFFFFFFF // Terminator or empty pointer.

char parser_err_msg[500];



//typedef char FILETIME[8];
typedef struct _FILETIME {  //"number of 100-nanosecond intervals that have elapsed since January 1, 1601"
    uint32_t low_datetime;
    uint32_t high_datetime;
} FILETIME;


struct header {
    char signature[8];
    char clsid[16];
    uint16_t minor_version;
    uint16_t major_version;
    uint16_t byte_order;
    uint16_t sector_shift;
    uint16_t minisector_shift;
    char     _reserved[6];
    uint32_t num_dir_sectors;
    uint32_t num_fat_sectors;
    uint32_t dir_sector_start;
    uint32_t trans_sign_number;
    uint32_t ministream_cutoff_size;
    uint32_t minifat_sector_start;
    uint32_t num_minifat_sectors;
    uint32_t difat_sector_start;
    uint32_t num_difat_sectors;
    uint32_t difat[109];
};


struct dir_entry {
    char name[64]; //Unicode string for the storage or stream name encoded in UTF-16
    uint16_t name_len; //in bytes, include the terminating null character in the count
    unsigned char obj_type; // 0x00, 0x01, 0x02, or 0x05
    unsigned char r_b;
    uint32_t left_id;
    uint32_t right_id;
    uint32_t child_id;
    char clsid[16];
    char state_bits[4];
    FILETIME creat_time;
    FILETIME mod_time;
    uint32_t start_sector;
    unsigned long long stream_size;
};




struct doc_file {
    struct header header;
    uint32_t sector_size;

    uint32_t *fat_entries;
    unsigned int n_fat_entries;

    struct dir_entry *dir_entries;
    unsigned int n_dir_entries;
};



//--------------------------------------------------------------

struct doc_file *parse_doc(char *filename);
bool validate_doc(struct doc_file *doc);

void print_header(struct doc_file *doc);
void print_fat(struct doc_file *doc);
void print_dir(struct doc_file *doc);


#endif  //PARSER_H

