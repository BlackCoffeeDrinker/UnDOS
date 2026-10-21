Configure this directory with a native compiler, separately from target builds:

```sh
cmake -S Tests/Hosted -B build/hosted
cmake --build build/hosted --parallel
ctest --test-dir build/hosted --output-on-failure
```

`python3 Scripts/build.py hosted` configures, builds, and runs the same project
under `build/hosted/Debug`. It does not compile target images.

Keep warnings-as-errors enabled. Remove stale fixture state instead of
suppressing unused-variable warnings. For `kstd::error_code`, use
`REQUIRE_FALSE(status)` for success and compare specific failures with their
error enums. Zero codes from different categories are all successful, but
are not equal. Graphics fixtures need a valid backing device because adapter
registration subscribes to device events.
