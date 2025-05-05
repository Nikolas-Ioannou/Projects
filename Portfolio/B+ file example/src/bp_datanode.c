#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "bp_file.h"
#include "record.h"
#include "bp_datanode.h"

// Inserting a record in a data block
int Insertdata(void* pointer,Record record) {
   BPLUS_INFO* info = pointer + BF_BLOCK_SIZE - sizeof(BPLUS_INFO);
    if(info->type) { // Check the type of the block
        printf( "Wrong block\n");
        return -2;
    }
    Record* rec = pointer;
    BPLUS_DATA_NODE* temp = pointer +  BF_BLOCK_SIZE - sizeof(BPLUS_INFO) - sizeof(BPLUS_DATA_NODE);
    if(temp->size == 0) {
        rec[0] = record;
        temp->size = 1;
        return temp->id;
    }
    for(int i = 0;  i < temp->size; i++) {
        if(rec[i].id == record.id) {
            printf("The record is already in here\n");
            return -2;
        }
        else if(i == 1) { // The data block is full
            return -1;
        }
    }
    if(rec[0].id > record.id) { // Inserting the record
        rec[1] = rec[0];
        rec[0] = record;
    }
    else {
        rec[1] = record;
    }
    temp->size = 2;
    return temp->id;
}

// Initializing a data block
void Init_data_block(void* block,int file_desc) {
    BPLUS_DATA_NODE data;
    BPLUS_INFO info;
    info.type = 0;
    data.size = 0;
    int bcount;
    BF_GetBlockCounter(file_desc,&bcount);
    data.id = bcount - 1;
    memcpy(block + BF_BLOCK_SIZE - sizeof(BPLUS_INFO), &info, sizeof(BPLUS_INFO)); // Add metadata at the end
    memcpy(block + BF_BLOCK_SIZE - sizeof(BPLUS_INFO) - sizeof(BPLUS_DATA_NODE), &data, sizeof(BPLUS_DATA_NODE)); // Add the struct before the struct BPLUS_INFO 
}
