package com.taskscheduler.domain;

public enum TaskState {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    FINISHED,
    DEADLOCK_ABORTED
}