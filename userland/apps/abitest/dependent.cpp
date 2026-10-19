extern "C" void abi_record(unsigned);
extern "C" int abi_shared_value();

__attribute__((constructor)) static void initialize() {
  if (abi_shared_value() == 73) abi_record(2);
}
__attribute__((destructor)) static void finalize() { abi_record(7); }
extern "C" int abi_dependent_value() { return abi_shared_value(); }
