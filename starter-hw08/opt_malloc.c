/*
#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
*/

#include "xmalloc.h"
#include <sys/mman.h>
#include <pthread.h>
//#include <stdio.h>

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.
//
// Then copied from xv6.

// TODO: Remove this stuff
typedef unsigned long uint;

/*
static char* sbrk(uint nn) { return 0; }
*/
// TODO: end of stuff to remove

static pthread_mutex_t lock[105];
static int lockAcq[105];

static int pthreadInitComplete = 0;

static pthread_mutex_t singleLock = PTHREAD_MUTEX_INITIALIZER;

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
static Header base[105];
static Header *freep[105];

int getThreadIndex() {
   long tid = pthread_self();
   int modulus = tid % 105;
   //printf("tid: %ld  mod: %d\n", tid, modulus);
   return modulus;
}

void
xfree(void *ap)
{
  Header *bp, *p;
  //printf("mutex locking\n");
  int threadIndex = 0;
  threadIndex = getThreadIndex();
  if (lockAcq[threadIndex] == 0)
    pthread_mutex_lock(&lock[threadIndex]);
  //printf("mutex unlocked\n");  

  bp = (Header*)ap - 1;
  for(p = freep[threadIndex]; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
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
  freep[threadIndex] = p;
  if (lockAcq[threadIndex] == 0)
    pthread_mutex_unlock(&lock[threadIndex]);
  //printf("mutex released\n");
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
  int threadIndex = 0;
  threadIndex = getThreadIndex();
  hp = (Header*)p;
  hp->s.size = nu;
  lockAcq[threadIndex] = 1;
  xfree((void*)(hp + 1));
  lockAcq[threadIndex] = 0;
  return freep[threadIndex];
}


void*
xmalloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  if(pthreadInitComplete == 0) {
    pthread_mutex_lock(&singleLock);
    if(pthreadInitComplete == 0 ) {
        for(int j = 0; j < 105; j++) {
            pthread_mutex_init(&lock[j], NULL);
            lockAcq[j] = 0; 
        }
        pthreadInitComplete = 1;
    }
    pthread_mutex_unlock(&singleLock);
  }

  int threadIndex = 0;
  threadIndex = getThreadIndex();
  
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
 
  //printf("xmalloc locking\n");
  pthread_mutex_lock(&lock[threadIndex]);
  //printf("xmalloc unlcoked\n");

  if((prevp = freep[threadIndex]) == 0) {
    base[threadIndex].s.ptr = freep[threadIndex] = prevp = &base[threadIndex];
    base[threadIndex].s.size = 0;
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
      freep[threadIndex] = prevp;
      pthread_mutex_unlock(&lock[threadIndex]);
     // printf("xmalloc freed nom\n");
      return (void*)(p + 1);
    }
    if(p == freep[threadIndex])
      if((p = morecore(nunits)) == 0) {
       // printf("xmalloc ret err\n");
        pthread_mutex_unlock(&lock[threadIndex]);
        return 0;
      }
  }
  //printf("xmalloc end return\n");
  pthread_mutex_unlock(&lock[threadIndex]);
}

void*
xrealloc(void* prev, size_t nn)
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
