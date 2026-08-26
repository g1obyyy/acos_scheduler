package com.taskscheduler.nativebridge;

import java.io.File;

public final class NativeScheduler {
    static {
        try {
            File libFile = new File("native/build/taskscheduler.so");
            if (!libFile.exists()) {
                libFile = new File("../native/build/taskscheduler.so");
            }
            System.load(libFile.getAbsolutePath());
        } catch (UnsatisfiedLinkError e) {
            System.loadLibrary("taskscheduler");
        }
    }

    public native void initialize(String shmName);
    public native void start();
    public native void stop();
    public native int submitTask(int id, int priority, long totalTimeMs, int requiredResources);
    public native void changePriority(int taskId, int priority);
    public native int getTaskCount();

    // Read-only методы для тестирования состояния задач и очередей
    public native int getTaskState(int taskId);
    public native long getRemainingTime(int taskId);
    public native int getBasePriority(int taskId);
    public native int getEffectivePriority(int taskId);
    public native int getHeldResources(int taskId);
    public native int getRunningTaskId();
    public native int getReadyQueueSize();
    public native int getBlockedQueueSize();
    public native long getWaitTicks(int taskId);
}

