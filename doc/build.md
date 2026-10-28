# 构建与测试

需要 CMake 3.16+、支持 C++20 chrono 时区的标准库，以及运行时 IANA 时区数据库（Linux 上通常由 `tzdata` 包提供）。当前已在 GCC 13.3 / libstdc++ 环境验证；CMake 会检查实际 API 的编译与链接支持。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

使用 `-DBUILD_TESTING=OFF` 可关闭测试构建。测试不依赖网络下载，在 Release 下也会执行所有断言。

