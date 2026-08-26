#ifndef IPC_SYNC_H
#define IPC_SYNC_H

#include "scheduler_shm.h"

int init_shared_memory_sync(SharedMemorySegment *shm);
void destroy_shared_memory_sync(SharedMemorySegment *shm);

#endif