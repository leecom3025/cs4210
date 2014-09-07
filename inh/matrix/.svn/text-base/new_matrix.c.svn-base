#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>

#include "../gt_include.h"



/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */



typedef struct __uthread_arg
{
  int *a;
  int *b;
  unsigned int tid;
  unsigned int gid;
  unsigned int size;
}uthread_arg_t;
	
struct timeval tv1;

static void generate_matrix(int *matrix, int size)
{
  int counter=0;
  for (counter =0 ;counter < size * size; counter++){
    *matrix = 1;
    matrix++;
  }

}

static void print_matrix(int *matrix, int size)
{
  int counter=0;
  for (counter =0 ;counter < size * size; counter++){
    fprintf(stderr, "%d ", *matrix);
    matrix++;
  }
  
}

static void * uthread_mulmat(void *p)
{
  fprintf(stderr, "inside mulmat\n");
  int i, j, k, result;
  unsigned int cpuid;
  struct timeval tv2;

#define ptr ((uthread_arg_t *)p)
  


#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif
	fprintf(stderr, "ptr->size is %d", ptr->size);
	for(i = 0; i < ptr->size; i++){
	  for (j = 0; j < ptr->size; j++){
	    for (k =0; k < ptr->size; k++){
	    result += (*(ptr->a + (ptr->size * i) + j)) * (*(ptr->b + (ptr->size*j) + i));
	  }
	      }
	}
#ifdef GT_THREADS
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif

#undef ptr
	return result;
}



uthread_arg_t uargs[512];
uthread_t utids[512];

int main()
{
  fprintf(stderr, "in main\n");
	uthread_arg_t *uarg;
	int inx;


	gtthread_app_init();

	gettimeofday(&tv1,NULL);
	int *matrices[4];
	int matrix_count=0;
	int matrix_size = 0;
	int thread_count = 0;
	int credits =0;
	for (matrix_size = 64, matrix_count =0; matrix_size < 513; matrix_size *=2, matrix_count++){
	  matrices[matrix_count] = malloc(sizeof(int) * matrix_size * matrix_size);
	  generate_matrix(matrices[matrix_count], matrix_size);
	  fprintf(stderr, "creating matrix of size %d\n", matrix_size);
	  for (credits = 32; credits < 129; credits +=32){
	    for(inx=0; inx<32; inx++, thread_count++) {
	      fprintf(stderr, "creating thread %d of credits %d of size %d\n", thread_count, credits, matrix_size
);
		uarg = &uargs[thread_count];
		uarg->tid = thread_count;
		uarg->size = matrix_size;
		uarg->gid = 0;
		uarg->a = matrices[matrix_count];
		uarg->b = matrices[matrix_count];
		uthread_create(&utids[thread_count], uthread_mulmat, uarg, uarg->gid, credits * 30);
	      }
	  }
	}

	gtthread_app_exit();

	printf("\n");

	fprintf(stderr, "********************************");
	return(0);
}
