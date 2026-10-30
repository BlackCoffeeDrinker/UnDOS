#pragma once

#include <kernel/kobject/KVideoPresentSource.hpp>

namespace kernel::gfx {
struct VideoPresentSourceModeInfo {
  uint32_t modeId{};
  uint32_t width{};
  uint32_t height{};
  uint32_t colorDepth{};
  PixelFormat pixelFormat{PixelFormat::RGB24};
};

struct VideoPresentTargetInfo {
  uint32_t targetId{};
  bool connected{};
  uint32_t modeCount{};
  uint32_t maxWidth{};
  uint32_t maxHeight{};
  uint32_t maxRefreshHz{};
};

struct VideoPresentTargetModeInfo {
  uint32_t targetId{};
  uint32_t modeId{};
  KDisplayModeInfo mode{};
};

struct VideoPresentTopologyInfo {
  uint32_t generation{};
  uint32_t sourceCount{};
  uint32_t sourceModeCount{};
  uint32_t targetCount{};
  uint32_t targetModeCount{};
  uint32_t pathCount{};
  uint32_t degraded{};
};

struct VideoPresentPathInfo {
  uint32_t sourceId{};
  uint32_t sourceModeId{};
  uint32_t targetId{};
  uint32_t targetModeId{};
  int32_t sourceX{};
  int32_t sourceY{};
  uint32_t width{};
  uint32_t height{};
  uint32_t flags{};
};

struct VideoPresentTargetResult {
  uint32_t targetId{};
  uint32_t requestedModeId{};
  uint32_t actualModeId{};
  uint32_t active{};
  uint32_t outcome{};
  uint32_t degraded{};
};

struct VideoPresentSnapshot;
// Private reference-counted lease: records remain immutable until the last reader releases it.
class VideoPresentSnapshotLease {
 public:
  VideoPresentSnapshotLease() noexcept = default;
  VideoPresentSnapshotLease(const VideoPresentSnapshotLease &) noexcept;
  VideoPresentSnapshotLease &operator=(const VideoPresentSnapshotLease &) noexcept;
  ~VideoPresentSnapshotLease();
  const VideoPresentSnapshot *operator->() const noexcept { return value_; }
  const VideoPresentSnapshot &operator*() const noexcept { return *value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }
  explicit VideoPresentSnapshotLease(VideoPresentSnapshot *value) noexcept : value_(value) {}
 private:
  VideoPresentSnapshot *value_{};
};

struct VideoPresentSnapshot {
  kstd::atomic<uint32_t> references{1};
  VideoPresentTopologyInfo topology{};
  KObjectPtr<KVideoPresentSource> *sources{};
  VideoPresentSourceModeInfo *sourceModes{};
  VideoPresentTargetInfo *targets{};
  VideoPresentTargetModeInfo *targetModes{};
  VideoPresentPathInfo *paths{};
  KObjectPtr<KDisplay> *targetReferences{};
  struct Observation {
    KMonitorCapabilities capabilities{};
    uint32_t resourceGeneration{}, adapterCaps{};
    bool present{}, degraded{};
    bool actualKnown{}, actualEnabled{};
    size_t actualMode{};
  };
  Observation *observations{};
  size_t pathCapacity{};
  bool pathsValid{};
  size_t allocatedBytes{};
  ~VideoPresentSnapshot();
};

inline constexpr size_t VideoPresentSnapshotBytesPerClassRecord =
    (sizeof(KObjectPtr<KVideoPresentSource>) + sizeof(VideoPresentSourceModeInfo) +
     sizeof(VideoPresentTargetInfo) + sizeof(VideoPresentTargetModeInfo) +
     sizeof(VideoPresentPathInfo) + sizeof(KObjectPtr<KDisplay>) +
     sizeof(VideoPresentSnapshot::Observation));
static_assert(VideoPresentRecordLimit <=
    (SIZE_MAX - sizeof(VideoPresentSnapshot)) / VideoPresentSnapshotBytesPerClassRecord);
inline constexpr size_t VideoPresentSnapshotBudgetBytes =
    sizeof(VideoPresentSnapshot) +
    VideoPresentRecordLimit * VideoPresentSnapshotBytesPerClassRecord;

kstd::expected<VideoPresentSnapshotLease, kstd::error_code>
acquire_video_present_snapshot(uint32_t generation = 0) noexcept;
// Mapped enumeration may only pin already published records; it never polls or builds.
kstd::expected<VideoPresentSnapshotLease, kstd::error_code>
acquire_published_video_present_snapshot(uint32_t generation) noexcept;
#ifdef UNDOS_TOPOLOGY_TEST_HOOKS
void video_present_test_fail_allocation_after(size_t successfulAllocations) noexcept;
void video_present_test_set_spinlock_observer(bool (*observer)()) noexcept;
size_t video_present_test_live_allocations() noexcept;
size_t video_present_test_locked_allocations() noexcept;
uint32_t video_present_test_generation() noexcept;
void video_present_test_before_commit_decision(void (*hook)()) noexcept;
bool video_present_test_callback_gate_held() noexcept;
#endif

kstd::error_code get_video_present_topology(VideoPresentTopologyInfo &out) noexcept;
kstd::expected<VideoPresentSourceModeInfo, kstd::error_code>
video_present_source_mode(uint32_t generation, uint32_t sourceId, uint32_t index) noexcept;
kstd::expected<VideoPresentTargetInfo, kstd::error_code>
video_present_target(uint32_t generation, uint32_t index) noexcept;
kstd::expected<VideoPresentTargetModeInfo, kstd::error_code>
video_present_target_mode(uint32_t generation, uint32_t targetId, uint32_t index) noexcept;
kstd::expected<VideoPresentPathInfo, kstd::error_code>
video_present_path(uint32_t generation, uint32_t index) noexcept;
kstd::expected<KObjectPtr<KDisplay>, kstd::error_code>
video_present_resolve_path(uint32_t generation, const VideoPresentPathInfo &path,
                           size_t &driverModeIndex) noexcept;
kstd::error_code video_present_prepare_path(
    uint32_t generation, const VideoPresentPathInfo &path,
    KObjectPtr<KDisplay> &target, size_t &driverModeIndex,
    KDisplayModeReservation &reservation,
    VideoPresentSnapshotLease *snapshot = nullptr) noexcept;
void video_present_release_reservation(
    const KObjectPtr<KDisplay> &target, KDisplayModeReservation &reservation) noexcept;
// firstActivation, when supplied, reports prior state from the serialized transaction.
kstd::expected<VideoPresentTargetResult, kstd::error_code>
commit_video_present_path(uint32_t generation, const VideoPresentPathInfo &path,
                          const KObjectPtr<KDisplay> &target,
                          KDisplayModeReservation &reservation,
                          size_t resultCapacity = 1,
                          bool *firstActivation = nullptr) noexcept;
KObjectPtr<KDisplay> active_video_present_target() noexcept;
bool has_video_present_targets() noexcept;
}// namespace kernel::gfx
