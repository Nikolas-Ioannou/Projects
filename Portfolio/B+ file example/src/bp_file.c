#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "bp_file.h"
#include "record.h"
#include <bp_datanode.h>
#include <stdbool.h>

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK)        \
    {                         \
      BF_PrintError(code);    \
      return bplus_ERROR;     \
    }                         \
  }

// Creating the B plus file
int BP_CreateFile(char *fileName)
{
    CALL_BF(BF_CreateFile(fileName));
    int fileDesc;
    CALL_BF(BF_OpenFile(fileName, &fileDesc));
    BF_Block *block;
    BF_Block_Init(&block);
    CALL_BF(BF_AllocateBlock(fileDesc, block));
    void *pointer = BF_Block_GetData(block);// The first block of the file will have the metadata
    BPLUS_INFO bp_info; //The metadata will be on a struct 
    bp_info.file_desc = fileDesc;// Store the desc
    strncpy(bp_info.fileName, fileName, sizeof(bp_info.fileName) - 1);  
    bp_info.root = -1;
    bp_info.fileName[sizeof(bp_info.fileName) - 1] = '\0'; // Store the file name
    if (sizeof(bp_info) > BF_BLOCK_SIZE) { // Check if that struct fits in the block
        printf("Error on BPLUS_INFO structure size\n");
        BF_CloseFile(fileDesc);
        BF_Block_Destroy(&block);
        return -1;
    }
    memcpy(pointer, &bp_info, sizeof(BPLUS_INFO)); // Add the bp_info struct at the start of the block
    BF_Block_SetDirty(block);
    CALL_BF(BF_UnpinBlock(block)) 
    CALL_BF(BF_CloseFile(fileDesc));
    BF_Block_Destroy(&block);
    return 0;
}

// Opening the B plus tree file 
BPLUS_INFO* BP_OpenFile(char *fileName, int *file_desc)
{
  if(BF_OpenFile(fileName, file_desc) != BF_OK) {
        printf("Error on opening the BP file\n");
        return NULL;
  }
  BF_Block* block;
  BF_Block_Init(&block);
  if(BF_GetBlock(*file_desc, 0, block) != BF_OK) {
      printf("Error on getting the first block\n");
      return NULL;
  }// Get the first block which have the matadata on a struct
  void* data = BF_Block_GetData(block);
  BPLUS_INFO* bp_info = data;
  bp_info->block = block;
  BF_Block_SetDirty(block);
  // Don't unpin the first block which means the file is open
  return bp_info; // Return the struct which has the metadata 
}

// Closing the file
int BP_CloseFile(int file_desc,BPLUS_INFO* info)
{  
  BF_Block *block = info->block; // Get the metadata block which is already in the memory
  CALL_BF(BF_UnpinBlock(block)); //Unpin it
  BF_Block_Destroy(&block);
  CALL_BF(BF_CloseFile(file_desc));
  return 0;
}

