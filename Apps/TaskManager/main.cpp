#if defined(UNDOS_TASK_MANAGER_HOSTED)
#include "task_manager_app_api.hpp"
#else
#include <userspace.h>
#endif
#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>

#include <stdlib.h>
#include <string.h>

#include "model.hpp"

namespace {
constexpr char WindowClass[] = "UnDOS.TaskManager";
constexpr COLORREF ColorBlack = 0;
constexpr COLORREF ColorBlue = RGB(0, 0, 170);
constexpr COLORREF ColorGreen = RGB(0, 170, 0);
constexpr COLORREF ColorRed = RGB(170, 0, 0);
constexpr COLORREF ColorDarkGray = RGB(85, 85, 85);
constexpr COLORREF ColorLightGray = RGB(170, 170, 170);
constexpr COLORREF ColorBrightGreen = RGB(85, 255, 85);
constexpr COLORREF ColorWhite = RGB(255, 255, 255);

struct Snapshot {
  TaskSnapshotHeaderV1 header{};
  TaskCpuRecordV1 *cpus{nullptr};
  TaskProcessRecordV1 *processes{nullptr};
  uint8_t *names{nullptr};
  uint32_t *cpuBasisPoints{nullptr};
  uint8_t *cpuPercentValid{nullptr};
  uint32_t *processBasisPoints{nullptr};
  uint8_t *processPercentValid{nullptr};
  uint32_t *sortedProcesses{nullptr};
  uint32_t cpuCount{0};
  uint32_t processCount{0};
  uint32_t nameBytes{0};
  bool valid{false};
  bool busyValid{false};
  uint32_t busyBasisPoints{0};
  bool systemValid{false};
  uint32_t systemBasisPoints{0};
  bool idleValid{false};
  uint32_t idleBasisPoints{0};
};

struct Rect {
  int left;
  int top;
  int right;
  int bottom;
};

struct Layout {
  Rect processesTab{};
  Rect performanceTab{};
  Rect table{};
  Rect performanceCpu{};
  Rect performanceMemory{};
  Rect status{};
  int rowHeight{0};
  int headerHeight{0};
  int footerHeight{0};
  int nameWidth{0};
  int pidWidth{0};
  int cpuWidth{0};
  int memoryWidth{0};
  int tableWidth{0};
  int visibleRows{0};
};

struct AppState {
  HWND window{nullptr};
  HANDLE waitEvent{0};
  UNDOS_FONT font{0};
  bool fontValid{false};
  uint32_t fontGeneration{0};
  UNDOS_TEXT_METRICS metrics{};
  Snapshot sample{};
  taskmgr::History history{};
  uint32_t *cpuHistory{nullptr};
  uint8_t *cpuHistoryValid{nullptr};
  uint32_t *cpuHistoryIds{nullptr};
  uint32_t cpuHistoryCount{0};
  uint32_t historyCpuScroll{0};
  uint32_t selectedPosition{0};
  uint32_t processScroll{0};
  int tableScrollX{0};
  uint32_t focusTarget{0};
  uint32_t view{0};
  uint32_t staleStatus{SyscallReturnCode_OKAY};
  uint32_t paintFailure{SyscallReturnCode_OKAY};
  uint32_t fontMeasureStatus{SyscallReturnCode_OKAY};
  bool stale{false};
  bool hasSample{false};
  bool running{true};
  uint8_t shiftKeysDown{0};
  bool fontStale{false};
  bool paintOkay{true};
  bool paintErrorShown{false};
  Rect failedPaint{};
  uint32_t paintRetries{0};
  uint64_t sampleGeneration{0};
  uint32_t observationSamples{0};
};

AppState app;
struct TextCacheEntry {
  bool used{};
  bool persistent{};
  UNDOS_FONT font{};
  uint32_t generation{}, style{}, mode{}, repertoire{};
  int width{-1};
  uint64_t age{};
  char text[128]{}, fitted[72]{};
  UNDOS_TEXT_EXTENT extent{};
};
TextCacheEntry textCache[64]{};
uint64_t cacheAge{};
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
constexpr UINT TabTimingMessage = WM_USER + 0x31;
struct TabTiming {
  bool pending{};
  bool countingInvalidations{};
  bool paintMarked{};
  uint32_t clickMs{}, paintBeginMs{}, invalidationCount{};
  uint64_t invalidationArea{}, paintArea{};
} tabTiming;
uint32_t currentMessageMs{};
uint64_t tabPaintSequence{};
struct TabPaintCounts {
  uint32_t fills{}, texts{}, glyphs{}, lines{}, colors{};
  uint64_t fillPixels{}, textBoundsPixels{}, linePixels{};
} tabPaintCounts;
bool tabPaintActive{};
void report_tab_paint(const char *phase) noexcept;
uint64_t tab_paint_area(Rect rect) noexcept;
#define TAB_PAINT_PHASE(phase) report_tab_paint(phase)
#else
#define TAB_PAINT_PHASE(phase) ((void)0)
#endif
struct RowText {
  bool used{};
  uint32_t pid{};
  uint64_t incarnation{}, sample{};
  uint64_t age{};
  uint32_t fontGeneration{};
  int width{};
  char cpu[24]{}, memory[24]{}, pidText[24]{};
};
RowText rowCache[64]{};
Layout cachedLayout{};
int cachedWidth{}, cachedHeight{};
uint32_t cachedFontGeneration{};
uint64_t cachedSampleGeneration{UINT64_MAX};
bool layoutValid{};
Rect paintDirty{};
bool painting{};
struct PaintColors {
  bool textKnown{}, backgroundKnown{}, modeKnown{};
  COLORREF text{}, background{};
  int mode{};
} paintColors;
Rect pendingInvalidations[32]{};
uint32_t pendingInvalidationCount{};

COLORREF paint_set_text_color(HDC dc, COLORREF color) noexcept {
  if (painting && paintColors.textKnown && paintColors.text == color)
    return color;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) ++tabPaintCounts.colors;
#endif
  const COLORREF previous = SetTextColor(dc, color);
  if (painting && previous != CLR_INVALID) {
    paintColors.textKnown = true;
    paintColors.text = color;
  }
  return previous;
}

COLORREF paint_set_background_color(HDC dc, COLORREF color) noexcept {
  if (painting && paintColors.backgroundKnown && paintColors.background == color)
    return color;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) ++tabPaintCounts.colors;
#endif
  const COLORREF previous = SetBkColor(dc, color);
  if (painting && previous != CLR_INVALID) {
    paintColors.backgroundKnown = true;
    paintColors.background = color;
  }
  return previous;
}

int paint_set_background_mode(HDC dc, int mode) noexcept {
  if (painting && paintColors.modeKnown && paintColors.mode == mode)
    return mode;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) ++tabPaintCounts.colors;
#endif
  const int previous = SetBkMode(dc, mode);
  if (painting && previous) {
    paintColors.modeKnown = true;
    paintColors.mode = mode;
  }
  return previous;
}

void reset_text_cache() noexcept {
  memset(textCache, 0, sizeof(textCache));
  memset(rowCache, 0, sizeof(rowCache));
}

TextCacheEntry *find_text(const char *text, int width) noexcept {
  for (auto &entry : textCache) {
    if (entry.used && entry.font == app.font &&
        entry.generation == app.fontGeneration &&
        entry.style == UNDOS_TEXT_REGULAR &&
        entry.mode == UNDOS_TEXT_PROPORTIONAL &&
        entry.repertoire == UNDOS_TEXT_BASIC &&
        entry.width == width && strcmp(entry.text, text) == 0) {
      entry.age = ++cacheAge;
      return &entry;
    }
  }
  return nullptr;
}

TextCacheEntry *store_text(const char *text, int width, bool persistent = false) noexcept {
  if (strlen(text) >= sizeof(textCache[0].text)) return nullptr;
  auto *victim = &textCache[0];
  for (auto &entry : textCache) {
    if (!entry.used) { victim = &entry; break; }
    if (entry.age < victim->age) victim = &entry;
  }
  *victim = {};
  victim->used = true;
  victim->persistent = persistent;
  victim->font = app.font;
  victim->generation = app.fontGeneration;
  victim->style = UNDOS_TEXT_REGULAR;
  victim->mode = UNDOS_TEXT_PROPORTIONAL;
  victim->repertoire = UNDOS_TEXT_BASIC;
  victim->width = width;
  victim->age = ++cacheAge;
  strcpy(victim->text, text);
  return victim;
}

bool dirty(const Rect &rect) noexcept {
  return !painting || (rect.left < paintDirty.right && rect.right > paintDirty.left &&
      rect.top < paintDirty.bottom && rect.bottom > paintDirty.top);
}
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
uint32_t diagnostic_measure_calls = 0;
uint32_t diagnostic_paints = 0;
#endif

void free_snapshot(Snapshot &snapshot) noexcept {
  free(snapshot.cpus);
  free(snapshot.processes);
  free(snapshot.names);
  free(snapshot.cpuBasisPoints);
  free(snapshot.cpuPercentValid);
  free(snapshot.processBasisPoints);
  free(snapshot.processPercentValid);
  free(snapshot.sortedProcesses);
  snapshot = {};
}

bool checked_allocation(uint32_t count, size_t itemSize, void **out) noexcept {
  *out = nullptr;
  if (!count) return true;
  if (itemSize > SIZE_MAX / count) return false;
  *out = malloc(static_cast<size_t>(count) * itemSize);
  return *out != nullptr;
}

const char *status_name(uint32_t status) noexcept {
  switch (status) {
    case SyscallReturnCode_OKAY: return "OK";
    case SyscallReturnCode_INVALID_SIZE: return "INVALID_SIZE";
    case SyscallReturnCode_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case SyscallReturnCode_INVALID_ADDRESS: return "INVALID_ADDRESS";
    case SyscallReturnCode_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case SyscallReturnCode_UNSUPPORTED: return "UNSUPPORTED";
    case SyscallReturnCode_TIMEOUT: return "TIMEOUT";
    case SyscallReturnCode_RETRY: return "RETRY";
    case SyscallReturnCode_NOT_FOUND: return "NOT_FOUND";
    default: return "UNKNOWN";
  }
}

void recover_failed_window_creations() noexcept {
  for (uint32_t attempt = 0; attempt < 64; ++attempt) {
    UNDOS_WINDOW_CREATION_RECOVERY recovery{};
    if (!UndosGetWindowCreationRecovery(&recovery)) return;
    if (DestroyWindow(recovery.window)) continue;
    const uint32_t cleanupError = GetLastError();
    const uint32_t cleanupStatus = UndosGetLastStatus();
    (void)crt_printf(
        "taskmgr: window setup error=%u status=%u; cleanup error=%u status=%u\n",
        recovery.setupError, recovery.setupStatus, cleanupError, cleanupStatus);
    return;
  }
}

const char *cpu_reason(uint32_t reason) noexcept {
  switch (reason) {
    case TaskCpuUnavailableReason_CpuClockUnavailable: return "clock unavailable";
    case TaskCpuUnavailableReason_CpuCounterDiscontinuity: return "counter discontinuity";
    case TaskCpuUnavailableReason_CpuTopologyUnsupported: return "CPU topology unsupported";
    default: return "unavailable";
  }
}

const char *memory_reason(uint32_t reason) noexcept {
  switch (reason) {
    case TaskMemoryUnavailableReason_MemoryManagerUninitialized:
      return "memory manager not initialized";
    case TaskMemoryUnavailableReason_MemoryOwnershipUnavailable:
      return "physical ownership unavailable";
    case TaskMemoryUnavailableReason_MemorySharedOwnershipUnsupported:
      return "shared physical ownership unsupported";
    default: return "unavailable";
  }
}

