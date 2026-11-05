#pragma once

#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <stdkrn.hpp>
#include <undos/syscalls/__aliases.h>

#include "gfx/KDeviceContextTable.hpp"
#include "gfx/KFontResourceTable.hpp"
#include <kernel/process_notify.hpp>

namespace kernel::gfx {


inline ProcessKey key_of(const KProcessObject &p) noexcept { return {.pid = p.pid, .incarnation = p.incarnation}; }

// Exit callback for KE_PS_RegisterNotify(..., ProcessNotifyOrder::Gfx); registered by gfx::init (Step 4).
kstd::error_code on_process_notify(ProcessEvent event, KProcessObject &process, void *ctx) noexcept;


kstd::expected<kstd::pair<KDeviceContextSlot *, HDC>, kstd::error_code> CreateDC(KProcessObject *process);
KDeviceContextSlot *FindDC(KProcessObject *process, HDC HDC);
// Moves the slot's owners into `released`; drop it after the USER scene lock.
kstd::error_code CloseDC(KProcessObject *process, HDC HDC, DetachedDC &released);
// Drops the owners inline: call without the USER scene lock.
kstd::error_code CloseDC(KProcessObject *process, HDC HDC);

// System-wide font resource table.
font::ResourceTable &font_resources() noexcept;
}// namespace kernel::gfx
