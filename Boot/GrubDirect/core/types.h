#ifndef UNDOS_GRUB_DIRECT_TYPES_H
#define UNDOS_GRUB_DIRECT_TYPES_H

typedef __UINT8_TYPE__ udl_u8;
typedef __UINT16_TYPE__ udl_u16;
typedef __UINT32_TYPE__ udl_u32;
typedef __UINT64_TYPE__ udl_u64;
typedef __SIZE_TYPE__ udl_size;

#ifdef __cplusplus
static_assert(sizeof(udl_u8) == 1, "udl_u8 must be 8 bits");
static_assert(sizeof(udl_u16) == 2, "udl_u16 must be 16 bits");
static_assert(sizeof(udl_u32) == 4, "udl_u32 must be 32 bits");
static_assert(sizeof(udl_u64) == 8, "udl_u64 must be 64 bits");
#else
_Static_assert(sizeof(udl_u8) == 1, "udl_u8 must be 8 bits");
_Static_assert(sizeof(udl_u16) == 2, "udl_u16 must be 16 bits");
_Static_assert(sizeof(udl_u32) == 4, "udl_u32 must be 32 bits");
_Static_assert(sizeof(udl_u64) == 8, "udl_u64 must be 64 bits");
#endif

#endif
