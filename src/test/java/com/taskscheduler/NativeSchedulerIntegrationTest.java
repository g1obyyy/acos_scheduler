package com.taskscheduler;

import com.taskscheduler.nativebridge.NativeScheduler;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

@Timeout(5)
public class NativeSchedulerIntegrationTest {

    @Test
    public void testLifecycleAndTaskSubmission() {
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/test_scheduler_shm_1";

        assertDoesNotThrow(() -> {
            scheduler.initialize(shmName);
            scheduler.start();
            
            int taskId1 = scheduler.submitTask(1, 5, 1000, 0);
            int taskId2 = scheduler.submitTask(2, 10, 2000, 1);
            
            scheduler.changePriority(1, 8);
            
            Thread.sleep(200);
            
            scheduler.stop();
        });
    }
}
