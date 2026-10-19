#pragma once

#ifdef __cplusplus
extern "C" {
#endif
int *__undos_errno(void);
#ifdef __cplusplus
}
#endif
#define errno (*__undos_errno())
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EINVAL 22
#define EMFILE 24
#define ERANGE 34
#define ENOSYS 38
#define EOVERFLOW 75