size_t format_u64(uint64_t value, char *out, size_t capacity) noexcept {
  if (!capacity) return 0;
  char reverse[24];
  size_t digits = 0;
  do {
    reverse[digits++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value && digits < sizeof(reverse));
  if (digits >= capacity) digits = capacity - 1;
  for (size_t i = 0; i < digits; ++i) out[i] = reverse[digits - i - 1];
  out[digits] = 0;
  return digits;
}

void format_basis_points(uint32_t value, char *out, size_t capacity) noexcept {
  if (value > 10000) value = 10000;
  const uint32_t tenths = value / 10;
  char whole[16];
  format_u64(tenths / 10, whole, sizeof(whole));
  size_t n = 0;
  while (whole[n] && n + 1 < capacity) {
    out[n] = whole[n];
    ++n;
  }
  if (n + 4 > capacity) {
    out[0] = 0;
    return;
  }
  out[n++] = '.';
  out[n++] = static_cast<char>('0' + tenths % 10);
  out[n++] = '%';
  out[n] = 0;
}

bool measure_text(const char *text, uint32_t &width, bool persistent = false) noexcept {
  if (app.fontValid) {
    if (auto *cached = find_text(text, -1)) {
      width = cached->extent.advanceWidth;
      return true;
    }
  }
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
  ++diagnostic_measure_calls;
#endif
  UNDOS_TEXT_EXTENT extent{};
  SetLastError(ERROR_SUCCESS);
  if (!app.fontValid ||
      !UndosMeasureText(app.font, UNDOS_TEXT_REGULAR,
                        UNDOS_TEXT_PROPORTIONAL,
                        UNDOS_TEXT_BASIC, text, &extent)) {
    app.fontStale = true;
    app.fontMeasureStatus = app.fontValid ? UndosGetLastStatus()
                                          : SyscallReturnCode_INVALID_ARGUMENT;
    if (app.fontMeasureStatus == SyscallReturnCode_OKAY)
      app.fontMeasureStatus = SyscallReturnCode_UNKNOWN;
    reset_text_cache();
    layoutValid = false;
    return false;
  }
  if (extent.generation != app.fontGeneration) {
    app.fontStale = true;
    app.fontMeasureStatus = SyscallReturnCode_RETRY;
    reset_text_cache();
    layoutValid = false;
    return false;
  }
  app.fontStale = false;
  app.fontMeasureStatus = SyscallReturnCode_OKAY;
  width = extent.advanceWidth;
  if (auto *cached = store_text(text, -1, persistent)) cached->extent = extent;
  return true;
}

bool acquire_font() noexcept {
  UNDOS_FONT candidate = 0;
  uint32_t generation = 0;
  UNDOS_TEXT_METRICS metrics{};
  if (!UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
                        UNDOS_GLYPH_FALLBACK_NOTDEF,
                        &candidate, &generation))
    return false;
  if (!UndosGetFontMetrics(candidate, UNDOS_TEXT_REGULAR,
                           UNDOS_TEXT_BASIC, &metrics)) {
    const uint32_t status = UndosGetLastStatus();
    (void)UndosCloseFont(candidate);
    app.fontMeasureStatus = status;
    return false;
  }
  if (!metrics.lineHeight || metrics.generation != generation) {
    (void)UndosCloseFont(candidate);
    app.fontMeasureStatus = SyscallReturnCode_RETRY;
    SetLastError(ERROR_RETRY);
    return false;
  }
  UNDOS_TEXT_EXTENT measuredTitle{};
  if (!UndosMeasureText(candidate, UNDOS_TEXT_REGULAR,
                        UNDOS_TEXT_PROPORTIONAL,
                        UNDOS_TEXT_BASIC, "Image Name", &measuredTitle)) {
    const uint32_t status = UndosGetLastStatus();
    (void)UndosCloseFont(candidate);
    app.fontMeasureStatus = status;
    return false;
  }
  if (measuredTitle.generation != generation ||
      !measuredTitle.advanceWidth ||
      measuredTitle.lineHeight != metrics.lineHeight) {
    (void)UndosCloseFont(candidate);
    app.fontMeasureStatus = SyscallReturnCode_RETRY;
    SetLastError(ERROR_RETRY);
    return false;
  }
  const UNDOS_FONT old = app.font;
  const bool hadOld = app.fontValid;
  app.font = candidate;
  app.fontValid = true;
  app.fontGeneration = generation;
  app.metrics = metrics;
  reset_text_cache();
  layoutValid = false;
  app.fontStale = false;
  app.fontMeasureStatus = SyscallReturnCode_OKAY;
  if (hadOld) (void)UndosCloseFont(old);
  if (app.window) (void)InvalidateRect(app.window, nullptr, TRUE);
  return true;
}

uint32_t process_rows() noexcept {
  return app.sample.processCount + 2;
}

int selected_real_index() noexcept {
  if (!app.hasSample || app.selectedPosition >= app.sample.processCount) return -1;
  return static_cast<int>(app.sample.sortedProcesses[app.selectedPosition]);
}

void sort_processes(Snapshot &snapshot) noexcept {
  for (uint32_t i = 0; i < snapshot.processCount; ++i) {
    uint32_t position = i;
    while (position &&
           snapshot.processes[snapshot.sortedProcesses[position - 1]].pid >
               snapshot.processes[i].pid) {
      snapshot.sortedProcesses[position] =
          snapshot.sortedProcesses[position - 1];
      --position;
    }
    snapshot.sortedProcesses[position] = i;
  }
}

bool same_cpu_set(const Snapshot &left, const Snapshot &right) noexcept {
  if (!left.valid || left.cpuCount != right.cpuCount) return false;
  for (uint32_t i = 0; i < left.cpuCount; ++i)
    if (left.cpus[i].cpuId != right.cpus[i].cpuId) return false;
  return true;
}

bool prepare_cpu_history(const Snapshot &snapshot) noexcept {
  bool same = app.cpuHistoryCount == snapshot.cpuCount;
  for (uint32_t i = 0; same && i < snapshot.cpuCount; ++i)
    same = app.cpuHistoryIds[i] == snapshot.cpus[i].cpuId;
  if (same) return true;

  size_t entries = 0;
  if (snapshot.cpuCount &&
      taskmgr::HistoryLength > SIZE_MAX / snapshot.cpuCount)
    return false;
  entries = static_cast<size_t>(snapshot.cpuCount) * taskmgr::HistoryLength;
  void *rawValues = nullptr, *rawValid = nullptr, *rawIds = nullptr;
  if (!checked_allocation(entries, sizeof(uint32_t), &rawValues) ||
      !checked_allocation(entries, sizeof(uint8_t), &rawValid) ||
      !checked_allocation(snapshot.cpuCount, sizeof(uint32_t), &rawIds)) {
    free(rawValues);
    free(rawValid);
    free(rawIds);
    app.paintFailure = SyscallReturnCode_OUT_OF_MEMORY;
    return false;
  }
  auto *values = static_cast<uint32_t *>(rawValues);
  auto *valid = static_cast<uint8_t *>(rawValid);
  auto *ids = static_cast<uint32_t *>(rawIds);
  if (entries) {
    memset(values, 0, entries * sizeof(uint32_t));
    memset(valid, 0, entries * sizeof(uint8_t));
  }
  if ((entries && (!values || !valid)) || (snapshot.cpuCount && !ids)) {
    free(values);
    free(valid);
    free(ids);
    app.paintFailure = SyscallReturnCode_OUT_OF_MEMORY;
    return false;
  }
  for (uint32_t i = 0; i < snapshot.cpuCount; ++i) ids[i] = snapshot.cpus[i].cpuId;
  free(app.cpuHistory);
  free(app.cpuHistoryValid);
  free(app.cpuHistoryIds);
  app.cpuHistory = values;
  app.cpuHistoryValid = valid;
  app.cpuHistoryIds = ids;
  app.cpuHistoryCount = snapshot.cpuCount;
  app.historyCpuScroll = 0;
  return true;
}

void add_history_gap() noexcept {
  app.history.push({});
  if (app.cpuHistory && app.cpuHistoryValid) {
    const uint32_t slot = (app.history.next + taskmgr::HistoryLength - 1) %
                          taskmgr::HistoryLength;
    for (uint32_t cpu = 0; cpu < app.cpuHistoryCount; ++cpu)
      app.cpuHistoryValid[cpu * taskmgr::HistoryLength + slot] = 0;
  }
}

void derive_counters(Snapshot &candidate) noexcept {
  if (!app.hasSample || !same_cpu_set(app.sample, candidate) ||
      !(app.sample.header.validity & taskmgr::CpuValidity) ||
      !(candidate.header.validity & taskmgr::CpuValidity) ||
      app.sample.header.counterEpoch != candidate.header.counterEpoch ||
      app.sample.header.clockHz != candidate.header.clockHz ||
      app.sample.header.clockResolutionCycles != candidate.header.clockResolutionCycles)
    return;

  uint64_t totalDelta = 0;
  uint64_t systemDelta = 0;
  uint64_t idleDelta = 0;
  for (uint32_t i = 0; i < candidate.cpuCount; ++i) {
    uint64_t total = 0, system = 0, idle = 0;
    if (!taskmgr::counter_delta(candidate.cpus[i].totalCycles,
                                app.sample.cpus[i].totalCycles, total) ||
        !taskmgr::counter_delta(candidate.cpus[i].systemCycles,
                                app.sample.cpus[i].systemCycles, system) ||
        !taskmgr::counter_delta(candidate.cpus[i].idleCycles,
                                app.sample.cpus[i].idleCycles, idle) ||
        totalDelta > UINT64_MAX - total ||
        systemDelta > UINT64_MAX - system ||
        idleDelta > UINT64_MAX - idle)
      return;
    totalDelta += total;
    systemDelta += system;
    idleDelta += idle;
    uint64_t cpuTotal = 0;
    if (!taskmgr::cpu_delta_basis_points(
            candidate.cpus[i], app.sample.cpus[i],
            candidate.header.counterEpoch, app.sample.header.counterEpoch,
            candidate.cpuBasisPoints[i], cpuTotal))
      return;
  }
  if (!totalDelta || systemDelta > totalDelta || idleDelta > totalDelta ||
      systemDelta > totalDelta - idleDelta)
    return;
  if (!taskmgr::ratio_basis_points(totalDelta - idleDelta, totalDelta,
                                   candidate.busyBasisPoints) ||
      !taskmgr::ratio_basis_points(systemDelta, totalDelta,
                                   candidate.systemBasisPoints) ||
      !taskmgr::ratio_basis_points(idleDelta, totalDelta,
                                   candidate.idleBasisPoints))
    return;
  for (uint32_t i = 0; i < candidate.cpuCount; ++i)
    candidate.cpuPercentValid[i] = 1;
  candidate.busyValid = true;
  candidate.systemValid = true;
  candidate.idleValid = true;

  for (uint32_t i = 0; i < candidate.processCount; ++i) {
    const auto &current = candidate.processes[i];
    const int previousIndex = taskmgr::find_process(
        app.sample.processes, app.sample.processCount,
        {current.pid, current.incarnation});
    if (previousIndex < 0) continue;
    if (taskmgr::process_delta_basis_points(
            current, app.sample.processes[previousIndex], totalDelta,
            candidate.processBasisPoints[i]))
      candidate.processPercentValid[i] = 1;
  }
}

bool allocate_snapshot(Snapshot &snapshot, uint32_t cpuCount,
                       uint32_t processCount, uint32_t nameBytes) noexcept {
  snapshot.cpuCount = cpuCount;
  snapshot.processCount = processCount;
  snapshot.nameBytes = nameBytes;
  if (!checked_allocation(cpuCount, sizeof(TaskCpuRecordV1),
                          reinterpret_cast<void **>(&snapshot.cpus)) ||
      !checked_allocation(processCount, sizeof(TaskProcessRecordV1),
                          reinterpret_cast<void **>(&snapshot.processes)) ||
      !checked_allocation(nameBytes, sizeof(uint8_t),
                          reinterpret_cast<void **>(&snapshot.names)) ||
      !checked_allocation(cpuCount, sizeof(uint32_t),
                          reinterpret_cast<void **>(&snapshot.cpuBasisPoints)) ||
      !checked_allocation(cpuCount, sizeof(uint8_t),
                          reinterpret_cast<void **>(&snapshot.cpuPercentValid)) ||
      !checked_allocation(processCount, sizeof(uint32_t),
                          reinterpret_cast<void **>(&snapshot.processBasisPoints)) ||
      !checked_allocation(processCount, sizeof(uint8_t),
                          reinterpret_cast<void **>(&snapshot.processPercentValid)) ||
      !checked_allocation(processCount, sizeof(uint32_t),
                          reinterpret_cast<void **>(&snapshot.sortedProcesses))) {
    free_snapshot(snapshot);
    return false;
  }
  if (snapshot.cpuBasisPoints)
    memset(snapshot.cpuBasisPoints, 0, cpuCount * sizeof(uint32_t));
  if (snapshot.cpuPercentValid)
    memset(snapshot.cpuPercentValid, 0, cpuCount * sizeof(uint8_t));
  if (snapshot.processBasisPoints)
    memset(snapshot.processBasisPoints, 0, processCount * sizeof(uint32_t));
  if (snapshot.processPercentValid)
    memset(snapshot.processPercentValid, 0, processCount * sizeof(uint8_t));
  return true;
}

void preserve_selection(const Snapshot &old, Snapshot &replacement,
                        uint32_t oldPosition) noexcept {
  if (!old.valid) {
    app.selectedPosition = 0;
    return;
  }
  if (oldPosition >= old.processCount) {
    app.selectedPosition = replacement.processCount +
        (oldPosition == old.processCount ? 0u : 1u);
    return;
  }
  const auto &selected = old.processes[old.sortedProcesses[oldPosition]];
  const int index = taskmgr::find_process(
      replacement.processes, replacement.processCount,
      {selected.pid, selected.incarnation});
  if (index >= 0) {
    for (uint32_t i = 0; i < replacement.processCount; ++i) {
      if (replacement.sortedProcesses[i] == static_cast<uint32_t>(index)) {
        app.selectedPosition = i;
        return;
      }
    }
  }
  app.selectedPosition = replacement.processCount
      ? (oldPosition < replacement.processCount
             ? oldPosition : replacement.processCount - 1)
      : 0;
}

