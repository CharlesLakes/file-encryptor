/**
 * @file    vector.c
 * @brief   Implementation of the generic dynamic array (see vector.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "vector.h"
#include <stdlib.h>

/**
 * @brief Increments the size by one, doubling the capacity when it is full.
 * @param vec Vector.
 */
void grow(vector* vec){
    if(vec->n == vec->m){
        vec->m *= 2;
        char* new_arr = (char *) malloc(vec->byte_size*vec->m);
        char* old_arr = (char *) vec->arr;
        for(int i = 0; i < vec->n; i++)
            for(int j = 0; j < vec->byte_size; j++)
                new_arr[vec->byte_size*i + j] = old_arr[vec->byte_size*i + j];
        free(vec->arr);
        vec->arr = (void *) new_arr;
    }
    vec->n++;
}

vector create_vector(unsigned int byte_size){

    vector vec;
    vec.n = 0;
    vec.m = 1;
    vec.byte_size = byte_size;
    
    vec.arr = (void *) malloc(byte_size * vec.m);

    return vec;
}

void push_back_vector(vector *vec, void* element){
    int pos = vec->n;
    grow(vec);
    char* bytes = (char *) element;
    char* current_arr = (char *) vec->arr;
    for(int i = 0; i < vec->byte_size; i++)
        current_arr[vec->byte_size*pos + i] = bytes[i];
}

void* get_value_vector(vector *vec,unsigned int index){
    char* current_arr = (char *) vec->arr;
    return (void *) &current_arr[vec->byte_size*index];
}

void free_vector(vector *vec){
    free(vec->arr);
}

