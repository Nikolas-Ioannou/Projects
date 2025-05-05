#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "bp_file.h"
#include "record.h"
#include "bp_indexnode.h"


// Initializing the index block
void Init_index_block(void* block,int file_desc) {
    BPLUS_INFO info;
    info.type = 1;
    BPLUS_INDEX_NODE index;
    index.size = 0;
    for (int i = 0; i < 3; i++) {
        index.next[i] = -1;       // Initialize next pointers to -1 (no next block yet)
    }
    int bcount;
    BF_GetBlockCounter(file_desc,&bcount);
    index.id = bcount - 1;
    memcpy(block + BF_BLOCK_SIZE - sizeof(BPLUS_INFO), &info, sizeof(BPLUS_INFO)); // At the end of the block store store the struct BPLUS_INFO 
    memcpy(block , &index, sizeof(BPLUS_INDEX_NODE));
}

// Insert an index block
int Insertindex(void* block,int id) {
    BPLUS_INDEX_NODE* temp = block;
    BPLUS_INFO* check = block + BF_BLOCK_SIZE - sizeof(BPLUS_INFO);
    if(check->type == 0) {
        printf("Wrong block\n");
        return -2;
    }
    if(temp->size == 0) {
        temp->items[0] = id;
        temp->size = 1;
        return temp->id;
    }
    if(temp->size == 2) { // If the index node is full
        return -1;
    }
    if(temp->items[0] < id) { // Add the index
        temp->items[1] = id;
    }
    else {
        temp->items[1] = temp->items[0];
        temp->items[0] = id;
    }
    temp->size = 2;
    return temp->id;
}