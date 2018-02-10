/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _alloc {
    char *mem;
    size_t size;
} alloc_t, *alloc_pt;

typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {

    // ensure that it's called only once until mem_free
    // allocate the pool store with initial capacity
    // note: holds pointers only, other functions to allocate/deallocate
    if(pool_store==NULL){
        pool_store=malloc(MEM_POOL_STORE_INIT_CAPACITY* sizeof(pool_mgr_pt));
        pool_store_size=MEM_POOL_STORE_INIT_CAPACITY;
        for(int i=0; i<MEM_POOL_STORE_INIT_CAPACITY; ++i){
            pool_store[i]=NULL;
        }
        if(pool_store==NULL){
            return ALLOC_FAIL;
        }
        return ALLOC_OK;
    }else{
        return ALLOC_CALLED_AGAIN;
    }

}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    // can free the pool store array
    // update static variables
    if(pool_store==NULL){
        return ALLOC_CALLED_AGAIN;
    }
    for(int i=0; i<MEM_POOL_STORE_INIT_CAPACITY; ++i){
        if(pool_store[i]!=NULL){
            //We ill call mem_pool_close() here
        }
    }
    free(pool_store);
    pool_store=NULL;
    pool_store_size=0;
    return ALLOC_OK;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    if(pool_store==NULL){
        return NULL;
    }
    alloc_status return_status=_mem_resize_pool_store();
    //assert(return_status ==ALLOC_OK);
    if(return_status !=ALLOC_OK){
        return NULL;
    }
    // expand the pool store, if necessary

    // allocate a new mem pool mgr
    pool_mgr_pt newMGR=malloc(sizeof(struct _pool_mgr));

    // check success, on error return null
    if(!newMGR){
        return NULL;
    }
    // allocate a new memory pool

    newMGR->pool.mem=malloc(size);
    // check success, on error deallocate mgr and return null
    if(!newMGR->pool.mem){
        free(newMGR);
        return NULL;
    }
    // allocate a new node heap
    newMGR->node_heap=malloc(sizeof(struct _node)*MEM_NODE_HEAP_INIT_CAPACITY);
    // check success, on error deallocate mgr/pool and return null
    if(!newMGR->node_heap){
        free(newMGR->pool.mem);
        free(newMGR);
        return NULL;
    }
    // allocate a new gap index
    newMGR->gap_ix=malloc(sizeof(gap_pt));
    // check success, on error deallocate mgr/pool/heap and return null
    if(!newMGR->gap_ix){
        free(newMGR->pool.mem);
        free(newMGR->node_heap);
        free(newMGR);
        return NULL;
    }
    // assign all the pointers and update meta data:
    newMGR->total_nodes=0;
    newMGR->pool.policy=policy;
    newMGR->pool.total_size=size;
    newMGR->pool.alloc_size=0;
    newMGR->pool.num_allocs=0;
    newMGR->pool.num_gaps=1;
    newMGR->used_nodes=0;

    newMGR->gap_ix_capacity=0;
    //   initialize top node of node heap
    newMGR->node_heap->allocated=0;
    newMGR->node_heap->used=1;
    newMGR->node_heap->alloc_record.mem=newMGR->pool.mem;
    newMGR->node_heap->alloc_record.size=0;
    newMGR->node_heap->prev=NULL;
    newMGR->node_heap->next=NULL;
    //   initialize top node of gap index
    newMGR->gap_ix->size=0;
    newMGR->gap_ix->node=newMGR->node_heap;

    //   initialize pool mgr
    //   link pool mgr to pool store
    // return the address of the mgr, cast to (pool_pt)
   pool_store[pool_store_capacity]=newMGR;


    return (pool_pt)newMGR;

}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mgr=(pool_mgr_pt)pool;
    // check if this pool is allocated
    if(!mgr){
        return ALLOC_FAIL;
    }
    // check if pool has only one gap
    if(mgr->pool.num_gaps!=1){
        return ALLOC_FAIL;
    }
    // check if it has zero allocations
    if(mgr->pool.num_allocs!=0){
        return ALLOC_FAIL;
    }
    // free memory pool
    free(mgr->pool.mem);
    // free node heap
    free(mgr->node_heap);
    // free gap index
    free(mgr->gap_ix);

    // find mgr in pool store and set to null
    for(int i=0; i<pool_store_size; ++i){
        if(pool_store[i]==mgr){
            pool_store[i]=NULL;
        }
    }
    // note: don't decrement pool_store_size, because it only grows
    // free mgr
    free(mgr);
    return ALLOC_OK;
}

void * mem_new_alloc(pool_pt pool, size_t size) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mgr=(pool_mgr_pt)pool;
    // check if any gaps, return null if none
    if(mgr->pool.num_gaps!=1){
        return NULL;
    }
    // expand heap node, if necessary, quit on error
    node_pt heap=mgr->node_heap;
    assert(heap!=NULL);
    if( heap->allocated=0)
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev;

    // check used nodes fewer than total nodes, quit on error
    // get a node for allocation:
    // if FIRST_FIT, then find the first sufficient node in the node heap
    // if BEST_FIT, then find the first sufficient node in the gap index
    // check if node found
    // update metadata (num_allocs, alloc_size)
    // calculate the size of the remaining gap, if any
    // remove node from gap index
    // convert gap_node to an allocation node of given size
    // adjust node heap:
    //   if remaining gap, need a new node
    //   find an unused one in the node heap
    //   make sure one was found
    //   initialize it to a gap node
    //   update metadata (used_nodes)
    //   update linked list (new node right after the node for allocation)
    //   add to gap index
    //   check if successful
    // return allocation record by casting the node to (alloc_pt)

    return NULL;
}

