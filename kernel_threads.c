
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "util.h"



/** 
  @brief Delete the PTCB
  */
void release_PTCB(rlnode* node)
{
  free(node->ptcb);
  rlist_remove(node);
}


/** 
  @brief Generate id for current thread
  */
Tid_t idgen()
{
  static unsigned int id = 2;
  //printf("Generating tid = %d \n", id+1);
  return (Tid_t)id++;
}

/*The Main function of every TCB*/
void start_thread()
{
  int exitval;
  PTCB* new_ptcb = CURTHREAD->owner_ptcb;
  Task call =  new_ptcb->task;
  exitval = call(new_ptcb->argl, new_ptcb->args);

  ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  printf("CREATE THREAD   ");
	PCB* pcb = CURPROC;
  //int*  exitval_fail = (int*) -1;

  /* Create the PTCB */
  PTCB* new_ptcb = (struct process_thread_control_block*)xmalloc(sizeof(struct process_thread_control_block));

  if(new_ptcb == NULL) /*If malloc fails*/
  {
    printf("OUT OF MEMORY!\n");
    return NOTHREAD;
  }



/* Initializing the PTCB*/
new_ptcb->task = task;
new_ptcb->argl = argl;
new_ptcb->args = args;
new_ptcb->pcb = pcb;
new_ptcb->cv = COND_INIT;

//printf("task: %s\n args length: %d\n args: %s\n", task,argl,args );

new_ptcb->joinable = 1;
new_ptcb->exited = 0;
new_ptcb->wt_counter = 0;
new_ptcb->tid = idgen();

 /*Create the Thread*/
//printf("Creating thread\n");
TCB* tcb = spawn_thread(pcb, start_thread);

/*Link TCB with PTCB*/
new_ptcb->tcb = tcb;
tcb->owner_ptcb = new_ptcb;

/*Create the node of ptcb*/
rlnode_init(&(new_ptcb->node), new_ptcb);

/*Add the node to the ptcb list*/
rlist_push_back(&(pcb->ptcb_list), &(new_ptcb->node));

pcb->active_threads++; /*It counts the active threads of the pcb*/

wakeup(tcb); /*Make the thread READY for scheduling*/

printf("new thread tid = %d\n", new_ptcb->tid );
return (Tid_t)new_ptcb->tid;
  
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
  //printf("Returning self tid\n");
	return (Tid_t) CURTHREAD->owner_ptcb->tid;

}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  printf("THREAD JOIN" );

	if(sys_ThreadSelf() == tid){
    printf("Thread Join self error!\n");
    return -1;
  }

  PCB* pcb = CURPROC;
  rlnode* ptcb_list_node = &(pcb->ptcb_list);
  while(ptcb_list_node != NULL) /* Check if there is a node*/
  { 
    //printf("Looking for node\n");
    if (ptcb_list_node->ptcb->tid == tid) /* Check if the ID is found*/
    {
      printf("found node\n");
      if (ptcb_list_node->ptcb->joinable == 0) /*Check it is joinable*/
      {
        printf("thread not joinable \n" );
        return -1;
      }
      else 
      { 
        printf("It is joinable\n");
        /*Count how many threads wait for this TCB*/
        ptcb_list_node->ptcb->wt_counter++;
        while(ptcb_list_node->ptcb->exited == 0)/*Check is the thread has finished*/
        {
          printf("Running thread\n");
          kernel_wait(&(ptcb_list_node->ptcb->cv), SCHED_USER);
        } 
      }

      *exitval = ptcb_list_node->ptcb->exitval; /*save the exit value*/
      ptcb_list_node->ptcb->wt_counter--;

      /*Check if there are other thread that wait the specific
      exit value. If not, delete the PTCB*/ 
      if(ptcb_list_node->ptcb->wt_counter <= 0)
      {
        release_PTCB(ptcb_list_node);
      }
      printf("Joined thread \n" );
      return 0;
    }

    ptcb_list_node = ptcb_list_node->node->next;

  }
  return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PCB* pcb = CURPROC;
  TCB* tcb = CURTHREAD;
  rlnode* ptcb_list_node = &(pcb->ptcb_list);

  if (tcb->owner_ptcb->tid == tid)
  {
    printf("Detaching thread\n");
    ptcb_list_node->ptcb->joinable = 0;
    return 0;
  }

  if (pcb->active_threads < 2)
  {
    return -1;
  }

  while(ptcb_list_node != NULL) /* Check if there is a node*/
  { 
    if (ptcb_list_node->ptcb->tid == tid) /* Check if the ID is found*/
    {
      if (ptcb_list_node->ptcb->exited == 0) /*Check if it exited*/
      {
        printf("Detaching thread\n");
        ptcb_list_node->ptcb->joinable = 0;
        return 0;
      }

    }
    
    if ((Tid_t)tcb->owner_ptcb->tid == tid)
    {
      printf("Detaching thread\n");
      ptcb_list_node->ptcb->joinable = 0;
      return 0;
    }

    ptcb_list_node = ptcb_list_node->next;
  }

  return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  TCB* tcb = CURTHREAD;
  PTCB* new_ptcb = tcb->owner_ptcb;

  new_ptcb->exitval = exitval; /* Save the exit value to ptcb*/
  new_ptcb->tcb = NULL; /* Free the tcb*/
  new_ptcb->exited = 1; /*Mark the thread as exited*/


  CURPROC->active_threads--; /*Reduce the number of active threads of the process*/
  kernel_broadcast(&(new_ptcb->cv)); /*Wake up ThreadJoin*/

  /*If the thread is the last one of this process, call Exit,
  otherwise relase the TCB*/
  printf("Exiting\n");
  if (CURPROC->active_threads <= 0)
  {
    sys_Exit(exitval);
  }
  else
  {
    kernel_sleep(EXITED,SCHED_USER);
  }

}

