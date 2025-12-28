#include <unistd.h>  // Required for sbrk
#include <stddef.h>  // Required for size_t (or use <stdlib.h>)
#include <pthread.h> // For pthread_mutex_lock, pthread_mutex_t
typedef char ALIGN[16];

pthread_mutex_t global_malloc_lock;
header_t *head, *tail;

struct header_t {
    size_t size;
    unsigned is_free;
    struct header_t* next;
};

//union is like a struct but it can  force a specific size ... in this case 16 bytes
union header {
    struct  {
    size_t size;
    unsigned is_free;
    struct header_t* next;
    }s;

    //the structure stretcher
    ALIGN stub;
};

typedef union header header_t;


void *malloc(size_t size){

    size_t total_size ;
    void* block;
    header_t* header;

    if (!size) return NULL;

    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);

    if (header){
        header->s.is_free = 0 ;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header+1);
    }

    total_size = sizeof(header_t)+size;
    
    //sbrk-> set pointer break ... increase heap size by "size" amount 
    block = sbrk(size);

    //void* is for type casting -1 into a void pointer
    if (block== (void*)-1){ 
        pthread_mutex_lock(&global_malloc_lock);
        return NULL;
    }
    header = block;
    header->s.size = size;
    header->s.is_free = 0 ;
    header->s.next = NULL;

    if (!head) head = header;

    if (tail) tail->s.next = header;

    tail = header;

	pthread_mutex_unlock(&global_malloc_lock);
	return (void*)(header + 1);
}

header_t* get_free_block(size_t size){
    header_t* curr = head ; 
    while (curr) {
        if (curr->s.is_free && curr->s.size >=size)
            return curr;
        curr = curr->s.next;
    }
    return NULL;
}