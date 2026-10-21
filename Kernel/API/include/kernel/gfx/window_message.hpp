
#pragma once

#include <stdint.h>

namespace kernel::user {
// Mirrors of the WindowMessage/WindowStyle IDL values (tools/usersyscall.txt).
constexpr uint32_t kMsgCreate = 0x0001;
constexpr uint32_t kMsgDestroy = 0x0002;
constexpr uint32_t kMsgMove = 0x0003;
constexpr uint32_t kMsgSize = 0x0005;
constexpr uint32_t kMsgActivate = 0x0006;
constexpr uint32_t kMsgSetFocus = 0x0007;
constexpr uint32_t kMsgKillFocus = 0x0008;
constexpr uint32_t kMsgCaptureChanged = 0x0215;
constexpr uint32_t kMsgPaint = 0x000F;
constexpr uint32_t kMsgClose = 0x0010;
constexpr uint32_t kMsgDisplayChange = 0x007E;
constexpr uint32_t kMsgKeyDown = 0x0100;
constexpr uint32_t kMsgKeyUp = 0x0101;
constexpr uint32_t kMsgChar = 0x0102;
constexpr uint32_t kMsgMouseMove = 0x0200;
constexpr uint32_t kMsgLButtonDown = 0x0201;
constexpr uint32_t kMsgLButtonUp = 0x0202;
constexpr uint32_t kMsgLButtonDoubleClick = 0x0203;
constexpr uint32_t kMsgRButtonDown = 0x0204;
constexpr uint32_t kMsgRButtonUp = 0x0205;

constexpr uint32_t kStyleVisible = 0x00000001;
constexpr uint32_t kStyleCaption = 0x00000002;
constexpr uint32_t kStyleBorder = 0x00000004;
constexpr uint32_t kStyleChild = 0x00000008;
}
