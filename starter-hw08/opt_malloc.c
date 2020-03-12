#include "xmalloc.h"
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <errno.h>

#define PGSIZE 4096
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))

typedef unsigned long uint;

static pthread_mutex_t lock[100][21];  //[threadId][bucketNo]

static int pthreadInitComplete = 0;
static pthread_mutex_t singleLock = PTHREAD_MUTEX_INITIALIZER;

struct memchunk {
    long totalsize;
    long currentsize;
    char *c;
    long allocatedChunks;
    int bucketNo;
    int threadIndex;
    struct memchunk *next;
};

struct memheader {
    struct memchunk *head;
};

struct memchunk *chunk32head[100][21];
struct memchunk *chunk32tail[100][21];

int limits[22] = {0, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3192, 4096, 122880};

int getThreadIndex() {
   long tid = pthread_self();
   int modulus = tid % 100;
   return modulus;
}

int indexForMemory(uint bytes) {
    if(bytes > 4096) {
        return 20;
    }
    
    for(int i = 0; i < 22; i++) {
        if(i == 21) {
            printf("something wrong\n");
            assert(i < 21);
        }
        if(bytes > limits[i] && bytes <= limits[i+1]) {
            return i;
        }
    }

    return -1;
}


void
xfree(void *freePtr)
{
  long memheaderSize = 0;
  memheaderSize = sizeof(struct memheader *);
  struct memheader *memAddr = (struct memheader *) ((void*) freePtr - memheaderSize);
  struct memchunk *memchunkHead = memAddr->head;

  int bucketNo = memchunkHead->bucketNo;
  int threadIndex = memchunkHead->threadIndex;
  if(pthread_mutex_lock(&lock[threadIndex][bucketNo]) < 0) {
        printf("pthread_lock failed\n");
        assert(0 == 1);
  };
  memchunkHead->allocatedChunks -= 1;
  if(memchunkHead->allocatedChunks == 0) {
    munmap(memchunkHead, memchunkHead->totalsize);
  }
  if(pthread_mutex_unlock(&lock[threadIndex][bucketNo]) < 0) {
        printf("pthread_lock failed\n");
        assert(0 == 1);
  };
}


void*
xmalloc(uint nbytes)
{
    if(pthreadInitComplete == 0) {
        pthread_mutex_lock(&singleLock);
        if(pthreadInitComplete == 0 ) {
            for(int jj = 0; jj < 100; jj++) {
            for(int ii = 0; ii < 21; ii++) {
               pthread_mutex_init(&lock[jj][ii], NULL);
            }
            }
            pthreadInitComplete = 1;
        }
        pthread_mutex_unlock(&singleLock);
    }
  
    int bucketNo = indexForMemory(nbytes); 
    int threadIndex = getThreadIndex();

    if(pthread_mutex_lock(&lock[threadIndex][bucketNo]) < 0) {
        printf("pthread_lock failed\n");
        assert(0 == 1);
    };

    long roundedMemory = limits[bucketNo + 1];

    struct memchunk *top, *oldPtr = 0;
    long bytesToAllocate;
    bytesToAllocate = 4096 * 100 * 32;

    if(roundedMemory > 40961) {
      bytesToAllocate = PGROUNDUP(nbytes);
      roundedMemory = bytesToAllocate;
    }

    long structSize = 0;
    structSize = sizeof(struct memchunk);
    structSize = PGROUNDUP(structSize);

    long structPtrSize = 0;
    structPtrSize = sizeof(struct memheader *);

 //   structPtrSize = 0;

    top = chunk32tail[threadIndex][bucketNo];
    int count = 0;
    while(top == 0 || (top->currentsize + roundedMemory + structPtrSize) > top->totalsize) {
    if(top == 0) {
        if(count > 5) {
            printf("Allocating too much memory %d\n", count);
            break;
        }
        top = mmap(0, structSize + structSize + bytesToAllocate, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
        if(MAP_FAILED == top) {
            printf("Something failed code: %d memory: %ld \n", errno, nbytes);
           // printf("top %p currentsize: %ld, roundedMem: %ld , structPtrSize: %ld\n", top, top->currentsize, roundedMemory, structPtrSize); 
            perror("mmap failed\n");
            assert(0 == 1);
        }

        top->totalsize = bytesToAllocate + structSize; //+ structSize - sizeof(struct memchunk);
        top->currentsize = 0;
        top->allocatedChunks = 0;
        top->bucketNo = bucketNo;
        top->threadIndex = threadIndex;
        char *cl;
        cl = (char *) top + sizeof(struct memchunk);
        //cl = mmap(0, bytesToAllocate, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        top->c = cl;
        if(oldPtr != 0) {
            oldPtr->next = top;
            chunk32tail[threadIndex][bucketNo] = top;
        }
    } else {
        oldPtr = top;
        top = top->next;
    }
    }

    if(chunk32head[threadIndex][bucketNo] == 0) {
        chunk32head[threadIndex][bucketNo] = top;
        chunk32tail[threadIndex][bucketNo] = top;
    }
    struct memheader *header = (struct memheader*) (top->c + top->currentsize);
    header->head = top;
    top->currentsize += structPtrSize;
    char *retAddress = top->c + top->currentsize;
    top->currentsize += roundedMemory;
    top->allocatedChunks += 1;
    if(pthread_mutex_unlock(&lock[threadIndex][bucketNo]) < 0) {
        printf("pthread_unlock failed\n");
        assert(0 == 1);
    };

    return (void*) retAddress;
}

void*
xrealloc(void* prev, uint nn)
{
  char *prevPtr = (char *) prev;
  char *newPtr = xmalloc(nn);

  for(long i = 0; i < nn; i++) {
     newPtr[i] = prevPtr[i];
  }

  return (void *) newPtr;
}
