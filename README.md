# ACOS Task Scheduler

Гибридный многопроцессорный планировщик задач на Java (JNI) и Си (POSIX Shared Memory, IPC, Processes, Semaphores).

## Системные требования
- Linux (x64)
- GCC / CMake (версия 3.10+)
- Java Development Kit (JDK 17+)
- Apache Maven

---

## Установка и сборка

1. Клонируйте репозиторий и перейдите в корень проекта.
2. Создайте директорию для сборки нативной части и скомпилируйте библиотеку:

```bash
mkdir -p native/build
cd native/build
cmake ..
make
cd ../..
```

---

## Запуск тестов

Для полной сборки проекта, обновления нативной библиотеки (`taskscheduler.so`) и запуска всех интеграционных тестов JUnit 5 используйте команду:

```bash
mvn clean test -Dsurefire.useFile=false
```

Логи выполнения нативных процессов сохраняются в директорию `logs/`.