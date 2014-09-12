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
#include <time.h>

#include "gt_include.h"
/**********************************************************************/
/** DECLARATIONS **/
/*Each set of <# of matrix, credit> should have same CPU time but higher the credit is lower wait time is 
  As # of matrix increase, CPU time increase.  */
/**********************************************************************/
extern void gt_yield();
static void calcuate(uthread_struct_t **u_obj);
static void u_update(uthread_struct_t **u_obj, struct timeval curr, struct timeval ncurr, struct timeval up);
long REAL[128];
long TAKEN[128];
long RUN_TIME[128];
long TOTAL_TIME[128];

long long u_begin[128];

#define MILL 1000000

/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID	0

/**********************************************************************/
/* uthread scheduling */
static void uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);

/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit);

/**********************************************************************/
/** DEFNITIONS **/

static void u_update(uthread_struct_t **u, struct timeval curr, struct timeval ncurr, struct timeval up)
{
	uthread_struct_t *u_obj = *u;

	*u = u_obj;
}

static void calculate(uthread_struct_t **u)
{
	uthread_struct_t *u_obj = *u;
	struct timeval curr, ncurr, up;

	up = ((&u_obj->credits)->updated);
	gettimeofday(&curr, NULL);

	// printf("Curr: %lld \n", ((curr.tv_sec * MILL) + curr.tv_usec) - ((up.tv_sec * MILL) + up.tv_usec));

	#if U_DEBUG
		printf("%s %d\n", "u_obj before:", u_obj->credits.used_sec);
	#endif	
	
	u_obj->credits.used_sec += ((curr.tv_sec * MILL) + curr.tv_usec) - ((up.tv_sec * MILL) + up.tv_usec);

	#if U_DEBUG
		printf("%s %d\n", "u_obj after: ", u_obj->credits.used_sec);
	#endif

	// u_obj->credits.usec_per_core[kthread_apic_id()] += ncurr.tv_sec * MILL + ncurr.tv_usec;
	unsigned long fuck = (((curr.tv_sec * MILL) + curr.tv_usec) - ((up.tv_sec * MILL) + up.tv_usec))/1000;
	u_obj->credits.credit_left -= fuck;

	#if U_DEBUG
		printf("\n%s[%d] %d decreased by %lu\n", "Credit left", u_obj->uthread_tid, u_obj->credits.credit_left, fuck);
	#endif

	*u = u_obj;
}

/**********************************************************************/
/* uthread scheduling */

/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gettimeofday(&(u_new->credits.begin), NULL);

	// printf("U: %lu\n", u_new->credits.begin.tv_sec);

	gt_spin_lock(&(ksched_shared_info.uthread_init_lock));

	/* Register a signal(SIGUSR2) for alternate stack */
	act.sa_handler = uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if(sigaction(SIGUSR2,&act,&oldact))
	{
		fprintf(stderr, "uthread sigusr2 install failed !!");
		return -1;
	}

	/* Install alternate signal stack (for SIGUSR2) */
	if(sigaltstack(&(u_new->uthread_stack), &oldstack))
	{
		fprintf(stderr, "uthread sigaltstack install failed.");
		return -1;
	}

	/* Unblock the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);


	/* SIGUSR2 handler expects kthread_runq->cur_uthread
	 * to point to the newly created thread. We will temporarily
	 * change cur_uthread, before entering the synchronous call
	 * to SIGUSR2. */

	/* kthread_runq is made to point to this new thread
	 * in the caller. Raise the signal(SIGUSR2) synchronously */
#if 0
	raise(SIGUSR2);
#endif
	syscall(__NR_tkill, kthread_cpu_map[kthread_apic_id()]->tid, SIGUSR2);

	/* Block the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if(sigaction(SIGUSR2,&oldact,NULL))
	{
		fprintf(stderr, "uthread sigusr2 revert failed !!");
		return -1;
	}

	/* Disable the stack for signal(SIGUSR2) handling */
	u_new->uthread_stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if(sigaltstack(&oldstack, NULL))
	{
		fprintf(stderr, "uthread sigaltstack revert failed.");
		return -1;
	}

	gt_spin_unlock(&(ksched_shared_info.uthread_init_lock));
	return 0;
}

