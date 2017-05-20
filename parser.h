#ifndef _PARSER_H
#define _PARSER_H


#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>


//----------------------------------------------------------------------
// Constants

//Sector numbers
#define MAXREGSECT 0xFFFFFFFA //Maximum regular sector number.
//Not applicable    0xFFFFFFFB //Reserved for future use.
#define DIFSECT    0xFFFFFFFC //Specifies a DIFAT sector in the FAT.
#define FATSECT    0xFFFFFFFD //Specifies a FAT sector in the FAT.
#define ENDOFCHAIN 0xFFFFFFFE //End of a linked chain of sectors.
#define FREESECT   0xFFFFFFFF //Specifies an unallocated sector in the FAT, Mini FAT, or DIFAT.
 

//Stream ids
#define MAXREGSID 0xFFFFFFFA // Maximum regular stream ID.
#define NOSTREAM  0xFFFFFFFF // Terminator or empty pointer.


//Property set stream format ids
#define FMTID_SummaryInformation    0xF29F85E04FF91068AB9108002B27B3D9
#define FMTID_DocSummaryInformation 0xD5CDD5022E9C101B939708002B2CF9AE
// #define FMTID_UserDefinedProperties 0xD5CDD5052E9C101B939708002B2CF9AE
// #define FMTID_GlobalInfo            0x56616F00C15411CE855300AA00A1F95B
// #define FMTID_ImageContents         0x56616400C15411CE855300AA00A1F95B
// #define FMTID_ImageInfo             0x56616500C15411CE855300AA00A1F95B



//Property ids
#define PIDSI_CodePage      0x00000001
#define PIDSI_TITLE         0x00000002
#define PIDSI_SUBJECT       0x00000003
#define PIDSI_AUTHOR        0x00000004
#define PIDSI_KEYWORDS      0x00000005
#define PIDSI_COMMENTS      0x00000006
#define PIDSI_TEMPLATE      0x00000007
#define PIDSI_LASTAUTHOR    0x00000008
#define PIDSI_REVNUMBER     0x00000009
#define PIDSI_EDITTIME      0x0000000A
#define PIDSI_LASTPRINTED   0x0000000B
#define PIDSI_CREATE_DTM    0x0000000C
#define PIDSI_LASTSAVE_DTM  0x0000000D
#define PIDSI_PAGECOUNT     0x0000000E
#define PIDSI_WORDCOUNT     0x0000000F
#define PIDSI_CHARCOUNT     0x00000010
#define PIDSI_THUMBNAIL     0x00000011
#define PIDSI_APPNAME       0x00000012
#define PIDSI_DOC_SECURITY  0x00000013

//Property value types
#define VT_I2        0x0002
#define VT_I4        0x0003
#define VT_LPSTR     0x001E
#define VT_FILETIME  0x0040
//#define VT_CF        0x0047


//----------------------------------------------------------------------
// Global variables
char parser_err_msg[500];

//----------------------------------------------------------------------
// Data structures

// typedef long long FILETIME;
typedef struct _FILETIME {  //"number of 100-nanosecond intervals that have elapsed since January 1, 1601"
    uint32_t low_datetime;
    uint32_t high_datetime;
} FILETIME;

// typedef unsigned char FILETIME[8];


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



struct property_set_stream {
    uint16_t byte_order;
    uint16_t version;
    uint32_t sys_id;
    char clsid[16];
    uint32_t num_property_sets;
    //struct property_set_header[num_property_sets];
    //struct property_set[num_property_sets];
};

struct property_set_header {
    char fmtid[16];
    uint32_t offset;
};

struct property_set {
    uint32_t size;
    uint32_t num_props;
    //struct propid_offset[num_props];
    //struct property[num_props];
};


struct propid_offset {
    uint32_t propid;
    uint32_t offset;
};

struct property {
    uint16_t type;
    uint16_t padding;
    //unsigned char value[?]; //size depends on type
};



struct doc_file {
    struct header header;
    uint32_t sector_size;
    FILE *fp;

    uint32_t *fat_entries;
    unsigned int n_fat_entries;

    struct dir_entry *dir_entries;
    unsigned int n_dir_entries;
};



//--------------------------------------------------------------
// Function declarations

struct doc_file *parse_doc(char *filename);
bool validate_doc(struct doc_file *doc);
void close_doc(struct doc_file *doc);

void print_header(struct doc_file *doc);
void print_fat(struct doc_file *doc);
void print_dir(struct doc_file *doc);


#endif  //PARSER_H

