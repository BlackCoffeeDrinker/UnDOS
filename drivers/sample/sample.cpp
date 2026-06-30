#include <kernel/io.hpp>
#include <kernel/event.hpp>
#include <kernel/pnp.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/memory/virtual_memory.hpp>

namespace kernel::sample {

class SampleDriver : public KDriverObject {
public:
  SampleDriver() noexcept : KDriverObject() {
    name = "Sample";
    eventHandler = [](KObjectPtr<KDriverObject> driver, const KEvent &event) {
      if (auto self = static_cast<SampleDriver *>(driver.get())) {
        self->HandleEvent(event);
      }
    };
  }

  void HandleEvent(const KEvent &event) {
    if (event.type == EventType::Pnp) {
      auto &pnpEvent = static_cast<const KPnpEvent &>(event);
      switch (pnpEvent.minorFunction) {
      case PnpMinorFunction::StartDevice:
        early_print("Sample Driver: Received StartDevice!\n");
        break;
      default:
        early_print_fmt("Sample Driver: Received PnP event {x}\n", 
                        static_cast<uint8_t>(pnpEvent.minorFunction));
        break;
      }
    }
  }
};

} // namespace kernel::sample

extern "C" void SampleDriverInit() {
  auto driver = kernel::KE_CreateObject<kernel::sample::SampleDriver>();
  if (driver) {
    Ke_PNP_RegisterDriver(kernel::KObjectPtr<kernel::KDriverObject>(driver));
  }
}
