#include "my_vm.h"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

unsigned int offset_mask(int offset){
    unsigned int mask = 0;
    for(int i=0; i<offset; i++){
        mask <<= 1;
        mask++;
    }
    return mask;
}

unsigned int getBit(char* bitmap, int offset){
    int charIndex = offset / 8;
    int bitIndex = offset % 8;
    char foundBit = bitmap[charIndex];
    int outBit = (int)foundBit | 0;
    outBit >>= (7 - bitIndex);
    outBit &= 1;
    return outBit;
}

//if bit that's there is 1, it's set to 0, and vice versa
void setBit(char* bitmap, int offset){
    int charIndex = offset / 8;
    int bitIndex = offset % 8;
    char newBit = 1 << (7 - bitIndex);
    bitmap[charIndex] ^= newBit;
    return;
}

int get_current_frame_number(pte_t* pa){
    for(int i=0; i<numFrames; i++){
        if(phys_mem[i] == pa){
            return i;
        }
    } 
    return -1;
}

/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {
    numFrames = MEMSIZE / PGSIZE / 4; // number of page frames in physical memory
    numPages = PGSIZE / 4; //number of pages per table
    numTables = (MAX_MEMSIZE / PGSIZE) / numPages; 
    offsetBits = (int)log2(PGSIZE); //bits needed for offset within a page
    tableBits = (int)log2(numPages); //bits needed for pages within a table (lower level page table)
    dirBits = 32 - offsetBits - tableBits; //bits needed for tables within a directory (higher level page table)


    //pte is same size as a mem address anyway
    phys_mem = (pte_t**)malloc(numFrames * 4);
    for(int i=0; i<numFrames; i++){
        phys_mem[i] = (pte_t*)malloc(PGSIZE);
    }
    
    phys_bitmap = malloc(numFrames/8); //creates bitmap with each char representing 8 frames at once
    virt_bitmap = malloc((numTables*numPages)/8); //creates bitmap with each char representing a virtual page (every page in every table gets a bit)
    memset(phys_bitmap, 0, (numFrames/8)); //initializes everything to 0
    memset(virt_bitmap, 0, ((numTables*numPages)/8)); //initializes everything to 0

    virt_bitmap[0] = 0x80; //initializes very first page table (dir[0], table[0]) as used so as to not have functions return null
    //allocates and initializes page tables and directory
    directory = (pde_t*)malloc(4*numTables);
    for(int i=0; i<numTables; i++){
        pte_t* table = (pte_t*)malloc(PGSIZE);
        for(int j=0; j<numPages; j++){
            table[j] = (i == 0 && j == 0) ? OFF_LIMITS : 0; //first page table is off limits
        }  
        directory[i] = (pde_t)table;
    }
    dir_base = (unsigned long)directory;
    table_base = (unsigned long)directory[0];
    
    memcpy(phys_mem[0], directory, (sizeof(directory) == PGSIZE) ? PGSIZE : sizeof(directory));
    memcpy(phys_mem[1], (pte_t*)(directory[0]), PGSIZE);
    phys_bitmap[0] = 0xC0; //same as virt_bitmap except it sets up the first 2 bits

    tlb_store.hits = 0;
    tlb_store.misses = 0;
    tlb_store.accesses = 0;
    tlb_store.entries = (tlb_entry*)malloc(TLB_ENTRIES * sizeof(tlb_entry));
    memset(tlb_store.entries, 0, TLB_ENTRIES * sizeof(tlb_entry));
    tlb_store.entries[0].virt_addr = OFF_LIMITS;
    tlb_store.entries[0].phys_addr = OFF_LIMITS;
}

/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int add_TLB(void *va, void *pa, int index) {

    /*Part 2 HINT: Add a virtual to physical page translation to the TLB */
    if(!va || !pa) return -1;

    tlb_entry* currentEntry = &(tlb_store.entries[index]);
    currentEntry->phys_addr = (pte_t)pa;
    currentEntry->virt_addr = (pte_t)va;
    return 0;
}

/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t check_TLB(void *va) {
    if(!va) return -999;
    pte_t pa;
    tlb_store.accesses++;
    unsigned long vpn = (unsigned long)va >> offsetBits;
    if(tlb_store.entries[vpn % TLB_ENTRIES].virt_addr == (pte_t)va){
        tlb_store.hits++;
        pa = tlb_store.entries[vpn % TLB_ENTRIES].phys_addr;
        return pa;
    }
    tlb_store.misses++;
    return -1;
}

/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void print_TLB_missrate() {
    double miss_rate = 0;	
    miss_rate = (double)tlb_store.misses / (double)tlb_store.accesses;
    /*Part 2 Code here to calculate and print the TLB miss rate*/
    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}

/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va) {
    //Part 1
    unsigned long offset = (unsigned long)va & offset_mask(offsetBits);
    unsigned long vpn = (unsigned long)va >> offsetBits;
    unsigned long tableIndex = vpn & offset_mask(tableBits);
    unsigned long dirIndex = vpn >> tableBits;
    pte_t phys_addr;

       //Part 2
    pte_t check = check_TLB((void*)(vpn<<offsetBits));
    if(check != (pte_t)-1) phys_addr = check;
    else{
        pde_t foundTable = pgdir[dirIndex]; //table has been found in our directory
        pte_t foundEntry = ((pte_t*)foundTable)[tableIndex]; //entry has been found in our table
        phys_addr = foundEntry; //frame pointer has been found in entry        
        add_TLB(va, (void*)phys_addr, (vpn % TLB_ENTRIES));
    }
    phys_addr |= offset;
    return (pte_t*)phys_addr;
}