/* 
Implement a	library function for voluntary preemption (gt_yield()).
When a user-level thread executes this function, it should yield the CPU to the scheduler,
which then schedules the next thread (per its scheduling scheme).	
On voluntary preemption, the thread	should be charged credits only for the acctual CPU cycles used.
*/
/**********************************************************************/

extern void gt_yield()
{
 	struct itimerval schd; 
#if U_DEBUG
	printf("\ngt_yield(%d) is called!\n", kthread_apic_id());
#endif	
// uthread_struct_t *cur_uthread;
//   kthread_runqueue_t *kthread_runq;

//   // Get the kthread runqueue on which this thread is running.
//   kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

//   // Get the thread object of the thread that is calling this function.
//   cur_uthread = kthread_runq->cur_uthread;
//   assert(cur_uthread->uthread_state == UTHREAD_RUNNING);

//   // Set state to UTHREAD_YIELD
//   cur_uthread->uthread_state = UTHREAD_YIELD;
//   uthread_schedule(&sched_find_best_uthread);

  	kthread_block_signal(SIGVTALRM);
  	kthread_block_signal(SIGUSR1);



  	kthread_context_t *k_ctx;
  	k_ctx = kthread_cpu_map[kthread_apic_id()];
  	k_ctx->yid = 1;
  	
  	schd.it_interval.tv_sec = 0;
  	schd.it_interval.tv_usec = 0;
  	schd.it_value.tv_sec = 0;
  	schd.it_value.tv_usec = 10000;

#if 0
  	printf("\n%d, %f, %f\n", schd.it_interval.tv_sec, schd.it_interval.tv_usec, schd.it_value.tv_sec);
#endif 

  	setitimer(ITIMER_VIRTUAL, &schd, NULL);
	kthread_unblock_signal(SIGVTALRM);
  	kthread_unblock_signal(SIGUSR1);
}


extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);

