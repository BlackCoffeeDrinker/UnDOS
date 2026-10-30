#pragma once

#include <kernel/gfx/video_present.hpp>
#include <kernel/kobject/KDisplay.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KVideoPresentValidationToken final
    : KObjectT<KVideoPresentValidationToken, 1, TYPE_VIDEO_PRESENT_TOKEN> {
  using KObjectT::KObjectT;
  uint32_t generation{};
  gfx::VideoPresentPathInfo path{};
  gfx::VideoPresentSnapshotLease snapshot{};
  KObjectPtr<KDisplay> target{nullptr};
  KDisplayModeReservation reservation{};

  ~KVideoPresentValidationToken() {
    if (reservation.driverData || reservation.binding.backing)
      gfx::video_present_release_reservation(target, reservation);
  }
};

template<>
struct ObjectTypeOf<TYPE_VIDEO_PRESENT_TOKEN> {
  using type = KVideoPresentValidationToken;
};
}// namespace kernel
