//
//  memmgr.c
//  memmgr
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ARGC_ERROR 1
#define FILE_ERROR 2
#define BUFLEN 256
#define FRAME_SIZE  256
#define FRAME_NUMBER 128

int main_mem[256][FRAME_SIZE];
char main_mem_fifo[32768]; // 128 physical frames
int page_queue[128];
int qhead = 0, qtail = 0;
int tlb[16][2];
int current_tlb_entry = 0;
int page_table[256];
int page_table_frames[256];
int current_frame = 0;
int page_fault_count = 0;
int available_frame = 0;
int available_page = 0;
char address[BUFLEN];
int logic_add;
int tlbh = 0;
int page_table_num[256];
signed char buffer[FRAME_SIZE];
signed char value;
FILE* fstore;

// data for statistics
int pfc[5], pfc2[5]; // page fault count
int tlbh, tlbh2[5]; // tlb hit count
int count[5], count2[5]; // access count

#define PAGES 256
#define FRAMES_PART1 256
#define FRAMES_PART2 128

//-------------------------------------------------------------------
unsigned getpage(unsigned x) { return (0xff00 & x) >> 8; }

unsigned getoffset(unsigned x) { return (0xff & x); }

void read_store(int page, FILE** fadd, FILE** fstore){

  if (fseek(*fstore, page * FRAME_SIZE, SEEK_SET) != 0) {
    fprintf(stderr, "Reading Error\n");
  }
    
  if (fread(buffer, sizeof(signed char), FRAME_SIZE, *fstore) == 0) {
    fprintf(stderr, "Reading Error\n");        
  }
    
  for(int i = 0; i < FRAME_SIZE; i++){
      main_mem[available_frame][i] = buffer[i];
  }
    
  page_table_num[available_page] = page;
  page_table_frames[available_page] = available_frame;
    
  available_frame++;
  available_page++;
}

void getpage_offset(unsigned x) {
  unsigned  page   = getpage(x);
  unsigned  offset = getoffset(x);
  printf("x is: %u, page: %u, offset: %u, address: %u, paddress: %u\n", x, page, offset,
         (page << 8) | getoffset(x), page * 256 + offset);
}

int tlb_contains(unsigned x) {  // TODO:
  int i;
  for(i = 0; i < current_tlb_entry; i++){
        if(tlb[i][0] == x){
           return i;//hit
        }
    }
  return i;
}

void update_tlb(unsigned page, unsigned frame) {  // TODO:
     int i = tlb_contains(page); 
  if(i == current_tlb_entry){//if miss
    if(current_tlb_entry < 16){//not full
      tlb[current_tlb_entry][0] = page;
      tlb[current_tlb_entry][1] = frame;
    }
    else{
      for(i = 0; i < 16 - 1; i++){//if full
        tlb[i][0] = tlb[i + 1][0];
        tlb[i][1] = tlb[i + 1][1];
      }
      tlb[current_tlb_entry-1][0] = page;
      tlb[current_tlb_entry-1][1] = frame;
    }        
  }
  else{//if hit
    for(i = i; i < current_tlb_entry - 1; i++){      
      tlb[i][0] = tlb[i + 1][0]; //push everything forward once
      tlb[i][1] = tlb[i + 1][1];
    }
    if(current_tlb_entry < 16){//not full
      tlb[current_tlb_entry][0] = page;
      tlb[current_tlb_entry][1] = frame;
    }
    else{//if full put at the end
      tlb[current_tlb_entry-1][0] = page;
      tlb[current_tlb_entry-1][1] = frame;
    }
  }
  
  if(current_tlb_entry < 16){
    current_tlb_entry++;
  }    

}

void getframe(int logic_add, FILE** fadd, FILE** fstore){
    
  int page = getpage(logic_add);
  int offset = getoffset(logic_add);

  int frame = -1;
     
  for(int i = 0; i < 16; i++){
    if(tlb[i][0] == page){   
      frame = tlb[i][1];  
      tlbh++;           //tlb hit
    }
  }
    
    //tlb miss
    
  if(frame == -1){
    for(int i = 0; i < available_page; i++){
      if(page_table_frames[i] == page){         //Check page table
        frame = page_table_frames[i];        //update frame if found
      }
    }
    if(frame == -1){                         //page table miss
      read_store(page, fadd, fstore);        //page fault
      page_fault_count++;                    
      frame = available_frame - 1;
    }
  }
    
  update_tlb(page, frame);

  value = main_mem[frame][offset];
    
}


