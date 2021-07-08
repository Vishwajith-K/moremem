#include <stdio.h> // puts
#include <stdlib.h> // malloc
#include <signal.h> // kill
#include <stdint.h> // uint*_t types
#include <errno.h> // errno
#include <string.h> // strerror
#include <sys/ipc.h> // shmget
#include <sys/shm.h> // shmget
#include <sys/types.h> // fork, getppid
#include <unistd.h> // fork, getppid, sleep
#include <sys/types.h> // kill, shmat, shmdt
#include <sys/shm.h> // shmat, shmdt
#include <sys/wait.h> // wait

#define LOG //printf("%s/%s/%d\n", __FILE__, __FUNCTION__, __LINE__)
#define PRINTPID //printf("%d\n", getpid())

// Parent may request for any of the below
typedef enum _req_type { NONE, MALLOC, FREE, DEREF8,
						 DEREF16, DEREF32,
						 DEREF64, STORE8,
						 STORE16, STORE32,
						 STORE64, QUIT
} req_type_t;

// Parent requests with this format
// if allocation, size is filled
// if free, p must be set by parent
// if deref*/store*, p must be set by parent
// request must initially be NONE and be set
// in the end while making a new request
typedef struct _req {
	size_t size;
	void *p;
	uint64_t data;
	req_type_t request;
} req_t;

// used to let each other know that both are ready
volatile unsigned char ch_shmat;
// man signal.7 and see what SIGRTMIN does
void sigrtmin_handler(int signo) {
	ch_shmat = 1;
}

// SEGFAULT handler is not practical
// SEGFAULT handler will be called again and again
// for a single SEGFAULT
// setjmp.h may not be helpful too
/* // re-route SEGFAULTs to parent */
/* void ch_sigsegv_handler(int signo) { */
/* 	kill(getppid(), SIGSEGV); */
/* } */

