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

#include "gt_include.h"

/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/


/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID	0
extern gt_spinlock_t log_lock;
FILE * log;
/**********************************************************************/
/* uthread scheduling */
static void uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);

/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credits);

extern void gt_yield(){
  kthread_block_signal(SIGVTALRM);
  kthread_block_signal(SIGUSR1);
  kthread_context_t *k_ctx;
  k_ctx = kthread_cpu_map[kthread_apic_id()];
  k_ctx->yielded = 1;
  struct itimerval next;
  next.it_interval.tv_sec =0;
  next.it_interval.tv_usec= 0;
  next.it_value.tv_sec =0;
  next.it_value.tv_usec= 10000;
  setitimer(ITIMER_VIRTUAL, &next, NULL);
  kthread_unblock_signal(SIGVTALRM);
  kthread_unblock_signal(SIGUSR1);
}
/**********************************************************************/
/** DEFNITIONS **/
/**********************************************************************/

/**********************************************************************/
/* uthread scheduling */

/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;
	gettimeofday(&u_new->start, NULL);
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
int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }
  
  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */

  return x->tv_sec < y->tv_sec;
}
extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;
	int results, counter;
	struct timeval now, results_timeval;
	/* Signals used for cpu_thread scheduling */
	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);

#if 0
	fprintf(stderr, "uthread_schedule invoked !!\n");
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);

	int inx;
	if (!k_ctx->scavenger){
	  for ( inx =0 ; inx < GT_MAX_KTHREADS; inx++){
	    if (kthread_cpu_map[inx]){
	      if (kthread_cpu_map[inx]->scavenger){
		//gt_spin_lock(&(kthread_cpu_map[inx]->krunqueue.kthread_runqlock));
		if((u_obj = kthread_best_sched_uthread(kthread_runq))) { 
		  fprintf(stderr, "uthread migrated!\n");
		  add_to_runqueue(kthread_cpu_map[inx]->krunqueue.active_runq,
				  &(kthread_cpu_map[inx]->krunqueue.kthread_runqlock),
				    u_obj);
		}
		//gt_spin_unlock(&(kthread_cpu_map[inx]->krunqueue.kthread_runqlock));
	      }
	    }
	    
	  }
	}
	
	if (k_ctx->yielded){
	  fprintf(stderr, "yielded\n");
	  k_ctx->yielded = 0;
	}
	if((u_obj = kthread_runq->cur_uthread))
	{
	  gettimeofday(&now, NULL);
	  timeval_subtract(&results_timeval, &now, &u_obj->last_updated);

	  fprintf(stderr, "usec before %d \n", u_obj->usec);
	  u_obj->usec += results_timeval.tv_sec * 1000000 + results_timeval.tv_usec;
	  fprintf(stderr, "usec after %d \n", u_obj->usec);

	  results = results_timeval.tv_sec * 1000 + results_timeval.tv_usec/1000;
	  u_obj->usec_per_core[kthread_apic_id()] +=results_timeval.tv_sec * 1000000 + results_timeval.tv_usec;
	  u_obj->credits_remaining -=results;
	  fprintf(stderr, "used %d\n adn now %d remaining", results, u_obj->credits_remaining);
	  
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
				gt_spin_lock(&log_lock);
				gettimeofday(&now, NULL);
				timeval_subtract(&results_timeval, &now, &u_obj->last_updated);
				fprintf(log, "uthread %d had %d schedulings and took %d usec took %d usec\n", u_obj->uthread_tid, u_obj->scheduling_times,u_obj->usec, results_timeval.tv_sec * 1000000 + results_timeval.tv_usec);
				for (counter =0; counter < GT_MAX_KTHREADS; counter++){
				  fprintf(log, "%d|", u_obj->usec_per_core[counter]);
				}
				fprintf(log, "\n");
				gt_spin_unlock(&log_lock);

			}
		}
		else
		{
			/* XXX: Apply uthread_group_penalty before insertion */
			u_obj->uthread_state = UTHREAD_RUNNABLE;
			if (u_obj->credits_remaining <=0){
			  u_obj->credits_remaining = u_obj->credits;
			  add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			}
			else
			  add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_obj);
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
	
	u_obj->scheduling_times++;
        struct itimerval current, next;
        getitimer(ITIMER_VIRTUAL, &current);
        fprintf(stderr, "\ncredits remaining is %d\nthe default is %d", u_obj->credits_remaining, 
                KTHREAD_VTALRM_SEC * 1000 + KTHREAD_VTALRM_USEC/1000);
        if (u_obj->credits_remaining < KTHREAD_VTALRM_SEC * 1000 + KTHREAD_VTALRM_USEC/1000){
	  if (u_obj->credits_remaining < 25){
	    next.it_value.tv_sec = 0;
	    next.it_value.tv_usec = 50000;
	  }
	  else{
          next.it_value.tv_sec = u_obj->credits_remaining /1000;
          next.it_value.tv_usec = 1000 * (u_obj->credits_remaining % 1000);
	  }
          fprintf(stderr, "schedule a short timeslice of time %d\n",
                  next.it_value.tv_sec * 1000 + next.it_value.tv_usec / 1000);

          setitimer(ITIMER_VIRTUAL, &next, NULL);
        }
        else{
          fprintf(stderr, "schedule a normal timeslice\n");
          kthread_init_vtalrm_timeslice();
        }
        gettimeofday(&u_obj->last_updated, NULL);
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
	int counter;
	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

	printf("..... uthread_context_func .....\n");
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

	u_new->uthread_state = UTHREAD_INIT;
	u_new->uthread_priority = DEFAULT_UTHREAD_PRIORITY;
	u_new->uthread_gid = u_gid;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;
	u_new->credits = credits;
	u_new->credits_remaining = credits;
	int counter =0;
	u_new->usec = 0;
	u_new->usec_per_core =  malloc(sizeof(int) * GT_MAX_KTHREADS);
	u_new->scheduling_times = 0;
	for (counter = 0; counter < GT_MAX_KTHREADS; counter++){
	  u_new->usec_per_core[counter] = 0;
	}

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