#if U_DEBUG
	fprintf(stderr, "uthread_schedule invoked !! %d\n", kthread_apic_id());
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);

	// load balancing part
	if(!k_ctx->yet)
	{
		int i;
		for(i = 0; i < GT_MAX_KTHREADS; i++) 
		{	
			if (kthread_cpu_map[i])
			{
				if(kthread_cpu_map[i]->yet &&
				 (u_obj = kthread_best_sched_uthread(kthread_runq)))
				{

					#if U_DEBUG
						printf("%s\n", "uthread is moved");
					#endif 
					// rem_from_runqueue(k_ctx->krunqueue.active_runq, &(k_ctx->krunqueue.kthread_runqlock), u_obj);
					add_to_runqueue(kthread_cpu_map[i]->krunqueue.active_runq, &(kthread_cpu_map[i]
									->krunqueue.kthread_runqlock), u_obj);
				}
			}
		}
	}

	if(k_ctx->yid) 
	{
		#if 1
			printf("\n%s\n", "yielded");
		#endif
		k_ctx->yid = 0;
	}

	if((u_obj = kthread_runq->cur_uthread))
	{
		// if the uthread is same thread that uthread running in kthread
		calculate(&u_obj);

		/*Go through the runq and schedule the next thread to run */
		kthread_runq->cur_uthread = NULL;
		
		if(u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED))
		{
			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */

			
			uthread_head_t * kthread_zhead = &(kthread_runq->zombie_uthreads);
			gt_spin_lock(&(kthread_runq->kthread_runqlock));
			kthread_runq->kthread_runqlock.holder = 0x01;
			TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
		
			{
				ksched_shared_info_t *ksched_info = &ksched_shared_info;	
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);
				

				REAL[u_obj->uthread_tid] = u_obj->credits.used_sec;
				TAKEN[u_obj->uthread_tid] = u_obj->credits.begin.tv_usec + (u_obj->credits.begin.tv_sec * MILL);
				u_begin[u_obj->uthread_tid] = u_obj->credits.begin.tv_usec + (u_obj->credits.begin.tv_sec * MILL);
				#if U_DEBUG
					printf("\nuthread (id:%d) created at %lus %lu\n", u_obj->uthread_tid, 
						u_obj->credits.begin.tv_sec, 
						u_obj->credits.begin.tv_usec);
				#endif
			}

		// }else if (u_obj->uthread_state & u_obj->) {

		}else{
			/* XXX: Apply uthread_group_penalty before insertion */
			u_obj->uthread_state = UTHREAD_RUNNABLE;

			if (u_obj->credits.credit_left < 1) 
			{
				u_obj->credits.credit_left = u_obj->credits.credit;
				add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			} else {
				add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_obj);
			}
			/* XXX: Save the context (signal mask not saved) */
			if(sigsetjmp(u_obj->uthread_env, 0))
				return;
		}
	}

	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	if(!(u_obj = kthread_best_sched_uthread(kthread_runq)))
	{
		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if(ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}
		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}

	kthread_runq->cur_uthread = u_obj;
	if((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}

	u_obj->uthread_state = UTHREAD_RUNNING;
	u_obj->credits.sched_time += 1;

	struct itimerval curr, nxt;
	getitimer(ITIMER_VIRTUAL, &curr);
	gettimeofday(&(u_obj->credits.updated), NULL);

	// printf("\nThread(id:%d, credit left: %d, default: %d)\n", u_obj->uthread_tid, u_obj->credits.credit_left, u_obj->credits.def_credit);
	// printf("\nKTHREAD %ld", KTHREAD_VTALRM_SEC * 1000 + KTHREAD_VTALRM_USEC/1000);

	if(u_obj->credits.credit_left == u_obj->credits.def_credit)
		kthread_init_vtalrm_timeslice();
	else
	{
		nxt.it_value.tv_sec = (100 - u_obj->credits.credit_left) / 1000;
		nxt.it_value.tv_usec = 1000 * ((100-u_obj->credits.credit_left) % 1000);
		setitimer(ITIMER_VIRTUAL, &nxt, NULL);
	}


	// if(u_obj->credits.credit_left < KTHREAD_VTALRM_SEC * 1000 + KTHREAD_VTALRM_USEC/1000) //25)
	// {
	// 	if(u_obj->credits.credit_left < 25) //KTHREAD_VTALRM_SEC * 1000 + KTHREAD_VTALRM_USEC/1000) // 25) 
	// 	{
	// 		nxt.it_value.tv_sec = 0;
	// 		nxt.it_value.tv_usec = 50000;
	// 	} else {
	// 		nxt.it_value.tv_sec = u_obj->credits.credit_left / 1000;
	// 		nxt.it_value.tv_usec = 1000 * (u_obj->credits.credit_left % 1000);
	// 	}
	// 	setitimer(ITIMER_VIRTUAL, &nxt, NULL);
	// } else {
	// 	// printf("AAAAAAAAAAAAAAAA");
	// 	kthread_init_vtalrm_timeslice();
	// }


	/* Re-install the scheduling signal handlers */
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);
	/* Jump to the selected uthread context */
	siglongjmp(u_obj->uthread_env, 1);

	return;
}


/* For uthreads, we obtain a seperate stack by registering an alternate
 * stack for SIGUSR2 signal. Once the context is saved, we turn this 
 * into a regular stack for uthread (by using SS_DISABLE). */
static void uthread_context_func(int signo)
{
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

	// printf("..... uthread_context_func .....\n");
	/* kthread->cur_uthread points to newly created uthread */
	if(!sigsetjmp(kthread_runq->cur_uthread->uthread_env,0))
	{
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		assert(kthread_runq->cur_uthread->uthread_state == UTHREAD_INIT);
		kthread_runq->cur_uthread->uthread_state = UTHREAD_RUNNABLE;
		return;
	}

	/* UTHREAD_RUNNING : siglongjmp was executed. */
	cur_uthread = kthread_runq->cur_uthread;
	assert(cur_uthread->uthread_state == UTHREAD_RUNNING);
	/* Execute the uthread task */
	cur_uthread->uthread_func(cur_uthread->uthread_arg);
	cur_uthread->uthread_state = UTHREAD_DONE;

	uthread_schedule(&sched_find_best_uthread);
	return;
}

