#include <unistd.h>  // Required for sbrk
#include <stddef.h>  // Required for size_t (or use <stdlib.h>)
#include <pthread.h> // For pthread_mutex_lock, pthread_mutex_t
#include <unistd.h>
#include <string.h>
typedef char ALIGN[16];

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

header_t *head, *tail;
pthread_mutex_t global_malloc_lock;

header_t* get_free_block(size_t size);

struct header_t {
    size_t size;
    unsigned is_free;
    struct header_t* next;
};






void *malloc(size_t size){

    //for testing purposes
    char msg[] = "Malloc was called!\n";
    write(1, msg, strlen(msg));

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
    block = sbrk(total_size);

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

void free(void* block){

    header_t *header , *tmp ; 
    void *programbreak;

    if (!block) return ; 

    pthread_mutex_lock(&global_malloc_lock);
    /*
    we are given the block pointer ... but t=we need the header pointer 
    which actually 1 memory adress minus the block (16 bytes minus the block actually)
    when we do -1 ... the compiler sees the header_t type and automatically substracts the size of 1 header_t 
    */
    header = (header_t*)block -1;

    programbreak = sbrk(0);
    /*
    if the block is in the edge of the heap.
    last_adress = first_adress + size ::: (char*) block + header->s.zie ::: these are identical
    why use char* ? because char means 1 byte (int is 4 and so on ...) 
    we tell the computer the block is char adress and the computer adds the s.size amount multiplied by the char size which is one this case
    if it were int then block + size*4 !!
    */
    if ((char*)block +header->s.size == programbreak){
        //changing the head and the tail accoridnlgy
        if (head == tail){
            head = tail = NULL;
        }else {
            tmp = head ; 
            while (tmp){
                if (tmp->s.next == tail){
                    tmp->s.next = NULL;
                    tail = tmp;
                }
                tmp = tmp->s.next;
            }
        }
        sbrk(0 - sizeof(header_t) - header->s.size);
        pthread_mutex_unlock(&global_malloc_lock);
        return;
    }

    /*if the mem block is not at the adge ... then we just mark that block as is_free
    later when the program exits we remove all of them ... or during the program time .. we just overwrite 
    the data on that block
    */
    header->s.is_free = 1 ;
    pthread_mutex_unlock(&global_malloc_lock);
}

void *calloc(size_t num , size_t nsize){
    /*
    calloc takes in arraylike data item , and no_of_items and allocates memory for the array items
    calloc allocates memory and also fills it with 0 bytes 
    */
    size_t size;
    void *block;

    if (!num || !nsize) return NULL;

    size = num * nsize ;

    /*
    if a*b=c then c/b=a , if size were to be too large overflow ...and give the wrong answer
    we can check if the multipication was fine with the following code.
    */
    if ((nsize != size / num)) return NULL;

    block = malloc (size);

    if (!block) return NULL;

    /*
    with memset we set the allocated memory area with 0 bytes (not 0 int)
    it's like pre cleaning the allocated area ! befpre using it
    in the normal malloc , the allocated area might have garbage data inside which will be over written
    */
    memset(block,0,size);
    return block ;
}

void *realloc(void *block , size_t size){
    header_t* header;
    void* ret;

    if (!block || !size) return malloc(size);

    header = (header_t*) block -1 ;

    /*
    if the reallocated size is smaller than the current size there is not actually anything to do 
    i mean the current size could fit a smaller size...
    */
    if (header->s.size >= size) return block;

    ret = malloc(size);

    /*
    if the size is bigger , then we should malloc a new area in the heap 
    and cpy all the from the old block to the new area
    */
    if (ret){

        memcpy(ret , block , header->s.size);
        free(block);
    }

    return ret;
}
