#include "cpu_profile.h"

udl_error
udl_cpu_cleanup_mask(udl_u32 id_flag_mutable, udl_u32 maximum_basic_leaf,
                     udl_u32 leaf1_edx, udl_u32 *cleanup_mask)
{
  if (!cleanup_mask || id_flag_mutable > 1)
    return UDL_BAD_ARGUMENT;

  *cleanup_mask = 0;
  if (!id_flag_mutable || maximum_basic_leaf < 1)
    return UDL_OK;

  if (leaf1_edx & UDL_CPU_FEATURE_PSE)
    *cleanup_mask |= UDL_CR4_PSE;
  if (leaf1_edx & UDL_CPU_FEATURE_PAE)
    *cleanup_mask |= UDL_CR4_PAE;
  return UDL_OK;
}
