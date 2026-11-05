#pragma once
#include <undos/syscalls.h>

uint32_t UD_SYS_GetTaskSnapshotV1(uint32_t, TaskCpuRecordV1 *, uint32_t *,
    TaskProcessRecordV1 *, uint32_t *, uint8_t *, uint32_t *,
    TaskSnapshotHeaderV1 *, uint32_t *, uint32_t *, uint32_t *,
    uint32_t *, uint32_t *, uint32_t *);
uint32_t UD_SYS_WaitForObjectV1(HANDLE, uint32_t);
uint32_t UD_SYS_CreateEventV1(uint32_t, HANDLE *);
uint32_t UD_SYS_CloseV1(HANDLE);
uint32_t UD_SYS_PrintV1(const char *);
