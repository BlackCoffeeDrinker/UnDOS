extern "C" {
__attribute__((visibility("hidden"))) void *__dso_handle = &__dso_handle;
void __cxa_finalize(void *);
}

__attribute__((destructor)) static void finalize_dso() {
  __cxa_finalize(__dso_handle);
}
