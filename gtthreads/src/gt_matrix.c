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
	int end_row;
}uthread_arg_t;
	
struct timeval tv1;
long long wait_time[128];


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
#if 1
	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++) 
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}
#endif 
	return;
}

static void print_matrix_by_size(matrix_t *mat, int size)
{
	int i, j, count = 0;

	printf ("\nMATRIX %d \n", mat->m[0][0]);
#if 1
	for(i=0;i<size;i++)
	{
		for(j=0;j<size;j++) {
			if (mat->m[i][j] == size)
				count++;
			// printf(" %d ",mat->m[i][j]);
		}
		// printf("\n");
	}

	if (count == (size*size))
		printf("%d%s%d has correct value\n", size, "x", size);
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
	end_row = ptr->end_row; //(ptr->start_row + PER_THREAD_ROWS);

	size = ptr->size;
#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = (ptr->start_col + PER_THREAD_ROWS);
#else
	start_col = 0;
	end_col = size;
#endif

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	// fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif

	for(i = start_row; i < end_row; i++)
	  for(j = start_col; j < end_col; j++)
			for(k = 0; k < size; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];
	gettimeofday(&tv2,NULL);

#ifdef GT_THREADS
	gt_yield(); // this one shouldn't be matter
	// fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			// ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));

#else
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif
	wait_time[ptr->tid] = (((tv2.tv_sec - tv1.tv_sec)*1000000) + (tv2.tv_usec - tv1.tv_usec)) - REAL[ptr->tid];
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

static int usage() 
{
	printf("USAGE: bin/matrix [1: credit, 2: pq]\n");
}

uthread_arg_t uargs[NUM_THREADS * 4];
uthread_t utids[NUM_THREADS * 4];
int m_size[4] = {32, 64, 128, 256};
int c_size[4] = {25, 50, 75, 100};
long long on_cpu[128]; // std for run time
long long on_exe[128]; // std for total execution time

int main( int argc, char * argv [] ) 
{
	int choose;
	if (argc != 2) {
		printf("%d\n", argc);
		usage();
		return 0;
	}

	choose = atoi(argv[1]);

	if (choose > 2 || choose < 1) {
		usage();
		return 0;
	}

	uthread_arg_t *uarg;
	int inx;


	gtthread_app_init();

	init_matrices();

	gettimeofday(&tv1,NULL);

	int mtx;
	for(mtx=0; mtx < 4; mtx++){
		int per_thread = m_size[mtx] / NUM_THREADS;
		for(inx=NUM_THREADS - 1; inx > -1; inx--)
		{
			uarg = &uargs[(mtx*NUM_THREADS) + inx];
			uarg->_A = &(A[mtx]);
			uarg->_B = &(B[mtx]);
			uarg->_C = &(C[mtx]);

			uarg->tid = (mtx*NUM_THREADS) + inx;

			uarg->gid = mtx; 

			uarg->start_row = (inx * per_thread);
			uarg->end_row = (inx * per_thread) + per_thread;
			uarg->size = m_size[mtx];
	#ifdef GT_GROUP_SPLIT
			/* Wanted to split the columns by groups !!! */
			uarg->start_col = (uarg->gid * PER_GROUP_COLS);
	#endif
			if (choose == 1) // credit
				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, c_size[(inx/8)]);
			else // pq
				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, 100);

		}
	}
	gtthread_app_exit();

	printf("\n");

	// print_matrix(&A);
	// fprintf(stderr, "********************************");
	// print_matrix(&B);
	fprintf(stderr, "********************************");
	for(inx=0; inx < 4; inx++){
		print_matrix_by_size(&(C[inx]), m_size[inx]);
	}
	fprintf(stderr, "********************************\n");


	int mSize, mCredit;
	long long run_time, total_time;

	long long avg_run[16], avg_exe[16], std_run[16], std_exe[16];

	for (mSize = 0; mSize < 16; mSize++){
		avg_run[mSize] = 0;
		avg_exe[mSize] = 0;
		std_run[mSize] = 0;
		std_exe[mSize] = 0;
	}

	for(mSize =0; mSize < 16; mSize++) {
		run_time = total_time = 0;
		for(mCredit = 0; mCredit < 8; mCredit++) {
			run_time += REAL[(mSize*8) + mCredit]; // run
			total_time += wait_time[(mSize*8) + mCredit];
		}

		avg_run[mSize] = run_time/8;
		avg_exe[mSize] = total_time/8;

		for(mCredit = 0; mCredit < 8; mCredit++) {
			on_cpu[(mSize*8) + mCredit] = (REAL[(mSize*8) + mCredit] - avg_run[mSize]) 
											* (REAL[(mSize*8) + mCredit] - avg_run[mSize]);
			on_exe[(mSize*8) + mCredit] = (wait_time[(mSize*8) + mCredit] - avg_exe[mSize]) * 
											(wait_time[(mSize*8) + mCredit] - avg_exe[mSize]);
		}

		for(mCredit = 0; mCredit < 8; mCredit++) {
			std_run[mSize] += on_cpu[(mSize*8) + mCredit];
			std_exe[mSize] += on_exe[(mSize*8) + mCredit];
		}

		std_run[mSize] /= 8;
		std_exe[mSize] /= 8;

		std_run[mSize] = sqrt(std_run[mSize]);
		std_exe[mSize] = sqrt(std_exe[mSize]);
	}

	printf("%s%12s%12s%11s%11s\n", " matrix  credit", "avg_run", "avg_wait", "std_run", "std_wait");
	for(mSize = 0; mSize < 16; mSize++) {
		if (mSize%4 == 0)
			printf("===============================================================\n");
		printf("%s%3d%s%3d %5d%s %10lld %10lld %10lld %10lld\n", 
			"(", m_size[mSize/4], "x", m_size[mSize/4], c_size[mSize%4], ")",
			avg_run[mSize], avg_exe[mSize], std_run[mSize], std_exe[mSize]);
	}
	printf("===============================================================\n");

	return 0;


}
