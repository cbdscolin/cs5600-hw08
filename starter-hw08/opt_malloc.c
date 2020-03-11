/*
#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
*/

#include "xmalloc.h"
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.
//
// Then copied from xv6.
typedef unsigned long uint;

// TODO: Remove this stuff

/*
static char* sbrk(uint nn) { return 0; }
*/
// TODO: end of stuff to remove

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int lockAcq = 0;


typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

// TODO: This is shared global data.
// You're going to want a mutex to protect this.
static Header base;
static Header *freep;

struct memchunk {
    int totalsize;
    int currentsize;
    char *c;
    struct memchunk *next;
};

struct memchunk *chunk32head[8];
struct memchunk *chunk32tail[8];

int limits[9] = {0, 8, 24, 72, 216, 648, 1944, 4096, 122880};

void
xfree(void *ap)
{
  //printf("Free called %p\n", ap);
  if(1 == 1) {
    return;
  }   
  Header *bp, *p;
  if (lockAcq == 0) {
    if(pthread_mutex_lock(&lock) < 0)
        return;
  }

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
  if (lockAcq == 0)
    if(pthread_mutex_unlock(&lock) < 0)
       return;
}

static Header*
morecore(uint nu)
{   
  char *p;
  Header *hp;
  if(nu < 4096)
    nu = 4096;
  // TODO: Replace sbrk use with mmap
  p = mmap(0, nu * sizeof(Header), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1 , 0); 

  if(p == (char*)-1) {
    return 0;
  }
  hp = (Header*)p;
  hp->s.size = nu;
  lockAcq = 1;
  xfree((void*)(hp + 1));
  lockAcq = 0;
  return freep;
}


int indexForMemory(uint bytes) {
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
    }
    return -1;
}

void*
xmalloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;
 
  int bucketNo = indexForMemory(nbytes); 

  if(bucketNo >= 0) {
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
  } 
  printf("high mem  bucket: %d  nbytes %ld\n", bucketNo, nbytes); 
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;


  if((prevp = freep) == 0) {
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    //printf("add: %p ,size: %ld , bytes: %ld\n", p->s.ptr, p->s.size, nbytes);
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      if(pthread_mutex_unlock(&lock) < 0) {
        return 0;
      }
     // printf("xmalloc freed nom\n");
      return (void*)(p + 1);
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0) {
       // printf("xmalloc ret err\n");
        if(pthread_mutex_unlock(&lock) < 0) {
            return 0;
        }
        return 0;
      }
  }
  //printf("xmalloc end return\n");
  if(pthread_mutex_unlock(&lock) < 0) {
    return 0;
  }
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