bool compute_layout(Layout &layout) noexcept;
void keep_selection_visible(const Layout &layout) noexcept;
void invalidate_sample_update(const Snapshot &previous, bool hadPrevious,
                              uint32_t previousSelection,
                              uint32_t previousScroll, bool previousStale,
                              uint32_t previousStaleStatus,
                              bool previousFontStale,
                              bool previousLayoutValid,
                              const Layout &previousLayout) noexcept;

bool collect_snapshot() noexcept {
  for (uint32_t attempt = 0; attempt < 2; ++attempt) {
    uint32_t cpuCapacity = 0, processCapacity = 0, nameCapacity = 0;
    uint32_t requiredCpus = 0, requiredProcesses = 0, requiredNames = 0;
    uint32_t writtenCpus = 0, writtenProcesses = 0, writtenNames = 0;
    TaskSnapshotHeaderV1 header{};
    const uint32_t queryStatus = UD_SYS_GetTaskSnapshotV1(
        1, nullptr, &cpuCapacity, nullptr, &processCapacity, nullptr,
        &nameCapacity, &header, &requiredCpus, &requiredProcesses,
        &requiredNames, &writtenCpus, &writtenProcesses, &writtenNames);
    if (queryStatus != SyscallReturnCode_OKAY) {
      app.staleStatus = queryStatus;
      return false;
    }

    Snapshot candidate{};
    if (!allocate_snapshot(candidate, requiredCpus, requiredProcesses,
                           requiredNames)) {
      app.staleStatus = SyscallReturnCode_OUT_OF_MEMORY;
      return false;
    }
    cpuCapacity = requiredCpus;
    processCapacity = requiredProcesses;
    nameCapacity = requiredNames;
    header = {};
    requiredCpus = requiredProcesses = requiredNames = 0;
    writtenCpus = writtenProcesses = writtenNames = 0;
    const uint32_t fillStatus = UD_SYS_GetTaskSnapshotV1(
        0, candidate.cpus, &cpuCapacity, candidate.processes,
        &processCapacity, candidate.names, &nameCapacity, &header,
        &requiredCpus, &requiredProcesses, &requiredNames, &writtenCpus,
        &writtenProcesses, &writtenNames);
    if (fillStatus == SyscallReturnCode_RETRY ||
        fillStatus == SyscallReturnCode_INVALID_SIZE) {
      free_snapshot(candidate);
      app.staleStatus = fillStatus;
      continue;
    }
    if (fillStatus != SyscallReturnCode_OKAY) {
      free_snapshot(candidate);
      app.staleStatus = fillStatus;
      return false;
    }
    if (writtenCpus != cpuCapacity || writtenProcesses != processCapacity ||
        writtenNames != nameCapacity ||
        writtenCpus != requiredCpus ||
        writtenProcesses != requiredProcesses ||
        writtenNames != requiredNames ||
        !taskmgr::validate_snapshot(
            header, candidate.cpus, writtenCpus, candidate.processes,
            writtenProcesses, candidate.names, writtenNames)) {
      free_snapshot(candidate);
      app.staleStatus = SyscallReturnCode_INVALID_ARGUMENT;
      return false;
    }
    candidate.cpuCount = writtenCpus;
    candidate.processCount = writtenProcesses;
    candidate.nameBytes = writtenNames;
    const bool hadPrevious = app.hasSample;
    const uint32_t previousSelection = app.selectedPosition;
    const uint32_t previousScroll = app.processScroll;
    const bool previousStale = app.stale;
    const uint32_t previousStaleStatus = app.staleStatus;
    const bool previousFontStale = app.fontStale;
    Layout previousLayout{};
    const bool previousLayoutValid = hadPrevious && app.window &&
                                     compute_layout(previousLayout);
    Snapshot previous = app.sample;

    candidate.header = header;
    candidate.valid = true;
    sort_processes(candidate);
    derive_counters(candidate);
#if defined(UNDOS_TASK_MEMORY_DIAGNOSTICS)
    if (app.observationSamples) {
      --app.observationSamples;
      char record[256] = "TMR4";
      const auto append = [&](const char *key, uint64_t number) {
        char digits[24]{};
        format_u64(number, digits, sizeof(digits));
        strcat(record, key);
        strcat(record, digits);
      };
      append(" epoch=", candidate.header.counterEpoch);
      append(" reason=", candidate.header.cpuReason);
      append(" total=", candidate.header.sampleCycles);
      append(" view=", app.view);
      append(" busy_valid=", candidate.busyValid);
      append(" busy_bp=", candidate.busyBasisPoints);
      append(" cpu0_valid=", candidate.cpuCount && candidate.cpuPercentValid[0]);
      append(" cpu0_bp=", candidate.cpuCount ? candidate.cpuBasisPoints[0] : 0);
      if (candidate.processCount) {
        append(" pid=", candidate.processes[0].pid);
        append(" process_valid=", candidate.processPercentValid[0]);
        append(" process_bp=", candidate.processBasisPoints[0]);
      }
      (void)UD_SYS_PrintV1(record);
    }
#endif
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
    char diagnostic[256] = "TMUI epoch=";
    char value[32]{};
    format_u64(candidate.header.counterEpoch, value, sizeof(value));
    strcat(diagnostic, value);
    strcat(diagnostic, " reason=");
    format_u64(candidate.header.cpuReason, value, sizeof(value));
    strcat(diagnostic, value);
    strcat(diagnostic, " total=");
    format_u64(candidate.header.sampleCycles, value, sizeof(value));
    strcat(diagnostic, value);
    strcat(diagnostic, " view=");
    strcat(diagnostic, app.view ? "1" : "0");
    strcat(diagnostic, " busy_valid=");
    strcat(diagnostic, candidate.busyValid ? "1" : "0");
    strcat(diagnostic, " busy_bp=");
    format_u64(candidate.busyBasisPoints, value, sizeof(value));
    strcat(diagnostic, value);
    strcat(diagnostic, " cpu0_valid=");
    strcat(diagnostic, candidate.cpuCount && candidate.cpuPercentValid[0] ? "1" : "0");
    strcat(diagnostic, " cpu0_bp=");
    format_u64(candidate.cpuCount ? candidate.cpuBasisPoints[0] : 0,
               value, sizeof(value));
    strcat(diagnostic, value);
    (void)UD_SYS_PrintV1(diagnostic);
    strcpy(diagnostic, "TMCOST D2 measures=");
    format_u64(diagnostic_measure_calls, value, sizeof(value));
    strcat(diagnostic, value);
    strcat(diagnostic, " paints=");
    format_u64(diagnostic_paints, value, sizeof(value));
    strcat(diagnostic, value);
    (void)UD_SYS_PrintV1(diagnostic);
    diagnostic_measure_calls = 0;
    diagnostic_paints = 0;
    for (uint32_t i = 0; i < candidate.processCount; ++i) {
      strcpy(diagnostic, "TMROW pid=");
      format_u64(candidate.processes[i].pid, value, sizeof(value));
      strcat(diagnostic, value);
      strcat(diagnostic, " cycles=");
      format_u64(candidate.processes[i].cpuCycles, value, sizeof(value));
      strcat(diagnostic, value);
      strcat(diagnostic, " valid=");
      strcat(diagnostic, candidate.processPercentValid[i] ? "1" : "0");
      strcat(diagnostic, " bp=");
      format_u64(candidate.processBasisPoints[i], value, sizeof(value));
      strcat(diagnostic, value);
      (void)UD_SYS_PrintV1(diagnostic);
    }
#endif
    preserve_selection(app.sample, candidate, app.selectedPosition);

    const bool cpuSetUnchanged = same_cpu_set(app.sample, candidate);
    if (!cpuSetUnchanged) {
      free(app.cpuHistory);
      free(app.cpuHistoryValid);
      free(app.cpuHistoryIds);
      app.cpuHistory = nullptr;
      app.cpuHistoryValid = nullptr;
      app.cpuHistoryIds = nullptr;
      app.cpuHistoryCount = 0;
    }
    const bool cpuHistoryReady = prepare_cpu_history(candidate);
    const uint32_t slot = app.history.next;
    if (cpuHistoryReady && app.cpuHistory && app.cpuHistoryValid) {
      for (uint32_t i = 0; i < candidate.cpuCount; ++i) {
        const size_t index = static_cast<size_t>(i) * taskmgr::HistoryLength + slot;
        app.cpuHistoryValid[index] = candidate.cpuPercentValid[i];
        app.cpuHistory[index] = candidate.cpuBasisPoints[i];
      }
    }
    taskmgr::HistorySlot history{};
    history.cpuValid = candidate.busyValid;
    history.cpuBasisPoints = candidate.busyBasisPoints;
    history.memoryValid = (candidate.header.validity & taskmgr::MemoryValidity) != 0;
    history.usedBytes = candidate.header.physicalUsedBytes;
    history.totalBytes = candidate.header.physicalTotalBytes;
    app.history.push(history);

    app.sample = candidate;
    ++app.sampleGeneration;
    for (auto &entry : textCache) if (!entry.persistent) entry = {};
    memset(rowCache, 0, sizeof(rowCache));
    layoutValid = false;
    app.hasSample = true;
    app.stale = false;
    app.staleStatus = SyscallReturnCode_OKAY;
    Layout currentLayout{};
    if (app.window && compute_layout(currentLayout))
      keep_selection_visible(currentLayout);
    invalidate_sample_update(previous, hadPrevious, previousSelection,
                             previousScroll, previousStale,
                             previousStaleStatus, previousFontStale,
                             previousLayoutValid, previousLayout);
    if (hadPrevious) free_snapshot(previous);
    return true;
  }
  return false;
}

uint32_t text_width(const char *text) noexcept {
  uint32_t width = 0;
  (void)measure_text(text, width, true);
  return width;
}

void format_row_value(const Snapshot &sample, uint32_t row,
                      char *cpuText, size_t cpuCapacity,
                      char *memoryText, size_t memoryCapacity,
                      char *pidText, size_t pidCapacity) noexcept;

void format_row_uncached(const Snapshot &sample, uint32_t row,
                      char *cpuText, size_t cpuCapacity,
                      char *memoryText, size_t memoryCapacity,
                      char *pidText, size_t pidCapacity) noexcept {
  if (row < sample.processCount) {
    const uint32_t processIndex = sample.sortedProcesses[row];
    const auto &process = sample.processes[processIndex];
    format_u64(process.pid, pidText, pidCapacity);
    if (sample.processPercentValid[processIndex]) {
      format_basis_points(sample.processBasisPoints[processIndex],
                          cpuText, cpuCapacity);
    } else {
      cpuText[0] = 0;
    }
    if (process.validity & taskmgr::MemoryValidity) {
      format_u64(process.physicalBytes / 1024, memoryText, memoryCapacity);
    } else {
      memoryText[0] = 0;
    }
    return;
  }
  pidText[0] = 0;
  if (row == sample.processCount) {
    if (sample.systemValid)
      format_basis_points(sample.systemBasisPoints, cpuText, cpuCapacity);
    else cpuText[0] = 0;
    if (sample.header.validity & taskmgr::MemoryValidity)
      format_u64(sample.header.systemPhysicalBytes / 1024,
                 memoryText, memoryCapacity);
    else memoryText[0] = 0;
  } else {
    if (sample.idleValid)
      format_basis_points(sample.idleBasisPoints, cpuText, cpuCapacity);
    else cpuText[0] = 0;
    memoryText[0] = 0;
  }
}

