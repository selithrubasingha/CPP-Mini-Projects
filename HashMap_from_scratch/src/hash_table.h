
//this is not the normal way of defining structs, the typedef word is used
//so now we won't need to write struct this and struct that when making a specific struct
//simply we don't need to write "struct" word often
typedef struct {
    int* key;
    int* value;
} ht_item;

typedef struct {
    int* key;
    int* value;
    //pointer to a pointer
    ht_item** items;
} ht_hash_table;

//double pointers are used in multidimensional arrays
//"he_hash_table" contains the adress to a place where all the other adresses of hit_items are sotres!
