#include "xmalloc.h"
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
typedef unsigned long uint;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int lockAcq = 0;

struct memchunk {
    int totalsize;
    int currentsize;
    char *c;
    struct memchunk *next;
};

struct memchunk *chunk32head[21];
struct memchunk *chunk32tail[21];

/*
struct memchunk *chunk32head[8];
struct memchunk *chunk32tail[8];
*/

int limits[22] = {0, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3192, 4096, 122880};
//int limits[9] = {0, 8, 24, 72, 216, 648, 1944, 4096, 122880};

void
xfree(void *ap)
{
  //printf("Free called %p\n", ap);
  lockAcq = 0;
  if(0 == 0) {
    return;
  }   
}


int indexForMemory(uint bytes) {
    if(bytes > 4096) {
        return 21;
    }
    
    for(int i = 0; i < 21; i++) {
        if(i == 20) {
            return 20;
        }
        if(bytes > limits[i] && bytes <= limits[i+1]) {
            return i;
        }
    }

    /*
    if(bytes > 4096) {
        return 8;
    }
    
    for(int i = 0; i < 8; i++) {
        if(i == 7) {
            return 7;
        }
        if(bytes > limits[i] && bytes <= limits[i+1]) {
            return i;        
        }
    }*/
    return -1;
}

void*
xmalloc(uint nbytes)
{
 
    int bucketNo = indexForMemory(nbytes); 

    if(pthread_mutex_lock(&lock) < 0) {
        printf("pthread_lock failed\n");
        assert(0 == 1);
    };

    long roundedMemory = limits[bucketNo + 1];
    //printf("bytes: %ld, rounded: %ld\n", nbytes, roundedMemory);
    struct memchunk *top, *oldPtr = 0;
    long bytesToAllocate = 6553600;
    if(roundedMemory > 40961) {
      bytesToAllocate = nbytes;
    }

    top = chunk32tail[bucketNo];
    while(top == 0 || top->currentsize + roundedMemory >= top->totalsize) {
    if(top == 0) {
        top = mmap(0, sizeof(struct memchunk), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
        if(MAP_FAILED == top) {
            printf("Something failed code: %d\n", errno);
            perror("mmap failed\n");
            assert(0 == 1);
        }

        top->totalsize = bytesToAllocate;
        top->currentsize = 0;
        char *cl;
        cl = mmap(0, bytesToAllocate, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if(cl == MAP_FAILED) {
            printf("Something failed code: %d\n", errno);
            perror("mmap failed\n");
            assert(0 == 1);
        }
        top->c = cl;
        if(oldPtr != 0) {
            oldPtr->next = top;
            chunk32tail[bucketNo] = top;
        }
    } else {
        oldPtr = top;
        top = top->next;
    }
    }
    if(chunk32head[bucketNo] == 0) {
        chunk32head[bucketNo] = top;
        chunk32tail[bucketNo] = top;
    }
    char *retAddress = top->c + top->currentsize;
    top->currentsize += roundedMemory;
    if(pthread_mutex_unlock(&lock) < 0) {
        printf("pthread_unlock failed\n");
        assert(0 == 1);
    };

    return (void*) retAddress;

  //printf("high mem  bucket: %d  nbytes %ld\n", bucketNo, nbytes); 
}

void*
xrealloc(void* prev, uint nn)
{
  // TODO: Actually build realloc.
  char *prevPtr = (char *) prev;
  char *newPtr = xmalloc(nn);

  for(long i = 0; i < nn; i++) {
     newPtr[i] = prevPtr[i];
  }
  //xfree(prev);
  return (void *) newPtr;
}
