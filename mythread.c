#include "mythread.h"
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <sys/types.h>
#include <signal.h>

int ALREADY_UP=0;
int INIT_ONCE=1;
DNODE qHead = NULL;
DNODE idleNode = NULL;

char* itoa(int val, int base){
        static char buf[32] = {0};
        int i = 30;
        for(; val && i ; --i, val /= base)
                buf[i] = "0123456789abcdef"[val % base];
        return &buf[i+1];
}
/*
mythread_join() 
1. queueLock
2. searchRunnable
3. if qHead==idleNode set ALREADY_UP=1
4. search for tid in queue
5. Add self node ptr to this tid nodes' join Q
6. set self state to WAIT
7. wake up the RUNNABLE from step 2
8. release queueLock i.e. futex_down(&queueLock)
9. lock self Lock and wait to be awakened
*/

int mythread_yield() {
	futex_down(&queueLock);
	DNODE temp=qHead;
	if(temp->next->next == temp) {
		//futex_up(&(getMember(idleNode,selfLock)));
		print("here in if\n");
		futex_up(&queueLock);
	} else {
		print("here in else\n");
		//DNODE temp=qHead;
		//qHead=qHead->next;
		qHead=searchRunnable();
		if(qHead==idleNode)
			ALREADY_UP=1; 
		futex_up(&(getMember(qHead,selfLock)));
		futex_up(&queueLock);
		futex_down(&(getMember(temp,selfLock)));		
	}
	print("unblocked after yield\n");
	return 0;
}

void mythread_exit(void* retVal) {
	
	*((int*)retVal)=0;
		
	getMember(qHead,state)=DEAD;
	DNODE temp=qHead;
	qHead=qHead->next;
//	futex_up(&schedulerLock);
	futex_down(&(getMember(temp,selfLock)));
}

int idleFunc() {
        DNODE toBeRun;
	while(getppid()) {
		//sleep(1);
		print("In idle thread\n");
		futex_down(&(getMember(idleNode,selfLock)));
		print("In idle thread after idleNode lock release\n");
		futex_down(&queueLock);
		print("In idle thread after lock acquire\n");
		//qHead=qHead->next;	
		ALREADY_UP=0;
        	qHead=searchRunnable();
	        futex_up(&getMember(qHead,selfLock));
	
                futex_up(&queueLock);
	
//		futex_up(&schedulerLock);
		if(qHead==NULL)
			print("QHEAD is null\n");
//		futex_down(&(getMember(idleNode,selfLock)));
	}
	return 0;
}
/*
int scheduler() {
	DNODE toBeRun;
	while(getppid()) {
		print("In scheduler \n");
		futex_down(&queueLock);
		toBeRun=searchRunnable();
		futex_up(&getMember(toBeRun,selfLock));
		futex_up(&queueLock);
		//print("Before selflock\n");
		futex_down(&schedulerLock);
		if(toBeRun != NULL) {
			print("found RUNNABLE\n");	
		//	print("after selflock\n");
		} else {
			print("to be run is null\n");	
			futex_up(&(getMember(idleNode,selfLock)));
			
		}
		//futex_down(&schedulerLock);
	}
	//do something here to ensure proper exit when main quits
	//take care to ensure idle thread doesnot remain locked;;
	return 0;
}
*/

int initThread() {
	futex_up(&queueLock);
	idleNode=createNode();
	getMember(idleNode,state) = RUNNABLE;
	qHead=idleNode;	
	//char* idleStack=malloc(STACK_SIZE);
	getMember(idleNode,threadId) = clone(idleFunc,(getMember(idleNode,stackPtr))+STACK_SIZE,SIGCHLD|CLONE_VM|CLONE_FILES | CLONE_FS | CLONE_SIGHAND | CLONE_IO | CLONE_CHILD_CLEARTID,0);
	//getMember(idleNode,threadId) = clone(idleFunc,idleStack+STACK_SIZE,SIGCHLD|CLONE_FILES|CLONE_FS|CLONE_VM,0);
//	char* schedStack=malloc(STACK_SIZE);
//	mythread_t schedId=clone(scheduler,schedStack+STACK_SIZE,SIGCHLD|CLONE_FILES|CLONE_FS|CLONE_VM,0);
	return 0;	
}

