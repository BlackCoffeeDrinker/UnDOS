#pragma once

#include <kernel/__core.hpp>

namespace kernel {

enum class EventType : uint32_t {
  Pnp = 0x01,
  Power = 0x02,
};

struct KEvent : Versioned<KEvent, 1> {
  EventType type;
  uint32_t flags;

  constexpr KEvent(EventType t) : type(t), flags(0) {}
};

enum class PnpMinorFunction : uint8_t {
  StartDevice = 0x01,
  QueryRemoveDevice = 0x02,
  RemoveDevice = 0x03,
  CancelRemoveDevice = 0x04,
  StopDevice = 0x05,
  QueryStopDevice = 0x06,
  CancelStopDevice = 0x07,
  QueryDeviceRelations = 0x08,
  QueryInterface = 0x09,
  QueryCapabilities = 0x0A,
  QueryResources = 0x0B,
  QueryResourceRequirements = 0x0C,
  QueryDeviceText = 0x0D,
  FilterResourceRequirements = 0x0E,
  ReadConfig = 0x0F,
  WriteConfig = 0x10,
  Eject = 0x11,
  SetLock = 0x12,
  QueryId = 0x13,
  QueryPnpDeviceState = 0x14,
  QueryBusInformation = 0x15,
  DeviceUsageNotification = 0x16,
  SurpriseRemoval = 0x17,
};

struct KPnpEvent : KEvent {
  PnpMinorFunction minorFunction;
  void *parameters;

  constexpr KPnpEvent(PnpMinorFunction minor)
      : KEvent(EventType::Pnp), minorFunction(minor), parameters(nullptr) {}
};

} // namespace kernel
