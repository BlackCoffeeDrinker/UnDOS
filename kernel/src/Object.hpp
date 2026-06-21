
#pragma once
#include "Core.hpp"

namespace kernel {
class Object { /* Ref counting, security descriptors, name */
};

class DirectoryObject : public Object {
  // Keeps a list/map of child Objects
};

class DeviceObject : public Object {
  // Represents hardware (e.g., \Device\Keyboard0)
};

class SymbolicLinkObject : public Object {
  // Points to another path (e.g., \DosDevices\A: -> \Device\Floppy0)
};
}// namespace Kernel