static pid_t gettid(void) {
	    return (pid_t) syscall(SYS_gettid);
}

mythread_t mythread_self() {
	return (mythread_t)gettid();
}



int mythread_create(mythread_t *new_thread_ID,mythread_attr_t *attr,void *(*start_func)(void *), void* arg) {
	
	if(INIT_ONCE) {
		initThread();
		INIT_ONCE=0;
		//while(qHead==NULL) {}
	}	
	
	DNODE newNode = createNode();
	//print("Before queueLock\n");
	futex_down(&queueLock);
	enqueue(newNode);

	print("After queueLock acquire in create_thread\n");	
	//creating task struct below
	struct task *t = (struct task *)malloc(sizeof(struct task));
	t->func=start_func;
	t->arg=(void *)arg;
	t->qPos=newNode;
	//end
	//print("After initialising task\n");
	
	*new_thread_ID = clone(&mythread_wrapper,getMember(newNode,stackPtr)+STACK_SIZE,SIGCHLD|CLONE_VM |CLONE_FILES | CLONE_FS | CLONE_SIGHAND | CLONE_IO|CLONE_CHILD_CLEARTID ,(void *)(t));

	
	//print("Wrapper cloned\n");	
	futex_up(&queueLock);
	futex_down(&mainLock);
	return 0;
}


int mythread_wrapper(void *t) {
	//print("Inside wrapper\n");
	futex_up(&mainLock);
	struct task* tWrapper=(struct task*)t;
	getMember(tWrapper->qPos,threadId)=mythread_self();
	getMember(tWrapper->qPos,state)=RUNNABLE;
	futex_down(&queueLock);
	print("wrapper acquired queueLock\n");	
	if((qHead == idleNode) &&(!ALREADY_UP)) {
		ALREADY_UP=1;
		//if(qHead->next==idleNode) //{
		//	qHead=tWrapper->qPos;
		futex_up(&(getMember(idleNode,selfLock)));
		print("released idleNode lock\n");
		//}
	}
	print("releasing queueLock inside wrapper\n");	
	futex_up(&queueLock);	
	
	futex_down(&(getMember(tWrapper->qPos,selfLock)));
	print("Scheduler released the selfLock\n");
	(tWrapper->func)(tWrapper->arg);
	wake(); //works on the assumption that qHead points to self
		//wake changes state from WAIT to RUNNABLE
		//doesn't change the lock values and hence doesn't cause
		//threads to run;;;;
	//exit(1);
	futex_down(&queueLock);
	dequeue();
	//below ensures that idleNode lock is released only when 
	//the only node remaining is idleNode;;
	if(qHead->next!=qHead) {
		ALREADY_UP=1;
		futex_up(&getMember(idleNode,selfLock));
		print("idleNode lock released in wrapper if condition\n");
	}
	print("node dequeued\n");
	//qHead=qHead->next; //done in dequeu;
	futex_up(&queueLock);
	//futex_up(&schedulerLock);

	return 0;
}
/*
* Enqueues thread in the run Q
*/
int enqueue(DNODE n) {
	return insertAtEnd(qHead,n);
} 

/*
* Creates and initialises the node to be joined in run Q
*/
DNODE createNode(){
	struct thread * t = (struct thread*)malloc(sizeof(struct thread));
	t->state = CREATED;
	t->threadId = -1;
	futex_init(&(t->selfLock),0);
	t->stackPtr = (char *)malloc(STACK_SIZE);
	t->joinQ = NULL;
	t->exitStatus = -1;
	
	DNODE newNode = getDllNode((void *)t);
	return newNode;	
}

/*
* Deques the node in the head of run Q
*/
int dequeue() {
	free(getMember(qHead,stackPtr));
	free(qHead->data);
	return dllDelete(qHead,&qHead);
}

