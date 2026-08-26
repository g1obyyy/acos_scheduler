/*
 * Реализация работы с POSIX Shared Memory.
 */

#include "scheduler_shm.h"
#include "ipc_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>


/*
 * Файловый дескриптор POSIX Shared Memory object.
 *
 * shm_open() возвращает обычный файловый дескриптор,
 * через который объект затем передается в mmap().
 *
 * Значение -1 означает, что shared memory сейчас не открыта.
 */
static int shm_fd = -1;


/*
 * Указатель на SharedMemorySegment, отображенный в память
 * текущего процесса через mmap().
 */
static SharedMemorySegment *global_shm = NULL;


/*
 * Создает новый POSIX Shared Memory object либо подключается
 * к уже существующему.
 *
 * name:
 *     имя shared memory, например "/advanced_scheduler_shm_1".
 *
 * is_creator:
 *     1 — текущий процесс является владельцем shared memory
 *         и отвечает за ее создание;
 *
 *     0 — процесс только подключается к уже созданной памяти.
 *
 * Возвращает указатель на SharedMemorySegment,
 * отображенный в адресное пространство процесса.
 *
 * При ошибке возвращает NULL.
 */
SharedMemorySegment* create_or_open_shm(const char *name, int is_creator) {

    /*
     * Creator отвечает за создание нового объекта shared memory.
     *
     * Сначала удаляем объект с таким именем, если он остался
     * после предыдущего некорректного завершения программы.
     *
     * shm_unlink() удаляет ИМЯ объекта shared memory,
     * но уже подключенные к старому объекту процессы могли бы
     * продолжать использовать его до munmap()/close().
     */
    if (is_creator) {
        shm_unlink(name);

        /*
         * Создаем новый POSIX Shared Memory object.
         */
        shm_fd = shm_open(
            name,
            O_CREAT | O_RDWR | O_EXCL,
            0666
        );

        if (shm_fd == -1) {
            shm_fd = shm_open(
                name,
                O_CREAT | O_RDWR,
                0666
            );
        }

    } else {

        /*
         * Scheduler/Worker не создают shared memory заново.
         */
        shm_fd = shm_open(name, O_RDWR, 0666);
    }

    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }


    /*
     * Только creator задает размер shared memory.
     */
    if (is_creator) {
        if (ftruncate(shm_fd, sizeof(SharedMemorySegment)) == -1) {
            perror("ftruncate failed");
            close(shm_fd);
            shm_fd = -1;
            return NULL;
        }
    }

    /*
     * Отображаем POSIX Shared Memory object в виртуальное адресное пространство текущего процесса.
     */
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


/*
 * Выполняет первоначальную инициализацию содержимого SharedMemorySegment.
 * Эту функцию должен вызывать creator после создания shared memory.
 */
void init_shm_content(SharedMemorySegment *shm) {

    /*
     * Обнуляем весь SharedMemorySegment:
     */
    memset(shm, 0, sizeof(SharedMemorySegment));

    /*
     * Инициализируем примитивы межпроцессной синхронизации:
     */
    if (init_shared_memory_sync(shm) != 0) {
        fprintf(stderr, "Failed to initialize shared memory synchronization primitives\n");
    }
}


/*
 * Отключает текущий процесс от shared memory и при необходимости полностью удаляет объект.
 * Эту функцию для creator нужно вызывать только после того, как Scheduler и Worker уже завершили работу.
 */
void close_and_unlink_shm(const char *name, SharedMemorySegment *shm, int is_creator) {

    /*
     * Сначала проверяем, что отображение вообще существует.
     */
    if (shm != NULL && shm != MAP_FAILED) {
        if (is_creator) {
            destroy_shared_memory_sync(shm);
        }

        /*
         * Удаляем отображение shared memory из виртуального адресного пространства текущего процесса.
         *
         */
        if (munmap(shm, sizeof(SharedMemorySegment)) == -1) {
            perror("munmap failed");
        }
    }

    if (global_shm == shm) {
        global_shm = NULL;
    }


    /*
     * Закрываем файловый дескриптор, полученный через shm_open().
     */
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
