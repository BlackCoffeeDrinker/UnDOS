# Executive: object manager, events, interrupt dispatch, scheduler, processes
# and memory. Split as one unit for now; see Docs/kernel-components-report.md.
add_library(kernel_executive OBJECT
        src/object_manager.cpp
        src/event_broadcaster.cpp
        src/dispatch.cpp
        src/wait.cpp
        src/wait_interface.cpp
        src/time.cpp
        src/percpu.cpp
        src/smp.cpp
        src/scheduler/CpuContext.cpp
        src/scheduler/Scheduler.cpp
        src/scheduler/SchedulerSwitch.cpp
        src/process.cpp
        src/process_notify.cpp
        src/user_process.cpp
        src/task_stats.cpp
        src/syscalls/CreateProcess.cpp
        src/syscalls/GetStdHandle.cpp
        src/vmm.cpp
        src/memory/Layout.cpp
        src/memory/Storage.cpp
        src/memory/Slab.cpp
        src/memory/Allocator.cpp
        src/memory/Cache.cpp
)
undos_kernel_component(kernel_executive Executive)