bool same_rendered_row(const Snapshot &left, const Snapshot &right,
                       uint32_t row) noexcept {
  const bool leftProcess = row < left.processCount;
  const bool rightProcess = row < right.processCount;
  const bool leftSystem = row == left.processCount;
  const bool rightSystem = row == right.processCount;
  if (leftProcess != rightProcess || leftSystem != rightSystem) return false;
  if (leftProcess) {
    const auto &a = left.processes[left.sortedProcesses[row]];
    const auto &b = right.processes[right.sortedProcesses[row]];
    if (a.pid != b.pid || a.incarnation != b.incarnation ||
        a.nameBytes != b.nameBytes ||
        memcmp(left.names + a.nameOffset, right.names + b.nameOffset,
               a.nameBytes) != 0)
      return false;
  }
  char leftCpu[24]{}, leftMemory[24]{}, leftPid[24]{};
  char rightCpu[24]{}, rightMemory[24]{}, rightPid[24]{};
  format_row_value(left, row, leftCpu, sizeof(leftCpu), leftMemory,
                   sizeof(leftMemory), leftPid, sizeof(leftPid));
  format_row_value(right, row, rightCpu, sizeof(rightCpu), rightMemory,
                   sizeof(rightMemory), rightPid, sizeof(rightPid));
  return strcmp(leftCpu, rightCpu) == 0 &&
         strcmp(leftMemory, rightMemory) == 0 &&
         strcmp(leftPid, rightPid) == 0 &&
         (left.header.validity & taskmgr::CpuValidity) ==
             (right.header.validity & taskmgr::CpuValidity) &&
         left.header.cpuReason == right.header.cpuReason &&
         (left.header.validity & taskmgr::MemoryValidity) ==
             (right.header.validity & taskmgr::MemoryValidity) &&
         left.header.memoryReason == right.header.memoryReason;
}

void format_row_value(const Snapshot &sample, uint32_t row,
                      char *cpuText, size_t cpuCapacity,
                      char *memoryText, size_t memoryCapacity,
                      char *pidText, size_t pidCapacity) noexcept {
  if (&sample != &app.sample || row >= sample.processCount) {
    format_row_uncached(sample, row, cpuText, cpuCapacity,
                         memoryText, memoryCapacity, pidText, pidCapacity);
    return;
  }
  const auto &process = sample.processes[sample.sortedProcesses[row]];
  RowText *cached = nullptr, *victim = &rowCache[0];
  for (auto &entry : rowCache) {
    if (entry.used && entry.pid == process.pid &&
        entry.incarnation == process.incarnation &&
        entry.sample == app.sampleGeneration &&
        entry.fontGeneration == app.fontGeneration && entry.width == cachedWidth) {
      cached = &entry;
      break;
    }
    if (!entry.used || entry.age < victim->age) victim = &entry;
  }
  if (!cached) {
    cached = victim;
    *cached = {};
    cached->used = true;
    cached->pid = process.pid;
    cached->incarnation = process.incarnation;
    cached->sample = app.sampleGeneration;
    cached->fontGeneration = app.fontGeneration;
    cached->width = cachedWidth;
    format_row_uncached(sample, row, cached->cpu, sizeof(cached->cpu),
        cached->memory, sizeof(cached->memory), cached->pidText, sizeof(cached->pidText));
  }
  cached->age = ++cacheAge;
  if (cpuCapacity) { strncpy(cpuText, cached->cpu, cpuCapacity - 1); cpuText[cpuCapacity - 1] = 0; }
  if (memoryCapacity) { strncpy(memoryText, cached->memory, memoryCapacity - 1); memoryText[memoryCapacity - 1] = 0; }
  if (pidCapacity) { strncpy(pidText, cached->pidText, pidCapacity - 1); pidText[pidCapacity - 1] = 0; }
}

void measure_columns(Layout &layout) noexcept {
  layout.pidWidth = static_cast<int>(text_width("PID"));
  layout.cpuWidth = static_cast<int>(text_width("CPU %"));
  layout.memoryWidth = static_cast<int>(text_width("Memory (KiB)"));
  // Measure each possible digit; proportional numerals need not be equal.
  uint32_t digit = 0;
  for (char c = '0'; c <= '9'; ++c) {
    const char text[]{c, 0};
    const uint32_t width = text_width(text);
    if (width > digit) digit = width;
  }
  const int pid = static_cast<int>(digit * 10);
  const int cpu = static_cast<int>(digit * 4 + text_width(".%"));
  const int memory = static_cast<int>(digit * 17); // UINT64_MAX bytes in KiB.
  if (pid > layout.pidWidth) layout.pidWidth = pid;
  if (cpu > layout.cpuWidth) layout.cpuWidth = cpu;
  if (memory > layout.memoryWidth) layout.memoryWidth = memory;
  layout.pidWidth += 12;
  layout.cpuWidth += 12;
  layout.memoryWidth += 12;
  if (layout.pidWidth < 40) layout.pidWidth = 40;
  if (layout.cpuWidth < 68) layout.cpuWidth = 68;
  if (layout.memoryWidth < 98) layout.memoryWidth = 98;
}

bool compute_layout(Layout &layout) noexcept {
  RECT client{};
  if (!GetClientRect(app.window, &client)) return false;
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0 || !app.metrics.lineHeight) return false;
  if (layoutValid && cachedWidth == width && cachedHeight == height &&
      cachedFontGeneration == app.fontGeneration &&
      cachedSampleGeneration == app.sampleGeneration) {
    layout = cachedLayout;
    return true;
  }
  const int line = static_cast<int>(app.metrics.lineHeight);
  layout.rowHeight = line + 6;
  layout.headerHeight = line + 8;
  layout.footerHeight = line + 10;
  layout.status = {8, height - layout.footerHeight - 4,
                   width - 8, height - 4};
  const int tabHeight = line + 10;
  uint32_t processesTabWidth = text_width("Processes") + 24;
  uint32_t performanceTabWidth = text_width("Performance") + 24;
  if (processesTabWidth < 92) processesTabWidth = 92;
  if (performanceTabWidth < 108) performanceTabWidth = 108;
  layout.processesTab = {8, 8, 8 + static_cast<int>(processesTabWidth),
                         8 + tabHeight};
  layout.performanceTab = {layout.processesTab.right + 4, 8,
      layout.processesTab.right + 4 + static_cast<int>(performanceTabWidth),
      8 + tabHeight};
  const int contentTop = layout.processesTab.bottom + 8;
  const int contentBottom = layout.status.top - 8;
  if (contentBottom <= contentTop) return false;
  layout.table = {8, contentTop, width - 8, contentBottom};
  if (app.view == 0 || !cachedLayout.tableWidth ||
      cachedFontGeneration != app.fontGeneration) {
    measure_columns(layout);
  } else {
    layout.pidWidth = cachedLayout.pidWidth;
    layout.cpuWidth = cachedLayout.cpuWidth;
    layout.memoryWidth = cachedLayout.memoryWidth;
  }
  const int fixed = layout.pidWidth + layout.cpuWidth + layout.memoryWidth;
  const int minimumName = static_cast<int>(text_width("Image Name")) + 16;
  layout.nameWidth = width - 16 - fixed;
  if (layout.nameWidth < minimumName) layout.nameWidth = minimumName;
  layout.tableWidth = layout.nameWidth + fixed;
  const int bodyHeight = layout.table.bottom - layout.table.top - layout.headerHeight;
  layout.visibleRows = bodyHeight > 0 ? bodyHeight / layout.rowHeight : 0;

  if (width >= 500) {
    const int split = width * 55 / 100;
    layout.performanceCpu = {8, contentTop, split - 4, contentBottom};
    layout.performanceMemory = {split + 4, contentTop,
                                width - 8, contentBottom};
  } else {
    const int middle = contentTop + (contentBottom - contentTop) / 2;
    layout.performanceCpu = {8, contentTop, width - 8, middle - 4};
    layout.performanceMemory = {8, middle + 4, width - 8, contentBottom};
  }
  if (app.fontStale) return false;
  cachedLayout = layout;
  cachedWidth = width;
  cachedHeight = height;
  cachedFontGeneration = app.fontGeneration;
  cachedSampleGeneration = app.sampleGeneration;
  layoutValid = true;
  return true;
}

void invalidate_area(const Rect &rect, bool erase) noexcept {
  if (!app.window || rect.right <= rect.left || rect.bottom <= rect.top) return;
  for (uint32_t i = 0; i < pendingInvalidationCount; ++i) {
    const Rect &pending = pendingInvalidations[i];
    if (pending.left == rect.left && pending.top == rect.top &&
        pending.right == rect.right && pending.bottom == rect.bottom)
      return;
  }
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabTiming.countingInvalidations) {
    ++tabTiming.invalidationCount;
    tabTiming.invalidationArea += static_cast<uint64_t>(rect.right - rect.left) *
                                 static_cast<uint64_t>(rect.bottom - rect.top);
  }
#endif
  const RECT nativeRect{rect.left, rect.top, rect.right, rect.bottom};
  if (InvalidateRect(app.window, &nativeRect, erase ? TRUE : FALSE) &&
      pendingInvalidationCount < 32)
    pendingInvalidations[pendingInvalidationCount++] = rect;
}

bool same_rect(const Rect &left, const Rect &right) noexcept {
  return left.left == right.left && left.top == right.top &&
         left.right == right.right && left.bottom == right.bottom;
}

bool same_layout(const Layout &left, const Layout &right) noexcept {
  return same_rect(left.processesTab, right.processesTab) &&
         same_rect(left.performanceTab, right.performanceTab) &&
         same_rect(left.table, right.table) &&
         same_rect(left.performanceCpu, right.performanceCpu) &&
         same_rect(left.performanceMemory, right.performanceMemory) &&
         same_rect(left.status, right.status) &&
         left.rowHeight == right.rowHeight &&
         left.headerHeight == right.headerHeight &&
         left.footerHeight == right.footerHeight &&
         left.nameWidth == right.nameWidth && left.pidWidth == right.pidWidth &&
         left.cpuWidth == right.cpuWidth &&
         left.memoryWidth == right.memoryWidth &&
         left.tableWidth == right.tableWidth &&
         left.visibleRows == right.visibleRows;
}

bool same_process_order(const Snapshot &left, const Snapshot &right) noexcept {
  if (left.processCount != right.processCount) return false;
  for (uint32_t row = 0; row < left.processCount; ++row) {
    const auto &a = left.processes[left.sortedProcesses[row]];
    const auto &b = right.processes[right.sortedProcesses[row]];
    if (a.pid != b.pid || a.incarnation != b.incarnation) return false;
  }
  return true;
}

void invalidate_table(const Layout &layout) noexcept {
  invalidate_area(layout.table, false);
}

void invalidate_sample_update(const Snapshot &previous, bool hadPrevious,
                              uint32_t previousSelection,
                              uint32_t previousScroll, bool previousStale,
                              uint32_t previousStaleStatus,
                              bool previousFontStale,
                              bool previousLayoutValid,
                              const Layout &previousLayout) noexcept {
  if (!app.window) return;
  Layout layout{};
  if (!hadPrevious || !previousLayoutValid || !compute_layout(layout) ||
      !same_layout(previousLayout, layout)) {
    (void)InvalidateRect(app.window, nullptr, TRUE);
    return;
  }

  const bool statusChanged = previousStale != app.stale ||
      previousStaleStatus != app.staleStatus ||
      previousFontStale != app.fontStale ||
      previous.processCount != app.sample.processCount;
  if (app.view == 1) {
    invalidate_area(layout.performanceCpu, false);
    invalidate_area(layout.performanceMemory, false);
  } else if (app.view == 0) {
    const auto visible = taskmgr::visible_row_range(
        app.processScroll, static_cast<uint32_t>(layout.visibleRows),
        app.sample.processCount + 2);
    const uint32_t visibleCount = visible.last - visible.first;
    void *rawRanges = nullptr;
    if (visibleCount && !checked_allocation(
            visibleCount, sizeof(taskmgr::RowRange), &rawRanges)) {
      invalidate_table(layout);
    } else {
      auto *ranges = static_cast<taskmgr::RowRange *>(rawRanges);
      taskmgr::invalidate_process_update(
          previous.processCount, app.sample.processCount, previousScroll,
          app.processScroll, app.processScroll,
          static_cast<uint32_t>(layout.visibleRows),
          [&] { return same_process_order(previous, app.sample); },
          [&](uint32_t row) {
            return !same_rendered_row(previous, app.sample, row) ||
                   ((row == previousSelection) !=
                    (row == app.selectedPosition));
          }, ranges, visibleCount,
          [&](taskmgr::RowRange changed) {
            const int rowTop = layout.table.top + layout.headerHeight + 1;
            const int top = rowTop + static_cast<int>(changed.first -
                app.processScroll) * layout.rowHeight;
            const int bottom = rowTop + static_cast<int>(changed.last -
                app.processScroll) * layout.rowHeight;
            invalidate_area({layout.table.left + 1, top,
                             layout.table.right - 1, bottom}, false);
          }, [&] { invalidate_table(layout); });
      free(ranges);
    }
  }
  if (statusChanged) invalidate_area(layout.status, false);
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
  invalidate_area(layout.status, false);
#endif
}

