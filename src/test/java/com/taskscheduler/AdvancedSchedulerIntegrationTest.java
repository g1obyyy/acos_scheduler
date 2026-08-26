package com.taskscheduler;

import com.taskscheduler.nativebridge.NativeScheduler;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

import static org.junit.jupiter.api.Assertions.*;

@Timeout(10)
public class AdvancedSchedulerIntegrationTest {

    @Test
    public void testTaskSubmissionAndSnapshot() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_1";

        assertDoesNotThrow(() -> {
            scheduler.initialize(shmName);
            scheduler.start();

            scheduler.submitTask(101, 5, 500, 1);
            scheduler.submitTask(102, 10, 1000, 2);

            Thread.sleep(200);

            int count = scheduler.getTaskCount();
            assertEquals(2, count);

            scheduler.stop();
        });
    }

    @Test
    public void testResourceContentionAndRelease() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_2";

        assertDoesNotThrow(() -> {
            scheduler.initialize(shmName);
            scheduler.start();

            // Two tasks requesting the exact same exclusive resource (Resource ID 4)
            scheduler.submitTask(201, 5, 300, 4);
            scheduler.submitTask(202, 10, 300, 4);

            Thread.sleep(400);

            int count = scheduler.getTaskCount();
            assertEquals(2, count);

            scheduler.stop();
        });
    }

    @Test
    public void testPartialResourceAllocationAndWakeup() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_4";

        assertDoesNotThrow(() -> {
            scheduler.initialize(shmName);
            scheduler.start();

            scheduler.submitTask(401, 5, 600, 1 | 2);
            scheduler.submitTask(402, 10, 200, 2);

            Thread.sleep(500);

            assertEquals(2, scheduler.getTaskCount());

            scheduler.stop();
        });
    }

    @Test
    public void testWorkerQuantumCompletionAndResourceReleaseWakeup() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_5";

        assertDoesNotThrow(() -> {
            scheduler.initialize(shmName);
            scheduler.start();

            // Task 501 requires resource R1 (bit 0)
            // Task 502 requires resource R1 (bit 0)
            // Task 501 runs first, finishes its quantum, releases R1.
            // Scheduler wakes up via scheduler_sem handshake, allocates R1 to Task 502.
            scheduler.submitTask(501, 10, 100, 1);
            scheduler.submitTask(502, 5, 100, 1);

            Thread.sleep(400);

            assertEquals(2, scheduler.getTaskCount());

            scheduler.stop();
        });
    }

    @Test
    public void testThreeTaskDeadlockCycle() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/advanced_scheduler_shm_3";

        assertDoesNotThrow(() -> {
            scheduler.initialize(shmName);
            scheduler.start();

            scheduler.submitTask(301, 5, 500, 1 | 2);
            scheduler.submitTask(302, 5, 500, 2 | 4);
            scheduler.submitTask(303, 5, 500, 4 | 1);

            Thread.sleep(600);

            assertEquals(3, scheduler.getTaskCount());

            scheduler.stop();
        });
    }
}
