// hash_table.c
#include <stdlib.h>
#include <string.h>

#include "hash_table.h"

//const char* k means: "A pointer to the beginning of a string representing the key."
//const char* v means: "A pointer to the beginning of a string representing the value."
static ht_item* ht_new_item(const char* k, const char* v) {
    //malloc--> memory allocation | calcs the no of bytes needed for ht_item struct
    ht_item* i = malloc(sizeof(ht_item));

    //strup | string duplicate | duplicates the string passed to it and returns a pointer to the duplicated string
    i->key = strdup(k);
    i->value = strdup(v);

    //returns the adress 
    return i;
}


ht_hash_table* ht_new() {
    //this is the adress , not the actual struct
    ht_hash_table* ht = malloc(sizeof(ht_hash_table));

    ht->size = 53;
    ht->count = 0;
    //size_t is a special type of integer! this tells the coompiler
    //"Treat the integer value in ht->size as a size_t value for this function call."
    //53* 4bytes for the adress list of items!
    ht->items = calloc((size_t)ht->size, sizeof(ht_item*));
    return ht;
}


static void ht_del_item(ht_item* i) {
    //undoing what ever malloc did and strdup did , if not this will call memory leaks
    free(i->key);
    free(i->value);
    free(i);
}


void ht_del_hash_table(ht_hash_table* ht) {
    //this items is a adress to where a bunch of other adresses are stored
    // in c , this bunch of adresses is treated as an array
    //so we can use array syntax! items[i]
    for (int i = 0; i < ht->size; i++) {
        ht_item* item = ht->items[i];
        if (item != NULL) {
            ht_del_item(item);
        }
    }
    //after all the adresses in the bunch is freed , you need to free the bunch itself
    free(ht->items);
    //and free the strcut as well
    free(ht);
}

//why do we hash?? hashing produces a random integer for the certain string , 
//with these int , we have the power of SUPER FAST LOOPUP times ,cause strings are slower than ints
static int ht_hash(const char* s , const int a,const int m ){
    long hash = 0;
    //strlrn checks the length of the string s
    const int len_s = strlen(s);
    //calculates a value that is somewhat random based on the string s
    for (int i = 0; i < len_s; i++) {
        hash += (long)pow(a, len_s - (i + 1)) * s[i];
        hash = hash % m;
    }
    //the somewhat random value is between 0 and m-1 (53 in this casee)
    return (int)hash;
}
//it is inevitable that the same hash value be in 2 or more different strings
//so in the saem hash(bucket) there may be multiple items