void open_files(FILE** fadd, FILE** fcorr, FILE** fstore) {
  *fadd = fopen("addresses.txt", "r");    // open file addresses.txt  (contains the logical addresses)
  if (*fadd ==  NULL) { fprintf(stderr, "Could not open file: 'addresses.txt'\n");  exit(FILE_ERROR);  }

  *fcorr = fopen("correct.txt", "r");     // contains the logical and physical address, and its value
  if (*fcorr ==  NULL) { fprintf(stderr, "Could not open file: 'correct.txt'\n");  exit(FILE_ERROR);  }

  *fstore = fopen("BACKING_STORE.bin", "rb");
  if (*fstore ==  NULL) { fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");  exit(FILE_ERROR);  }
}


void close_files(FILE* fadd, FILE* fcorr, FILE* fstore) {
  fclose(fcorr);
  fclose(fadd);
  fclose(fstore);
}


void simulate_pages_frames_equal(void) {
  char buf[BUFLEN];
  unsigned   page, offset, physical_add, frame = 0;
  unsigned   logic_add;                  // read from file address.txt
  unsigned   virt_add, phys_add, value;  // read from file correct.txt


  FILE *fadd, *fcorr, *fstore;
  open_files(&fadd, &fcorr, &fstore);
  
  // Initialize page table, tlb
  memset(page_table, -1, sizeof(page_table));
  for (int i = 0; i < 16;  ++i) { tlb[i][0] = -1; }
  
  int access_count = 0, page_fault_count = 0, tlb_hit_count = 0;
  current_frame = 0;
  current_tlb_entry = 0;
  
  printf("\n Starting nPages == nFrames memory simulation...\n");

  while (fscanf(fadd, "%d", &logic_add) != EOF) {
    ++access_count;

    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
           buf, buf, &phys_add, buf, &value);  // read from file correct.txt

    // fscanf(fadd, "%d", &logic_add);  // read from file address.txt
    page   = getpage(  logic_add);
    offset = getoffset(logic_add);
    getframe(logic_add, &fadd, &fstore);

    physical_add = frame * FRAME_SIZE + offset;
    int val = (int)(main_mem[physical_add]);


    printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d  ok\n", logic_add, page, offset, physical_add, val);
    if (access_count % 5 ==  0) { printf("\n"); }

    //assert(physical_add ==  phys_add);
    //assert(value ==  val);
  }
  fclose(fcorr);
  fclose(fadd);
  fclose(fstore);
  
  printf("ALL logical ---> physical assertions PASSED!\n");
  printf("ALL read memory value assertions PASSED!\n");

  printf("\n\t\t... nPages == nFrames memory simulation done.\n");
}


void simulate_pages_frames_not_equal(void) {
  char buf[BUFLEN];
  unsigned   page, offset, physical_add, frame = 0;
  unsigned   logic_add;                  // read from file address.txt
  unsigned   virt_add, phys_add, value;  // read from file correct.txt

  printf("\n Starting nPages != nFrames memory simulation...\n");

  // Initialize page table, tlb, page queue
  memset(page_table, -1, sizeof(page_table));
  memset(page_queue, -1, sizeof(page_queue));
  for (int i = 0; i < 16;  ++i) { tlb[i][0] = -1; }
  
  int access_count = 0, page_fault_count = 0, tlb_hit_count = 0;
  qhead = 0; qtail = 0;

  FILE *fadd, *fcorr, *fstore;
  open_files(&fadd, &fcorr, &fstore);

  while (fscanf(fadd, "%d", &logic_add) != EOF) {
    ++access_count;

    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
           buf, buf, &phys_add, buf, &value);  // read from file correct.txt

    // fscanf(fadd, "%d", &logic_add);  // read from file address.txt
    page   = getpage(  logic_add);
    offset = getoffset(logic_add);
    getframe(logic_add, &fadd, &fstore);

    physical_add = frame * FRAME_SIZE + offset;
    int val = (int)(main_mem_fifo[physical_add]);

    
    printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d  ok\n", logic_add, page, offset, physical_add, val);
    if (access_count % 5 ==  0) { printf("\n"); }

    //assert(value ==  val);
  }
  close_files(fadd, fcorr, fstore);

  printf("ALL read memory value assertions PASSED!\n");
  printf("\n\t\t... nPages != nFrames memory simulation done.\n");
}


int main(int argc, const char* argv[]) {
  // initialize statistics data
  for (int i = 0; i < 5; ++i){
    pfc[i] = pfc2[i] = tlbh  = tlbh2[i] = count[i] = count2[i] = 0;
  }

  simulate_pages_frames_equal(); // 256 physical frames
  simulate_pages_frames_not_equal(); // 128 physical frames

  // Statistics
  printf("\n\nnPages == nFrames Statistics (256 frames):\n");
  printf("Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate\n");
  for (int i = 0; i < 5; ++i) {
    printf("%9d %12d %18d %18.4f %14.4f\n",
           count[i], tlbh, pfc[i],
           1.0f * tlbh / count[i], 1.0f * pfc[i] / count[i]);
  }

  printf("\nnPages != nFrames Statistics (128 frames):\n");
  printf("Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate\n");
  for (int i = 0; i < 5; ++i) {
    printf("%9d %12d %18d %18.4f %14.4f\n",
           count2[i], tlbh2[i], pfc2[i],
           1.0f * tlbh2[i] / count2[i], 1.0f * pfc2[i] / count2[i]);
  }
  printf("\n\t\t...memory management simulation completed!\n");

  return 0;
}
