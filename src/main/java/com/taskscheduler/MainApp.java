package com.taskscheduler;

import com.taskscheduler.nativebridge.NativeScheduler;

public class MainApp {
    public static void main(String[] args) {
        System.out.println("[JAVA] Starting Task Scheduler Application...");
        NativeScheduler scheduler = new NativeScheduler();
        String shmName = "/main_app_shm";

        try {
            scheduler.initialize(shmName);
            scheduler.start();

            System.out.println("[JAVA] Submitting tasks...");
            scheduler.submitTask(1, 5, 800, 1);
            scheduler.submitTask(2, 10, 1200, 2);

            Thread.sleep(2500);

            System.out.println("[JAVA] Stopping scheduler...");
            scheduler.stop();
            System.out.println("[JAVA] Application finished successfully.");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
