#include "scheduler_shm.h"
#include "ipc_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static int shm_fd = -1;
static SharedMemorySegment *global_shm = NULL;

SharedMemorySegment* create_or_open_shm(const char *name, int is_creator) {
    if (is_creator) {
        shm_unlink(name);
        shm_fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm_fd == -1) {
            // If it already existed, try opening without O_EXCL or unlink and retry
            shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        }
    } else {
        shm_fd = shm_open(name, O_RDWR, 0666);
    }

    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }

    if (is_creator) {
        if (ftruncate(shm_fd, sizeof(SharedMemorySegment)) == -1) {
            perror("ftruncate failed");
            close(shm_fd);
            shm_fd = -1;
            return NULL;
        }
    }

    void *ptr = mmap(NULL, sizeof(SharedMemorySegment), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        shm_fd = -1;
        return NULL;
    }

    global_shm = (SharedMemorySegment *)ptr;
    return global_shm;
}

void init_shm_content(SharedMemorySegment *shm) {
    memset(shm, 0, sizeof(SharedMemorySegment));
    if (init_shared_memory_sync(shm) != 0) {
        fprintf(stderr, "Failed to initialize shared memory synchronization primitives\n");
    }
}

void close_and_unlink_shm(const char *name, SharedMemorySegment *shm, int is_creator) {
    if (shm != NULL && shm != MAP_FAILED) {
        if (is_creator) {
            destroy_shared_memory_sync(shm);
        }
        if (munmap(shm, sizeof(SharedMemorySegment)) == -1) {
            perror("munmap failed");
        }
    }
    if (global_shm == shm) {
        global_shm = NULL;
    }
    if (shm_fd != -1) {
        if (close(shm_fd) == -1) {
            perror("close fd failed");
        }
        shm_fd = -1;
    }
    if (is_creator) {
        shm_unlink(name);
    }
}
