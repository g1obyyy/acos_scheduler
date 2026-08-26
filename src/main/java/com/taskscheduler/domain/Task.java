package com.taskscheduler.domain;

public class Task {
    private final int id;
    private int priority;
    private TaskState state;
    private final long totalTimeMs;
    private long remainingTimeMs;
    private final int requiredResources;

    public Task(int id, int priority, long totalTimeMs, int requiredResources) {
        this.id = id;
        this.priority = priority;
        this.state = TaskState.NEW;
        this.totalTimeMs = totalTimeMs;
        this.remainingTimeMs = totalTimeMs;
        this.requiredResources = requiredResources;
    }

    public int getId() { return id; }
    public int getPriority() { return priority; }
    public void setPriority(int priority) { this.priority = priority; }
    public TaskState getState() { return state; }
    public void setState(TaskState state) { this.state = state; }
    public long getTotalTimeMs() { return totalTimeMs; }
    public long getRemainingTimeMs() { return remainingTimeMs; }
    public void setRemainingTimeMs(long remainingTimeMs) { this.remainingTimeMs = remainingTimeMs; }
    public int getRequiredResources() { return requiredResources; }
}
