#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h> // For true and false
#include <time.h>
#include "bf.h"
#include "hp_file.h"

#define RECORDS_NUM 1000 // you can change it if you want
#define FILE_NAME "data.db"

#define CALL_OR_DIE(call)     \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK)        \
    {                         \
      BF_PrintError(code);    \
      exit(code);             \
    }                         \
  }

void insertEntries(int file_desc, HP_info *hp_info, int entryNumber,Record sampledRecords[10]);
void printEntries(int file_desc, HP_info *hp_info, int entryNumber,Record sampledRecords[10]);
void test1(int entryNumber);
void test2(int entryNumber);

int main(int argc, char *argv[])
{
  int entryNumber = atoi(argv[1]);
  test1(entryNumber);
}

void test1(int entryNumber)
{
  BF_Init(LRU);
  HP_CreateFile(FILE_NAME);
  int file_desc;
  // Insert Records
  HP_info *hp_info = HP_OpenFile(FILE_NAME, &file_desc);
  Record sampledRecords[10];
  insertEntries(file_desc, hp_info, entryNumber,sampledRecords);
  for (int i = 0; i < 10; i++)
  {
    printRecord(sampledRecords[i]);
  }
  
  // Print Records
  printEntries(file_desc, hp_info, entryNumber,sampledRecords);
  HP_CloseFile(file_desc, hp_info);
  BF_Close();
}

void test2(int entryNumber)
{
  BF_Init(LRU);
  HP_CreateFile(FILE_NAME);
  int file_desc;
  // Insert Records
  HP_info *hp_info = HP_OpenFile(FILE_NAME, &file_desc);
  Record sampledRecords[10];
  insertEntries(file_desc, hp_info, entryNumber,sampledRecords);
  HP_CloseFile(file_desc, hp_info);
  BF_Close();
  // Print Records
  BF_Init(LRU);
  hp_info = HP_OpenFile(FILE_NAME, &file_desc);
  printEntries(file_desc, hp_info, entryNumber,sampledRecords);
  HP_CloseFile(file_desc, hp_info);
  BF_Close();
}

void insertEntries(int file_desc, HP_info *hp_info, int entryNumber,Record sampledRecords[10])
{
  Record record;
  int counter=0;
  while(counter<10)
  {
    double random_value = (double)rand() / RAND_MAX;
    record = randomRecord();
    HP_InsertEntry(file_desc, hp_info, record);
    if (random_value <= (double)10 / entryNumber){
      sampledRecords[counter++]=record;
    }
  }
}

void printEntries(int file_desc, HP_info *hp_info, int entryNumber,Record sampledRecords[10])
{
  for (int i = 0; i < 10; i++)
  {
    Record searchedRec= sampledRecords[i];
    printf("Searching: *%d*\n", searchedRec.id);
    HP_GetAllEntries(file_desc, hp_info, searchedRec.id);
  }
}
