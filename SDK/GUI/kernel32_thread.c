#include <windows.h>
#include <undoscrt/heap.h>

#include "private/backend.h"
#include "private/ui_internal.h"

/* kernel32 threads and synchronisation over the PROC syscalls. A thread
   handle and an event handle are kernel handles: WaitForSingleObject and
   CloseHandle work on both. The SDK's window tables are not locked, so only
   the thread that created a window may use it; other threads should post
   messages to it. */

typedef struct ThreadStart {
  LPTHREAD_START_ROUTINE routine;
  LPVOID parameter;
} ThreadStart;

static BOOL ok(uint32_t status) {
  if (status == UI_STATUS_OK) return TRUE;
  ui_core_failure(status, ui_core_error_from_status(status));
  return FALSE;
}

/* The kernel enters here with a zero return address and the block as the
   only argument; the routine's result becomes the thread's exit code. */
static void thread_start(ThreadStart *start) {
  const LPTHREAD_START_ROUTINE routine = start->routine;
  const LPVOID parameter = start->parameter;
  crt_free(start);
  ExitThread(routine(parameter));
}

HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stackSize,
                    LPTHREAD_START_ROUTINE routine, LPVOID parameter,
                    DWORD flags, LPDWORD threadId) {
  (void)attributes;
  if (threadId) *threadId = 0;
  if (!routine || stackSize > 0x400000u) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (flags & CREATE_SUSPENDED) {
    ui_core_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
  }
  ThreadStart *start = (ThreadStart *)crt_malloc(sizeof(ThreadStart));
  if (!start) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  start->routine = routine;
  start->parameter = parameter;
  uint32_t handle = 0, id = 0;
  if (!ok(ui_backend_create_thread((uint32_t)(uintptr_t)&thread_start,
                                   (uint32_t)(uintptr_t)start, (uint32_t)stackSize,
                                   &handle, &id))) {
    crt_free(start);
    return 0;
  }
  if (threadId) *threadId = id;
  return (HANDLE)handle;
}

void ExitThread(DWORD code) { ui_backend_exit_thread(code); }

BOOL GetExitCodeThread(HANDLE thread, LPDWORD code) {
  if (!code) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uint32_t value = 0;
  const uint32_t status = ui_backend_thread_exit_code(thread, &value);
  if (status == UI_STATUS_OK) {
    *code = value;
    return TRUE;
  }
  if (status == UI_STATUS_RETRY) {
    *code = STILL_ACTIVE;
    return TRUE;
  }
  return ok(status);
}

DWORD GetCurrentThreadId(void) { return ui_backend_current_thread_id(); }

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
  const uint32_t status = ui_backend_wait_object(handle, milliseconds);
  if (status == UI_STATUS_OK) return WAIT_OBJECT_0;
  if (status == UI_STATUS_TIMEOUT) return WAIT_TIMEOUT;
  (void)ok(status);
  return WAIT_FAILED;
}

void Sleep(DWORD milliseconds) { ui_backend_sleep(milliseconds); }

HANDLE CreateEvent(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset,
                   BOOL initialState, LPCSTR name) {
  (void)attributes;
  if (name) {
    ui_core_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
  }
  uint32_t handle = 0;
  if (!ok(ui_backend_create_event(manualReset != FALSE, initialState != FALSE, &handle)))
    return 0;
  return (HANDLE)handle;
}

BOOL SetEvent(HANDLE event) { return ok(ui_backend_set_event(event)); }
BOOL ResetEvent(HANDLE event) { return ok(ui_backend_reset_event(event)); }

BOOL CloseHandle(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return ok(ui_backend_close_handle(handle));
}

/* Interlocked operations. The SDK targets a plain 386 (-march=i386), which
   has no CMPXCHG or XADD; only XCHG is atomic there. On i386 every
   interlocked update therefore runs under a tiny XCHG spin lock (yielding
   while contended, so it is safe on one CPU with preemption). Other targets
   use the compiler's atomics. */
#if defined(__i386__)
static volatile LONG interlock_guard;
static void interlock_enter(void) {
  while (__atomic_exchange_n(&interlock_guard, 1, __ATOMIC_ACQUIRE)) Sleep(0);
}
static void interlock_leave(void) { __atomic_store_n(&interlock_guard, 0, __ATOMIC_RELEASE); }
LONG UndosInterlockedAdd(LONG volatile *target, LONG delta) {
  interlock_enter();
  LONG value = *target + delta;
  *target = value;
  interlock_leave();
  return value;
}
LONG UndosInterlockedExchange(LONG volatile *target, LONG value) {
  interlock_enter();
  LONG old = *target;
  *target = value;
  interlock_leave();
  return old;
}
LONG UndosInterlockedCompareExchange(LONG volatile *target, LONG value, LONG comparand) {
  interlock_enter();
  LONG old = *target;
  if (old == comparand) *target = value;
  interlock_leave();
  return old;
}
#else
LONG UndosInterlockedAdd(LONG volatile *target, LONG delta) {
  return __atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}
LONG UndosInterlockedExchange(LONG volatile *target, LONG value) {
  return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
LONG UndosInterlockedCompareExchange(LONG volatile *target, LONG value, LONG comparand) {
  __atomic_compare_exchange_n(target, &comparand, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return comparand;
}
#endif

/* Critical sections: LockCount starts at -1 and counts waiters; contended
   threads sleep on an auto-reset event created with the section. */
void InitializeCriticalSection(LPCRITICAL_SECTION section) {
  section->LockCount = -1;
  section->RecursionCount = 0;
  section->OwningThread = 0;
  uint32_t event = 0;
  section->LockSemaphore =
      ui_backend_create_event(FALSE, FALSE, &event) == UI_STATUS_OK ? (HANDLE)event : 0;
}

void DeleteCriticalSection(LPCRITICAL_SECTION section) {
  if (section->LockSemaphore) (void)ui_backend_close_handle(section->LockSemaphore);
  section->LockSemaphore = 0;
}

void EnterCriticalSection(LPCRITICAL_SECTION section) {
  const DWORD self = GetCurrentThreadId();
  if (section->OwningThread == self && section->RecursionCount) {
    ++section->RecursionCount;
    (void)UndosInterlockedAdd(&section->LockCount, 1);
    return;
  }
  if (UndosInterlockedAdd(&section->LockCount, 1) != 0) {
    if (section->LockSemaphore)
      (void)ui_backend_wait_object(section->LockSemaphore, INFINITE);
  }
  section->OwningThread = self;
  section->RecursionCount = 1;
}

BOOL TryEnterCriticalSection(LPCRITICAL_SECTION section) {
  const DWORD self = GetCurrentThreadId();
  if (section->OwningThread == self && section->RecursionCount) {
    ++section->RecursionCount;
    (void)UndosInterlockedAdd(&section->LockCount, 1);
    return TRUE;
  }
  if (UndosInterlockedCompareExchange(&section->LockCount, 0, -1) != -1)
    return FALSE;
  section->OwningThread = self;
  section->RecursionCount = 1;
  return TRUE;
}

void LeaveCriticalSection(LPCRITICAL_SECTION section) {
  if (--section->RecursionCount > 0) {
    (void)UndosInterlockedAdd(&section->LockCount, -1);
    return;
  }
  section->OwningThread = 0;
  if (UndosInterlockedAdd(&section->LockCount, -1) >= 0 &&
      section->LockSemaphore)
    (void)ui_backend_set_event(section->LockSemaphore);
}
