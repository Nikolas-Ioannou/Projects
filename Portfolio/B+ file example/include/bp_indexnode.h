#ifndef BP_INDEX_NODE_H
#define BP_INDEX_NODE_H
#include <record.h>
#include <bf.h>
#include <bp_file.h>



typedef struct
{
    int id; // Block id
    int items[2]; // In here there are the indexes
    int size; // The amount of indexes there are (max 2) 
    int next[3]; // The id of the next block
} BPLUS_INDEX_NODE;

void Init_index_block(void* block,int file_desc);
int Insertindex(void* block,int id);

#endif