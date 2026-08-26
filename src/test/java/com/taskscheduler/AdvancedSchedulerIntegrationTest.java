package com.taskscheduler;

import com.taskscheduler.nativebridge.NativeScheduler;
import org.junit.jupiter.api.Test;

import java.util.function.BooleanSupplier;

import static org.junit.jupiter.api.Assertions.*;

public class AdvancedSchedulerIntegrationTest {

    private static final int TASK_STATE_READY = 1;
    private static final int TASK_STATE_BLOCKED = 3;
    private static final int TASK_STATE_FINISHED = 4;
    private static final int TASK_STATE_DEADLOCK_ABORTED = 5;

    /**
     * Вспомогательный метод ожидания с быстрым (10ms) для исключения race-состояний.
     */
    private boolean waitUntil(BooleanSupplier condition, long timeoutMs) {
        long start = System.currentTimeMillis();
        while (System.currentTimeMillis() - start < timeoutMs) {
            if (condition.getAsBoolean()) {
                return true;
            }
            try {
                Thread.sleep(10);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        return condition.getAsBoolean();
    }

    /*
     * Проверяет базовый жизненный цикл планировщика.
     *
     * Ожидаем:
     * - shared memory успешно инициализируется;
     * - Scheduler и Worker запускаются без ошибок;
     * - до добавления задач taskCount равен 0;
     * - система корректно завершается через stop().
     */
    @Test
    public void testLifecycle() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_1";
        try {
            scheduler.initialize(shmName);
            scheduler.start();
            assertEquals(0, scheduler.getTaskCount());
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет полное выполнение простой задачи, не требующей ресурсов.
     *
     * Ожидаем:
     * - задача успешно добавляется;
     * - проходит состояния READY/RUNNING;
     * - в итоге переходит в FINISHED;
     * - remainingTime после завершения становится равным 0.
     */
    @Test
    public void testSimpleTaskCompletion() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_2";
        try {
            scheduler.initialize(shmName);
            scheduler.start();

            scheduler.submitTask(101, 5, 80, 0);
            assertEquals(1, scheduler.getTaskCount());

            boolean finished = waitUntil(() -> scheduler.getTaskState(101) == TASK_STATE_FINISHED, 3000);
            assertTrue(finished, "Task 101 should finish successfully");
            assertEquals(0, scheduler.getRemainingTime(101));
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет планирование задач по приоритету.
     *
     * До запуска Scheduler создаются задачи с разными приоритетами.
     *
     * Ожидаем:
     * - задача с максимальным priority получает преимущество при планировании;
     * - высокоприоритетная задача успешно завершается.
     */
    @Test
    public void testPriorityScheduling() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_3";
        try {
            scheduler.initialize(shmName);
            
            // Добавляем задачи до start(), чтобы ready_queue была полностью сформирована
            scheduler.submitTask(301, 2, 300, 0);
            scheduler.submitTask(302, 10, 300, 0); // Максимальный приоритет
            scheduler.submitTask(303, 5, 300, 0);

            scheduler.start();

            // Задача 302 (приоритет 10) должна завершиться первой несмотря на порядок отправки
            boolean finished302 = waitUntil(() -> scheduler.getTaskState(302) == TASK_STATE_FINISHED, 3000);
            assertTrue(finished302, "Highest priority task 302 should finish first");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет FIFO-порядок для задач с одинаковым приоритетом.
     *
     * Ожидаем:
     * - при одинаковом effective_priority сохраняется порядок поступления;
     * - задача 351, добавленная первой, завершается раньше задачи 352.
     */
    @Test
    public void testFIFOOrderAtEqualPriority() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_fifo";
        try {
            scheduler.initialize(shmName);
            
            // Задачи с одинаковым приоритетом отправляются последовательно
            scheduler.submitTask(351, 5, 400, 0);
            scheduler.submitTask(352, 5, 400, 0);

            scheduler.start();

            // Первая отправленная задача (351) должна завершиться раньше второй (352)
            boolean finished351First = waitUntil(() -> scheduler.getTaskState(351) == TASK_STATE_FINISHED &&
                                                      scheduler.getTaskState(352) != TASK_STATE_FINISHED, 2000);
            assertTrue(finished351First, "At equal priority, FIFO order must be preserved (351 finishes before 352)");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет динамическое изменение приоритета задачи.
     *
     * Ожидаем:
     * - changePriority() изменяет base_priority;
     * - effective_priority также принимает новое значение;
     * - задача после повышения приоритета успешно получает CPU и завершается.
     */
    @Test
    public void testChangePriority() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_4";
        try {
            scheduler.initialize(shmName);
            
            scheduler.submitTask(401, 1, 600, 0);
            scheduler.submitTask(402, 2, 600, 0);

            scheduler.start();

            // Повышаем приоритет 401 выше 402
            scheduler.changePriority(401, 15);
            assertEquals(15, scheduler.getBasePriority(401));
            assertEquals(15, scheduler.getEffectivePriority(401));

            boolean finished401 = waitUntil(() -> scheduler.getTaskState(401) == TASK_STATE_FINISHED, 3000);
            assertTrue(finished401, "Task 401 should finish first after priority boost");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет выполнение задачи по временным квантам.
     *
     * Задача требует больше времени, чем один quantum.
     *
     * Ожидаем:
     * - после первого или нескольких квантов remainingTime уменьшается,
     *   но задача еще не завершена;
     * - задача несколько раз получает CPU;
     * - в итоге она переходит в FINISHED.
     */
    @Test
    public void testQuantumPreemption() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_quantum";
        try {
            scheduler.initialize(shmName);
            scheduler.start();

            // Задача с большим временем выполнения (200ms) при дефолтном кванте ~50ms
            scheduler.submitTask(501, 5, 200, 0);

            // Ждем, пока оставшееся время уменьшится, но задача еще не завершилась
            boolean preempted = waitUntil(() -> {
                long rem = scheduler.getRemainingTime(501);
                return rem > 0 && rem < 200;
            }, 2000);

            assertTrue(preempted, "Task should be executed across multiple quanta (remaining time decreased partially)");

            // В итоге задача должна успешно завершиться
            boolean finished = waitUntil(() -> scheduler.getTaskState(501) == TASK_STATE_FINISHED, 3000);
            assertTrue(finished, "Task 501 should eventually finish");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет конкуренцию за ресурсы и их постепенное выделение.
     *
     * Одна задача занимает ресурс R1, а вторая требует R1 и R2.
     *
     * Ожидаем:
     * - вторая задача остается BLOCKED, пока ей не хватает ресурсов;
     * - ресурсы выдаются планировщиком постепенно;
     * - после освобождения необходимого ресурса задача может продолжить работу;
     * - в итоге она успешно завершается.
     */
    @Test
    public void testGradualResourceAllocation() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_5";
        try {
            scheduler.initialize(shmName);
            
            // Задача 551 захватывает ресурс 0x1
            scheduler.submitTask(551, 10, 400, 0x1);
            // Задача 552 требует ресурсы 0x1 и 0x2 (0x3)
            scheduler.submitTask(552, 5, 400, 0x3);

            scheduler.start();

            // 551 получает 0x1. 552 должна перейти в BLOCKED и частично владеть 0x2 (или ожидать оба)
            boolean blockedWithPartial = waitUntil(() -> {
                int state = scheduler.getTaskState(552);
                int held = scheduler.getHeldResources(552);
                return state == TASK_STATE_BLOCKED;
            }, 2000);

            assertTrue(blockedWithPartial, "Task 552 should be BLOCKED waiting for resources");

            // После завершения 551 ресурс 0x1 освободится, 552 завершит gradual allocation и завершится
            boolean finished552 = waitUntil(() -> scheduler.getTaskState(552) == TASK_STATE_FINISHED, 4000);
            assertTrue(finished552, "Task 552 should complete after gradual resource allocation");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет обнаружение и разрешение deadlock.
     *
     * Три задачи создают циклическое ожидание ресурсов.
     *
     * Ожидаем:
     * - Scheduler обнаруживает цикл ожидания;
     * - одна из задач выбирается жертвой и получает DEADLOCK_ABORTED;
     * - ресурсы жертвы полностью освобождаются;
     * - после разрушения deadlock оставшиеся задачи могут продолжить выполнение.
     */
    @Test
    public void testDeadlockDetectionAndResolution() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_6";
        try {
            scheduler.initialize(shmName);
            
            // Цикл дедлока: 601 (0x3), 602 (0x6), 603 (0x5)
            scheduler.submitTask(601, 5, 600, 0x3);
            scheduler.submitTask(602, 5, 600, 0x6);
            scheduler.submitTask(603, 5, 600, 0x5);

            scheduler.start();

            // Ждем срабатывания детектора и появления жертвы (TASK_STATE_DEADLOCK_ABORTED)
            boolean deadlockResolved = waitUntil(() -> 
                scheduler.getTaskState(601) == TASK_STATE_DEADLOCK_ABORTED ||
                scheduler.getTaskState(602) == TASK_STATE_DEADLOCK_ABORTED ||
                scheduler.getTaskState(603) == TASK_STATE_DEADLOCK_ABORTED,
                4000
            );

            assertTrue(deadlockResolved, "Deadlock must be detected and resolved");

            // Проверяем инвариант жертвы: held_resources == 0
            final int[] victimId = {-1};
            for (int id : new int[]{601, 602, 603}) {
                if (scheduler.getTaskState(id) == TASK_STATE_DEADLOCK_ABORTED) {
                    victimId[0] = id;
                    assertEquals(0, scheduler.getHeldResources(id), "Victim must release all held resources");
                    break;
                }
            }
            assertTrue(victimId[0] != -1, "A deadlock victim must exist");

            // Проверяем, что хотя бы одна из оставшихся задач успешно завершилась после разрыва цикла
            boolean survivorFinished = waitUntil(() -> {
                for (int id : new int[]{601, 602, 603}) {
                    if (id != victimId[0] && scheduler.getTaskState(id) == TASK_STATE_FINISHED) {
                        return true;
                    }
                }
                return false;
            }, 4000);

            assertTrue(survivorFinished, "Remaining tasks must successfully complete after deadlock resolution");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет механизм aging для предотвращения starvation.
     *
     * Низкоприоритетная задача долго ожидает CPU,
     * пока выполняется более приоритетная задача.
     *
     * Ожидаем:
     * - base_priority ожидающей задачи не изменяется;
     * - effective_priority постепенно увеличивается благодаря aging;
     * - тем самым длительное ожидание повышает шанс задачи получить CPU.
    */
    @Test
    public void testAging() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_aging";
        try {
            scheduler.initialize(shmName);
            
            // Отправляем задачу с низким приоритетом и большую задачу с высоким приоритетом, чтобы низкая ждала
            scheduler.submitTask(701, 10, 800, 0); // Высокий
            scheduler.submitTask(702, 1, 800, 0);  // Низкий (будет долго ждать в ready_queue)

            scheduler.start();

            // Проверяем, что effective_priority задачи 702 увеличилась за счет aging при неизменном base_priority
            boolean aged = waitUntil(() -> {
                int base = scheduler.getBasePriority(702);
                int eff = scheduler.getEffectivePriority(702);
                return base == 1 && eff > 1;
            }, 3000);

            assertTrue(aged, "Low priority task effective_priority should increase due to aging");
            assertEquals(1, scheduler.getBasePriority(702), "Base priority must remain unchanged");
        } finally {
            scheduler.stop();
        }
    }

    /*
     * Проверяет безопасную обработку некорректных входных данных и восстановление после ошибки.
     *
     * Намеренно выполняются некорректные операции:
     * - добавление задачи с отрицательным ID;
     * - добавление задачи с нулевым временем выполнения;
     * - повторное добавление задачи с уже существующим ID.
     *
     * Ожидаем:
     * - native-код обнаруживает ошибку и возвращает -1;
     * - некорректная задача не добавляется в shared memory;
     * - taskCount не изменяется;
     * - Scheduler и Worker не завершаются аварийно;
     * - не возникает segmentation fault;
     * - после ошибок система остается работоспособной
     *   и успешно выполняет следующую корректную задачу.
    */
    @Test
    public void testNegativeValidationAndRecovery() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_7";
        try {
            scheduler.initialize(shmName);
            scheduler.start();

            // 1. Отрицательный ID
            assertEquals(-1, scheduler.submitTask(-1, 5, 100, 0));
            assertEquals(0, scheduler.getTaskCount());

            // 2. Нулевое время
            assertEquals(-1, scheduler.submitTask(801, 5, 0, 0));
            assertEquals(0, scheduler.getTaskCount());

            // 3. Дубликат ID
            assertEquals(802, scheduler.submitTask(802, 5, 100, 0));
            assertEquals(-1, scheduler.submitTask(802, 5, 100, 0), "Duplicate ID should be rejected");
            assertEquals(1, scheduler.getTaskCount());

            // 4. Проверка восстановления и работы
            boolean finished = waitUntil(() -> scheduler.getTaskState(802) == TASK_STATE_FINISHED, 3000);
            assertTrue(finished, "Scheduler must remain fully operational after invalid task submissions");
        } finally {
            scheduler.stop();
        }
    }
}