void invalidate_selected_rows(uint32_t previousSelection,
                               uint32_t previousScroll) noexcept {
  if (!app.window || app.view != 0 || !app.hasSample) return;
  Layout layout{};
  if (!compute_layout(layout)) {
    (void)InvalidateRect(app.window, nullptr, TRUE);
    return;
  }
  if (previousScroll != app.processScroll) {
    invalidate_table(layout);
    return;
  }
  const auto visible = taskmgr::visible_row_range(
      app.processScroll, static_cast<uint32_t>(layout.visibleRows), process_rows());
  const uint32_t first = visible.first;
  const uint32_t last = visible.last;
  const int rowTop = layout.table.top + layout.headerHeight + 1;
  const uint32_t positions[] = {previousSelection, app.selectedPosition};
  for (uint32_t position : positions) {
    if (position < first || position >= last || position >= process_rows()) continue;
    const int top = rowTop + static_cast<int>(position - first) * layout.rowHeight;
    invalidate_area({layout.table.left + 1, top, layout.table.right - 1,
                     top + layout.rowHeight}, false);
  }
}

void invalidate_focus_change(uint32_t previous, uint32_t current,
                             const Layout &layout) noexcept {
  if (previous < 2) invalidate_area(
      previous == 0 ? layout.processesTab : layout.performanceTab, false);
  if (current < 2 && current != previous) invalidate_area(
      current == 0 ? layout.processesTab : layout.performanceTab, false);
}

void invalidate_collection_error() noexcept {
  if (!app.window) return;
  Layout layout{};
  if (!compute_layout(layout)) {
    (void)InvalidateRect(app.window, nullptr, TRUE);
    return;
  }
  if (app.view == 1) {
    invalidate_area(layout.performanceCpu, false);
    invalidate_area(layout.performanceMemory, false);
  } else if (!app.hasSample && app.view == 0) {
    invalidate_table(layout);
  }
  invalidate_area(layout.status, false);
}

void fill_rect(HDC dc, Rect rect, COLORREF color) noexcept {
  if (painting) {
    if (rect.left < paintDirty.left) rect.left = paintDirty.left;
    if (rect.top < paintDirty.top) rect.top = paintDirty.top;
    if (rect.right > paintDirty.right) rect.right = paintDirty.right;
    if (rect.bottom > paintDirty.bottom) rect.bottom = paintDirty.bottom;
  }
  if (rect.right <= rect.left || rect.bottom <= rect.top) return;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) {
    ++tabPaintCounts.fills;
    tabPaintCounts.fillPixels += tab_paint_area(rect);
  }
#endif
  const COLORREF oldText = paint_set_text_color(dc, color);
  const COLORREF oldBackground = paint_set_background_color(dc, color);
  const int oldMode = paint_set_background_mode(dc, OPAQUE);
  if (oldText == CLR_INVALID || oldBackground == CLR_INVALID || !oldMode ||
      !UndosFillRectangle(dc, rect.left, rect.top, rect.right, rect.bottom))
    { app.paintOkay = false; app.paintFailure = UndosGetLastStatus(); }
  if (oldText != CLR_INVALID) (void)paint_set_text_color(dc, oldText);
  if (oldBackground != CLR_INVALID) (void)paint_set_background_color(dc, oldBackground);
  if (oldMode) (void)paint_set_background_mode(dc, oldMode);
}

void draw_line(HDC dc, int x1, int y1, int x2, int y2,
               COLORREF color) noexcept {
  const Rect bounds{x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
                   (x1 > x2 ? x1 : x2) + 1, (y1 > y2 ? y1 : y2) + 1};
  if (!dirty(bounds)) return;
  if (painting && x1 == x2) {
    if (y1 < paintDirty.top) y1 = paintDirty.top;
    if (y2 >= paintDirty.bottom) y2 = paintDirty.bottom - 1;
  }
  if (painting && y1 == y2) {
    if (x1 < paintDirty.left) x1 = paintDirty.left;
    if (x2 >= paintDirty.right) x2 = paintDirty.right - 1;
  }
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) {
    ++tabPaintCounts.lines;
    const uint32_t dx = x2 >= x1 ? x2 - x1 : x1 - x2;
    const uint32_t dy = y2 >= y1 ? y2 - y1 : y1 - y2;
    tabPaintCounts.linePixels += (dx > dy ? dx : dy) + 1;
  }
#endif
  if (paint_set_text_color(dc, color) == CLR_INVALID ||
      !UndosMoveTo(dc, x1, y1) || !UndosLineTo(dc, x2, y2))
    { app.paintOkay = false; app.paintFailure = UndosGetLastStatus(); }
}

void draw_bevel(HDC dc, Rect rect, bool pressed) noexcept {
  const COLORREF light = pressed ? ColorDarkGray : ColorWhite;
  const COLORREF dark = pressed ? ColorWhite : ColorDarkGray;
  draw_line(dc, rect.left, rect.top, rect.right - 1, rect.top, light);
  draw_line(dc, rect.left, rect.top, rect.left, rect.bottom - 1, light);
  draw_line(dc, rect.left, rect.bottom - 1, rect.right - 1,
            rect.bottom - 1, dark);
  draw_line(dc, rect.right - 1, rect.top, rect.right - 1,
            rect.bottom - 1, dark);
}

void draw_text(HDC dc, int x, int y, const char *text,
               COLORREF color = ColorBlack) noexcept {
  const Rect bounds{x + app.metrics.inkLeft, y,
      x + static_cast<int>(strlen(text) * app.metrics.cellWidth + app.metrics.cellWidth),
      y + static_cast<int>(app.metrics.lineHeight)};
  if (!dirty(bounds)) return;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) {
    ++tabPaintCounts.texts;
    for (const auto *c = reinterpret_cast<const unsigned char *>(text); *c; ++c)
      if ((*c & 0xC0) != 0x80) ++tabPaintCounts.glyphs;
    tabPaintCounts.textBoundsPixels += tab_paint_area(bounds);
  }
#endif
  if (paint_set_text_color(dc, color) == CLR_INVALID ||
      !UndosDrawText(dc, x, y, UNDOS_TEXT_REGULAR,
                     UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC, text))
    { app.paintOkay = false; app.paintFailure = UndosGetLastStatus(); }
}

void draw_fitted_text(HDC dc, int x, int y, const char *text, size_t bytes,
                      int width, COLORREF color) noexcept {
  char fitted[72]{};
  if (width <= 0) return;
  if (!dirty({x, y, x + width, y + static_cast<int>(app.metrics.lineHeight)}))
    return;
  char key[128]{};
  const bool cacheable = bytes < sizeof(key);
  if (cacheable) {
    memcpy(key, text, bytes);
    if (auto *cached = find_text(key, width)) {
      if (cached->fitted[0]) draw_text(dc, x, y, cached->fitted, color);
      return;
    }
  }
  size_t length = taskmgr::utf8_prefix_bytes(text, bytes,
      bytes < sizeof(fitted) ? bytes : sizeof(fitted) - 1);
  memcpy(fitted, text, length);
  fitted[length] = 0;
  uint32_t measured = 0;
  if (!measure_text(fitted, measured)) return;
  if (static_cast<int>(measured) <= width) {
    if (cacheable) if (auto *cached = store_text(key, width)) strcpy(cached->fitted, fitted);
    draw_text(dc, x, y, fitted, color);
    return;
  }
  static constexpr char Ellipsis[] = "...";
  const size_t dots = sizeof(Ellipsis) - 1;
  while (length) {
    size_t prefix = length - 1;
    while (prefix && (static_cast<uint8_t>(text[prefix]) & 0xC0) == 0x80)
      --prefix;
    length = prefix;
    if (length + dots >= sizeof(fitted)) continue;
    memcpy(fitted, text, length);
    memcpy(fitted + length, Ellipsis, dots + 1);
    if (!measure_text(fitted, measured)) return;
    if (static_cast<int>(measured) <= width) {
      if (cacheable) if (auto *cached = store_text(key, width)) strcpy(cached->fitted, fitted);
      draw_text(dc, x, y, fitted, color);
      return;
    }
  }
  for (size_t count = dots - 1; count; --count) {
    memcpy(fitted, Ellipsis, count);
    fitted[count] = 0;
    if (!measure_text(fitted, measured)) return;
    if (static_cast<int>(measured) <= width) {
      if (cacheable) if (auto *cached = store_text(key, width)) strcpy(cached->fitted, fitted);
      draw_text(dc, x, y, fitted, color);
      return;
    }
  }
  if (cacheable) (void)store_text(key, width);
}

void draw_processes(HDC dc, const Layout &layout) noexcept {
  const Rect table = layout.table;
  if (!dirty(table)) return;
  fill_rect(dc, table, ColorWhite);
  draw_bevel(dc, table, true);
  const int offset = app.tableScrollX;
  const int x0 = table.left + 5 - offset;
  const int nameRight = x0 + layout.nameWidth;
  const int pidRight = nameRight + layout.pidWidth;
  const int cpuRight = pidRight + layout.cpuWidth;
  const int memoryRight = cpuRight + layout.memoryWidth;
  const Rect header{table.left + 1, table.top + 1, table.right - 1,
                    table.top + layout.headerHeight};
  if (dirty(header)) {
  fill_rect(dc, header, ColorLightGray);
  draw_text(dc, x0 + 2, header.top + 4, "Image Name");
  draw_text(dc, nameRight + 2, header.top + 4, "PID");
  draw_text(dc, pidRight + 2, header.top + 4, "CPU %");
  draw_text(dc, cpuRight + 2, header.top + 4, "Memory (KiB)");
  }
  draw_line(dc, nameRight, header.top, nameRight,
            table.bottom - 1, ColorDarkGray);
  draw_line(dc, pidRight, header.top, pidRight,
            table.bottom - 1, ColorDarkGray);
  draw_line(dc, cpuRight, header.top, cpuRight,
            table.bottom - 1, ColorDarkGray);

  if (!app.hasSample) {
    char error[96] = "Collection unavailable: ";
    const char *name = status_name(app.staleStatus);
    size_t n = strlen(error);
    size_t i = 0;
    while (name[i] && n + i + 1 < sizeof(error)) {
      error[n + i] = name[i];
      ++i;
    }
    error[n + i] = 0;
    draw_text(dc, table.left + 12, header.bottom + 8, error);
    return;
  }

  const uint32_t rows = process_rows();
  if (app.selectedPosition >= rows) app.selectedPosition = rows - 1;
  if (layout.visibleRows == 0) return;
  if (app.selectedPosition < app.processScroll)
    app.processScroll = app.selectedPosition;
  auto visible = taskmgr::visible_row_range(
      app.processScroll, static_cast<uint32_t>(layout.visibleRows), rows);
  if (app.selectedPosition >= visible.last) {
    app.processScroll = app.selectedPosition >=
            static_cast<uint32_t>(layout.visibleRows)
        ? app.selectedPosition - static_cast<uint32_t>(layout.visibleRows) + 1
        : 0;
  }
  if (app.processScroll > rows - 1) app.processScroll = rows - 1;
  visible = taskmgr::visible_row_range(
      app.processScroll, static_cast<uint32_t>(layout.visibleRows), rows);
  const int rowTop = header.bottom + 1;
  for (uint32_t row = visible.first; row < visible.last; ++row) {
    const int top = rowTop + static_cast<int>(row - app.processScroll) *
                    layout.rowHeight;
    const Rect rowRect{table.left + 1, top, table.right - 1,
                       top + layout.rowHeight};
    if (!dirty(rowRect)) continue;
    const bool selected = row == app.selectedPosition;
    const COLORREF back = selected ? ColorBlue : ColorWhite;
    const COLORREF fore = selected ? ColorWhite : ColorBlack;
    fill_rect(dc, rowRect, back);
    if (row < app.sample.processCount) {
      const auto &process =
          app.sample.processes[app.sample.sortedProcesses[row]];
      draw_fitted_text(dc, x0 + 2, top + 3,
          reinterpret_cast<const char *>(app.sample.names + process.nameOffset),
          process.nameBytes - 1, layout.nameWidth - 8, fore);
    } else if (row == app.sample.processCount) {
      draw_text(dc, x0 + 2, top + 3, "System (kernel)", fore);
    } else {
      draw_text(dc, x0 + 2, top + 3, "System Idle", fore);
    }

    char cpu[24]{}, memory[24]{}, pid[24]{};
    format_row_value(app.sample, row, cpu, sizeof(cpu), memory,
                     sizeof(memory), pid, sizeof(pid));
    if (row < app.sample.processCount && !cpu[0]) {
      const char *text = app.sample.header.validity & taskmgr::CpuValidity
          ? "Collecting..." : cpu_reason(app.sample.header.cpuReason);
      draw_fitted_text(dc, pidRight + 2, top + 3, text, strlen(text),
                       layout.cpuWidth - 8, fore);
    } else if (row < app.sample.processCount + 2 && !cpu[0]) {
      const char *text = app.sample.header.validity & taskmgr::CpuValidity
          ? "Collecting..." : cpu_reason(app.sample.header.cpuReason);
      draw_fitted_text(dc, pidRight + 2, top + 3, text, strlen(text),
                       layout.cpuWidth - 8, fore);
    } else {
      draw_text(dc, pidRight + 2, top + 3, cpu, fore);
    }
    draw_text(dc, nameRight + 2, top + 3, pid[0] ? pid : "—", fore);
    if (memory[0]) {
      draw_text(dc, cpuRight + 2, top + 3, memory, fore);
    } else if (row == app.sample.processCount + 1) {
      draw_text(dc, cpuRight + 2, top + 3, "—", fore);
    } else if (!(app.sample.header.validity & taskmgr::MemoryValidity) ||
               row < app.sample.processCount) {
      draw_fitted_text(dc, cpuRight + 2, top + 3,
          memory_reason(app.sample.header.memoryReason),
          strlen(memory_reason(app.sample.header.memoryReason)),
          layout.memoryWidth - 8, fore);
    }
  }
}

