#ifdef __APPLE__
#define NAME(x) _##x
#else
#define NAME(x) x
#endif

.data
.globl NAME(undos_test_preinit_start)
.globl NAME(undos_test_preinit_end)
.globl NAME(undos_test_init_start)
.globl NAME(undos_test_init_end)
.globl NAME(undos_test_fini_start)
.globl NAME(undos_test_fini_end)
NAME(undos_test_preinit_start):
NAME(undos_test_preinit_end):
NAME(undos_test_init_start):
NAME(undos_test_init_end):
NAME(undos_test_fini_start):
NAME(undos_test_fini_end):
#ifndef __APPLE__
.section .note.GNU-stack,"",@progbits
#endif
