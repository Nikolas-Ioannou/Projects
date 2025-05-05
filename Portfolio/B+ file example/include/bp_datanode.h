#ifndef BP_DATANODE_H
#define BP_DATANODE_H
#include <record.h>
#include <record.h>
#include <bf.h>
#include <bp_file.h>
#include <bp_indexnode.h>

typedef struct {
    int id; // Blocks id
    int size; // How many data there are (the max is 2)
} BPLUS_DATA_NODE; //The struct is stored before the struct BPLUS_INFO when there is data block  

int Insertdata(void* pointer,Record record);
void Init_data_block(void* block,int file_desc);
#endif 