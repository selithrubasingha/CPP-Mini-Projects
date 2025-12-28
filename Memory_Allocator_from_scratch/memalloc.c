#include <unistd.h>  // Required for sbrk
#include <stddef.h>  // Required for size_t (or use <stdlib.h>)

typedef char ALIGN[16];

struct header_t {
    size_t size;
    unsigned is_free;
    struct header_t* next;
};

union header {
    struct  {
    size_t size;
    unsigned is_free;
    struct header_t* next;
    }s;

    ALIGN stub;
};

typedef union header header_t;


void *malloc(size_t size){
    void* block;

    //sbrk-> set pointer break ... increase heap size by "size" amount 
    block = sbrk(size);

    //void* is for type casting -1 into a void pointer
    if (block== (void*)-1) return NULL;

    return block ;
}