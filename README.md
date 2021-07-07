1. Often more memory is needed for computations in real world.
But amount of memory assigned to each process is limited.
2. OS makes use of swap memory concept to swap some part of
primary memory to secondary memory/storage.
3. OS also allows a process to create another process (child:
forking) and inter-process communication across them.
4. To get unlimited memory (virtually/logically), a process
forks and creates a child process. Memory assigned to child
process can then be used by parent. Accessing child's memory
directly is not possible for parent. Any allocation, release,
dereference shall be done via child process through IPC mechanisms.
With this approach parent nearly controls twice the memory offered
to it. Another perspective of viewing this would be:
multi-precision pointers in C.


What I am expecing to build:
- Get a memory of some bytes (malloc-alike)
- Release a memory pointed to by a pointer (free-alike)
- Deref 1B
- Deref 2B
- Deref 4B
- Deref 8B
