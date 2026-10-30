#ifndef UNDOS_GRUB_DIRECT_CPU_PROFILE_H
#define UNDOS_GRUB_DIRECT_CPU_PROFILE_H

#include "loader_core.h"

enum {
  UDL_CPU_FEATURE_PSE = 1u << 3,
  UDL_CPU_FEATURE_PAE = 1u << 6,
  UDL_CR4_PSE = 1u << 4,
  UDL_CR4_PAE = 1u << 5,
};

udl_error udl_cpu_cleanup_mask(udl_u32 id_flag_mutable,
                               udl_u32 maximum_basic_leaf,
                               udl_u32 leaf1_edx,
                               udl_u32 *cleanup_mask);

#endif
