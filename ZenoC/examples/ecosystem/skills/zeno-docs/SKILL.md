---
name: zeno-docs
description: ZenoC documentation skill: how to build, run the test suite and release the runtime.
---
# ZenoC docs skill

Build with cmake --preset offline-debug. Test with ctest. For live tests set
OPENAI_API_KEY and OPENAI_BASE_URL env vars. Release bumps ZENO_VERSION in
include/zeno.h and CMakeLists.txt together.