void draw_graph(HDC dc, Rect rect, const uint32_t *values,
                const uint8_t *valid, size_t stride, uint32_t historyCount,
                uint32_t firstSlot, uint32_t count,
                const char *title, uint32_t reservedLines) noexcept {
  if (rect.right <= rect.left || rect.bottom <= rect.top) return;
  if (!dirty(rect)) return;
  fill_rect(dc, rect, ColorBlack);
  draw_bevel(dc, rect, true);
  draw_text(dc, rect.left + 8, rect.top + 7, title, ColorWhite);
  const int graphLeft = rect.left + 8;
  const int graphRight = rect.right - 8;
  const int graphTop = rect.top +
      static_cast<int>(app.metrics.lineHeight * reservedLines) + 14;
  const int graphBottom = rect.bottom -
      2 * static_cast<int>(app.metrics.lineHeight) - 10;
  if (graphRight <= graphLeft || graphBottom <= graphTop) return;
  for (int line = 1; line < 4; ++line) {
    const int y = graphTop + (graphBottom - graphTop) * line / 4;
    draw_line(dc, graphLeft, y, graphRight, y, ColorGreen);
  }
  if (!values || !valid || !historyCount || !count) return;
  bool havePrevious = false;
  int previousX = 0, previousY = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t slot = (firstSlot + i) % taskmgr::HistoryLength;
    const size_t index = stride * slot;
    if (!valid[index]) {
      havePrevious = false;
      continue;
    }
    const int x = graphLeft +
        (graphRight - graphLeft - 1) * static_cast<int>(i) /
            static_cast<int>(count > 1 ? count - 1 : 1);
    const uint32_t bp = values[index] > 10000 ? 10000 : values[index];
    const int y = graphBottom -
        (graphBottom - graphTop) * static_cast<int>(bp) / 10000;
    if (havePrevious)
      draw_line(dc, previousX, previousY, x, y, ColorBrightGreen);
    previousX = x;
    previousY = y;
    havePrevious = true;
  }
}

void draw_performance(HDC dc, const Layout &layout) noexcept {
  if (!app.hasSample) {
    fill_rect(dc, layout.performanceCpu, ColorLightGray);
    fill_rect(dc, layout.performanceMemory, ColorLightGray);
    draw_text(dc, layout.performanceCpu.left + 8,
              layout.performanceCpu.top + 8,
              "Telemetry unavailable; no complete sample has been collected.");
    return;
  }

  const uint32_t count = app.sample.cpuCount;
  if (!count && dirty(layout.performanceCpu)) {
    fill_rect(dc, layout.performanceCpu, ColorLightGray);
    draw_text(dc, layout.performanceCpu.left + 8,
              layout.performanceCpu.top + 8, "No online CPU records.");
  } else if (count && dirty(layout.performanceCpu)) {
    const uint32_t index = app.historyCpuScroll < count
        ? app.historyCpuScroll : count - 1;
    char title[48] = "CPU ";
    format_u64(app.sample.cpus[index].cpuId, title + 4, sizeof(title) - 4);
    uint32_t historyCount = app.history.count;
    const uint32_t firstSlot =
        (app.history.next + taskmgr::HistoryLength - historyCount) %
        taskmgr::HistoryLength;
    if (app.cpuHistory && app.cpuHistoryValid &&
        app.cpuHistoryCount == count) {
      draw_graph(dc, layout.performanceCpu,
                 app.cpuHistory + index * taskmgr::HistoryLength,
                 app.cpuHistoryValid + index * taskmgr::HistoryLength,
                 1, historyCount, firstSlot, historyCount, title, 2);
      TAB_PAINT_PHASE("cpu_graph_done");
    } else {
      fill_rect(dc, layout.performanceCpu, ColorBlack);
      draw_bevel(dc, layout.performanceCpu, true);
      draw_text(dc, layout.performanceCpu.left + 8,
                layout.performanceCpu.top + 7, title,
                ColorWhite);
      draw_text(dc, layout.performanceCpu.left + 8,
                layout.performanceCpu.top + 30,
                "CPU history unavailable.", ColorWhite);
    }
    char summary[64] = "CPU ";
    format_u64(app.sample.cpus[index].cpuId, summary + 4,
               sizeof(summary) - 4);
    size_t length = strlen(summary);
    if (app.sample.cpuPercentValid[index]) {
      summary[length++] = ' ';
      summary[length] = 0;
      format_basis_points(app.sample.cpuBasisPoints[index],
                          summary + length, sizeof(summary) - length);
    } else if (!(app.sample.header.validity & taskmgr::CpuValidity)) {
      summary[length++] = ' ';
      summary[length] = 0;
      const char *reason = cpu_reason(app.sample.header.cpuReason);
      size_t r = 0;
      while (reason[r] && length + r + 1 < sizeof(summary)) {
        summary[length + r] = reason[r];
        ++r;
      }
      summary[length + r] = 0;
    } else {
      summary[length++] = ' ';
      summary[length] = 0;
      const char *collecting = "Collecting...";
      for (size_t i = 0; collecting[i] && length + i + 1 < sizeof(summary); ++i)
        summary[length + i] = collecting[i];
      summary[length + strlen(collecting)] = 0;
    }
    draw_text(dc, layout.performanceCpu.left + 8,
              layout.performanceCpu.bottom - app.metrics.lineHeight - 5,
              summary, ColorWhite);
    char overall[64] = "Overall busy ";
    if (app.sample.busyValid)
      format_basis_points(app.sample.busyBasisPoints, overall + 13,
                          sizeof(overall) - 13);
    else {
      const char *reason = app.sample.header.validity & taskmgr::CpuValidity
          ? "Collecting..." : cpu_reason(app.sample.header.cpuReason);
      size_t pos = 13;
      for (size_t i = 0; reason[i] && pos + i + 1 < sizeof(overall); ++i)
        overall[pos + i] = reason[i];
      overall[pos + strlen(reason)] = 0;
    }
    draw_text(dc, layout.performanceCpu.left + 8,
              layout.performanceCpu.top +
                  static_cast<int>(app.metrics.lineHeight) + 8,
              overall, ColorWhite);
  }

  TAB_PAINT_PHASE("cpu_done");
  if (!dirty(layout.performanceMemory)) return;
  if (app.sample.header.validity & taskmgr::MemoryValidity) {
    uint32_t memoryValues[taskmgr::HistoryLength]{};
    uint8_t memoryValid[taskmgr::HistoryLength]{};
    for (uint32_t i = 0; i < app.history.count; ++i) {
      const auto &slot = app.history.at(i);
      const uint32_t target = (app.history.next + taskmgr::HistoryLength -
                               app.history.count + i) % taskmgr::HistoryLength;
      if (slot.memoryValid &&
          taskmgr::ratio_basis_points(slot.usedBytes, slot.totalBytes,
                                      memoryValues[target]))
        memoryValid[target] = 1;
    }
    draw_graph(dc, layout.performanceMemory, memoryValues, memoryValid, 1,
               app.history.count,
               (app.history.next + taskmgr::HistoryLength -
                app.history.count) % taskmgr::HistoryLength,
               app.history.count, "Physical RAM (OS usable)", 4);
    TAB_PAINT_PHASE("memory_graph_done");
    char total[24]{}, used[24]{}, freeBytes[24]{}, line[96]{};
    format_u64(app.sample.header.physicalTotalBytes / 1024,
               total, sizeof(total));
    format_u64(app.sample.header.physicalUsedBytes / 1024,
               used, sizeof(used));
    format_u64(app.sample.header.physicalFreeBytes / 1024,
               freeBytes, sizeof(freeBytes));
    const char *parts[] = {"Total ", total, " KiB"};
    size_t pos = 0;
    for (const char *part : parts) {
      for (size_t i = 0; part[i] && pos + 1 < sizeof(line); ++i)
        line[pos++] = part[i];
    }
    line[pos] = 0;
    draw_text(dc, layout.performanceMemory.left + 8,
              layout.performanceMemory.top +
                  static_cast<int>(app.metrics.lineHeight) + 10, line,
              ColorWhite);
    pos = 0;
    const char *usedParts[] = {"Used ", used, " KiB"};
    for (const char *part : usedParts) {
      for (size_t i = 0; part[i] && pos + 1 < sizeof(line); ++i)
        line[pos++] = part[i];
    }
    line[pos] = 0;
    draw_text(dc, layout.performanceMemory.left + 8,
              layout.performanceMemory.top +
                  2 * static_cast<int>(app.metrics.lineHeight) + 10, line,
              ColorWhite);
    pos = 0;
    const char *freeParts[] = {"Free ", freeBytes, " KiB"};
    for (const char *part : freeParts) {
      for (size_t i = 0; part[i] && pos + 1 < sizeof(line); ++i)
        line[pos++] = part[i];
    }
    line[pos] = 0;
    draw_text(dc, layout.performanceMemory.left + 8,
              layout.performanceMemory.top +
                  3 * static_cast<int>(app.metrics.lineHeight) + 10, line,
              ColorWhite);
    if (app.sample.header.unmanagedPhysicalBytes) {
      draw_text(dc, layout.performanceMemory.left + 8,
                layout.performanceMemory.bottom -
                    app.metrics.lineHeight - 5,
                "Warning: some usable RAM is outside the allocator range.",
                ColorWhite);
    }
  } else {
    fill_rect(dc, layout.performanceMemory, ColorLightGray);
    draw_text(dc, layout.performanceMemory.left + 8,
              layout.performanceMemory.top + 8,
              memory_reason(app.sample.header.memoryReason));
  }
}

void draw_status(HDC dc, const Layout &layout) noexcept {
  fill_rect(dc, layout.status, ColorLightGray);
  draw_bevel(dc, layout.status, true);
  char text[128]{};
  if (app.paintFailure != SyscallReturnCode_OKAY) {
    strcpy(text, app.paintRetries >= 2 ? "Paint failed; automatic retries exhausted: "
                                      : "Paint interrupted; retrying: ");
    const char *error = status_name(app.paintFailure);
    size_t pos = strlen(text);
    for (size_t i = 0; error[i] && pos + 1 < sizeof(text); ++i)
      text[pos++] = error[i];
  } else if (app.stale) {
    const char prefix[] = "Stale: ";
    memcpy(text, prefix, sizeof(prefix) - 1);
    const char *name = status_name(app.staleStatus);
    size_t pos = sizeof(prefix) - 1;
    for (size_t i = 0; name[i] && pos + i + 1 < sizeof(text); ++i)
      text[pos + i] = name[i];
  } else if (!app.hasSample) {
    const char prefix[] = "Unavailable: ";
    memcpy(text, prefix, sizeof(prefix) - 1);
    const char *name = status_name(app.staleStatus);
    size_t pos = sizeof(prefix) - 1;
    for (size_t i = 0; name[i] && pos + i + 1 < sizeof(text); ++i)
      text[pos + i] = name[i];
  } else {
    const uint32_t processCount = app.sample.processCount;
    const char prefix[] = "Processes: ";
    memcpy(text, prefix, sizeof(prefix) - 1);
    size_t pos = sizeof(prefix) - 1;
    char count[24]{};
    format_u64(processCount, count, sizeof(count));
    for (size_t i = 0; count[i] && pos + i + 1 < sizeof(text); ++i)
      text[pos + i] = count[i];
    pos += strlen(count);
    if (app.fontStale) {
      const char prefix[] = " | Font measure ";
      for (size_t i = 0; prefix[i] && pos + i + 1 < sizeof(text); ++i)
        text[pos++] = prefix[i];
      const char *error = status_name(app.fontMeasureStatus);
      for (size_t i = 0; error[i] && pos + i + 1 < sizeof(text); ++i)
        text[pos++] = error[i];
    }
  }
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
  char identity[96] = "D2 epoch=";
  char number[24]{};
  format_u64(app.sample.header.counterEpoch, number, sizeof(number));
  strcat(identity, number);
  strcat(identity, " reason=");
  format_u64(app.sample.header.cpuReason, number, sizeof(number));
  strcat(identity, number);
  draw_text(dc, layout.status.left + 8, layout.status.top + 5, identity);
#else
  draw_text(dc, layout.status.left + 8, layout.status.top + 5, text);
#endif
  if (app.stale) {
    draw_text(dc, layout.status.right - 92, layout.status.top + 5,
              "Stale", ColorRed);
  }
}