/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int page_map(pde_t *pgdir, void *va, void *pa) {

    unsigned long offset, dirOff, tableOff, a_virt = (unsigned long)va, a_phys = (unsigned long)pa;
    pte_t* foundTable;
    pte_t foundEntry;
    offset = a_virt & offset_mask(offsetBits);
    tableOff = (a_virt & offset_mask(offsetBits+tableBits)) >> offsetBits;
    dirOff = a_virt >> (offsetBits+tableBits);
    foundTable = (pte_t*)(pgdir[dirOff]); //page table corresponding to dirIndex;
    foundEntry = foundTable[tableOff];
     //page frame corresponding to tableIndex
    if(foundEntry == 0){
        //page frame is null (nothing is mapped there)
        foundTable[tableOff] = (pte_t)a_phys;
        setBit(virt_bitmap, (tableOff + dirOff*numTables));
        return 0;
    }
    //something already mapped there
    return -1;
}

/*Function that gets the next available page
*/
void *get_next_avail(int num_pages) {
    for(int i=0; i<(num_pages); i++){
        //checks bit in bitmap
        if(!getBit(virt_bitmap, i)){
            //empty page is found
            unsigned int tab = i / numTables;
            unsigned int pag = i % numPages;
            return (void*)(((tab << tableBits) | pag) << offsetBits);
        }
    }
    //all of them are used up
    return NULL;
}

/* Function responsible for allocating pages
and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes) {
    pthread_mutex_lock(&lock);
    if(!phys_mem){
        set_physical_mem();
    }
    void* va = get_next_avail(numPages * numTables);
    void* out_addr = va;
    //find next physical page
    int frameIndex = -1;
    for(int i=0; i<numFrames; i++){
        if(!getBit(phys_bitmap, i)){
            frameIndex = i;
            break;
        }
    }
    if(frameIndex == -1){
        perror("No more free memory\n");
        exit(1);
    }
    while(1){
        //runs only once if allocating 4096 bytes or lesss
        pte_t* pa = phys_mem[frameIndex];
        if(pa == NULL){
            perror("Cannot allocate more\n");
            exit(1);
        }
        page_map(directory, va, pa);
        setBit(phys_bitmap, frameIndex);
        if(num_bytes <= PGSIZE) break;
        else{
            va += PGSIZE; //going up one entry at a time
            num_bytes -= PGSIZE;
            frameIndex++;   
        }
    }    
    pthread_mutex_unlock(&lock);
    return out_addr;
}

/* Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {
    //remove data from pages
    if(va == 0){
        perror("Freeing a null pointer\n");
        exit(1);
    }
    pthread_mutex_lock(&lock);
    while(1){
        pte_t* pa = translate(directory, va);
        int frameIndex = get_current_frame_number(pa);
        if(frameIndex == -1){
            perror("Not found frame\n");
            exit(1);
        }
        pte_t* current_page = phys_mem[frameIndex];
        if(!getBit(phys_bitmap, frameIndex)){
            perror("Freeing memory that wasn't allocated\n");
            exit(1);
        }
        setBit(phys_bitmap, frameIndex); //was 1, now 0
        memset(current_page, 0, PGSIZE);
        int offset = (int)va & offset_mask(offsetBits);
        int tableOffset = ((int)va & offset_mask(offsetBits+tableBits)) >> offsetBits;
        int dirOffset = (int)va >> (offsetBits+tableBits);
        int tableIndex = (numTables * dirOffset) + tableOffset;
        if(!getBit(virt_bitmap, tableIndex)){
            perror("Freeing memory that wasn't allocated\n");
            exit(1);
        }
        setBit(virt_bitmap, (tableOffset + dirOffset*numTables));
        pte_t* delTable = (pte_t*)(directory[dirOffset]);
        delTable[tableOffset] = 0;
        if(size <= PGSIZE){
            break;
        }
        else{
            va += PGSIZE;
            size -= PGSIZE;
        }
    }
    pthread_mutex_unlock(&lock);
}

/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
void put_value(void *va, void *val, int size) {
    while(1){
        pthread_mutex_lock(&lock);
        pte_t* pa = translate(directory, va);
        memcpy(pa, val, (size > PGSIZE) ? PGSIZE : size);
        if(size > PGSIZE){
            va += PGSIZE;
            size -= PGSIZE;
        } else {
            pthread_mutex_unlock(&lock);
            break;
        }
    }
}

/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {
    while(1){
        pthread_mutex_lock(&lock);
        pte_t* pa = translate(directory, va);
        memcpy(val, pa, (size > PGSIZE) ? PGSIZE : size);
        if(size > PGSIZE){
            va += PGSIZE;
            size -= PGSIZE;
        } else{
            pthread_mutex_unlock(&lock);
            break;
        }
    }

}

/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));

                // printf("Values at the index: %d, %d, %d, %d, %d\n", 
                //      a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}
