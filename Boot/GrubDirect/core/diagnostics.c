#include "diagnostics.h"

const char udl_diag_names[UDL_DIAG_STOP_COUNT][16] = {
  "command", "loaded", "callback", "profile", "prepared", "copied",
  "parameters", "segments", "cleanup", "paging", "frame", "jump"
};

static int
name_matches(const char *name, const char *expected)
{
  udl_size index = 0;
  if (!name)
    return 0;
  while (expected[index])
    {
      if (name[index] != expected[index])
        return 0;
      ++index;
    }
  return name[index] == '\0';
}

udl_u32
udl_diag_stop_from_name(const char *name)
{
  if (name_matches(name, "kernel"))
    return UDL_DIAG_STOP_KERNEL;
  for (udl_u32 index = 0; index < UDL_DIAG_STOP_COUNT; ++index)
    if (name_matches(name, udl_diag_names[index]))
      return index + 1;
  return UDL_DIAG_STOP_INVALID;
}

int
udl_diag_video_profile_supported(udl_u8 mode, udl_u16 columns,
                                 udl_u8 active_page)
{
  return mode == 3 && columns == 80 && active_page == 0;
}