/**********************************************************************/
/* uthread creation */

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credits)
{
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_new;

	/* Signals used for cpu_thread scheduling */
	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);

	/* create a new uthread structure and fill it */
	if(!(u_new = (uthread_struct_t *)MALLOCZ_SAFE(sizeof(uthread_struct_t))))
	{
		fprintf(stderr, "uthread mem alloc failure !!");
		exit(0);
	}

	gettimeofday(&(u_new->credits.begin), NULL);
	u_new->uthread_state = UTHREAD_INIT;
	u_new->uthread_priority = DEFAULT_UTHREAD_PRIORITY;
	u_new->uthread_gid = u_gid;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;
	u_new->credits.credit = u_new->credits.credit_left = u_new->credits.def_credit = credits;
	u_new->credits.usec_per_core = malloc(sizeof(int) * GT_MAX_KTHREADS);


	/* Allocate new stack for uthread */
	u_new->uthread_stack.ss_flags = 0; /* Stack enabled for signal handling */
	if(!(u_new->uthread_stack.ss_sp = (void *)MALLOC_SAFE(UTHREAD_DEFAULT_SSIZE)))
	{
		fprintf(stderr, "uthread stack mem alloc failure !!");
		return -1;
	}
	u_new->uthread_stack.ss_size = UTHREAD_DEFAULT_SSIZE;


	{
		ksched_shared_info_t *ksched_info = &ksched_shared_info;

		gt_spin_lock(&ksched_info->ksched_lock);
		u_new->uthread_tid = ksched_info->kthread_tot_uthreads++;
		ksched_info->kthread_cur_uthreads++;
		gt_spin_unlock(&ksched_info->ksched_lock);
	}

	/* XXX: ksched_find_target should be a function pointer */
	kthread_runq = ksched_find_target(u_new);

	*u_tid = u_new->uthread_tid;

	#if U_DEBUG
		printf("\nuthread (id:%d) created at %lus %lu\n", u_new->uthread_tid, u_new->credits.begin.tv_sec, 
				u_new->credits.begin.tv_usec);
	#endif

	/* Queue the uthread for target-cpu. Let target-cpu take care of initialization. */
	add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_new);


	/* WARNING : DONOT USE u_new WITHOUT A LOCK, ONCE IT IS ENQUEUED. */

	/* Resume with the old thread (with all signals enabled) */
	kthread_unblock_signal(SIGVTALRM);
	kthread_unblock_signal(SIGUSR1);

	return 0;
}

#if 0
/**********************************************************************/
kthread_runqueue_t kthread_runqueue;
kthread_runqueue_t *kthread_runq = &kthread_runqueue;
sigjmp_buf kthread_env;

/* Main Test */
typedef struct uthread_arg
{
	int num1;
	int num2;
	int num3;
	int num4;	
} uthread_arg_t;

#define NUM_THREADS 10
static int func(void *arg);

int main()
{
	uthread_struct_t *uthread;
	uthread_t u_tid;
	uthread_arg_t *uarg;

	int inx;

	/* XXX: Put this lock in kthread_shared_info_t */
	gt_spinlock_init(&uthread_group_penalty_lock);

	/* spin locks are initialized internally */
	kthread_init_runqueue(kthread_runq);

	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = (uthread_arg_t *)MALLOC_SAFE(sizeof(uthread_arg_t));
		uarg->num1 = inx;
		uarg->num2 = 0x33;
		uarg->num3 = 0x55;
		uarg->num4 = 0x77;
		uthread_create(&u_tid, func, uarg, (inx % MAX_UTHREAD_GROUPS));
	}

	kthread_init_vtalrm_timeslice();
	kthread_install_sighandler(SIGVTALRM, kthread_sched_vtalrm_handler);
	if(sigsetjmp(kthread_env, 0) > 0)
	{
		/* XXX: (TODO) : uthread cleanup */
		exit(0);
	}
	
	uthread_schedule(&ksched_priority);
	return(0);
}

static int func(void *arg)
{
	unsigned int count;
#define u_info ((uthread_arg_t *)arg)
	printf("Thread %d created\n", u_info->num1);
	count = 0;
	while(count <= 0xffffff)
	{
		if(!(count % 5000000))
			printf("uthread(%d) => count : %d\n", u_info->num1, count);
		count++;
	}
#undef u_info
	return 0;
}
#endif