// Inserting a record
int BP_InsertEntry(int file_desc,BPLUS_INFO *bplus_info, Record record)
{ 
    if (bplus_info->root == -1) { // If only the metadata block exists, create the root and store it in the metadata
        BF_Block* index;
        BF_Block* data;
        void* indexpoint;
        void* datapoint;
        int value;
        BF_Block_Init(&index); // Creating the index block
        CALL_BF(BF_AllocateBlock(file_desc, index));
        indexpoint = BF_Block_GetData(index);
        Init_index_block(indexpoint,file_desc);
        value = Insertindex(indexpoint,record.id);
        bplus_info->root = value;
        BF_Block_Init(&data); // Creating the data block
        CALL_BF(BF_AllocateBlock(file_desc, data));
        datapoint = BF_Block_GetData(data); 
        Init_data_block(datapoint,file_desc);
        value = Insertdata(datapoint,record); // The functiin returns the ID of the block
        BPLUS_INDEX_NODE* temp = indexpoint; // Connect the 2 blocks
        temp->next[1] = value;
        BF_Block_SetDirty(index);
        BF_UnpinBlock(index);
        BF_Block_Destroy(&index);
        BF_Block_SetDirty(data);
        BF_UnpinBlock(data);
        BF_Block_Destroy(&data);
        return value; // Return the ID of the data block where the element was stored
    }
    int next; // The ID of the next blocks
    int i; // This is for when the index node has as next one NULL(-1)
    int currentid = bplus_info->root; // the first block is an index node always. At the end it will have the id of the data block
    int idblock2 = -1; // It will have the id of the index node  
    int idblock3 = -1; //  It will have the id of the previous index node 
    while (1) { // While it is an index block
        void* pointer;// This variable will help with pointing blocks
        BF_Block* block;// at the end of the loop it will contain the data block
        BF_Block_Init(&block);
        CALL_BF(BF_GetBlock(file_desc, currentid, block)); // Get the block from the id which was given 
        pointer = BF_Block_GetData(block); 
        BPLUS_INDEX_NODE* temp = pointer;// Get the data of the index node (we know it is and index)
        // Determine which child pointer to follow
        if(temp->size == 0) {
            printf("Error on index block: No size\n");
            return -1;
        }
        if (temp->items[0] > record.id) { //picking the next node based on the indexes
            next = temp->next[0];
            i = 0; 
        } else if (temp->size == 1) {
            next = temp->next[1];
            i = 1;
        }
        else if(temp->items[1] > record.id) {
            next = temp->next[1];
            i = 1;
        }
        else {
            next = temp->next[2];
            i = 2;
        }
        // If no next block exists, allocate a new data block
        if (next == -1) {
            BF_Block* newblock; // New data block
            BF_Block_Init(&newblock);
            CALL_BF(BF_AllocateBlock(file_desc, newblock));
            void* newpointer = BF_Block_GetData(newblock);
            Init_data_block(newpointer,file_desc);
            int id = Insertdata(newpointer,record); // insert the record to the data block 
            temp->next[i] = id; // Based on the i change the next block on the index node
            // Unpin all remaining blocks and return
            BF_Block_SetDirty(block);
            BF_UnpinBlock(block);
            BF_Block_Destroy(&block);
            BF_Block_SetDirty(newblock);
            BF_UnpinBlock(newblock);
            BF_Block_Destroy(&newblock);
            return id;
        }
        idblock3 = idblock2;
        idblock2 = currentid;
        currentid = next; // update the IDS
        //Checking if the next block is data
        BF_Block* nextblock;
        BF_Block_Init(&nextblock);
        CALL_BF(BF_GetBlock(file_desc, next, nextblock));
        pointer = BF_Block_GetData(nextblock);
        BPLUS_INFO* temp2 = pointer + BF_BLOCK_SIZE - sizeof(BPLUS_INFO); // Update metadata pointer
        if(temp2->type == 0) { // The data block was found
            BF_UnpinBlock(nextblock);
            BF_Block_Destroy(&nextblock);
            BF_UnpinBlock(block);
            BF_Block_Destroy(&block);
            break;
        }
        BF_UnpinBlock(nextblock);
        BF_Block_Destroy(&nextblock);
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
    }
    BF_Block* datablock; // Get the datablock
    BF_Block_Init(&datablock);
    CALL_BF(BF_GetBlock(file_desc, currentid, datablock));
    void* pointdata = BF_Block_GetData(datablock);
    BPLUS_DATA_NODE* datainfo = pointdata + BF_BLOCK_SIZE - sizeof(BPLUS_INFO) - sizeof(BPLUS_DATA_NODE);// We get the data structure from the data block 
    int got = Insertdata(pointdata,record); // Insert at the data block
    if(got == -2) { // Returned an error
        BF_UnpinBlock(datablock);
        BF_Block_Destroy(&datablock);
        return -1;
    }
    else if(got != -1) { // The record was added 
        BF_Block_SetDirty(datablock);
        BF_UnpinBlock(datablock);
        BF_Block_Destroy(&datablock);
        return got; // It has the id of the data block
    }
    // If the data block is full we need to see find the case
    // First ocassion is on the size of the index blocks 
    BF_Block* indexblock; // Get the index block
    BF_Block_Init(&indexblock);
    CALL_BF(BF_GetBlock(file_desc, idblock2, indexblock));
    void* pointerindex = BF_Block_GetData(indexblock); 
    BPLUS_INDEX_NODE* index = pointerindex; // Get from the index block the struct 
    if (index->size == 1) { // If the index node has only one item, find the second biggest from the items, the data node, and the record and add it
        int sizerec = 0;
        Record allrec[5]; // Max 5 records from 2 blocks + new record
        // From the index block get all the records which points
        for (int i = 0; i < 3; i++) {
            if (index->next[i] == -1) {
                continue;
            }
            BF_Block* tempblock;
            BF_Block_Init(&tempblock);
            CALL_BF(BF_GetBlock(file_desc, index->next[i], tempblock));
            void* pointerblock = BF_Block_GetData(tempblock);
            BPLUS_DATA_NODE* infotemp = pointerblock + BF_BLOCK_SIZE - sizeof(BPLUS_INFO)-sizeof(BPLUS_DATA_NODE);
            Record* rectemp = pointerblock;
            for (int j = 0; j < infotemp->size; j++) {
                allrec[sizerec] = rectemp[j];  
                sizerec++;
            }
            infotemp->size = 0; // Set the block to 0 size because the records were taken (they will be added later)
            BF_Block_SetDirty(tempblock); 
            BF_UnpinBlock(tempblock); 
            BF_Block_Destroy(&tempblock);
        }
        allrec[sizerec] = record;  // Add the new record 
        sizerec++;
        Record* rec = pointdata;
        int newid;
        if (rec[0].id > record.id) { // Get the second biggest from the data block and it will be placed at the index block
            newid = rec[0].id;
        } 
        else {
            newid = record.id;
        }
        int got = Insertindex(pointerindex,newid); // Add it to the index block
        BF_Block_SetDirty(indexblock);
        BF_UnpinBlock(indexblock);
        BF_Block_Destroy(&indexblock);
        BF_UnpinBlock(datablock); 
        BF_Block_Destroy(&datablock);
        // Insert the records into the tree
        for (int i = 0; i < sizerec; i++) {
            int j = BP_InsertEntry(file_desc, bplus_info, allrec[i]); // Kee
            if (allrec[i].id == record.id) { // The last one is the new record so there is no problem
               return j;
            }
        }
    }
    //If the index block is full check the previous index block
    //The previous index block is full
    if (idblock3 == -1) { // No previous previous index block, so create one
        int sizerec = 0;
        Record allrec[7]; // Max 7 records from 3 blocks + new record
        // Get the the record the index block points
        for (int i = 0; i < 3; i++) {
            if (index->next[i] == -1) {
                continue;
            }
            BF_Block* tempblock;
            BF_Block_Init(&tempblock);
            CALL_BF(BF_GetBlock(file_desc, index->next[i], tempblock));
            void* pointerblock = BF_Block_GetData(tempblock);
            BPLUS_DATA_NODE* infotemp = pointerblock + BF_BLOCK_SIZE - sizeof(BPLUS_INFO)-sizeof(BPLUS_DATA_NODE);
            Record* rectemp = pointerblock;
            for (int j = 0; j < infotemp->size; j++) {
                allrec[sizerec] = rectemp[j];  
                sizerec++;
            }
            infotemp->size = 0;
            BF_Block_SetDirty(tempblock);
            BF_UnpinBlock(tempblock); 
            BF_Block_Destroy(&tempblock);
        }
        allrec[sizerec] = record; 
        sizerec++;
        int newid; // This will be stored in one of the new blocks as a guide index
        Record* rec = pointdata; // The items of the data block
        // Find from the data block the second biggest
        if (rec[0].id > record.id) {
            newid = rec[0].id;
        } 
        else if(rec[1].id > record.id) {
            newid = record.id;
        }
        else {
            newid = rec[1].id;
        }
        int topid,rightid; // The indexes of the new top and right index block will be stored  these variables 
        // The current index block will be the new left index block
        if (index->items[0] > newid) { // Determine where each index will be stored(The second biggest among them goes at top)
            topid = index->items[0];
            index->items[0] = newid; 
            rightid = index->items[1];
        } else if (index->items[1] > newid) {
            topid = newid;
            rightid = index->items[1];
        } else {
            topid = index->items[1];
            rightid = newid;
        }
        index->size = 1; // Created the left block
        BF_Block* rightblock; // Creating the right block
        BF_Block_Init(&rightblock);
        CALL_BF(BF_AllocateBlock(file_desc, rightblock));
        void* right = BF_Block_GetData(rightblock);
        Init_index_block(right,file_desc);
        int value = Insertindex(right,rightid);// The ID of the right index node

        BF_Block* topblock; // Creating the top block which will be also the root
        BF_Block_Init(&topblock);
        CALL_BF(BF_AllocateBlock(file_desc, topblock));
        void* top = BF_Block_GetData(topblock);
        Init_index_block(top,file_desc);
        int got = Insertindex(top,topid); // The ID of the top block
        bplus_info->root = got; // Change the root
        BPLUS_INDEX_NODE* temp = top;
        temp->next[0] = index->id;
        temp->next[1] = value; // Created the top block
        BF_UnpinBlock(datablock);
        BF_Block_Destroy(&datablock);
        BF_Block_SetDirty(indexblock);
        BF_UnpinBlock(indexblock);
        BF_Block_Destroy(&indexblock);
        BF_Block_SetDirty(topblock);
        BF_Block_SetDirty(rightblock);
        BF_UnpinBlock(topblock);
        BF_UnpinBlock(rightblock);
        BF_Block_Destroy(&rightblock);
        BF_Block_Destroy(&topblock);
        // Insert the records into the tree
        for (int i = 0; i < sizerec; i++) {
            int j = BP_InsertEntry(file_desc, bplus_info, allrec[i]);
            if (allrec[i].id == record.id) {
                return j; 
            }
        }
    }
    // If there is a previous index block get it and check its size 
    BF_Block* indexblock2;
    BF_Block_Init(&indexblock2);
    CALL_BF(BF_GetBlock(file_desc, idblock3, indexblock2));
    void* pointerindex2 = BF_Block_GetData(indexblock2);
    BPLUS_INDEX_NODE* index2 = pointerindex2; // Take the previous index node
    if(index2->size == 1) { // If the previous index block has size 1
        int sizerec = 0;
        Record allrec[7];
        for (int i = 0; i < 3; i++) {
            if (index->next[i] == -1) {
                continue;
            }
            BF_Block* tempblock;
            BF_Block_Init(&tempblock);
            CALL_BF(BF_GetBlock(file_desc, index->next[i], tempblock));
            void* pointerblock = BF_Block_GetData(tempblock);
            BPLUS_DATA_NODE* infotemp = pointerblock + BF_BLOCK_SIZE - sizeof(BPLUS_INFO)-sizeof(BPLUS_DATA_NODE);
            Record* rectemp = pointerblock;
            for (int j = 0; j < infotemp->size; j++) {
                allrec[sizerec] = rectemp[j];  
                sizerec++;
            }
            infotemp->size = 0; 
            BF_Block_SetDirty(tempblock);
            BF_UnpinBlock(tempblock);  
            BF_Block_Destroy(&tempblock);
        }
        allrec[sizerec] = record;  // Add the current record
        sizerec++;
        int newid; 
        Record* rec = pointdata; 
        // Get the 2 biggest id as a index from the current data block
        if (rec[0].id > record.id) {
            newid = rec[0].id;
        } 
        else if(rec[1].id > record.id) {
            newid = record.id;
        }
        else {
            newid = rec[1].id;
        }
        int topid, leftid, rightid; // From the index node and the ID which was taken,find the placement
        if(index->items[0] > newid) {
            topid = index->items[0];
            leftid = newid;
            rightid = index->items[1];
        } else if (index->items[1] > newid) {
            topid = newid;
            leftid = index->items[0];
            rightid = index->items[1];
        } else {
            topid = index->items[1];
            rightid = newid;
            leftid = index->items[0];
        }
        index->items[0] = leftid; // The current index will be the new left index block
        index->size = 1;
        BF_Block* rightblock; // Creating the right block 
        BF_Block_Init(&rightblock);
        CALL_BF(BF_AllocateBlock(file_desc, rightblock));
        void* right = BF_Block_GetData(rightblock);
        Init_index_block(right,file_desc);
        int value = Insertindex(right,rightid);
        Insertindex(index2,topid); // Add the new top index to the previous index block
        if(index2->items[0] == topid) { // If the new top index is smaller than the first item of the previous block  
            index2->next[2] = index2->next[1];
            index2->next[0] = index->id;
            index2->next[1] =  value;
        }
        else {
            index2->next[1] = index->id;
            index2->next[2] = value;
        }
        BF_Block_SetDirty(rightblock);
        BF_UnpinBlock(rightblock);
        BF_Block_Destroy(&rightblock);
        BF_Block_SetDirty(indexblock2);
        BF_UnpinBlock(indexblock2);
        BF_Block_Destroy(&indexblock2);
        BF_Block_SetDirty(indexblock);
        BF_UnpinBlock(indexblock);
        BF_UnpinBlock(datablock);
        BF_Block_Destroy(&datablock);
        BF_Block_Destroy(&indexblock);
        // Insert the records into the tree
        for (int i = 0; i < sizerec; i++) {
            int j = BP_InsertEntry(file_desc, bplus_info, allrec[i]);
            if (allrec[i].id == record.id) { 
                return j; 
            }
        }
    }
    // Case the previous index block is size 2
    int sizerec = 0;
    Record allrec[7]; 
    // Get the record of the index node
    for (int i = 0; i < 3; i++) {
        if (index->next[i] == -1) {
            continue;
        }
        BF_Block* tempblock;
        BF_Block_Init(&tempblock);
        CALL_BF(BF_GetBlock(file_desc, index->next[i], tempblock));
        void* pointerblock = BF_Block_GetData(tempblock);
        BPLUS_DATA_NODE* infotemp = pointerblock + BF_BLOCK_SIZE - sizeof(BPLUS_INFO)-sizeof(BPLUS_DATA_NODE);
        Record* rectemp = pointerblock;
        for (int j = 0; j < infotemp->size; j++) {
            allrec[sizerec] = rectemp[j];  
            sizerec++;
        }
        infotemp->size = 0; 
        BF_Block_SetDirty(tempblock);
        BF_UnpinBlock(tempblock); 
        BF_Block_Destroy(&tempblock);
    }
    allrec[sizerec] = record; 
    sizerec++; 
    int newid; 
    Record* rec = pointdata; 
    if (rec[0].id > record.id) { // Get the second biggest from the data node and the new record
        newid = rec[0].id;
    } else if (record.id < rec[1].id) {
        newid = record.id;
    } else {
        newid = rec[1].id;
    } 
    int topchildid, leftchildid, rightchildid; // Each one will have the index for each new children block 
    if (index->items[0] > newid) {
        topchildid = index->items[0];
        leftchildid = newid;
        rightchildid = index->items[1];
    } else if (index->items[1] > newid) {
        topchildid = newid;
        leftchildid = index->items[0];
        rightchildid = index->items[1];
    } else {
        topchildid = index->items[1];
        rightchildid = newid;
        leftchildid = index->items[0];
    }
    int topid,leftid,rightid; // Each one will have the index for each the new parent blocks
    index->items[0] = leftchildid; 
    index->size = 1; // The current index block will be left child

    BF_Block* rightchildrenblock;
    BF_Block_Init(&rightchildrenblock);
    CALL_BF(BF_AllocateBlock(file_desc, rightchildrenblock));
    void* rightchildrenpoint = BF_Block_GetData(rightchildrenblock);
    Init_index_block(rightchildrenpoint,file_desc);
    int value3 = Insertindex(rightchildrenpoint,rightchildid);// Done the right children 
    BF_Block* leftparentblock;
    BF_Block_Init(&leftparentblock);
    CALL_BF(BF_AllocateBlock(file_desc, leftparentblock));
    void* leftparentpoint = BF_Block_GetData(leftparentblock);
    Init_index_block(leftparentpoint,file_desc); // Done the left parent block
    BF_Block* rightparentblock; 
    BF_Block_Init(&rightparentblock);
    CALL_BF(BF_AllocateBlock(file_desc, rightparentblock));
    void* rightparentpoint = BF_Block_GetData(rightparentblock);
    Init_index_block(rightparentpoint,file_desc); // Done the right parent block
    if (index2->items[0] > topchildid) { // If the split is at the first pointer of the previous index block 
        leftid = topchildid;
        rightid = index2->items[1];
        int value = Insertindex(rightparentpoint,rightid);
        int value2 = Insertindex(leftparentpoint,leftid);
        BPLUS_INDEX_NODE* lefttemp = leftparentpoint;
        BPLUS_INDEX_NODE* righttemp = rightparentpoint;
        lefttemp->next[0] = index->id;
        lefttemp->next[1] = value3; // Left parent
        righttemp->next[0] =  index2->next[1];
        righttemp->next[1] = index2->next[2];
        index2->size = 1; // The current previous index node will be the root 
        index2->next[0] = value2;
        index2->next[1] = value;
        index2->next[2] = -1; // Change the pointers

    } else if (index2->items[1] > topchildid) { // If the split is happening on the middle pointer 
        topid = topchildid;
        leftid = index2->items[0];
        rightid = index2->items[1];
        int value = Insertindex(rightparentpoint,rightid);
        int value2 = Insertindex(leftparentpoint,leftid);
        BPLUS_INDEX_NODE* lefttemp = leftparentpoint;
        BPLUS_INDEX_NODE* righttemp = rightparentpoint;
        lefttemp->next[0] = index2->next[0];
        lefttemp->next[1] = index->id; // Left parent
        righttemp->next[0] =  value3;
        righttemp->next[1] = index2->next[2];
        index2->items[0] = topid;
        index2->size = 1; 
        index2->next[0] = value2;
        index2->next[1] = value;
        index2->next[2] = -1; 
    } else { // If the split i happening on the 3rd pointer
        topid = index2->items[1];
        rightid = topchildid;
        leftid = index2->items[0];
        int value = Insertindex(rightparentpoint,rightid);
        int value2 = Insertindex(leftparentpoint,leftid);
        BPLUS_INDEX_NODE* lefttemp = leftparentpoint;
        BPLUS_INDEX_NODE* righttemp = rightparentpoint;
        lefttemp->next[0] = index2->next[0];
        lefttemp->next[1] = index2->next[1]; // Left parent
        righttemp->next[0] =  index->id;
        righttemp->next[1] = value3;
        index2->items[0] = topid;
        index2->size = 1; 
        index2->next[0] = value2;
        index2->next[1] = value;
        index2->next[2] = -1; 
    }
    BF_Block_SetDirty(indexblock);
    BF_Block_SetDirty(indexblock2);
    BF_Block_SetDirty(rightparentblock);
    BF_Block_SetDirty(leftparentblock);
    BF_Block_SetDirty(rightchildrenblock);
    BF_UnpinBlock(indexblock);
    BF_UnpinBlock(indexblock2);
    BF_UnpinBlock(rightparentblock);
    BF_UnpinBlock(leftparentblock);
    BF_UnpinBlock(rightchildrenblock);
    BF_Block_Destroy(&indexblock);
    BF_Block_Destroy(&indexblock2);
    BF_Block_Destroy(&rightparentblock);
    BF_Block_Destroy(&leftparentblock);
    BF_Block_Destroy(&rightchildrenblock);
    //Insert the records we got
    for (int i = 0; i < sizerec; i++) {
        int j = BP_InsertEntry(file_desc, bplus_info, allrec[i]);
        if (allrec[i].id == record.id) {
            return j;
        }
    }
}

