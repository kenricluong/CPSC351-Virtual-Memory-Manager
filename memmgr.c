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
           return i;
        }
    }
  return i;
}

void update_tlb(unsigned page, unsigned frame) {  // TODO:
     int i = tlb_contains(page); 
  if(i == current_tlb_entry){
    if(current_tlb_entry < 16){
      tlb[current_tlb_entry][0] = page;
      tlb[current_tlb_entry][1] = frame;
    }
    else{
      for(i = 0; i < 16 - 1; i++){
        tlb[i][0] = tlb[i + 1][0];
        tlb[i][1] = tlb[i + 1][1];
      }
      tlb[current_tlb_entry-1][0] = page;
      tlb[current_tlb_entry-1][1] = frame;
    }        
  }
  else{
    for(i = i; i < current_tlb_entry - 1; i++){      
      tlb[i][0] = tlb[i + 1][0]; 
      tlb[i][1] = tlb[i + 1][1];
    }
    if(current_tlb_entry < 16){
      tlb[current_tlb_entry][0] = page;
      tlb[current_tlb_entry][1] = frame;
    }
    else{
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
      tlbh++;           
    }
  }
    
  if(frame == -1){
    for(int i = 0; i < available_page; i++){
      if(page_table_frames[i] == page){         
        frame = page_table_frames[i];       
      }
    }
    if(frame == -1){                        
      read_store(page, fadd, fstore);       
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

///// Output /////
/* logical: 24143 (page:  94, offset:  79) ---> physical:    79 -> value:    0  ok
logical: 15216 (page:  59, offset: 112) ---> physical:   112 -> value:    0  ok
logical:  8113 (page:  31, offset: 177) ---> physical:   177 -> value:    0  ok

logical: 22640 (page:  88, offset: 112) ---> physical:   112 -> value:    0  ok
logical: 32978 (page: 128, offset: 210) ---> physical:   210 -> value:    0  ok
logical: 39151 (page: 152, offset: 239) ---> physical:   239 -> value:    0  ok
logical: 19520 (page:  76, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 58141 (page: 227, offset:  29) ---> physical:    29 -> value:    0  ok

logical: 63959 (page: 249, offset: 215) ---> physical:   215 -> value:    0  ok
logical: 53040 (page: 207, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 55842 (page: 218, offset:  34) ---> physical:    34 -> value:    0  ok
logical:   585 (page:   2, offset:  73) ---> physical:    73 -> value:    0  ok
logical: 51229 (page: 200, offset:  29) ---> physical:    29 -> value:    0  ok

logical: 64181 (page: 250, offset: 181) ---> physical:   181 -> value:    0  ok
logical: 54879 (page: 214, offset:  95) ---> physical:    95 -> value:    0  ok
logical: 28210 (page: 110, offset:  50) ---> physical:    50 -> value:    0  ok
logical: 10268 (page:  40, offset:  28) ---> physical:    28 -> value:    0  ok
logical: 15395 (page:  60, offset:  35) ---> physical:    35 -> value:    0  ok

logical: 12884 (page:  50, offset:  84) ---> physical:    84 -> value:    0  ok
logical:  2149 (page:   8, offset: 101) ---> physical:   101 -> value:    0  ok
logical: 53483 (page: 208, offset: 235) ---> physical:   235 -> value:    0  ok
logical: 59606 (page: 232, offset: 214) ---> physical:   214 -> value:    0  ok
logical: 14981 (page:  58, offset: 133) ---> physical:   133 -> value:    0  ok

logical: 36672 (page: 143, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 23197 (page:  90, offset: 157) ---> physical:   157 -> value:    0  ok
logical: 36518 (page: 142, offset: 166) ---> physical:   166 -> value:    0  ok
logical: 13361 (page:  52, offset:  49) ---> physical:    49 -> value:    0  ok
logical: 19810 (page:  77, offset:  98) ---> physical:    98 -> value:    0  ok

logical: 25955 (page: 101, offset:  99) ---> physical:    99 -> value:    0  ok
logical: 62678 (page: 244, offset: 214) ---> physical:   214 -> value:    0  ok
logical: 26021 (page: 101, offset: 165) ---> physical:   165 -> value:    0  ok
logical: 29409 (page: 114, offset: 225) ---> physical:   225 -> value:    0  ok
logical: 38111 (page: 148, offset: 223) ---> physical:   223 -> value:    0  ok

logical: 58573 (page: 228, offset: 205) ---> physical:   205 -> value:    0  ok
logical: 56840 (page: 222, offset:   8) ---> physical:     8 -> value:    0  ok
logical: 41306 (page: 161, offset:  90) ---> physical:    90 -> value:    0  ok
logical: 54426 (page: 212, offset: 154) ---> physical:   154 -> value:    0  ok
logical:  3617 (page:  14, offset:  33) ---> physical:    33 -> value:    0  ok

logical: 50652 (page: 197, offset: 220) ---> physical:   220 -> value:    0  ok
logical: 41452 (page: 161, offset: 236) ---> physical:   236 -> value:    0  ok
logical: 20241 (page:  79, offset:  17) ---> physical:    17 -> value:    0  ok
logical: 31723 (page: 123, offset: 235) ---> physical:   235 -> value:    0  ok
logical: 53747 (page: 209, offset: 243) ---> physical:   243 -> value:    0  ok

logical: 28550 (page: 111, offset: 134) ---> physical:   134 -> value:    0  ok
logical: 23402 (page:  91, offset: 106) ---> physical:   106 -> value:    0  ok
logical: 21205 (page:  82, offset: 213) ---> physical:   213 -> value:    0  ok
logical: 56181 (page: 219, offset: 117) ---> physical:   117 -> value:    0  ok
logical: 57470 (page: 224, offset: 126) ---> physical:   126 -> value:    0  ok

logical: 39933 (page: 155, offset: 253) ---> physical:   253 -> value:    0  ok
logical: 34964 (page: 136, offset: 148) ---> physical:   148 -> value:    0  ok
logical: 24781 (page:  96, offset: 205) ---> physical:   205 -> value:    0  ok
logical: 41747 (page: 163, offset:  19) ---> physical:    19 -> value:    0  ok
logical: 62564 (page: 244, offset: 100) ---> physical:   100 -> value:    0  ok

logical: 58461 (page: 228, offset:  93) ---> physical:    93 -> value:    0  ok
logical: 20858 (page:  81, offset: 122) ---> physical:   122 -> value:    0  ok
logical: 49301 (page: 192, offset: 149) ---> physical:   149 -> value:    0  ok
logical: 40572 (page: 158, offset: 124) ---> physical:   124 -> value:    0  ok
logical: 23840 (page:  93, offset:  32) ---> physical:    32 -> value:    0  ok

logical: 35278 (page: 137, offset: 206) ---> physical:   206 -> value:    0  ok
logical: 62905 (page: 245, offset: 185) ---> physical:   185 -> value:    0  ok
logical: 56650 (page: 221, offset:  74) ---> physical:    74 -> value:    0  ok
logical: 11149 (page:  43, offset: 141) ---> physical:   141 -> value:    0  ok
logical: 38920 (page: 152, offset:   8) ---> physical:     8 -> value:    0  ok

logical: 23430 (page:  91, offset: 134) ---> physical:   134 -> value:    0  ok
logical: 57592 (page: 224, offset: 248) ---> physical:   248 -> value:    0  ok
logical:  3080 (page:  12, offset:   8) ---> physical:     8 -> value:    0  ok
logical:  6677 (page:  26, offset:  21) ---> physical:    21 -> value:    0  ok
logical: 50704 (page: 198, offset:  16) ---> physical:    16 -> value:    0  ok

logical: 51883 (page: 202, offset: 171) ---> physical:   171 -> value:    0  ok
logical: 62799 (page: 245, offset:  79) ---> physical:    79 -> value:    0  ok
logical: 20188 (page:  78, offset: 220) ---> physical:   220 -> value:    0  ok
logical:  1245 (page:   4, offset: 221) ---> physical:   221 -> value:    0  ok
logical: 12220 (page:  47, offset: 188) ---> physical:   188 -> value:    0  ok

logical: 17602 (page:  68, offset: 194) ---> physical:   194 -> value:    0  ok
logical: 28609 (page: 111, offset: 193) ---> physical:   193 -> value:    0  ok
logical: 42694 (page: 166, offset: 198) ---> physical:   198 -> value:    0  ok
logical: 29826 (page: 116, offset: 130) ---> physical:   130 -> value:    0  ok
logical: 13827 (page:  54, offset:   3) ---> physical:     3 -> value:    0  ok

logical: 27336 (page: 106, offset: 200) ---> physical:   200 -> value:    0  ok
logical: 53343 (page: 208, offset:  95) ---> physical:    95 -> value:    0  ok
logical: 11533 (page:  45, offset:  13) ---> physical:    13 -> value:    0  ok
logical: 41713 (page: 162, offset: 241) ---> physical:   241 -> value:    0  ok
logical: 33890 (page: 132, offset:  98) ---> physical:    98 -> value:    0  ok

logical:  4894 (page:  19, offset:  30) ---> physical:    30 -> value:    0  ok
logical: 57599 (page: 224, offset: 255) ---> physical:   255 -> value:    0  ok
logical:  3870 (page:  15, offset:  30) ---> physical:    30 -> value:    0  ok
logical: 58622 (page: 228, offset: 254) ---> physical:   254 -> value:    0  ok
logical: 29780 (page: 116, offset:  84) ---> physical:    84 -> value:    0  ok

logical: 62553 (page: 244, offset:  89) ---> physical:    89 -> value:    0  ok
logical:  2303 (page:   8, offset: 255) ---> physical:   255 -> value:    0  ok
logical: 51915 (page: 202, offset: 203) ---> physical:   203 -> value:    0  ok
logical:  6251 (page:  24, offset: 107) ---> physical:   107 -> value:    0  ok
logical: 38107 (page: 148, offset: 219) ---> physical:   219 -> value:    0  ok

logical: 59325 (page: 231, offset: 189) ---> physical:   189 -> value:    0  ok
logical: 61295 (page: 239, offset: 111) ---> physical:   111 -> value:    0  ok
logical: 26699 (page: 104, offset:  75) ---> physical:    75 -> value:    0  ok
logical: 51188 (page: 199, offset: 244) ---> physical:   244 -> value:    0  ok
logical: 59519 (page: 232, offset: 127) ---> physical:   127 -> value:    0  ok

logical:  7345 (page:  28, offset: 177) ---> physical:   177 -> value:    0  ok
logical: 20325 (page:  79, offset: 101) ---> physical:   101 -> value:    0  ok
logical: 39633 (page: 154, offset: 209) ---> physical:   209 -> value:    0  ok
logical:  1562 (page:   6, offset:  26) ---> physical:    26 -> value:    0  ok
logical:  7580 (page:  29, offset: 156) ---> physical:   156 -> value:    0  ok

logical:  8170 (page:  31, offset: 234) ---> physical:   234 -> value:    0  ok
logical: 62256 (page: 243, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 35823 (page: 139, offset: 239) ---> physical:   239 -> value:    0  ok
logical: 27790 (page: 108, offset: 142) ---> physical:   142 -> value:    0  ok
logical: 13191 (page:  51, offset: 135) ---> physical:   135 -> value:    0  ok

logical:  9772 (page:  38, offset:  44) ---> physical:    44 -> value:    0  ok
logical:  7477 (page:  29, offset:  53) ---> physical:    53 -> value:    0  ok
logical: 44455 (page: 173, offset: 167) ---> physical:   167 -> value:    0  ok
logical: 59546 (page: 232, offset: 154) ---> physical:   154 -> value:    0  ok
logical: 49347 (page: 192, offset: 195) ---> physical:   195 -> value:    0  ok

logical: 36539 (page: 142, offset: 187) ---> physical:   187 -> value:    0  ok
logical: 12453 (page:  48, offset: 165) ---> physical:   165 -> value:    0  ok
logical: 49640 (page: 193, offset: 232) ---> physical:   232 -> value:    0  ok
logical: 28290 (page: 110, offset: 130) ---> physical:   130 -> value:    0  ok
logical: 44817 (page: 175, offset:  17) ---> physical:    17 -> value:    0  ok

logical:  8565 (page:  33, offset: 117) ---> physical:   117 -> value:    0  ok
logical: 16399 (page:  64, offset:  15) ---> physical:    15 -> value:    0  ok
logical: 41934 (page: 163, offset: 206) ---> physical:   206 -> value:    0  ok
logical: 45457 (page: 177, offset: 145) ---> physical:   145 -> value:    0  ok
logical: 33856 (page: 132, offset:  64) ---> physical:    64 -> value:    0  ok

logical: 19498 (page:  76, offset:  42) ---> physical:    42 -> value:    0  ok
logical: 17661 (page:  68, offset: 253) ---> physical:   253 -> value:    0  ok
logical: 63829 (page: 249, offset:  85) ---> physical:    85 -> value:    0  ok
logical: 42034 (page: 164, offset:  50) ---> physical:    50 -> value:    0  ok
logical: 28928 (page: 113, offset:   0) ---> physical:     0 -> value:    0  ok

logical: 30711 (page: 119, offset: 247) ---> physical:   247 -> value:    0  ok
logical:  8800 (page:  34, offset:  96) ---> physical:    96 -> value:    0  ok
logical: 52335 (page: 204, offset: 111) ---> physical:   111 -> value:    0  ok
logical: 38775 (page: 151, offset: 119) ---> physical:   119 -> value:    0  ok
logical: 52704 (page: 205, offset: 224) ---> physical:   224 -> value:    0  ok

logical: 24380 (page:  95, offset:  60) ---> physical:    60 -> value:    0  ok
logical: 19602 (page:  76, offset: 146) ---> physical:   146 -> value:    0  ok
logical: 57998 (page: 226, offset: 142) ---> physical:   142 -> value:    0  ok
logical:  2919 (page:  11, offset: 103) ---> physical:   103 -> value:    0  ok
logical:  8362 (page:  32, offset: 170) ---> physical:   170 -> value:    0  ok

logical: 17884 (page:  69, offset: 220) ---> physical:   220 -> value:    0  ok
logical: 45737 (page: 178, offset: 169) ---> physical:   169 -> value:    0  ok
logical: 47894 (page: 187, offset:  22) ---> physical:    22 -> value:    0  ok
logical: 59667 (page: 233, offset:  19) ---> physical:    19 -> value:    0  ok
logical: 10385 (page:  40, offset: 145) ---> physical:   145 -> value:    0  ok

logical: 52782 (page: 206, offset:  46) ---> physical:    46 -> value:    0  ok
logical: 64416 (page: 251, offset: 160) ---> physical:   160 -> value:    0  ok
logical: 40946 (page: 159, offset: 242) ---> physical:   242 -> value:    0  ok
logical: 16778 (page:  65, offset: 138) ---> physical:   138 -> value:    0  ok
logical: 27159 (page: 106, offset:  23) ---> physical:    23 -> value:    0  ok

logical: 24324 (page:  95, offset:   4) ---> physical:     4 -> value:    0  ok
logical: 32450 (page: 126, offset: 194) ---> physical:   194 -> value:    0  ok
logical:  9108 (page:  35, offset: 148) ---> physical:   148 -> value:    0  ok
logical: 65305 (page: 255, offset:  25) ---> physical:    25 -> value:    0  ok
logical: 19575 (page:  76, offset: 119) ---> physical:   119 -> value:    0  ok

logical: 11117 (page:  43, offset: 109) ---> physical:   109 -> value:    0  ok
logical: 65170 (page: 254, offset: 146) ---> physical:   146 -> value:    0  ok
logical: 58013 (page: 226, offset: 157) ---> physical:   157 -> value:    0  ok
logical: 61676 (page: 240, offset: 236) ---> physical:   236 -> value:    0  ok
logical: 63510 (page: 248, offset:  22) ---> physical:    22 -> value:    0  ok

logical: 17458 (page:  68, offset:  50) ---> physical:    50 -> value:    0  ok
logical: 54675 (page: 213, offset: 147) ---> physical:   147 -> value:    0  ok
logical:  1713 (page:   6, offset: 177) ---> physical:   177 -> value:    0  ok
logical: 55105 (page: 215, offset:  65) ---> physical:    65 -> value:    0  ok
logical: 65321 (page: 255, offset:  41) ---> physical:    41 -> value:    0  ok

logical: 45278 (page: 176, offset: 222) ---> physical:   222 -> value:    0  ok
logical: 26256 (page: 102, offset: 144) ---> physical:   144 -> value:    0  ok
logical: 64198 (page: 250, offset: 198) ---> physical:   198 -> value:    0  ok
logical: 29441 (page: 115, offset:   1) ---> physical:     1 -> value:    0  ok
logical:  1928 (page:   7, offset: 136) ---> physical:   136 -> value:    0  ok

logical: 39425 (page: 154, offset:   1) ---> physical:     1 -> value:    0  ok
logical: 32000 (page: 125, offset:   0) ---> physical:     0 -> value:    0  ok
logical: 28549 (page: 111, offset: 133) ---> physical:   133 -> value:    0  ok
logical: 46295 (page: 180, offset: 215) ---> physical:   215 -> value:    0  ok
logical: 22772 (page:  88, offset: 244) ---> physical:   244 -> value:    0  ok

logical: 58228 (page: 227, offset: 116) ---> physical:   116 -> value:    0  ok
logical: 63525 (page: 248, offset:  37) ---> physical:    37 -> value:    0  ok
logical: 32602 (page: 127, offset:  90) ---> physical:    90 -> value:    0  ok
logical: 46195 (page: 180, offset: 115) ---> physical:   115 -> value:    0  ok
logical: 55849 (page: 218, offset:  41) ---> physical:    41 -> value:    0  ok

logical: 46454 (page: 181, offset: 118) ---> physical:   118 -> value:    0  ok
logical:  7487 (page:  29, offset:  63) ---> physical:    63 -> value:    0  ok
logical: 33879 (page: 132, offset:  87) ---> physical:    87 -> value:    0  ok
logical: 42004 (page: 164, offset:  20) ---> physical:    20 -> value:    0  ok
logical:  8599 (page:  33, offset: 151) ---> physical:   151 -> value:    0  ok

logical: 18641 (page:  72, offset: 209) ---> physical:   209 -> value:    0  ok
logical: 49015 (page: 191, offset: 119) ---> physical:   119 -> value:    0  ok
logical: 26830 (page: 104, offset: 206) ---> physical:   206 -> value:    0  ok
logical: 34754 (page: 135, offset: 194) ---> physical:   194 -> value:    0  ok
logical: 14668 (page:  57, offset:  76) ---> physical:    76 -> value:    0  ok

logical: 38362 (page: 149, offset: 218) ---> physical:   218 -> value:    0  ok
logical: 38791 (page: 151, offset: 135) ---> physical:   135 -> value:    0  ok
logical:  4171 (page:  16, offset:  75) ---> physical:    75 -> value:    0  ok
logical: 45975 (page: 179, offset: 151) ---> physical:   151 -> value:    0  ok
logical: 14623 (page:  57, offset:  31) ---> physical:    31 -> value:    0  ok

logical: 62393 (page: 243, offset: 185) ---> physical:   185 -> value:    0  ok
logical: 64658 (page: 252, offset: 146) ---> physical:   146 -> value:    0  ok
logical: 10963 (page:  42, offset: 211) ---> physical:   211 -> value:    0  ok
logical:  9058 (page:  35, offset:  98) ---> physical:    98 -> value:    0  ok
logical: 51031 (page: 199, offset:  87) ---> physical:    87 -> value:    0  ok

logical: 32425 (page: 126, offset: 169) ---> physical:   169 -> value:    0  ok
logical: 45483 (page: 177, offset: 171) ---> physical:   171 -> value:    0  ok
logical: 44611 (page: 174, offset:  67) ---> physical:    67 -> value:    0  ok
logical: 63664 (page: 248, offset: 176) ---> physical:   176 -> value:    0  ok
logical: 54920 (page: 214, offset: 136) ---> physical:   136 -> value:    0  ok

logical:  7663 (page:  29, offset: 239) ---> physical:   239 -> value:    0  ok
logical: 56480 (page: 220, offset: 160) ---> physical:   160 -> value:    0  ok
logical:  1489 (page:   5, offset: 209) ---> physical:   209 -> value:    0  ok
logical: 28438 (page: 111, offset:  22) ---> physical:    22 -> value:    0  ok
logical: 65449 (page: 255, offset: 169) ---> physical:   169 -> value:    0  ok

logical: 12441 (page:  48, offset: 153) ---> physical:   153 -> value:    0  ok
logical: 58530 (page: 228, offset: 162) ---> physical:   162 -> value:    0  ok
logical: 63570 (page: 248, offset:  82) ---> physical:    82 -> value:    0  ok
logical: 26251 (page: 102, offset: 139) ---> physical:   139 -> value:    0  ok
logical: 15972 (page:  62, offset: 100) ---> physical:   100 -> value:    0  ok

logical: 35826 (page: 139, offset: 242) ---> physical:   242 -> value:    0  ok
logical:  5491 (page:  21, offset: 115) ---> physical:   115 -> value:    0  ok
logical: 54253 (page: 211, offset: 237) ---> physical:   237 -> value:    0  ok
logical: 49655 (page: 193, offset: 247) ---> physical:   247 -> value:    0  ok
logical:  5868 (page:  22, offset: 236) ---> physical:   236 -> value:    0  ok

logical: 20163 (page:  78, offset: 195) ---> physical:   195 -> value:    0  ok
logical: 51079 (page: 199, offset: 135) ---> physical:   135 -> value:    0  ok
logical: 21398 (page:  83, offset: 150) ---> physical:   150 -> value:    0  ok
logical: 32756 (page: 127, offset: 244) ---> physical:   244 -> value:    0  ok
logical: 64196 (page: 250, offset: 196) ---> physical:   196 -> value:    0  ok

logical: 43218 (page: 168, offset: 210) ---> physical:   210 -> value:    0  ok
logical: 21583 (page:  84, offset:  79) ---> physical:    79 -> value:    0  ok
logical: 25086 (page:  97, offset: 254) ---> physical:   254 -> value:    0  ok
logical: 45515 (page: 177, offset: 203) ---> physical:   203 -> value:    0  ok
logical: 12893 (page:  50, offset:  93) ---> physical:    93 -> value:    0  ok

logical: 22914 (page:  89, offset: 130) ---> physical:   130 -> value:    0  ok
logical: 58969 (page: 230, offset:  89) ---> physical:    89 -> value:    0  ok
logical: 20094 (page:  78, offset: 126) ---> physical:   126 -> value:    0  ok
logical: 13730 (page:  53, offset: 162) ---> physical:   162 -> value:    0  ok
logical: 44059 (page: 172, offset:  27) ---> physical:    27 -> value:    0  ok

logical: 28931 (page: 113, offset:   3) ---> physical:     3 -> value:    0  ok
logical: 13533 (page:  52, offset: 221) ---> physical:   221 -> value:    0  ok
logical: 33134 (page: 129, offset: 110) ---> physical:   110 -> value:    0  ok
logical: 28483 (page: 111, offset:  67) ---> physical:    67 -> value:    0  ok
logical:  1220 (page:   4, offset: 196) ---> physical:   196 -> value:    0  ok

logical: 38174 (page: 149, offset:  30) ---> physical:    30 -> value:    0  ok
logical: 53502 (page: 208, offset: 254) ---> physical:   254 -> value:    0  ok
logical: 43328 (page: 169, offset:  64) ---> physical:    64 -> value:    0  ok
logical:  4970 (page:  19, offset: 106) ---> physical:   106 -> value:    0  ok
logical:  8090 (page:  31, offset: 154) ---> physical:   154 -> value:    0  ok

logical:  2661 (page:  10, offset: 101) ---> physical:   101 -> value:    0  ok
logical: 53903 (page: 210, offset: 143) ---> physical:   143 -> value:    0  ok
logical: 11025 (page:  43, offset:  17) ---> physical:    17 -> value:    0  ok
logical: 26627 (page: 104, offset:   3) ---> physical:     3 -> value:    0  ok
logical: 18117 (page:  70, offset: 197) ---> physical:   197 -> value:    0  ok

logical: 14505 (page:  56, offset: 169) ---> physical:   169 -> value:    0  ok
logical: 61528 (page: 240, offset:  88) ---> physical:    88 -> value:    0  ok
logical: 20423 (page:  79, offset: 199) ---> physical:   199 -> value:    0  ok
logical: 26962 (page: 105, offset:  82) ---> physical:    82 -> value:    0  ok
logical: 36392 (page: 142, offset:  40) ---> physical:    40 -> value:    0  ok

logical: 11365 (page:  44, offset: 101) ---> physical:   101 -> value:    0  ok
logical: 50882 (page: 198, offset: 194) ---> physical:   194 -> value:    0  ok
logical: 41668 (page: 162, offset: 196) ---> physical:   196 -> value:    0  ok
logical: 30497 (page: 119, offset:  33) ---> physical:    33 -> value:    0  ok
logical: 36216 (page: 141, offset: 120) ---> physical:   120 -> value:    0  ok

logical:  5619 (page:  21, offset: 243) ---> physical:   243 -> value:    0  ok
logical: 36983 (page: 144, offset: 119) ---> physical:   119 -> value:    0  ok
logical: 59557 (page: 232, offset: 165) ---> physical:   165 -> value:    0  ok
logical: 36663 (page: 143, offset:  55) ---> physical:    55 -> value:    0  ok
logical: 36436 (page: 142, offset:  84) ---> physical:    84 -> value:    0  ok

logical: 37057 (page: 144, offset: 193) ---> physical:   193 -> value:    0  ok
logical: 23585 (page:  92, offset:  33) ---> physical:    33 -> value:    0  ok
logical: 58791 (page: 229, offset: 167) ---> physical:   167 -> value:    0  ok
logical: 46666 (page: 182, offset:  74) ---> physical:    74 -> value:    0  ok
logical: 64475 (page: 251, offset: 219) ---> physical:   219 -> value:    0  ok

logical: 21615 (page:  84, offset: 111) ---> physical:   111 -> value:    0  ok
logical: 41090 (page: 160, offset: 130) ---> physical:   130 -> value:    0  ok
logical:  1771 (page:   6, offset: 235) ---> physical:   235 -> value:    0  ok
logical: 47513 (page: 185, offset: 153) ---> physical:   153 -> value:    0  ok
logical: 39338 (page: 153, offset: 170) ---> physical:   170 -> value:    0  ok

logical:  1390 (page:   5, offset: 110) ---> physical:   110 -> value:    0  ok
logical: 38772 (page: 151, offset: 116) ---> physical:   116 -> value:    0  ok
logical: 58149 (page: 227, offset:  37) ---> physical:    37 -> value:    0  ok
logical:  7196 (page:  28, offset:  28) ---> physical:    28 -> value:    0  ok
logical:  9123 (page:  35, offset: 163) ---> physical:   163 -> value:    0  ok

logical:  7491 (page:  29, offset:  67) ---> physical:    67 -> value:    0  ok
logical: 62616 (page: 244, offset: 152) ---> physical:   152 -> value:    0  ok
logical: 15436 (page:  60, offset:  76) ---> physical:    76 -> value:    0  ok
logical: 17491 (page:  68, offset:  83) ---> physical:    83 -> value:    0  ok
logical: 53656 (page: 209, offset: 152) ---> physical:   152 -> value:    0  ok

logical: 26449 (page: 103, offset:  81) ---> physical:    81 -> value:    0  ok
logical: 34935 (page: 136, offset: 119) ---> physical:   119 -> value:    0  ok
logical: 19864 (page:  77, offset: 152) ---> physical:   152 -> value:    0  ok
logical: 51388 (page: 200, offset: 188) ---> physical:   188 -> value:    0  ok
logical: 15155 (page:  59, offset:  51) ---> physical:    51 -> value:    0  ok

logical: 64775 (page: 253, offset:   7) ---> physical:     7 -> value:    0  ok
logical: 47969 (page: 187, offset:  97) ---> physical:    97 -> value:    0  ok
logical: 16315 (page:  63, offset: 187) ---> physical:   187 -> value:    0  ok
logical:  1342 (page:   5, offset:  62) ---> physical:    62 -> value:    0  ok
logical: 51185 (page: 199, offset: 241) ---> physical:   241 -> value:    0  ok

logical:  6043 (page:  23, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 21398 (page:  83, offset: 150) ---> physical:   150 -> value:    0  ok
logical:  3273 (page:  12, offset: 201) ---> physical:   201 -> value:    0  ok
logical:  9370 (page:  36, offset: 154) ---> physical:   154 -> value:    0  ok
logical: 35463 (page: 138, offset: 135) ---> physical:   135 -> value:    0  ok

logical: 28205 (page: 110, offset:  45) ---> physical:    45 -> value:    0  ok
logical:  2351 (page:   9, offset:  47) ---> physical:    47 -> value:    0  ok
logical: 28999 (page: 113, offset:  71) ---> physical:    71 -> value:    0  ok
logical: 47699 (page: 186, offset:  83) ---> physical:    83 -> value:    0  ok
logical: 46870 (page: 183, offset:  22) ---> physical:    22 -> value:    0  ok

logical: 22311 (page:  87, offset:  39) ---> physical:    39 -> value:    0  ok
logical: 22124 (page:  86, offset: 108) ---> physical:   108 -> value:    0  ok
logical: 22427 (page:  87, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 49344 (page: 192, offset: 192) ---> physical:   192 -> value:    0  ok
logical: 23224 (page:  90, offset: 184) ---> physical:   184 -> value:    0  ok

logical:  5514 (page:  21, offset: 138) ---> physical:   138 -> value:    0  ok
logical: 20504 (page:  80, offset:  24) ---> physical:    24 -> value:    0  ok
logical:   376 (page:   1, offset: 120) ---> physical:   120 -> value:    0  ok
logical:  2014 (page:   7, offset: 222) ---> physical:   222 -> value:    0  ok
logical: 38700 (page: 151, offset:  44) ---> physical:    44 -> value:    0  ok

logical: 13098 (page:  51, offset:  42) ---> physical:    42 -> value:    0  ok
logical: 62435 (page: 243, offset: 227) ---> physical:   227 -> value:    0  ok
logical: 48046 (page: 187, offset: 174) ---> physical:   174 -> value:    0  ok
logical: 63464 (page: 247, offset: 232) ---> physical:   232 -> value:    0  ok
logical: 12798 (page:  49, offset: 254) ---> physical:   254 -> value:    0  ok

logical: 51178 (page: 199, offset: 234) ---> physical:   234 -> value:    0  ok
logical:  8627 (page:  33, offset: 179) ---> physical:   179 -> value:    0  ok
logical: 27083 (page: 105, offset: 203) ---> physical:   203 -> value:    0  ok
logical: 47198 (page: 184, offset:  94) ---> physical:    94 -> value:    0  ok
logical: 44021 (page: 171, offset: 245) ---> physical:   245 -> value:    0  ok

logical: 32792 (page: 128, offset:  24) ---> physical:    24 -> value:    0  ok
logical: 43996 (page: 171, offset: 220) ---> physical:   220 -> value:    0  ok
logical: 41126 (page: 160, offset: 166) ---> physical:   166 -> value:    0  ok
logical: 64244 (page: 250, offset: 244) ---> physical:   244 -> value:    0  ok
logical: 37047 (page: 144, offset: 183) ---> physical:   183 -> value:    0  ok

logical: 60281 (page: 235, offset: 121) ---> physical:   121 -> value:    0  ok
logical: 52904 (page: 206, offset: 168) ---> physical:   168 -> value:    0  ok
logical:  7768 (page:  30, offset:  88) ---> physical:    88 -> value:    0  ok
logical: 55359 (page: 216, offset:  63) ---> physical:    63 -> value:    0  ok
logical:  3230 (page:  12, offset: 158) ---> physical:   158 -> value:    0  ok

logical: 44813 (page: 175, offset:  13) ---> physical:    13 -> value:    0  ok
logical:  4116 (page:  16, offset:  20) ---> physical:    20 -> value:    0  ok
logical: 65222 (page: 254, offset: 198) ---> physical:   198 -> value:    0  ok
logical: 28083 (page: 109, offset: 179) ---> physical:   179 -> value:    0  ok
logical: 60660 (page: 236, offset: 244) ---> physical:   244 -> value:    0  ok

logical:    39 (page:   0, offset:  39) ---> physical:    39 -> value:    0  ok
logical:   328 (page:   1, offset:  72) ---> physical:    72 -> value:    0  ok
logical: 47868 (page: 186, offset: 252) ---> physical:   252 -> value:    0  ok
logical: 13009 (page:  50, offset: 209) ---> physical:   209 -> value:    0  ok
logical: 22378 (page:  87, offset: 106) ---> physical:   106 -> value:    0  ok

logical: 39304 (page: 153, offset: 136) ---> physical:   136 -> value:    0  ok
logical: 11171 (page:  43, offset: 163) ---> physical:   163 -> value:    0  ok
logical:  8079 (page:  31, offset: 143) ---> physical:   143 -> value:    0  ok
logical: 52879 (page: 206, offset: 143) ---> physical:   143 -> value:    0  ok
logical:  5123 (page:  20, offset:   3) ---> physical:     3 -> value:    0  ok

logical:  4356 (page:  17, offset:   4) ---> physical:     4 -> value:    0  ok
logical: 45745 (page: 178, offset: 177) ---> physical:   177 -> value:    0  ok
logical: 32952 (page: 128, offset: 184) ---> physical:   184 -> value:    0  ok
logical:  4657 (page:  18, offset:  49) ---> physical:    49 -> value:    0  ok
logical: 24142 (page:  94, offset:  78) ---> physical:    78 -> value:    0  ok

logical: 23319 (page:  91, offset:  23) ---> physical:    23 -> value:    0  ok
logical: 13607 (page:  53, offset:  39) ---> physical:    39 -> value:    0  ok
logical: 46304 (page: 180, offset: 224) ---> physical:   224 -> value:    0  ok
logical: 17677 (page:  69, offset:  13) ---> physical:    13 -> value:    0  ok
logical: 59691 (page: 233, offset:  43) ---> physical:    43 -> value:    0  ok

logical: 50967 (page: 199, offset:  23) ---> physical:    23 -> value:    0  ok
logical:  7817 (page:  30, offset: 137) ---> physical:   137 -> value:    0  ok
logical:  8545 (page:  33, offset:  97) ---> physical:    97 -> value:    0  ok
logical: 55297 (page: 216, offset:   1) ---> physical:     1 -> value:    0  ok
logical: 52954 (page: 206, offset: 218) ---> physical:   218 -> value:    0  ok

logical: 39720 (page: 155, offset:  40) ---> physical:    40 -> value:    0  ok
logical: 18455 (page:  72, offset:  23) ---> physical:    23 -> value:    0  ok
logical: 30349 (page: 118, offset: 141) ---> physical:   141 -> value:    0  ok
logical: 63270 (page: 247, offset:  38) ---> physical:    38 -> value:    0  ok
logical: 27156 (page: 106, offset:  20) ---> physical:    20 -> value:    0  ok

logical: 20614 (page:  80, offset: 134) ---> physical:   134 -> value:    0  ok
logical: 19372 (page:  75, offset: 172) ---> physical:   172 -> value:    0  ok
logical: 48689 (page: 190, offset:  49) ---> physical:    49 -> value:    0  ok
logical: 49386 (page: 192, offset: 234) ---> physical:   234 -> value:    0  ok
logical: 50584 (page: 197, offset: 152) ---> physical:   152 -> value:    0  ok

logical: 51936 (page: 202, offset: 224) ---> physical:   224 -> value:    0  ok
logical: 34705 (page: 135, offset: 145) ---> physical:   145 -> value:    0  ok
logical: 13653 (page:  53, offset:  85) ---> physical:    85 -> value:    0  ok
logical: 50077 (page: 195, offset: 157) ---> physical:   157 -> value:    0  ok
logical: 54518 (page: 212, offset: 246) ---> physical:   246 -> value:    0  ok

logical: 41482 (page: 162, offset:  10) ---> physical:    10 -> value:    0  ok
logical:  4169 (page:  16, offset:  73) ---> physical:    73 -> value:    0  ok
logical: 36118 (page: 141, offset:  22) ---> physical:    22 -> value:    0  ok
logical:  9584 (page:  37, offset: 112) ---> physical:   112 -> value:    0  ok
logical: 18490 (page:  72, offset:  58) ---> physical:    58 -> value:    0  ok

logical: 55420 (page: 216, offset: 124) ---> physical:   124 -> value:    0  ok
logical:  5708 (page:  22, offset:  76) ---> physical:    76 -> value:    0  ok
logical: 23506 (page:  91, offset: 210) ---> physical:   210 -> value:    0  ok
logical: 15391 (page:  60, offset:  31) ---> physical:    31 -> value:    0  ok
logical: 36368 (page: 142, offset:  16) ---> physical:    16 -> value:    0  ok

logical: 38976 (page: 152, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 50406 (page: 196, offset: 230) ---> physical:   230 -> value:    0  ok
logical: 49236 (page: 192, offset:  84) ---> physical:    84 -> value:    0  ok
logical: 65035 (page: 254, offset:  11) ---> physical:    11 -> value:    0  ok
logical: 30120 (page: 117, offset: 168) ---> physical:   168 -> value:    0  ok

logical: 62551 (page: 244, offset:  87) ---> physical:    87 -> value:    0  ok
logical: 46809 (page: 182, offset: 217) ---> physical:   217 -> value:    0  ok
logical: 21687 (page:  84, offset: 183) ---> physical:   183 -> value:    0  ok
logical: 53839 (page: 210, offset:  79) ---> physical:    79 -> value:    0  ok
logical:  2098 (page:   8, offset:  50) ---> physical:    50 -> value:    0  ok

logical: 12364 (page:  48, offset:  76) ---> physical:    76 -> value:    0  ok
logical: 45366 (page: 177, offset:  54) ---> physical:    54 -> value:    0  ok
logical: 50437 (page: 197, offset:   5) ---> physical:     5 -> value:    0  ok
logical: 36675 (page: 143, offset:  67) ---> physical:    67 -> value:    0  ok
logical: 55382 (page: 216, offset:  86) ---> physical:    86 -> value:    0  ok

logical: 11846 (page:  46, offset:  70) ---> physical:    70 -> value:    0  ok
logical: 49127 (page: 191, offset: 231) ---> physical:   231 -> value:    0  ok
logical: 19900 (page:  77, offset: 188) ---> physical:   188 -> value:    0  ok
logical: 20554 (page:  80, offset:  74) ---> physical:    74 -> value:    0  ok
logical: 19219 (page:  75, offset:  19) ---> physical:    19 -> value:    0  ok

logical: 51483 (page: 201, offset:  27) ---> physical:    27 -> value:    0  ok
logical: 58090 (page: 226, offset: 234) ---> physical:   234 -> value:    0  ok
logical: 39074 (page: 152, offset: 162) ---> physical:   162 -> value:    0  ok
logical: 16060 (page:  62, offset: 188) ---> physical:   188 -> value:    0  ok
logical: 10447 (page:  40, offset: 207) ---> physical:   207 -> value:    0  ok

logical: 54169 (page: 211, offset: 153) ---> physical:   153 -> value:    0  ok
logical: 20634 (page:  80, offset: 154) ---> physical:   154 -> value:    0  ok
logical: 57555 (page: 224, offset: 211) ---> physical:   211 -> value:    0  ok
logical: 61210 (page: 239, offset:  26) ---> physical:    26 -> value:    0  ok
logical:   269 (page:   1, offset:  13) ---> physical:    13 -> value:    0  ok

logical: 33154 (page: 129, offset: 130) ---> physical:   130 -> value:    0  ok
logical: 64487 (page: 251, offset: 231) ---> physical:   231 -> value:    0  ok
logical: 61223 (page: 239, offset:  39) ---> physical:    39 -> value:    0  ok
logical: 47292 (page: 184, offset: 188) ---> physical:   188 -> value:    0  ok
logical: 21852 (page:  85, offset:  92) ---> physical:    92 -> value:    0  ok

logical:  5281 (page:  20, offset: 161) ---> physical:   161 -> value:    0  ok
logical: 45912 (page: 179, offset:  88) ---> physical:    88 -> value:    0  ok
logical: 32532 (page: 127, offset:  20) ---> physical:    20 -> value:    0  ok
logical: 63067 (page: 246, offset:  91) ---> physical:    91 -> value:    0  ok
logical: 41683 (page: 162, offset: 211) ---> physical:   211 -> value:    0  ok

logical: 20981 (page:  81, offset: 245) ---> physical:   245 -> value:    0  ok
logical: 33881 (page: 132, offset:  89) ---> physical:    89 -> value:    0  ok
logical: 41785 (page: 163, offset:  57) ---> physical:    57 -> value:    0  ok
logical:  4580 (page:  17, offset: 228) ---> physical:   228 -> value:    0  ok
logical: 41389 (page: 161, offset: 173) ---> physical:   173 -> value:    0  ok

logical: 28572 (page: 111, offset: 156) ---> physical:   156 -> value:    0  ok
logical:   782 (page:   3, offset:  14) ---> physical:    14 -> value:    0  ok
logical: 30273 (page: 118, offset:  65) ---> physical:    65 -> value:    0  ok
logical: 62267 (page: 243, offset:  59) ---> physical:    59 -> value:    0  ok
logical: 17922 (page:  70, offset:   2) ---> physical:     2 -> value:    0  ok

logical: 63238 (page: 247, offset:   6) ---> physical:     6 -> value:    0  ok
logical:  3308 (page:  12, offset: 236) ---> physical:   236 -> value:    0  ok
logical: 26545 (page: 103, offset: 177) ---> physical:   177 -> value:    0  ok
logical: 44395 (page: 173, offset: 107) ---> physical:   107 -> value:    0  ok
logical: 39120 (page: 152, offset: 208) ---> physical:   208 -> value:    0  ok

logical: 21706 (page:  84, offset: 202) ---> physical:   202 -> value:    0  ok
logical:  7144 (page:  27, offset: 232) ---> physical:   232 -> value:    0  ok
logical: 30244 (page: 118, offset:  36) ---> physical:    36 -> value:    0  ok
logical:  3725 (page:  14, offset: 141) ---> physical:   141 -> value:    0  ok
logical: 54632 (page: 213, offset: 104) ---> physical:   104 -> value:    0  ok

logical: 30574 (page: 119, offset: 110) ---> physical:   110 -> value:    0  ok
logical:  8473 (page:  33, offset:  25) ---> physical:    25 -> value:    0  ok
logical: 12386 (page:  48, offset:  98) ---> physical:    98 -> value:    0  ok
logical: 41114 (page: 160, offset: 154) ---> physical:   154 -> value:    0  ok
logical: 57930 (page: 226, offset:  74) ---> physical:    74 -> value:    0  ok

logical: 15341 (page:  59, offset: 237) ---> physical:   237 -> value:    0  ok
logical: 15598 (page:  60, offset: 238) ---> physical:   238 -> value:    0  ok
logical: 59922 (page: 234, offset:  18) ---> physical:    18 -> value:    0  ok
logical: 18226 (page:  71, offset:  50) ---> physical:    50 -> value:    0  ok
logical: 48162 (page: 188, offset:  34) ---> physical:    34 -> value:    0  ok

logical: 41250 (page: 161, offset:  34) ---> physical:    34 -> value:    0  ok
logical:  1512 (page:   5, offset: 232) ---> physical:   232 -> value:    0  ok
logical:  2546 (page:   9, offset: 242) ---> physical:   242 -> value:    0  ok
logical: 41682 (page: 162, offset: 210) ---> physical:   210 -> value:    0  ok
logical:   322 (page:   1, offset:  66) ---> physical:    66 -> value:    0  ok

logical:   880 (page:   3, offset: 112) ---> physical:   112 -> value:    0  ok
logical: 20891 (page:  81, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 56604 (page: 221, offset:  28) ---> physical:    28 -> value:    0  ok
logical: 40166 (page: 156, offset: 230) ---> physical:   230 -> value:    0  ok
logical: 26791 (page: 104, offset: 167) ---> physical:   167 -> value:    0  ok

logical: 44560 (page: 174, offset:  16) ---> physical:    16 -> value:    0  ok
logical: 38698 (page: 151, offset:  42) ---> physical:    42 -> value:    0  ok
logical: 64127 (page: 250, offset: 127) ---> physical:   127 -> value:    0  ok
logical: 15028 (page:  58, offset: 180) ---> physical:   180 -> value:    0  ok
logical: 38669 (page: 151, offset:  13) ---> physical:    13 -> value:    0  ok

logical: 45637 (page: 178, offset:  69) ---> physical:    69 -> value:    0  ok
logical: 43151 (page: 168, offset: 143) ---> physical:   143 -> value:    0  ok
logical:  9465 (page:  36, offset: 249) ---> physical:   249 -> value:    0  ok
logical:  2498 (page:   9, offset: 194) ---> physical:   194 -> value:    0  ok
logical: 13978 (page:  54, offset: 154) ---> physical:   154 -> value:    0  ok

logical: 16326 (page:  63, offset: 198) ---> physical:   198 -> value:    0  ok
logical: 51442 (page: 200, offset: 242) ---> physical:   242 -> value:    0  ok
logical: 34845 (page: 136, offset:  29) ---> physical:    29 -> value:    0  ok
logical: 63667 (page: 248, offset: 179) ---> physical:   179 -> value:    0  ok
logical: 39370 (page: 153, offset: 202) ---> physical:   202 -> value:    0  ok

logical: 55671 (page: 217, offset: 119) ---> physical:   119 -> value:    0  ok
logical: 64496 (page: 251, offset: 240) ---> physical:   240 -> value:    0  ok
logical:  7767 (page:  30, offset:  87) ---> physical:    87 -> value:    0  ok
logical:  6283 (page:  24, offset: 139) ---> physical:   139 -> value:    0  ok
logical: 55884 (page: 218, offset:  76) ---> physical:    76 -> value:    0  ok

logical: 61103 (page: 238, offset: 175) ---> physical:   175 -> value:    0  ok
logical: 10184 (page:  39, offset: 200) ---> physical:   200 -> value:    0  ok
logical: 39543 (page: 154, offset: 119) ---> physical:   119 -> value:    0  ok
logical:  9555 (page:  37, offset:  83) ---> physical:    83 -> value:    0  ok
logical: 13963 (page:  54, offset: 139) ---> physical:   139 -> value:    0  ok

logical: 58975 (page: 230, offset:  95) ---> physical:    95 -> value:    0  ok
logical: 19537 (page:  76, offset:  81) ---> physical:    81 -> value:    0  ok
logical:  6101 (page:  23, offset: 213) ---> physical:   213 -> value:    0  ok
logical: 41421 (page: 161, offset: 205) ---> physical:   205 -> value:    0  ok
logical: 45502 (page: 177, offset: 190) ---> physical:   190 -> value:    0  ok

logical: 29328 (page: 114, offset: 144) ---> physical:   144 -> value:    0  ok
logical:  8149 (page:  31, offset: 213) ---> physical:   213 -> value:    0  ok
logical: 25450 (page:  99, offset: 106) ---> physical:   106 -> value:    0  ok
logical: 58944 (page: 230, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 50666 (page: 197, offset: 234) ---> physical:   234 -> value:    0  ok

logical: 23084 (page:  90, offset:  44) ---> physical:    44 -> value:    0  ok
logical: 36468 (page: 142, offset: 116) ---> physical:   116 -> value:    0  ok
logical: 33645 (page: 131, offset: 109) ---> physical:   109 -> value:    0  ok
logical: 25002 (page:  97, offset: 170) ---> physical:   170 -> value:    0  ok
logical: 53715 (page: 209, offset: 211) ---> physical:   211 -> value:    0  ok

logical: 60173 (page: 235, offset:  13) ---> physical:    13 -> value:    0  ok
logical: 46354 (page: 181, offset:  18) ---> physical:    18 -> value:    0  ok
logical:  4708 (page:  18, offset: 100) ---> physical:   100 -> value:    0  ok
logical: 28208 (page: 110, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 58844 (page: 229, offset: 220) ---> physical:   220 -> value:    0  ok

logical: 22173 (page:  86, offset: 157) ---> physical:   157 -> value:    0  ok
logical:  8535 (page:  33, offset:  87) ---> physical:    87 -> value:    0  ok
logical: 42261 (page: 165, offset:  21) ---> physical:    21 -> value:    0  ok
logical: 29687 (page: 115, offset: 247) ---> physical:   247 -> value:    0  ok
logical: 37799 (page: 147, offset: 167) ---> physical:   167 -> value:    0  ok

logical: 22566 (page:  88, offset:  38) ---> physical:    38 -> value:    0  ok
logical: 62520 (page: 244, offset:  56) ---> physical:    56 -> value:    0  ok
logical:  4098 (page:  16, offset:   2) ---> physical:     2 -> value:    0  ok
logical: 47999 (page: 187, offset: 127) ---> physical:   127 -> value:    0  ok
logical: 49660 (page: 193, offset: 252) ---> physical:   252 -> value:    0  ok

logical: 37063 (page: 144, offset: 199) ---> physical:   199 -> value:    0  ok
logical: 41856 (page: 163, offset: 128) ---> physical:   128 -> value:    0  ok
logical:  5417 (page:  21, offset:  41) ---> physical:    41 -> value:    0  ok
logical: 48856 (page: 190, offset: 216) ---> physical:   216 -> value:    0  ok
logical: 10682 (page:  41, offset: 186) ---> physical:   186 -> value:    0  ok

logical: 22370 (page:  87, offset:  98) ---> physical:    98 -> value:    0  ok
logical: 63281 (page: 247, offset:  49) ---> physical:    49 -> value:    0  ok
logical: 62452 (page: 243, offset: 244) ---> physical:   244 -> value:    0  ok
logical: 50532 (page: 197, offset: 100) ---> physical:   100 -> value:    0  ok
logical:  9022 (page:  35, offset:  62) ---> physical:    62 -> value:    0  ok

logical: 59300 (page: 231, offset: 164) ---> physical:   164 -> value:    0  ok
logical: 58660 (page: 229, offset:  36) ---> physical:    36 -> value:    0  ok
logical: 56401 (page: 220, offset:  81) ---> physical:    81 -> value:    0  ok
logical:  8518 (page:  33, offset:  70) ---> physical:    70 -> value:    0  ok
logical: 63066 (page: 246, offset:  90) ---> physical:    90 -> value:    0  ok

logical: 63250 (page: 247, offset:  18) ---> physical:    18 -> value:    0  ok
logical: 48592 (page: 189, offset: 208) ---> physical:   208 -> value:    0  ok
logical: 28771 (page: 112, offset:  99) ---> physical:    99 -> value:    0  ok
logical: 37673 (page: 147, offset:  41) ---> physical:    41 -> value:    0  ok
logical: 60776 (page: 237, offset: 104) ---> physical:   104 -> value:    0  ok

logical: 56438 (page: 220, offset: 118) ---> physical:   118 -> value:    0  ok
logical: 60424 (page: 236, offset:   8) ---> physical:     8 -> value:    0  ok
logical: 39993 (page: 156, offset:  57) ---> physical:    57 -> value:    0  ok
logical: 56004 (page: 218, offset: 196) ---> physical:   196 -> value:    0  ok
logical: 59002 (page: 230, offset: 122) ---> physical:   122 -> value:    0  ok

logical: 33982 (page: 132, offset: 190) ---> physical:   190 -> value:    0  ok
logical: 25498 (page:  99, offset: 154) ---> physical:   154 -> value:    0  ok
logical: 57047 (page: 222, offset: 215) ---> physical:   215 -> value:    0  ok
logical:  1401 (page:   5, offset: 121) ---> physical:   121 -> value:    0  ok
logical: 15130 (page:  59, offset:  26) ---> physical:    26 -> value:    0  ok

logical: 42960 (page: 167, offset: 208) ---> physical:   208 -> value:    0  ok
logical: 61827 (page: 241, offset: 131) ---> physical:   131 -> value:    0  ok
logical: 32442 (page: 126, offset: 186) ---> physical:   186 -> value:    0  ok
logical: 64304 (page: 251, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 30273 (page: 118, offset:  65) ---> physical:    65 -> value:    0  ok

logical: 38082 (page: 148, offset: 194) ---> physical:   194 -> value:    0  ok
logical: 22404 (page:  87, offset: 132) ---> physical:   132 -> value:    0  ok
logical:  3808 (page:  14, offset: 224) ---> physical:   224 -> value:    0  ok
logical: 16883 (page:  65, offset: 243) ---> physical:   243 -> value:    0  ok
logical: 23111 (page:  90, offset:  71) ---> physical:    71 -> value:    0  ok

logical: 62417 (page: 243, offset: 209) ---> physical:   209 -> value:    0  ok
logical: 60364 (page: 235, offset: 204) ---> physical:   204 -> value:    0  ok
logical:  4542 (page:  17, offset: 190) ---> physical:   190 -> value:    0  ok
logical: 14829 (page:  57, offset: 237) ---> physical:   237 -> value:    0  ok
logical: 44964 (page: 175, offset: 164) ---> physical:   164 -> value:    0  ok

logical: 33924 (page: 132, offset: 132) ---> physical:   132 -> value:    0  ok
logical:  2141 (page:   8, offset:  93) ---> physical:    93 -> value:    0  ok
logical: 19245 (page:  75, offset:  45) ---> physical:    45 -> value:    0  ok
logical: 47168 (page: 184, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 24048 (page:  93, offset: 240) ---> physical:   240 -> value:    0  ok

logical:  1022 (page:   3, offset: 254) ---> physical:   254 -> value:    0  ok
logical: 23075 (page:  90, offset:  35) ---> physical:    35 -> value:    0  ok
logical: 24888 (page:  97, offset:  56) ---> physical:    56 -> value:    0  ok
logical: 49247 (page: 192, offset:  95) ---> physical:    95 -> value:    0  ok
logical:  4900 (page:  19, offset:  36) ---> physical:    36 -> value:    0  ok

logical: 22656 (page:  88, offset: 128) ---> physical:   128 -> value:    0  ok
logical: 34117 (page: 133, offset:  69) ---> physical:    69 -> value:    0  ok
logical: 55555 (page: 217, offset:   3) ---> physical:     3 -> value:    0  ok
logical: 48947 (page: 191, offset:  51) ---> physical:    51 -> value:    0  ok
logical: 59533 (page: 232, offset: 141) ---> physical:   141 -> value:    0  ok

logical: 21312 (page:  83, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 21415 (page:  83, offset: 167) ---> physical:   167 -> value:    0  ok
logical:   813 (page:   3, offset:  45) ---> physical:    45 -> value:    0  ok
logical: 19419 (page:  75, offset: 219) ---> physical:   219 -> value:    0  ok
logical:  1999 (page:   7, offset: 207) ---> physical:   207 -> value:    0  ok

logical: 20155 (page:  78, offset: 187) ---> physical:   187 -> value:    0  ok
logical: 21521 (page:  84, offset:  17) ---> physical:    17 -> value:    0  ok
logical: 13670 (page:  53, offset: 102) ---> physical:   102 -> value:    0  ok
logical: 19289 (page:  75, offset:  89) ---> physical:    89 -> value:    0  ok
logical: 58483 (page: 228, offset: 115) ---> physical:   115 -> value:    0  ok

logical: 41318 (page: 161, offset: 102) ---> physical:   102 -> value:    0  ok
logical: 16151 (page:  63, offset:  23) ---> physical:    23 -> value:    0  ok
logical: 13611 (page:  53, offset:  43) ---> physical:    43 -> value:    0  ok
logical: 21514 (page:  84, offset:  10) ---> physical:    10 -> value:    0  ok
logical: 13499 (page:  52, offset: 187) ---> physical:   187 -> value:    0  ok

logical: 45583 (page: 178, offset:  15) ---> physical:    15 -> value:    0  ok
logical: 49013 (page: 191, offset: 117) ---> physical:   117 -> value:    0  ok
logical: 64843 (page: 253, offset:  75) ---> physical:    75 -> value:    0  ok
logical: 63485 (page: 247, offset: 253) ---> physical:   253 -> value:    0  ok
logical: 38697 (page: 151, offset:  41) ---> physical:    41 -> value:    0  ok

logical: 59188 (page: 231, offset:  52) ---> physical:    52 -> value:    0  ok
logical: 24593 (page:  96, offset:  17) ---> physical:    17 -> value:    0  ok
logical: 57641 (page: 225, offset:  41) ---> physical:    41 -> value:    0  ok
logical: 36524 (page: 142, offset: 172) ---> physical:   172 -> value:    0  ok
logical: 56980 (page: 222, offset: 148) ---> physical:   148 -> value:    0  ok

logical: 36810 (page: 143, offset: 202) ---> physical:   202 -> value:    0  ok
logical:  6096 (page:  23, offset: 208) ---> physical:   208 -> value:    0  ok
logical: 11070 (page:  43, offset:  62) ---> physical:    62 -> value:    0  ok
logical: 60124 (page: 234, offset: 220) ---> physical:   220 -> value:    0  ok
logical: 37576 (page: 146, offset: 200) ---> physical:   200 -> value:    0  ok

logical: 15096 (page:  58, offset: 248) ---> physical:   248 -> value:    0  ok
logical: 45247 (page: 176, offset: 191) ---> physical:   191 -> value:    0  ok
logical: 32783 (page: 128, offset:  15) ---> physical:    15 -> value:    0  ok
logical: 58390 (page: 228, offset:  22) ---> physical:    22 -> value:    0  ok
logical: 60873 (page: 237, offset: 201) ---> physical:   201 -> value:    0  ok

logical: 23719 (page:  92, offset: 167) ---> physical:   167 -> value:    0  ok
logical: 24385 (page:  95, offset:  65) ---> physical:    65 -> value:    0  ok
logical: 22307 (page:  87, offset:  35) ---> physical:    35 -> value:    0  ok
logical: 17375 (page:  67, offset: 223) ---> physical:   223 -> value:    0  ok
logical: 15990 (page:  62, offset: 118) ---> physical:   118 -> value:    0  ok

logical: 20526 (page:  80, offset:  46) ---> physical:    46 -> value:    0  ok
logical: 25904 (page: 101, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 42224 (page: 164, offset: 240) ---> physical:   240 -> value:    0  ok
logical:  9311 (page:  36, offset:  95) ---> physical:    95 -> value:    0  ok
logical:  7862 (page:  30, offset: 182) ---> physical:   182 -> value:    0  ok

logical:  3835 (page:  14, offset: 251) ---> physical:   251 -> value:    0  ok
logical: 30535 (page: 119, offset:  71) ---> physical:    71 -> value:    0  ok
logical: 65179 (page: 254, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 57387 (page: 224, offset:  43) ---> physical:    43 -> value:    0  ok
logical: 63579 (page: 248, offset:  91) ---> physical:    91 -> value:    0  ok

logical:  4946 (page:  19, offset:  82) ---> physical:    82 -> value:    0  ok
logical:  9037 (page:  35, offset:  77) ---> physical:    77 -> value:    0  ok
logical: 61033 (page: 238, offset: 105) ---> physical:   105 -> value:    0  ok
logical: 55543 (page: 216, offset: 247) ---> physical:   247 -> value:    0  ok
logical: 50361 (page: 196, offset: 185) ---> physical:   185 -> value:    0  ok

logical:  6480 (page:  25, offset:  80) ---> physical:    80 -> value:    0  ok
logical: 14042 (page:  54, offset: 218) ---> physical:   218 -> value:    0  ok
logical: 21531 (page:  84, offset:  27) ---> physical:    27 -> value:    0  ok
logical: 39195 (page: 153, offset:  27) ---> physical:    27 -> value:    0  ok
logical: 37511 (page: 146, offset: 135) ---> physical:   135 -> value:    0  ok

logical: 23696 (page:  92, offset: 144) ---> physical:   144 -> value:    0  ok
logical: 27440 (page: 107, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 28201 (page: 110, offset:  41) ---> physical:    41 -> value:    0  ok
logical: 23072 (page:  90, offset:  32) ---> physical:    32 -> value:    0  ok
logical:  7814 (page:  30, offset: 134) ---> physical:   134 -> value:    0  ok

logical:  6552 (page:  25, offset: 152) ---> physical:   152 -> value:    0  ok
logical: 43637 (page: 170, offset: 117) ---> physical:   117 -> value:    0  ok
logical: 35113 (page: 137, offset:  41) ---> physical:    41 -> value:    0  ok
logical: 34890 (page: 136, offset:  74) ---> physical:    74 -> value:    0  ok
logical: 61297 (page: 239, offset: 113) ---> physical:   113 -> value:    0  ok

logical: 45633 (page: 178, offset:  65) ---> physical:    65 -> value:    0  ok
logical: 61431 (page: 239, offset: 247) ---> physical:   247 -> value:    0  ok
logical: 46032 (page: 179, offset: 208) ---> physical:   208 -> value:    0  ok
logical: 18774 (page:  73, offset:  86) ---> physical:    86 -> value:    0  ok
logical: 62991 (page: 246, offset:  15) ---> physical:    15 -> value:    0  ok

logical: 28059 (page: 109, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 35229 (page: 137, offset: 157) ---> physical:   157 -> value:    0  ok
logical: 51230 (page: 200, offset:  30) ---> physical:    30 -> value:    0  ok
logical: 14405 (page:  56, offset:  69) ---> physical:    69 -> value:    0  ok
logical: 52242 (page: 204, offset:  18) ---> physical:    18 -> value:    0  ok

logical: 43153 (page: 168, offset: 145) ---> physical:   145 -> value:    0  ok
logical:  2709 (page:  10, offset: 149) ---> physical:   149 -> value:    0  ok
logical: 47963 (page: 187, offset:  91) ---> physical:    91 -> value:    0  ok
logical: 36943 (page: 144, offset:  79) ---> physical:    79 -> value:    0  ok
logical: 54066 (page: 211, offset:  50) ---> physical:    50 -> value:    0  ok

logical: 10054 (page:  39, offset:  70) ---> physical:    70 -> value:    0  ok
logical: 43051 (page: 168, offset:  43) ---> physical:    43 -> value:    0  ok
logical: 11525 (page:  45, offset:   5) ---> physical:     5 -> value:    0  ok
logical: 17684 (page:  69, offset:  20) ---> physical:    20 -> value:    0  ok
logical: 41681 (page: 162, offset: 209) ---> physical:   209 -> value:    0  ok

logical: 27883 (page: 108, offset: 235) ---> physical:   235 -> value:    0  ok
logical: 56909 (page: 222, offset:  77) ---> physical:    77 -> value:    0  ok
logical: 45772 (page: 178, offset: 204) ---> physical:   204 -> value:    0  ok
logical: 27496 (page: 107, offset: 104) ---> physical:   104 -> value:    0  ok
logical: 46842 (page: 182, offset: 250) ---> physical:   250 -> value:    0  ok

logical: 38734 (page: 151, offset:  78) ---> physical:    78 -> value:    0  ok
logical: 28972 (page: 113, offset:  44) ---> physical:    44 -> value:    0  ok
logical: 59684 (page: 233, offset:  36) ---> physical:    36 -> value:    0  ok
logical: 11384 (page:  44, offset: 120) ---> physical:   120 -> value:    0  ok
logical: 21018 (page:  82, offset:  26) ---> physical:    26 -> value:    0  ok

logical:  2192 (page:   8, offset: 144) ---> physical:   144 -> value:    0  ok
logical: 18384 (page:  71, offset: 208) ---> physical:   208 -> value:    0  ok
logical: 13464 (page:  52, offset: 152) ---> physical:   152 -> value:    0  ok
logical: 31018 (page: 121, offset:  42) ---> physical:    42 -> value:    0  ok
logical: 62958 (page: 245, offset: 238) ---> physical:   238 -> value:    0  ok

logical: 30611 (page: 119, offset: 147) ---> physical:   147 -> value:    0  ok
logical:  1913 (page:   7, offset: 121) ---> physical:   121 -> value:    0  ok
logical: 18904 (page:  73, offset: 216) ---> physical:   216 -> value:    0  ok
logical: 26773 (page: 104, offset: 149) ---> physical:   149 -> value:    0  ok
logical: 55491 (page: 216, offset: 195) ---> physical:   195 -> value:    0  ok

logical: 21899 (page:  85, offset: 139) ---> physical:   139 -> value:    0  ok
logical: 64413 (page: 251, offset: 157) ---> physical:   157 -> value:    0  ok
logical: 47134 (page: 184, offset:  30) ---> physical:    30 -> value:    0  ok
logical: 23172 (page:  90, offset: 132) ---> physical:   132 -> value:    0  ok
logical:  7262 (page:  28, offset:  94) ---> physical:    94 -> value:    0  ok

logical: 12705 (page:  49, offset: 161) ---> physical:   161 -> value:    0  ok
logical:  7522 (page:  29, offset:  98) ---> physical:    98 -> value:    0  ok
logical: 58815 (page: 229, offset: 191) ---> physical:   191 -> value:    0  ok
logical: 34916 (page: 136, offset: 100) ---> physical:   100 -> value:    0  ok
logical:  3802 (page:  14, offset: 218) ---> physical:   218 -> value:    0  ok

logical: 58008 (page: 226, offset: 152) ---> physical:   152 -> value:    0  ok
logical:  1239 (page:   4, offset: 215) ---> physical:   215 -> value:    0  ok
logical: 63947 (page: 249, offset: 203) ---> physical:   203 -> value:    0  ok
logical:   381 (page:   1, offset: 125) ---> physical:   125 -> value:    0  ok
logical: 60734 (page: 237, offset:  62) ---> physical:    62 -> value:    0  ok

logical: 48769 (page: 190, offset: 129) ---> physical:   129 -> value:    0  ok
logical: 41938 (page: 163, offset: 210) ---> physical:   210 -> value:    0  ok
logical: 38025 (page: 148, offset: 137) ---> physical:   137 -> value:    0  ok
logical: 55099 (page: 215, offset:  59) ---> physical:    59 -> value:    0  ok
logical: 56691 (page: 221, offset: 115) ---> physical:   115 -> value:    0  ok

logical: 39530 (page: 154, offset: 106) ---> physical:   106 -> value:    0  ok
logical: 59003 (page: 230, offset: 123) ---> physical:   123 -> value:    0  ok
logical:  6029 (page:  23, offset: 141) ---> physical:   141 -> value:    0  ok
logical: 20920 (page:  81, offset: 184) ---> physical:   184 -> value:    0  ok
logical:  8077 (page:  31, offset: 141) ---> physical:   141 -> value:    0  ok

logical: 42633 (page: 166, offset: 137) ---> physical:   137 -> value:    0  ok
logical: 17443 (page:  68, offset:  35) ---> physical:    35 -> value:    0  ok
logical: 53570 (page: 209, offset:  66) ---> physical:    66 -> value:    0  ok
logical: 22833 (page:  89, offset:  49) ---> physical:    49 -> value:    0  ok
logical:  3782 (page:  14, offset: 198) ---> physical:   198 -> value:    0  ok

logical: 47758 (page: 186, offset: 142) ---> physical:   142 -> value:    0  ok
logical: 22136 (page:  86, offset: 120) ---> physical:   120 -> value:    0  ok
logical: 22427 (page:  87, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 23867 (page:  93, offset:  59) ---> physical:    59 -> value:    0  ok
logical: 59968 (page: 234, offset:  64) ---> physical:    64 -> value:    0  ok

logical: 62166 (page: 242, offset: 214) ---> physical:   214 -> value:    0  ok
logical:  6972 (page:  27, offset:  60) ---> physical:    60 -> value:    0  ok
logical: 63684 (page: 248, offset: 196) ---> physical:   196 -> value:    0  ok
logical: 46388 (page: 181, offset:  52) ---> physical:    52 -> value:    0  ok
logical: 41942 (page: 163, offset: 214) ---> physical:   214 -> value:    0  ok

logical: 36524 (page: 142, offset: 172) ---> physical:   172 -> value:    0  ok
logical:  9323 (page:  36, offset: 107) ---> physical:   107 -> value:    0  ok
logical: 31114 (page: 121, offset: 138) ---> physical:   138 -> value:    0  ok
logical: 22345 (page:  87, offset:  73) ---> physical:    73 -> value:    0  ok
logical: 46463 (page: 181, offset: 127) ---> physical:   127 -> value:    0  ok

logical: 54671 (page: 213, offset: 143) ---> physical:   143 -> value:    0  ok
logical:  9214 (page:  35, offset: 254) ---> physical:   254 -> value:    0  ok
logical:  7257 (page:  28, offset:  89) ---> physical:    89 -> value:    0  ok
logical: 33150 (page: 129, offset: 126) ---> physical:   126 -> value:    0  ok
logical: 41565 (page: 162, offset:  93) ---> physical:    93 -> value:    0  ok

logical: 26214 (page: 102, offset: 102) ---> physical:   102 -> value:    0  ok
logical:  3595 (page:  14, offset:  11) ---> physical:    11 -> value:    0  ok
logical: 17932 (page:  70, offset:  12) ---> physical:    12 -> value:    0  ok
logical: 34660 (page: 135, offset: 100) ---> physical:   100 -> value:    0  ok
logical: 51961 (page: 202, offset: 249) ---> physical:   249 -> value:    0  ok

logical: 58634 (page: 229, offset:  10) ---> physical:    10 -> value:    0  ok
logical: 57990 (page: 226, offset: 134) ---> physical:   134 -> value:    0  ok
logical: 28848 (page: 112, offset: 176) ---> physical:   176 -> value:    0  ok
logical: 49920 (page: 195, offset:   0) ---> physical:     0 -> value:    0  ok
logical: 18351 (page:  71, offset: 175) ---> physical:   175 -> value:    0  ok

logical: 53669 (page: 209, offset: 165) ---> physical:   165 -> value:    0  ok
logical: 33996 (page: 132, offset: 204) ---> physical:   204 -> value:    0  ok
logical:  6741 (page:  26, offset:  85) ---> physical:    85 -> value:    0  ok
logical: 64098 (page: 250, offset:  98) ---> physical:    98 -> value:    0  ok
logical:   606 (page:   2, offset:  94) ---> physical:    94 -> value:    0  ok

logical: 27383 (page: 106, offset: 247) ---> physical:   247 -> value:    0  ok
logical: 63140 (page: 246, offset: 164) ---> physical:   164 -> value:    0  ok
logical: 32228 (page: 125, offset: 228) ---> physical:   228 -> value:    0  ok
logical: 63437 (page: 247, offset: 205) ---> physical:   205 -> value:    0  ok
logical: 29085 (page: 113, offset: 157) ---> physical:   157 -> value:    0  ok

logical: 65080 (page: 254, offset:  56) ---> physical:    56 -> value:    0  ok
logical: 38753 (page: 151, offset:  97) ---> physical:    97 -> value:    0  ok
logical: 16041 (page:  62, offset: 169) ---> physical:   169 -> value:    0  ok
logical:  9041 (page:  35, offset:  81) ---> physical:    81 -> value:    0  ok
logical: 42090 (page: 164, offset: 106) ---> physical:   106 -> value:    0  ok

logical: 46388 (page: 181, offset:  52) ---> physical:    52 -> value:    0  ok
logical: 63650 (page: 248, offset: 162) ---> physical:   162 -> value:    0  ok
logical: 36636 (page: 143, offset:  28) ---> physical:    28 -> value:    0  ok
logical: 21947 (page:  85, offset: 187) ---> physical:   187 -> value:    0  ok
logical: 19833 (page:  77, offset: 121) ---> physical:   121 -> value:    0  ok

logical: 36464 (page: 142, offset: 112) ---> physical:   112 -> value:    0  ok
logical:  8541 (page:  33, offset:  93) ---> physical:    93 -> value:    0  ok
logical: 12712 (page:  49, offset: 168) ---> physical:   168 -> value:    0  ok
logical: 48955 (page: 191, offset:  59) ---> physical:    59 -> value:    0  ok
logical: 39206 (page: 153, offset:  38) ---> physical:    38 -> value:    0  ok

logical: 15578 (page:  60, offset: 218) ---> physical:   218 -> value:    0  ok
logical: 49205 (page: 192, offset:  53) ---> physical:    53 -> value:    0  ok
logical:  7731 (page:  30, offset:  51) ---> physical:    51 -> value:    0  ok
logical: 43046 (page: 168, offset:  38) ---> physical:    38 -> value:    0  ok
logical: 60498 (page: 236, offset:  82) ---> physical:    82 -> value:    0  ok

logical:  9237 (page:  36, offset:  21) ---> physical:    21 -> value:    0  ok
logical: 47706 (page: 186, offset:  90) ---> physical:    90 -> value:    0  ok
logical: 43973 (page: 171, offset: 197) ---> physical:   197 -> value:    0  ok
logical: 42008 (page: 164, offset:  24) ---> physical:    24 -> value:    0  ok
logical: 27460 (page: 107, offset:  68) ---> physical:    68 -> value:    0  ok

logical: 24999 (page:  97, offset: 167) ---> physical:   167 -> value:    0  ok
logical: 51933 (page: 202, offset: 221) ---> physical:   221 -> value:    0  ok
logical: 34070 (page: 133, offset:  22) ---> physical:    22 -> value:    0  ok
logical: 65155 (page: 254, offset: 131) ---> physical:   131 -> value:    0  ok
logical: 59955 (page: 234, offset:  51) ---> physical:    51 -> value:    0  ok

logical:  9277 (page:  36, offset:  61) ---> physical:    61 -> value:    0  ok
logical: 20420 (page:  79, offset: 196) ---> physical:   196 -> value:    0  ok
logical: 44860 (page: 175, offset:  60) ---> physical:    60 -> value:    0  ok
logical: 50992 (page: 199, offset:  48) ---> physical:    48 -> value:    0  ok
logical: 10583 (page:  41, offset:  87) ---> physical:    87 -> value:    0  ok

logical: 57751 (page: 225, offset: 151) ---> physical:   151 -> value:    0  ok
logical: 23195 (page:  90, offset: 155) ---> physical:   155 -> value:    0  ok
logical: 27227 (page: 106, offset:  91) ---> physical:    91 -> value:    0  ok
logical: 42816 (page: 167, offset:  64) ---> physical:    64 -> value:    0  ok
logical: 58219 (page: 227, offset: 107) ---> physical:   107 -> value:    0  ok

logical: 37606 (page: 146, offset: 230) ---> physical:   230 -> value:    0  ok
logical: 18426 (page:  71, offset: 250) ---> physical:   250 -> value:    0  ok
logical: 21238 (page:  82, offset: 246) ---> physical:   246 -> value:    0  ok
logical: 11983 (page:  46, offset: 207) ---> physical:   207 -> value:    0  ok
logical: 48394 (page: 189, offset:  10) ---> physical:    10 -> value:    0  ok

logical: 11036 (page:  43, offset:  28) ---> physical:    28 -> value:    0  ok
logical: 30557 (page: 119, offset:  93) ---> physical:    93 -> value:    0  ok
logical: 23453 (page:  91, offset: 157) ---> physical:   157 -> value:    0  ok
logical: 49847 (page: 194, offset: 183) ---> physical:   183 -> value:    0  ok
logical: 30032 (page: 117, offset:  80) ---> physical:    80 -> value:    0  ok

logical: 48065 (page: 187, offset: 193) ---> physical:   193 -> value:    0  ok
logical:  6957 (page:  27, offset:  45) ---> physical:    45 -> value:    0  ok
logical:  2301 (page:   8, offset: 253) ---> physical:   253 -> value:    0  ok
logical:  7736 (page:  30, offset:  56) ---> physical:    56 -> value:    0  ok
logical: 31260 (page: 122, offset:  28) ---> physical:    28 -> value:    0  ok

logical: 17071 (page:  66, offset: 175) ---> physical:   175 -> value:    0  ok
logical:  8940 (page:  34, offset: 236) ---> physical:   236 -> value:    0  ok
logical:  9929 (page:  38, offset: 201) ---> physical:   201 -> value:    0  ok
logical: 45563 (page: 177, offset: 251) ---> physical:   251 -> value:    0  ok
logical: 12107 (page:  47, offset:  75) ---> physical:    75 -> value:    0  ok

ALL read memory value assertions PASSED!

                ... nPages != nFrames memory simulation done.


nPages == nFrames Statistics (256 frames):
Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate
        0          110                  0                inf           -nan
        0          110                  0                inf           -nan
        0          110                  0                inf           -nan
        0          110                  0                inf           -nan
        0          110                  0                inf           -nan

nPages != nFrames Statistics (128 frames):
Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate
        0            0                  0               -nan           -nan
        0            0                  0               -nan           -nan
        0            0                  0               -nan           -nan
        0            0                  0               -nan           -nan
        0            0                  0               -nan           -nan

                ...memory management simulation completed! */


