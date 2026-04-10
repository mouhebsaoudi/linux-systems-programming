#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>


typedef struct block{
    size_t size;
    struct block* next;
    int free; 
} block;


#define ALIGN8(size) (((size) + 7) & ~7)

static block *head = NULL; 


/*
 * extend_heap_size:
 */

void *extend_heap_size(size_t size) {
    void *current_base = sbrk(0);
    void *extended = sbrk(size);
    if (extended == (void *)-1) {
        return NULL; // Handle sbrk failure
    }
    return current_base;
}

block* find_free_block(block** prev, size_t size){
    block* block = head;
    while(block){
        if(block ->free && block->size >= size){
            break;
        }
        *prev = block;
        block = block->next;
    }
    return block;
}

block* create_new_block(block* last_block, size_t size){
    block* new_block = extend_heap_size(size +  sizeof(block));
    if(!new_block){
        return NULL;
    }
    new_block->size = size;
    new_block->free =0;
    new_block->next = NULL;

    if(last_block){
        last_block->next = new_block;
    }
    
    return new_block;
}

void split_large_block(block* large_block, size_t size){
    if(large_block->size>= size +  sizeof(block) +8){
          block* new_block = (block *)((char *) large_block +  sizeof(block) + size);
          new_block->size = large_block->size -size -  sizeof(block);
          new_block->free =1;
          new_block->next = large_block->next;
          large_block->size = size;
          large_block->next = new_block;
          
    }
}

void *malloc(size_t size) {

    size = ALIGN8(size);
    block* my_block;
    if(!head){
        my_block = create_new_block(NULL, size);
        head = my_block;
    }
    else{
        block* last_block = head;
        my_block = find_free_block(&last_block, size);
        if(my_block){
            my_block->free =0;
            split_large_block(my_block,size);
        }
        else{
            my_block = create_new_block(last_block,size);
        }
    }
  if(my_block){
    return my_block +1;
  }
  return NULL;
}

void *calloc(size_t nitems, size_t nsize) {
   
   size_t total_size= nitems* nsize;
   if(total_size ==0){
    return NULL;
   }
   void* ptr = malloc(total_size);
   if(ptr){

    memset(ptr, 0, ALIGN8(total_size));
   }
  

  return ptr;
}

void free(void *ptr) {
  
  if(ptr){
    block* bloc_tofree = (block*)ptr -1;
    bloc_tofree->free =1;
  }

  block* current_block = head;
  while(current_block){
    if(current_block->free && current_block->next && current_block->next->free){
        current_block->size = current_block->size + current_block->next->size +  sizeof(block);
        current_block->next = current_block->next->next;
    }
    current_block =current_block->next;
  }
}

void *realloc(void *ptr, size_t size) {

  if(ptr){
    block* my_block = (block*)ptr-1;
    if(my_block->size>= size){
        return ptr;
    } 

    if(my_block->next && my_block->next->free){
        my_block->size = my_block->size + my_block->next->size +  sizeof(block);
        my_block->next = my_block->next->next;
        if(my_block->size >= size){
            return ptr;
        }
    }

    void* new_ptr = malloc(size);
    if(new_ptr){
       memcpy(new_ptr, ptr, my_block->size);
       free(ptr);
       return new_ptr;
    }
    else{
        return NULL;
    }
  }
  else{
    return malloc(size);
  }

}