// Get a record form tje B plus tree 
int BP_GetEntry(int file_desc, BPLUS_INFO *bplus_info, int value, Record** result) {
    void* pointer;
    BF_Block* block;
    BF_Block_Init(&block);
    int next;
    // Get the root block
    CALL_BF(BF_GetBlock(file_desc, bplus_info->root, block));
    pointer = BF_Block_GetData(block);
    BPLUS_INFO* bplus_info_block = pointer + BF_BLOCK_SIZE - sizeof(BPLUS_INFO); 
    while (bplus_info_block->type) { // While the block is index  
        BPLUS_INDEX_NODE* index_node = pointer;
        // Find the correct next pointer based on the value
        if (value < index_node->items[0]) {
            next = index_node->next[0];
        } else if (value < index_node->items[1] || index_node->size == 1) {
            next = index_node->next[1];
        } else {
            next = index_node->next[2];
        }
        CALL_BF(BF_UnpinBlock(block));
        if (next == -1) {  // If there is no next block 
            printf("Entry not found\n");
            BF_Block_Destroy(&block);
            *result = NULL;
            return -1;
        }
        // Load the next block (which will be another index or the data block)
        CALL_BF(BF_GetBlock(file_desc, next, block));
        pointer = BF_Block_GetData(block);
        bplus_info_block = pointer + BF_BLOCK_SIZE - sizeof(BPLUS_INFO); // Update the block info
    }
    // Now we are in a data block, so search for the record
    Record* found = pointer;
    BPLUS_DATA_NODE* data_node = pointer + BF_BLOCK_SIZE - sizeof(BPLUS_INFO) - sizeof(BPLUS_DATA_NODE);
    if (data_node->size == 0) {  // If the data block is empty, return not found
        *result = NULL;
        printf("Entry not found\n");
        CALL_BF(BF_UnpinBlock(block));
        BF_Block_Destroy(&block);
        return -1;
    }
    // Search the records in the data block
    for (int i = 0; i < data_node->size; i++) {
        if (found[i].id == value) {  // If the record is found
            *result = &found[i];
            CALL_BF(BF_UnpinBlock(block));
            BF_Block_Destroy(&block);
            return 0;
        }
    }
    // If the record wasn't found
    *result = NULL;
    printf("Entry not found\n");
    CALL_BF(BF_UnpinBlock(block));
    BF_Block_Destroy(&block);
    return -1;
}


