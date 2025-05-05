#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hp_file.h"
#include "record.h"

#define CALL_BF(call)       \
{                           \
  BF_ErrorCode code = call; \
  if (code != BF_OK) {         \
    BF_PrintError(code);    \
    return HP_ERROR;        \
  }                         \
}

/*Create the hp file*/
int HP_CreateFile(char *fileName) {
    CALL_BF(BF_CreateFile(fileName));
    int fileDesc;
    CALL_BF(BF_OpenFile(fileName, &fileDesc));
    BF_Block *block;
    BF_Block_Init(&block);
    CALL_BF(BF_AllocateBlock(fileDesc, block));
    void *data = BF_Block_GetData(block);//The first block of the file will have the meta data
    HP_info hp_info; //The meta data will be on a struct 
    hp_info.file_desc = fileDesc;//We store the desc
    strncpy(hp_info.fileName, fileName, sizeof(hp_info.fileName) - 1);  
    hp_info.fileName[sizeof(hp_info.fileName) - 1] = '\0'; //We store the file name
    hp_info.maxrec = (BF_BLOCK_SIZE - sizeof(HP_block_info)) / sizeof(Record);//Store the amount of records that can be stored
    if (sizeof(hp_info) > BF_BLOCK_SIZE) { //We check if that struct fits in the block
        printf("Error on HP_info structure size\n");
        BF_UnpinBlock(block);
        BF_CloseFile(fileDesc);
        BF_Block_Destroy(&block);
        return -1;
    }
    memcpy(data, &hp_info, sizeof(HP_info)); //Add the hp_info struct
    BF_Block_SetDirty(block);
    CALL_BF(BF_UnpinBlock(block)) 
    CALL_BF(BF_CloseFile(fileDesc));
    BF_Block_Destroy(&block);
    return 0;
}

/*Opening the HP file*/
HP_info* HP_OpenFile(char *fileName, int *file_desc){
    if(BF_OpenFile(fileName, file_desc) != BF_OK) {
        printf("Error on opening the hp file\n");
        return NULL;
    }
    BF_Block* block;
    BF_Block_Init(&block);
    if(BF_GetBlock(*file_desc, 0, block) != BF_OK) {
        printf("Error on getting the first block\n");
        return NULL;
    }//We get the first block which has a struct
    void* data = BF_Block_GetData(block);
    HP_info* hpinfo =  data;
    //We don't unpin the first block
    BF_Block_Destroy(&block);
    return hpinfo;//We return the struct which has the metadata 
}

/*Close the HP file*/
int HP_CloseFile(int file_desc,HP_info* header_info){
    BF_Block *block;
    BF_Block_Init(&block);
    int bcount;
    CALL_BF(BF_GetBlockCounter(file_desc, &bcount));
    for(int i = 0; i < bcount; i++) {
        CALL_BF(BF_GetBlock(file_desc, i, block));
        CALL_BF(BF_UnpinBlock(block));
    }//Unpin every block we have in the file
    CALL_BF(BF_CloseFile(file_desc));
    BF_Block_Destroy(&block);
    return 0; 
}

/*Insert a record in the HP file*/
int HP_InsertEntry(int file_desc, HP_info* header_info, Record record) {
    void* data;
    int bcount;//How many blocks we have
    int i; 
    static int key = 1;//This is for a special case
    BF_Block* block;
    BF_Block_Init(&block);
    CALL_BF(BF_GetBlockCounter(file_desc, &bcount));
    if (bcount == 1 && key == 1) {//If only the metadata block exists we create the first data block 
        HP_block_info info; 
        key = 0; 
        CALL_BF(BF_AllocateBlock(file_desc, block));//We allocate a new block
        for (i = 0; i < header_info->maxrec; i++) { //Initialize the hasrec array to false 
            info.hasrec[i] = false; //This means that no records have been registered
        }
        data = BF_Block_GetData(block); //Get the block's data pointer
        Record* rec = data; 
        rec[0] = record; //Insert the new record
        info.hasrec[0] = true; //Mark that the first record is registered
        memcpy(data + BF_BLOCK_SIZE - sizeof(HP_block_info), &info, sizeof(HP_block_info));
        CALL_BF(BF_GetBlockCounter(file_desc, &bcount));
        BF_Block_SetDirty(block); //Mark block as dirty
        CALL_BF(BF_UnpinBlock(block));//Unpin it
        return bcount;
    }
    //The other case is to retrieve the last block
    CALL_BF(BF_GetBlock(file_desc, bcount - 1, block)); //Minus 1 because the 0 block has the metadata 
    data = BF_Block_GetData(block); //Get the block's data
    HP_block_info* info = data + BF_BLOCK_SIZE - sizeof(HP_block_info); //Access at the end of the block
    for (i = 0; i < header_info->maxrec; i++) { //See if we can add the record to the current block
        if (!info->hasrec[i]) {
            break;  //Found an empty slot
        }
    }
    if (i == header_info->maxrec) { //Block is full we need a new block
        CALL_BF(BF_UnpinBlock(block));//Unpin the previous block
        CALL_BF(BF_AllocateBlock(file_desc, block));
        data = BF_Block_GetData(block); 
        Record* rec = data;
        rec[0] = record;  //Add the new record to the new block
        HP_block_info new_info;
        new_info.hasrec[0] = true; //This means that we put a record
        for (int j = 1; j < header_info->maxrec; j++) {
            new_info.hasrec[j] = false; 
        }  //Initialize a new HP_block_info
        memcpy(data + BF_BLOCK_SIZE - sizeof(HP_block_info), &new_info, sizeof(HP_block_info)); //Copy the new HP_block_info to the end of the block
    } else {  //There is space in the current block
        Record* rec = data;
        rec[i] = record;  //Add the record to the first available slot
        info->hasrec[i] = true; //Mark as used
    }
    BF_Block_SetDirty(block); 
    CALL_BF(BF_UnpinBlock(block));
    CALL_BF(BF_GetBlockCounter(file_desc, &bcount));
    return bcount;//This is the block which the record has been added
}

/*Find a record with a specific value*/
int HP_GetAllEntries(int file_desc, HP_info* header_info, int id) {  
    void* data;
    BF_Block* block;
    BF_Block_Init(&block);  
    bool done = false;//If we have at least one place where there is no record we are done
    int count = 0;//How many blocks we read
    int bcount;//All the blocks we have
    CALL_BF(BF_GetBlockCounter(file_desc, &bcount));
    for (int i = 1; i < bcount; i++) {//For every block we check the records and we start at 1(0 is the meta data)
        CALL_BF(BF_GetBlock(file_desc, i, block));
        count++;
        data = BF_Block_GetData(block);
        HP_block_info* info = data + BF_BLOCK_SIZE - sizeof(HP_block_info);//Block metadata
        Record* rec = data;
        for (int j = 0; j < header_info->maxrec; j++) {//Check if we have a record
            if (info->hasrec[j] == true) {
                if (rec[j].id == id) {
                    printRecord(rec[j]);
                }
            }
            else {//It means that we have read all records
                done = true;
                break;
            }
        }
        CALL_BF(BF_UnpinBlock(block));
        if (done) {
            break;
        }
    }
    BF_Block_Destroy(&block);
    return count; //Return the amount of blocks we read
}

