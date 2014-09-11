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
#include <math.h>

#include "gt_include.h"

#define GT_THREADS 1

#define ROWS 256
#define COLS ROWS
#define SIZE COLS

#define NUM_CPUS 4
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE/NUM_GROUPS)

#define NUM_THREADS 32
#define PER_THREAD_ROWS (SIZE/NUM_THREADS)


/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	
	int size;
}uthread_arg_t;
	
struct timeval tv1;

static void generate_matrix(matrix_t *mat, int val)
{

	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{
			mat->m[i][j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	printf ("\nMATRIX %d \n", mat->m[0][0]);
#if 0
	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++)
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}
#endif 
	return;
}

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;
	int size;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	start_row = ptr->start_row;
	end_row = (ptr->start_row + PER_THREAD_ROWS);

	size = ptr->size;
#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = (ptr->start_col + PER_THREAD_ROWS);
#else
	start_col = 0;
	end_col = SIZE;
#endif

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif

	for(i = start_row; i < end_row; i++)
	  for(j = start_col; j < end_col; j++)
			for(k = 0; k < size; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

#ifdef GT_THREADS
	gt_yield();
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif

#undef ptr
	return 0;
}

matrix_t A[4], B[4], C[4];

static void init_matrices()
{
	int index;
	for (index = 0; index < 4; index++){
		generate_matrix(&(A[index]), 1);
		generate_matrix(&(B[index]), 1);
		generate_matrix(&(C[index]), 0);
	}
	return;
}

long sum (long *array, int start, int end)
{
	int i;
	long sum_value = 0;
	for(i = start; i<end; i++)
	sum_value += array[i];

	return sum_value;
}
		
float standard_deviation(long *array, int start, int end, float mean)
{
	int i;
	long sd = 0;
	long temp = 0;
	for(i=start; i<end; i++)
		temp += (array[i] - mean)*(array[i] - mean);

	sd = (float) sqrt(temp/32);

	return sd;
}

uthread_arg_t uargs[NUM_THREADS * 4];
uthread_t utids[NUM_THREADS * 4];
int m_size[4] = {32, 64, 128, 256};
int c_size[4] = {25, 50, 75, 100};

int main()
{
	uthread_arg_t *uarg;
	int inx;


	gtthread_app_init();

	init_matrices();

	gettimeofday(&tv1,NULL);

	int mtx;
	for(mtx=0; mtx < 4; mtx++){
		int per_thread = m_size[mtx] / NUM_THREADS;
		for(inx=0; inx<NUM_THREADS; inx++)
		{
			uarg = &uargs[(mtx*NUM_THREADS) + inx];
			uarg->_A = &(A[mtx]);
			uarg->_B = &(B[mtx]);
			uarg->_C = &(C[mtx]);

			uarg->tid = inx;

			uarg->gid = (inx % NUM_GROUPS);

			uarg->start_row = (inx * per_thread);
			uarg->size = m_size[mtx];
	#ifdef GT_GROUP_SPLIT
			/* Wanted to split the columns by groups !!! */
			uarg->start_col = (uarg->gid * PER_GROUP_COLS);
	#endif

			uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, c_size[(inx/8)]);
		}
	}
	gtthread_app_exit();

	printf("\n");

	// print_matrix(&A);
	// fprintf(stderr, "********************************");
	// print_matrix(&B);
	fprintf(stderr, "********************************");
	for(inx=0; inx < 4; inx++){
		print_matrix(&(C[inx]));
	}
	fprintf(stderr, "********************************");
	return(0);


}
