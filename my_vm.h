#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

//Assume the address space is 32 bits, so the max memory size is 4GB
//Page size is 4KB

//Add any important includes here which you may need

#define PGSIZE 4096

// Maximum size of virtual memory
#define MAX_MEMSIZE 4ULL*1024*1024*1024

// Size of "physcial memory"
#define MEMSIZE 1024*1024*1024

// Represents a page table entry
typedef unsigned long pte_t;

// Represents a page directory entry
typedef unsigned long pde_t;

#define TLB_ENTRIES 512

#define OFF_LIMITS 0xAAAAAAAA //used to protect against returning null

typedef struct tlb_entry{
    pte_t phys_addr;
    pte_t virt_addr;
} tlb_entry;


//Structure to represents TLB
struct tlb {
    /*Assume your TLB is a direct mapped TLB with number of entries as TLB_ENTRIES
    * Think about the size of each TLB entry that performs virtual to physical
    * address translation.
    */
    tlb_entry* entries; //array of entries storing translations
    int hits; //number of hits
    int misses; //number of misses
    int accesses;
};
struct tlb tlb_store;

//self declared items
#define LAYERS 2
pte_t** phys_mem; //each page is a set of PGSIZE pte_ts
int offsetBits;
int tableBits;
int dirBits;
int numPages;
int numTables;
int numFrames;
pde_t* directory;
char* phys_bitmap;
char* virt_bitmap;
unsigned long phys_base;
unsigned long table_base;
unsigned long dir_base;

void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int page_map(pde_t *pgdir, void *va, void* pa);
bool check_in_tlb(void *va);
void put_in_tlb(void *va, void *pa);
void *t_malloc(unsigned int num_bytes);
void t_free(void *va, int size);
void put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);
void print_TLB_missrate();

#endif