int main() {
	int shmid;
	pid_t pid;
	void *shmp;

	// Create a shared memory in parent
	// only memory is created and will be ready to be
	// shared across processes
	shmid = shmget(IPC_PRIVATE, sizeof(req_t), 0666 | IPC_CREAT);
	if (shmid == -1) {
		puts("Unable to get shared memory id");
		goto shm_failed;
	}
	// child will raise the below signal once it's ready
	// we register now here even before forking
	// What if the child is ready, even before parent is
	// scheduled?
	// Ready here refers indicates that the child has attached
	// to created shared memory segment.
	signal(SIGRTMIN, sigrtmin_handler);
	// Not an essential stuff (probably)
	// used it to help with debug stuff
	// fflush is not required as the recieved
	// value is straight forwarded to stdout
	setvbuf(stdout, NULL, _IONBF, 0);
	pid = fork();
	if (pid < 0) {
		puts("Unable to fork");
		goto fork_failed;
	}
	else if (pid > 0) {
		// Parent
		// mark the volatile here
		// this will point to shared memory
		// and so, modifications to data pointed by this pointer
		// are asynchronous to parent
		volatile req_t *parents_req;
		// signal(SIGSEGV, parent_sigsegv_handler);
		// attach to shared memory so that we can IPC with child
		shmp = shmat(shmid, NULL, 0);
		if (shmp == (void *) -1) {
			puts("Parent: failed to attach to shared memory segment");
			goto shmat_failed;
		}
		// shmp is voidp; cumbersome to typecast everytime
		// to the actual req_t concrete type
		// better use a pointer which can point to req_t
		parents_req = shmp;
		*parents_req = (req_t) {0, 0, 0, 0};
		parents_req->request = NONE;
		// wait till child attaches
		// child will signal parent
		// parent's signal handler will
		// change the value of the variable
		while (ch_shmat != 1);
		// and then signal child that we got to know that it's ready
		// recieve requests
		// on success, pid in parent will have child's pid
		kill(pid, SIGRTMIN);
		// **********************Goal*********************
		// actual address extension starts now
		{
			uint32_t *p;

			LOG;

			// ***First request***
			// request child for 10 blocks of ui32's
			// request shall be updated at the last
			parents_req->size = 10 * sizeof(uint32_t);
			// changing this member must be done in the end
			// since, any changes to this member will act as
			// a notification to child
			parents_req->request = MALLOC;
			// wait till request is processed
			// once request is processed, child will set request
			// member to NONE back and that is our (parent's)
			// notification
			while(parents_req->request != NONE);
			// will take out the address shared by child
			// may be NULL checking is required (ignoring it for now)
			p = parents_req->p;

			// lets store 10 values in it (it = 10 contiguous ui32s
			for (uint32_t i = 0; i < 10; ++i) {
				// will store value of i
				parents_req->data = i;
				// make sure of the pointer that gets into request container
				// p + 10 is invalid (=>SEGFAULT in child)
				// p + 0 to p + 9 are valid
				parents_req->p = p + i;
				parents_req->request = STORE32;
				while(parents_req->request != NONE);
			}

			// lets access them and print them
			for (uint32_t i = 0; i < 10; ++i) {
				parents_req->p = p + i;
				parents_req->request = DEREF32;
				while(parents_req->request != NONE);
				printf("%i\t", (uint32_t)parents_req->data);
			}

			// lets free it
			parents_req->p = p;
			parents_req->request = FREE;
			while(parents_req->request != NONE);

			// we ask child to stop all
			// and quit
			// If this isn't done, it keeps waiting
			// and make us (parent) wait forever (at wait(NULL) statement)
			parents_req->request = QUIT;
			while(parents_req->request != NONE);
		}

		// ********************************************************
		// detach parent from accessing shared memory
		// this may remove VA-PA mappings but not the actual sh-mem
		shmdt(shmp);
	shmat_failed: ;
	}
	else {
		// Child
		volatile req_t *parents_req;
		// may not be required
		// we'll sleep so that parent gets first chance
		sleep(1);
		// parent will signal us that, it got to know
		// we're (child) ready - handler for us to know
		// about it
		signal(SIGRTMIN, sigrtmin_handler);
		// signal(SIGSEGV, ch_sigsegv_handler);
		shmp = shmat(shmid, NULL, 0);
		if (shmp == (void *) -1) {
			puts("Child: failed to attach to shared memory segment");
			goto ch_shmat_failed;
		}
		// signal parent that, child is ready to receive instructions
		kill(getppid(), SIGRTMIN);
		// and wait till parent ACKs
		// mark that, this variable is not shared across processes
		// each process gets different copies of this global-volatile byte
		while (ch_shmat != 1);
		parents_req = shmp;

		// ************************Core job handling******
		while (1) {
			// process the request if there's a request in the container
			// else wait
			// It's my wish to have an else clause to while
			if (parents_req->request != NONE) {
				LOG;
				switch (parents_req->request) {
				case MALLOC:
					parents_req->p = malloc(parents_req->size);
					break;
				case FREE:
					free(parents_req->p);
					break;
				case DEREF8:
					parents_req->data = *(uint8_t*)(parents_req->p);
					break;
				case DEREF16:
					parents_req->data = *(uint16_t*)(parents_req->p);
					break;
				case DEREF32:
					parents_req->data = *(uint32_t*)(parents_req->p);
					break;
				case DEREF64:
					parents_req->data = *(uint64_t*)(parents_req->p);
					break;
				case STORE8:
					*(uint8_t*)(parents_req->p) = (uint8_t)(parents_req->data);
					break;
				case STORE16:
					*(uint16_t*)(parents_req->p) = (uint16_t)(parents_req->data);
					break;
				case STORE32:
					*(uint32_t*)(parents_req->p) = (uint32_t)(parents_req->data);
					break;
				case STORE64:
					*(uint64_t*)(parents_req->p) = (uint64_t)parents_req->data;
					break;
				case QUIT:
					parents_req->request = NONE;
					goto ch_quit;
				}
				// once the request is processed
				// reset the request member inorder to
				// notify parent that request is processed
				// else it'd wait forever
				parents_req->request = NONE;
			}
		}
	ch_quit:
		shmdt(shmp);
	ch_shmat_failed: ;
	} // Child end

	wait(NULL);
 fork_failed:
	if (shmctl(shmid, IPC_RMID, 0) == -1) {
		printf("Error while removing shared memory:");
		puts(strerror(errno));
		// puts("Parent: failed to destroy the created memory segment");
	}
 shm_failed:
	return 0;
}