/*
* Search for the threadId in the run Q and return the node
*/
DNODE search(int threadId) {
	return find(qHead,compare,(void *)&threadId);
}

/*Compare helper function for search()
*
*/
int compare(DNODE n,void *d) {

        int id = *(int *)d;
       // printf("Comparing %d and %d\n",getMember(n,threadId),id);
        if(getMember(n,threadId) == id)
                return 1;
        else
                return 0;

}

/* Helper function for searchRunnable
*
*/
int compareState(DNODE n,void *d) {

        int state = *(int *)d;
       // printf("Comparing %d and %d\n",getMember(n,threadId),id);
        if(getMember(n,state) == state)
                return 1;
        else
                return 0;

}

/*
* Search for first Runnable thread in the run Q 
*/

DNODE searchRunnable() {
	if(qHead==idleNode)
		return qHead->next;
	int state = RUNNABLE;
	return find(qHead,compareState,(void *)&state);
}

/*
* Print the run Q . Only for debugging . Contains prints so should not be used when multiple threads are running parallely.
*/
int printQ() {
	DNODE p = qHead;
	if(p==NULL) {
		printf("qHead is NULL\n");
		return -1;
	}
	
	printf("Queue elements\n");
	printf("==================\n");
	do {
		printf(" TID:%d ; STATE:%d ; Lock:%d ; exitStatus:%d \n",getMember(p,threadId) , getMember(p,state) , (getMember(p,selfLock)).count , getMember(p,exitStatus) );
		p = p->next;
	} while(p!=qHead);

	return 0;
}

/*
* Sets the state of all threads in Join Q RUNNABLE 
*/
int wake() {

	return 0;
}

/*
* Join the calling thread Node(pointed by head) to join Q of target thread 
*/
int joinQ(mythread_t thr ) {

}

void* sayHello7788() {
	print("I say hello7788\n");
	//exit(1);
}
void* sayHello() {
	//while(1) {
		print("I say hello\n");
		mythread_t tidx;
		mythread_yield();
		print("I say hello\n");
	//}
	//mythread_create(&tidx,NULL,sayHello7788,NULL);
	//mythread_yield();
	//print("IN the grandest parent sayHello\n");
	//sleep(1);
	//exit(1);
}

void* sayHello2() {
	//while(1) {
		print("I say hello2\n");
	//	mythread_yield();
//		print("I say hello2\n");
	//}
}

void* sayHello3() {
	//while(1) {
		print("I say hello3\n");
		mythread_yield();
		print("I say hello3\n");
	//}
}

void* sayHello6() {
	print("I say hello66\n");
	//exit(1);
}



int main() {
	/*
	DNODE node1 = createNode();
	getMember(node1,threadId) = 1;
	qHead = node1;

	DNODE node2 = createNode();
	getMember(node2,threadId) = 2;
	enqueue(node2);	
		
	printQ();

	DNODE foundNode = search(2);
	printf("id of foundNode : %d \n" , getMember(foundNode,threadId));

	DNODE node3 = createNode();
	getMember(node3,threadId) = 3;
	getMember(node3,state) = RUNNABLE;
	enqueue(node3);

	foundNode  = searchRunnable();
	printf("id of runnable Node : %d \n" , getMember(foundNode,threadId));

	dequeue();
	printQ();

	dequeue();
	printQ();
	*/
	int tid1,tid2,tid3;
	mythread_create(&tid1,NULL,sayHello,NULL);
	mythread_create(&tid2,NULL,sayHello2,NULL);
	mythread_create(&tid3,NULL,sayHello3,NULL);
/*
//	mythread_create(&tid1,NULL,sayHello,NULL);
//	sleep(9);
	mythread_create(&tid1,NULL,sayHello6,NULL);
*/
	print("All threads atleast enqueued\n");
	//while(1) {}
//	waitpid(tid1,0,0);
//	sleep(1);	
}

