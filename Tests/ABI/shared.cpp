extern "C" void abi_record(unsigned);

__attribute__((constructor)) static void initialize() { abi_record(1); }
__attribute__((destructor)) static void finalize() { abi_record(8); }

extern "C" int abi_shared_value() { return 73; }