alloc_status mem_del_alloc(pool_pt pool, void * alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // get node from alloc by casting the pointer to (node_pt)
    // find the node in the node heap
    // this is node-to-delete
    // make sure it's found
    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    // if the next node in the list is also a gap, merge into node-to-delete
    //   remove the next node from gap index
    //   check success
    //   add the size to the node-to-delete
    //   update node as unused
    //   update metadata (used nodes)
    //   update linked list:
    /*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success
    //   add the size of node-to-delete to the previous
    //   update node-to-delete as unused
    //   update metadata (used_nodes)
    //   update linked list
    /*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    return ALLOC_FAIL;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    // get the mgr from the pool
    // allocate the segments array with size == used_nodes
    // check successful
    // loop through the node heap and the segments array
    //    for each node, write the size and allocated in the segment
    // "return" the values:
    /*
                    *segments = segs;
                    *num_segments = pool_mgr->used_nodes;
     */
}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary

                if (((float) pool_store_size / pool_store_capacity)
                    > MEM_POOL_STORE_FILL_FACTOR) {
                    pool_store_size*=MEM_POOL_STORE_EXPAND_FACTOR;
                    pool_store_capacity=pool_store_size;
                    pool_store=realloc(pool_store,pool_store_size* sizeof(pool_mgr_pt));
                    if(pool_store==NULL){
                        return ALLOC_FAIL;
                    }

                }


    // don't forget to update capacity variables

    return ALLOC_OK;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    // see above
    if (((float) pool_mgr->used_nodes / pool_mgr->total_nodes)
        > MEM_NODE_HEAP_FILL_FACTOR) {
        pool_mgr->total_nodes=pool_mgr->total_nodes*MEM_NODE_HEAP_EXPAND_FACTOR;

        pool_mgr->node_heap=realloc(pool_mgr->node_heap, pool_mgr->total_nodes* sizeof(struct _node));
        if(pool_mgr->node_heap==NULL){
            return ALLOC_FAIL;
        }

    }

    return ALLOC_OK;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    // see above
    if (((float) pool_mgr->pool.num_gaps / pool_mgr->gap_ix_capacity)
        > MEM_GAP_IX_FILL_FACTOR ) {

        pool_mgr->gap_ix_capacity= pool_mgr->gap_ix_capacity*MEM_GAP_IX_EXPAND_FACTOR;
        pool_mgr->gap_ix=realloc(pool_mgr->gap_ix, pool_mgr->gap_ix_capacity* sizeof(struct _gap));
        if(pool_mgr->gap_ix==NULL){
            return ALLOC_FAIL;
        }

    }
    return ALLOC_OK;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {

    // expand the gap index, if necessary (call the function)
    assert(_mem_resize_gap_ix(pool_mgr)==ALLOC_OK);




    // add the entry at the end
    int num_gaps=pool_mgr->pool.num_gaps;
    pool_mgr->gap_ix[num_gaps].size=size;
    pool_mgr->gap_ix[num_gaps].node=node;

    // update metadata (num_gaps)
    pool_mgr->pool.num_gaps++;
    num_gaps++;
    // sort the gap index (call the function)

    // check success


    return _mem_sort_gap_ix(pool_mgr);
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    int num_gaps=pool_mgr->pool.num_gaps;
    int chosen_node=num_gaps;
    for(int i=0; i<num_gaps; ++i){
        if(pool_mgr->gap_ix[i].node==node){
            chosen_node=i;
        }
        if(i>num_gaps){
            pool_mgr->gap_ix[i-1]=pool_mgr->gap_ix[i];
        }
    }
    pool_mgr->gap_ix[num_gaps].node=NULL;
    pool_mgr->gap_ix[num_gaps].size=0;
    // find the position of the node in the gap index
    // loop from there to the end of the array:
    //    pull the entries (i.e. copy over) one position up
    //    this effectively deletes the chosen node
    // update metadata (num_gaps)
    // zero out the element at position num_gaps!

    return ALLOC_OK;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {

    // the new entry is at the end, so "bubble it up"
    int num_gaps=pool_mgr->pool.num_gaps;
    size_t size=pool_mgr->gap_ix[num_gaps].size;

    for(int i=(num_gaps-1); i>0; --i){
        if(pool_mgr->gap_ix[i].size<size||((pool_mgr->gap_ix[i].size==pool_mgr->gap_ix[num_gaps].size)
                                           &(pool_mgr->gap_ix[num_gaps].node<pool_mgr->gap_ix[i].node))){

            struct _gap temp=pool_mgr->gap_ix[i];
            pool_mgr->gap_ix[i]=pool_mgr->gap_ix[i+1];
            pool_mgr->gap_ix[i+1]=temp;
        }else{
            break;
        }
    }
    // loop from num_gaps - 1 until but not including 0:
    //    if the size of the current entry is less than the previous (u - 1)
    //    or if the sizes are the same but the current entry points to a
    //    node with a lower address of pool allocation address (mem)
    //       swap them (by copying) (remember to use a temporary variable)

    return ALLOC_OK;
}

static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr) {
    return ALLOC_FAIL;
}