void fill_background_gaps(HDC dc, const Layout &layout) noexcept {
  Rect pending[32]{};
  Rect next[32]{};
  uint32_t count = 1;
  pending[0] = paintDirty;
  const Rect panels[] = {
      layout.processesTab, layout.performanceTab,
      app.view == 0 ? layout.table : layout.performanceCpu,
      app.view == 1 ? layout.performanceMemory : Rect{},
      layout.status};
  for (const Rect &panel : panels) {
    uint32_t nextCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const Rect source = pending[i];
      const int left = source.left > panel.left ? source.left : panel.left;
      const int top = source.top > panel.top ? source.top : panel.top;
      const int right = source.right < panel.right ? source.right : panel.right;
      const int bottom = source.bottom < panel.bottom ? source.bottom : panel.bottom;
      if (right <= left || bottom <= top) {
        if (nextCount < 32) next[nextCount++] = source;
        continue;
      }
      const Rect pieces[] = {
          {source.left, source.top, source.right, top},
          {source.left, bottom, source.right, source.bottom},
          {source.left, top, left, bottom},
          {right, top, source.right, bottom}};
      for (const Rect &piece : pieces)
        if (piece.right > piece.left && piece.bottom > piece.top && nextCount < 32)
          next[nextCount++] = piece;
    }
    count = nextCount;
    for (uint32_t i = 0; i < count; ++i) pending[i] = next[i];
  }
  for (uint32_t i = 0; i < count; ++i) fill_rect(dc, pending[i], ColorLightGray);
}

#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
void append_timing_text(char *out, size_t capacity, size_t &position,
                        const char *text) noexcept {
  while (*text && position + 1 < capacity) out[position++] = *text++;
  out[position] = 0;
}

void append_timing_number(char *out, size_t capacity, size_t &position,
                          uint64_t value) noexcept {
  char number[24]{};
  format_u64(value, number, sizeof(number));
  append_timing_text(out, capacity, position, number);
}

uint64_t tab_paint_area(Rect rect) noexcept {
  if (rect.left < paintDirty.left) rect.left = paintDirty.left;
  if (rect.top < paintDirty.top) rect.top = paintDirty.top;
  if (rect.right > paintDirty.right) rect.right = paintDirty.right;
  if (rect.bottom > paintDirty.bottom) rect.bottom = paintDirty.bottom;
  return rect.right > rect.left && rect.bottom > rect.top
      ? static_cast<uint64_t>(rect.right - rect.left) * (rect.bottom - rect.top)
      : 0;
}

void report_tab_paint(const char *phase) noexcept {
  if (!tabPaintActive) return;
  char line[384] = "TMTABP seq=";
  size_t position = strlen(line);
  append_timing_number(line, sizeof(line), position, tabPaintSequence);
  append_timing_text(line, sizeof(line), position, " phase=");
  append_timing_text(line, sizeof(line), position, phase);
  const auto field = [&](const char *name, uint64_t value) {
    append_timing_text(line, sizeof(line), position, name);
    append_timing_number(line, sizeof(line), position, value);
  };
  field(" n_fill=", tabPaintCounts.fills);
  field(" fill_px=", tabPaintCounts.fillPixels);
  field(" n_text=", tabPaintCounts.texts);
  field(" glyphs=", tabPaintCounts.glyphs);
  field(" text_bounds_px=", tabPaintCounts.textBoundsPixels);
  field(" n_line=", tabPaintCounts.lines);
  field(" line_px_est=", tabPaintCounts.linePixels);
  field(" n_color=", tabPaintCounts.colors);
  // Preserve the paint operation's error across diagnostic output.
  const DWORD error = GetLastError();
  (void)UD_SYS_PrintV1(line);
  SetLastError(error);
  tabPaintCounts = {};
}

void report_tab_timing(uint32_t paintEndMs) noexcept {
  char line[192] = "TMTAB click_tick=";
  size_t position = strlen(line);
  append_timing_number(line, sizeof(line), position, tabTiming.clickMs / 10);
  append_timing_text(line, sizeof(line), position, " paint_begin_tick=");
  append_timing_number(line, sizeof(line), position, tabTiming.paintBeginMs / 10);
  append_timing_text(line, sizeof(line), position, " paint_end_tick=");
  append_timing_number(line, sizeof(line), position, paintEndMs / 10);
  append_timing_text(line, sizeof(line), position, " invalidations=");
  append_timing_number(line, sizeof(line), position, tabTiming.invalidationCount);
  append_timing_text(line, sizeof(line), position, " invalidated_area=");
  append_timing_number(line, sizeof(line), position, tabTiming.invalidationArea);
  append_timing_text(line, sizeof(line), position, " rc_area=");
  append_timing_number(line, sizeof(line), position, tabTiming.paintArea);
  append_timing_text(line, sizeof(line), position, " tick_source=MSG.time_ms/10");
  (void)UD_SYS_PrintV1(line);
  tabTiming.pending = false;
}
#endif

void paint_window() noexcept {
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
  ++diagnostic_paints;
#endif
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabTiming.pending && !tabTiming.paintMarked) {
    tabTiming.paintMarked = true;
    tabPaintActive = true;
    tabPaintCounts = {};
    ++tabPaintSequence;
    report_tab_paint("begin");
  }
#endif
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(app.window, &paint);
  TAB_PAINT_PHASE("beginpaint_done");
  if (!dc) {
    app.paintFailure = UndosGetLastStatus();
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
    tabPaintActive = false;
#endif
    return;
  }
  pendingInvalidationCount = 0;
  app.paintOkay = true;
  paintColors = {};
  paintDirty = {paint.rcPaint.left, paint.rcPaint.top,
                 paint.rcPaint.right, paint.rcPaint.bottom};
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabTiming.pending) {
    tabTiming.paintBeginMs = currentMessageMs;
    tabTiming.paintArea = paintDirty.right > paintDirty.left &&
                          paintDirty.bottom > paintDirty.top
        ? static_cast<uint64_t>(paintDirty.right - paintDirty.left) *
              static_cast<uint64_t>(paintDirty.bottom - paintDirty.top)
        : 0;
  }
#endif
  painting = true;
  UNDOS_FONT previous = 0;
  BOOL hadPrevious = FALSE;
  if (!UndosSelectFont(dc, app.font, &previous, &hadPrevious)) {
    app.paintOkay = false;
    app.paintFailure = UndosGetLastStatus();
  } else if (hadPrevious) {
    (void)UndosCloseFont(previous);
  }
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (tabPaintActive) ++tabPaintCounts.colors;
#endif
  (void)SetBkMode(dc, TRANSPARENT);
  Layout layout{};
  if (app.paintOkay && compute_layout(layout)) {
    TAB_PAINT_PHASE("setup_done");
    fill_background_gaps(dc, layout);
    TAB_PAINT_PHASE("bg_done");
    if (dirty(layout.processesTab)) {
      fill_rect(dc, layout.processesTab, ColorLightGray);
      draw_bevel(dc, layout.processesTab, app.view == 0);
    }
    if (dirty(layout.performanceTab)) {
      fill_rect(dc, layout.performanceTab, ColorLightGray);
      draw_bevel(dc, layout.performanceTab, app.view == 1);
    }
    draw_text(dc, layout.processesTab.left + 12,
              layout.processesTab.top + 5, "Processes");
    draw_text(dc, layout.performanceTab.left + 12,
              layout.performanceTab.top + 5, "Performance");
    if (app.focusTarget == 0)
      draw_bevel(dc, {layout.processesTab.left + 3,
                      layout.processesTab.top + 3,
                      layout.processesTab.right - 3,
                      layout.processesTab.bottom - 3}, true);
    if (app.focusTarget == 1)
      draw_bevel(dc, {layout.performanceTab.left + 3,
                      layout.performanceTab.top + 3,
                      layout.performanceTab.right - 3,
                      layout.performanceTab.bottom - 3}, true);
    TAB_PAINT_PHASE("tabs_done");
    if (app.view == 0) {
      draw_processes(dc, layout);
      TAB_PAINT_PHASE("list_done");
    } else {
      draw_performance(dc, layout);
      TAB_PAINT_PHASE("panels_done");
    }
    if (dirty(layout.status)) draw_status(dc, layout);
    TAB_PAINT_PHASE("status_done");
  }
  painting = false;
  TAB_PAINT_PHASE("endpaint_begin");
  if (!EndPaint(app.window, &paint)) {
    app.paintOkay = false;
    app.paintFailure = UndosGetLastStatus();
  }
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  report_tab_paint("endpaint_done");
  if (tabPaintActive) {
    for (uint32_t i = 0; i < 5; ++i) report_tab_paint("calib");
    tabPaintActive = false;
  }
  if (tabTiming.pending) {
    if (!PostMessage(app.window, TabTimingMessage, 0, 0)) {
      tabTiming.pending = false;
      (void)UD_SYS_PrintV1("TMTAB timing completion message failed");
    }
  }
#endif
  if (!app.paintOkay && app.paintFailure == SyscallReturnCode_OKAY)
    app.paintFailure = UndosGetLastStatus();
  if (!app.paintOkay) {
    if (app.failedPaint.right <= app.failedPaint.left) {
      app.failedPaint = paintDirty;
    } else {
      if (paintDirty.left < app.failedPaint.left) app.failedPaint.left = paintDirty.left;
      if (paintDirty.top < app.failedPaint.top) app.failedPaint.top = paintDirty.top;
      if (paintDirty.right > app.failedPaint.right) app.failedPaint.right = paintDirty.right;
      if (paintDirty.bottom > app.failedPaint.bottom) app.failedPaint.bottom = paintDirty.bottom;
    }
  }
  if (!app.paintOkay && app.paintRetries < 2) {
    ++app.paintRetries;
    (void)InvalidateRect(app.window, &paint.rcPaint, FALSE);
  } else if (app.paintOkay &&
             (app.paintFailure == SyscallReturnCode_OKAY ||
              (paintDirty.left <= app.failedPaint.left &&
               paintDirty.top <= app.failedPaint.top &&
               paintDirty.right >= app.failedPaint.right &&
               paintDirty.bottom >= app.failedPaint.bottom))) {
    app.paintRetries = 0;
    app.paintFailure = SyscallReturnCode_OKAY;
    app.paintErrorShown = false;
    app.failedPaint = {};
  } else if (!app.paintOkay && !app.paintErrorShown) {
    app.paintErrorShown = true;
    if (layoutValid) {
      const RECT status{layout.status.left, layout.status.top,
                        layout.status.right, layout.status.bottom};
      (void)InvalidateRect(app.window, &status, FALSE);
    }
  }
}

void set_view(uint32_t view) noexcept {
  const uint32_t previousFocus = app.focusTarget;
  const uint32_t previousView = app.view;
  app.view = view > 1 ? 0 : view;
  app.focusTarget = app.view;
  Layout layout{};
  if (!app.window || !compute_layout(layout)) return;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
  if (previousView != app.view) {
    tabTiming = {};
    tabTiming.pending = true;
    tabTiming.countingInvalidations = true;
    tabTiming.clickMs = currentMessageMs;
  }
#endif
  if (previousView != app.view) {
    invalidate_area(previousView == 0 ? layout.processesTab : layout.performanceTab, false);
    invalidate_area(app.view == 0 ? layout.processesTab : layout.performanceTab, false);
    if (app.view == 0) invalidate_area(layout.table, false);
    else {
      invalidate_area(layout.performanceCpu, false);
      invalidate_area(layout.performanceMemory, false);
    }
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
    tabTiming.countingInvalidations = false;
#endif
  } else {
    invalidate_focus_change(previousFocus, app.focusTarget, layout);
  }
}

void keep_selection_visible(const Layout &layout) noexcept {
  if (!layout.visibleRows) return;
  if (app.selectedPosition < app.processScroll)
    app.processScroll = app.selectedPosition;
  if (app.selectedPosition >= app.processScroll + layout.visibleRows)
    app.processScroll = app.selectedPosition - layout.visibleRows + 1;
}

void move_selection(int direction) noexcept {
  if (!app.hasSample) return;
  const uint32_t previousSelection = app.selectedPosition;
  const uint32_t previousScroll = app.processScroll;
  const uint32_t rows = process_rows();
  if (direction < 0 && app.selectedPosition) --app.selectedPosition;
  if (direction > 0 && app.selectedPosition + 1 < rows)
    ++app.selectedPosition;
  Layout layout{};
  if (compute_layout(layout)) keep_selection_visible(layout);
  invalidate_selected_rows(previousSelection, previousScroll);
}

