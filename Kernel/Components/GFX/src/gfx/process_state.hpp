#pragma once

#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <stdkrn.hpp>
#include <undos/syscalls/__aliases.h>

#include "gfx/KDeviceContextTable.hpp"
#include "gfx/KFontResourceTable.hpp"
#include <kernel/gfx/screen.hpp>
#include <kernel/process_notify.hpp>

namespace kernel::gfx {
inline ProcessKey key_of(const KProcessObject &p) noexcept { return {.pid = p.pid, .incarnation = p.incarnation}; }

// Exit callback for KE_PS_RegisterNotify(..., ProcessNotifyOrder::Gfx); registered by gfx::init (Step 4).
kstd::error_code on_process_notify(ProcessEvent event, KProcessObject &process, void *ctx) noexcept;
// System-wide font resource table.
font::ResourceTable &font_resources() noexcept;
}// namespace kernel::gfx
