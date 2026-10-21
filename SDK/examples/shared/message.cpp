namespace {
int value;
struct Lifetime {
  Lifetime() { value = 73; }
  ~Lifetime() { value = 0; }
} lifetime;
}

extern "C" int message_value() { return value; }