void navigate_performance(int direction) noexcept {
  if (!app.hasSample || !app.sample.cpuCount) return;
  const uint32_t previous = app.historyCpuScroll;
  if (direction < 0 && app.historyCpuScroll) --app.historyCpuScroll;
  if (direction > 0 && app.historyCpuScroll + 1 < app.sample.cpuCount)
    ++app.historyCpuScroll;
  if (previous != app.historyCpuScroll) {
    Layout layout{};
    if (app.window && compute_layout(layout))
      invalidate_area(layout.performanceCpu, false);
  }
}

bool shift_down() noexcept {
  return app.shiftKeysDown != 0;
}

void update_shift_state(uint32_t key, bool pressed) noexcept {
  uint8_t mask = 0;
  if (key == VK_SHIFT) mask = 4;
  else if (key == VK_LSHIFT) mask = 1;
  else if (key == VK_RSHIFT) mask = 2;
  if (pressed) app.shiftKeysDown |= mask;
  else app.shiftKeysDown &= static_cast<uint8_t>(~mask);
}

void handle_key(uint32_t key) noexcept {
  if (key == VK_TAB) {
    const uint32_t previousFocus = app.focusTarget;
    if (shift_down()) app.focusTarget = app.focusTarget
        ? app.focusTarget - 1 : 2;
    else app.focusTarget = (app.focusTarget + 1) % 3;
    Layout layout{};
    if (app.window && compute_layout(layout))
      invalidate_focus_change(previousFocus, app.focusTarget, layout);
    return;
  }
  if (app.focusTarget < 2) {
    if (key == VK_LEFT || key == VK_RIGHT) {
      set_view(key == VK_LEFT ? 0 : 1);
      return;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
      set_view(app.focusTarget);
      return;
    }
  }
  if (key == VK_RETURN || key == VK_SPACE) {
    const uint32_t previousFocus = app.focusTarget;
    app.focusTarget = 2;
    Layout layout{};
    if (app.window && compute_layout(layout))
      invalidate_focus_change(previousFocus, app.focusTarget, layout);
    return;
  }
  if (app.view == 0) {
    if (key == VK_UP) move_selection(-1);
    else if (key == VK_DOWN) move_selection(1);
    else if (key == VK_HOME) {
      const uint32_t previousSelection = app.selectedPosition;
      const uint32_t previousScroll = app.processScroll;
      app.selectedPosition = 0;
      app.processScroll = 0;
      invalidate_selected_rows(previousSelection, previousScroll);
    } else if (key == VK_END && app.hasSample) {
      const uint32_t previousSelection = app.selectedPosition;
      const uint32_t previousScroll = app.processScroll;
      app.selectedPosition = process_rows() - 1;
      Layout layout{};
      if (compute_layout(layout)) keep_selection_visible(layout);
      invalidate_selected_rows(previousSelection, previousScroll);
    } else if (key == VK_PRIOR) {
      const uint32_t previousSelection = app.selectedPosition;
      const uint32_t previousScroll = app.processScroll;
      Layout layout{};
      const uint32_t page = compute_layout(layout)
          ? static_cast<uint32_t>(layout.visibleRows > 1
                ? layout.visibleRows - 1 : 1) : 1;
      app.selectedPosition = app.selectedPosition > page
          ? app.selectedPosition - page : 0;
      if (compute_layout(layout)) keep_selection_visible(layout);
      invalidate_selected_rows(previousSelection, previousScroll);
    } else if (key == VK_NEXT && app.hasSample) {
      const uint32_t previousSelection = app.selectedPosition;
      const uint32_t previousScroll = app.processScroll;
      Layout layout{};
      const uint32_t page = compute_layout(layout)
          ? static_cast<uint32_t>(layout.visibleRows > 1
                ? layout.visibleRows - 1 : 1) : 1;
      const uint32_t last = process_rows() - 1;
      app.selectedPosition = app.selectedPosition + page < last
          ? app.selectedPosition + page : last;
      if (compute_layout(layout)) keep_selection_visible(layout);
      invalidate_selected_rows(previousSelection, previousScroll);
    } else if (shift_down() && key == VK_LEFT) {
      app.tableScrollX = app.tableScrollX > 16 ? app.tableScrollX - 16 : 0;
      Layout layout{};
      if (app.window && compute_layout(layout)) invalidate_table(layout);
    } else if (shift_down() && key == VK_RIGHT) {
      Layout layout{};
      if (compute_layout(layout)) {
        const int viewWidth = layout.table.right - layout.table.left;
        const int maximum = layout.tableWidth - viewWidth;
        app.tableScrollX += 16;
        if (app.tableScrollX > maximum) app.tableScrollX = maximum;
        if (app.tableScrollX < 0) app.tableScrollX = 0;
      }
      if (app.window && compute_layout(layout)) invalidate_table(layout);
    }
  } else if (key == VK_PRIOR) {
    navigate_performance(-1);
  } else if (key == VK_NEXT) {
    navigate_performance(1);
  }
}

void handle_click(int x, int y) noexcept {
  Layout layout{};
  if (!compute_layout(layout)) return;
  if (x >= layout.processesTab.left && x < layout.processesTab.right &&
      y >= layout.processesTab.top && y < layout.processesTab.bottom) {
    set_view(0);
    return;
  }
  if (x >= layout.performanceTab.left && x < layout.performanceTab.right &&
      y >= layout.performanceTab.top && y < layout.performanceTab.bottom) {
    set_view(1);
    return;
  }
  if (app.view == 0 && app.hasSample &&
      x >= layout.table.left && x < layout.table.right &&
      y >= layout.table.top + layout.headerHeight &&
      y < layout.table.bottom && layout.rowHeight) {
    const uint32_t row = app.processScroll +
        static_cast<uint32_t>((y - layout.table.top - layout.headerHeight) /
                              layout.rowHeight);
    if (row < process_rows()) {
      const uint32_t previousSelection = app.selectedPosition;
      const uint32_t previousScroll = app.processScroll;
      const uint32_t previousFocus = app.focusTarget;
      app.selectedPosition = row;
      app.focusTarget = 2;
      invalidate_selected_rows(previousSelection, previousScroll);
      invalidate_focus_change(previousFocus, app.focusTarget, layout);
    }
  } else if (app.view == 1 &&
             y >= layout.performanceCpu.top &&
             y < layout.performanceCpu.bottom) {
    if (x < (layout.performanceCpu.left + layout.performanceCpu.right) / 2)
      navigate_performance(-1);
    else
      navigate_performance(1);
  }
}

LRESULT CALLBACK task_window_proc(HWND window, UINT message,
                                  WPARAM wParam, LPARAM lParam) {
  switch (message) {
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
    case TabTimingMessage:
      if (tabTiming.pending) report_tab_timing(currentMessageMs);
      return 0;
#endif
    case WM_PAINT:
      paint_window();
      return 0;
    case WM_SIZE:
    case WM_DISPLAYCHANGE:
      layoutValid = false;
      memset(rowCache, 0, sizeof(rowCache));
      (void)InvalidateRect(window, nullptr, TRUE);
      return 0;
    case WM_FONTCHANGED:
      if (!acquire_font()) {
        app.fontStale = true;
        app.staleStatus = app.fontMeasureStatus;
        Layout layout{};
        if (app.window && compute_layout(layout))
          invalidate_area(layout.status, false);
      }
      return 0;
    case WM_KEYDOWN:
      update_shift_state(static_cast<uint32_t>(wParam), true);
      handle_key(static_cast<uint32_t>(wParam));
      return 0;
    case WM_KEYUP:
      update_shift_state(static_cast<uint32_t>(wParam), false);
      return 0;
    case WM_CHAR:
#if defined(UNDOS_TASK_MEMORY_DIAGNOSTICS)
      if (wParam == 'o' || wParam == 'O') app.observationSamples = 6;
#endif
      if (wParam == '\t') handle_key(VK_TAB);
      return 0;
    case WM_LBUTTONDOWN: {
      const int x = static_cast<int16_t>(static_cast<uintptr_t>(lParam) & 0xFFFF);
      const int y = static_cast<int16_t>(
          (static_cast<uintptr_t>(lParam) >> 16) & 0xFFFF);
      handle_click(x, y);
      return 0;
    }
    case WM_CLOSE:
      return DefWindowProc(window, message, wParam, lParam);
    case WM_DESTROY:
      app.window = nullptr;
      app.running = false;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(window, message, wParam, lParam);
  }
}

bool sample_due() noexcept {
  uint32_t elapsed = 0;
  uint32_t saturatedBatches = 0;
  while (app.running) {
    bool saturated = true;
    bool sawMessage = false;
    for (uint32_t i = 0; i < 32; ++i) {
      MSG message{};
      SetLastError(ERROR_SUCCESS);
      if (!PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (GetLastError() != ERROR_SUCCESS) {
          app.staleStatus = UndosGetLastStatus();
          app.stale = true;
          add_history_gap();
          invalidate_collection_error();
        }
        saturated = false;
        break;
      }
      sawMessage = true;
#if defined(UNDOS_TASK_TAB_DIAGNOSTICS)
      currentMessageMs = message.time;
#endif
      if (message.message == WM_QUIT) {
        app.running = false;
        return false;
      }
      (void)DispatchMessage(&message);
    }
    if (!app.running) return false;
    if (saturated && sawMessage) {
      if (++saturatedBatches >= 8) return true;
    } else {
      saturatedBatches = 0;
    }
    if (elapsed >= 1000) return true;

    const uint32_t wait = elapsed < 1000 ? 1000 - elapsed : 0;
    const uint32_t slice = wait > 10 ? 10 : wait;
    const uint32_t status = UD_SYS_WaitForObjectV1(app.waitEvent, slice);
    if (status == SyscallReturnCode_TIMEOUT) {
      elapsed += slice;
    } else {
      app.staleStatus = status == SyscallReturnCode_OKAY
          ? SyscallReturnCode_INVALID_ARGUMENT : status;
      app.stale = true;
      add_history_gap();
      invalidate_collection_error();
      if (status == SyscallReturnCode_OKAY) return true;
      return false;
    }
  }
  return false;
}

} // namespace

int main() {
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
  (void)UD_SYS_PrintV1("TMAPP CPU-D2: diagnostic Task Manager starting");
#endif
  app.waitEvent = 0;
  if (UD_SYS_CreateEventV1(EventFlags_ManualReset, &app.waitEvent) !=
      SyscallReturnCode_OKAY) {
    (void)UD_SYS_PrintV1("Task Manager could not create its wait event");
    return 1;
  }
  if (!acquire_font()) {
    (void)UD_SYS_CloseV1(app.waitEvent);
    (void)UD_SYS_PrintV1("Task Manager could not acquire the system font");
    return 1;
  }
  WNDCLASS windowClass{};
  windowClass.lpfnWndProc = task_window_proc;
  windowClass.hbrBackground = UNDOS_PALETTE_BRUSH(7);
  windowClass.lpszClassName = WindowClass;
  if (!RegisterClass(&windowClass)) {
    (void)UndosCloseFont(app.font);
    (void)UD_SYS_CloseV1(app.waitEvent);
    (void)UD_SYS_PrintV1("Task Manager could not register its window class");
    return 1;
  }
  app.window = CreateWindow(WindowClass,
#if defined(UNDOS_TASK_CPU_DIAGNOSTICS)
      "Task Manager CPU-D2",
#else
      "Task Manager",
#endif
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, 30, 30, 580, 400,
      nullptr, nullptr, nullptr, nullptr);
  if (!app.window) {
    recover_failed_window_creations();
    (void)UndosCloseFont(app.font);
    (void)UD_SYS_CloseV1(app.waitEvent);
    (void)UD_SYS_PrintV1("Task Manager could not create its window");
    return 1;
  }
  (void)UndosShowWindow(app.window, SW_SHOW);
  app.focusTarget = 0;
  (void)collect_snapshot();
  if (!app.hasSample) {
    app.stale = true;
    if (app.staleStatus == SyscallReturnCode_OKAY)
      app.staleStatus = SyscallReturnCode_UNKNOWN;
  }
  (void)InvalidateRect(app.window, nullptr, TRUE);

  while (sample_due()) {
    if (!collect_snapshot()) {
      app.stale = true;
      add_history_gap();
      invalidate_collection_error();
    }
  }

  if (app.window) (void)DestroyWindow(app.window);
  free_snapshot(app.sample);
  free(app.cpuHistory);
  free(app.cpuHistoryValid);
  free(app.cpuHistoryIds);
  if (app.fontValid) (void)UndosCloseFont(app.font);
  if (app.waitEvent) (void)UD_SYS_CloseV1(app.waitEvent);
  return 0;
}
