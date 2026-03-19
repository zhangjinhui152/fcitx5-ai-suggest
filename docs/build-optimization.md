# C++ 编译优化方案

## 背景

本项目使用了 `httplib.h` (16000+行) 和 `json.hpp` 两个大型单头文件库，导致编译缓慢。

---

## 1. 预编译头文件 (PCH)

### 为什么需要

每次编译时，编译器需要重新解析所有头文件。`httplib.h` 和 `json.hpp` 加起来接近 3 万行代码，每次编译都要处理一遍，非常耗时。

PCH (Precompiled Header) 的工作原理：
1. **首次编译**：将常用头文件预编译成二进制 `.gch` 文件
2. **后续编译**：直接加载 `.gch`，跳过头文件解析步骤

### 实施步骤

**1. 修改 CMakeLists.txt**，添加 `target_precompile_headers`：

```cmake
cmake_minimum_required(VERSION 3.16.0)  # PCH 需要 3.16+

# ... 其他配置 ...

target_precompile_headers(fcitx5-ai PRIVATE
    <cstdio>
    <cstdlib>
    <memory>
    <string>
    <functional>
    <atomic>
    <chrono>
    <thread>
    "${CMAKE_SOURCE_DIR}/src/httplib.h"
    "${CMAKE_SOURCE_DIR}/src/json.hpp"
)
```

**2. 清理重新编译**：

```bash
rm -rf build
cmake -B build && cmake --build build
```

### 效果

| 场景 | 优化前 | 优化后 |
|------|--------|--------|
| 首次完整编译 | ~15s | ~17s (生成PCH) |
| 增量编译 (改ai.cpp) | ~15s | ~7s |

---

## 2. ccache (编译缓存)

### 为什么需要

ccache (compiler cache) 会缓存编译的**中间产物**。当相同的源文件使用相同的编译选项再次编译时，直接从缓存获取结果，无需重新编译。

适用场景：
- 切换 git 分支后重新编译
- `make clean` 后重新编译
- 多个项目使用相同的头文件

### 实施步骤

**1. 安装 ccache**：

```bash
# Ubuntu/Debian
sudo apt install ccache

# Arch
sudo pacman -S ccache
```

**2. 修改 CMakeLists.txt**，在 `project()` 之后添加：

```cmake
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()
```

**3. 查看缓存统计**：

```bash
ccache -s  # 查看缓存命中情况
ccache -z  # 清零统计
ccache -C  # 清空缓存
```

### 效果

| 场景 | 无 ccache | 有 ccache |
|------|-----------|-----------|
| 首次编译 | ~17s | ~17s (填充缓存) |
| `make clean && make` | ~17s | **<1s** (缓存命中) |
| 切换分支后编译 | ~17s | **<1s** (缓存命中) |

---

## 3. Ninja 构建系统

### 为什么需要

Ninja 比 Make 更快，原因：
1. **并行构建更智能**：自动检测 CPU 核心数，最大化并行
2. **依赖分析更快**：启动时间几乎为零
3. **增量构建更精确**：只重新编译真正改变的文件

### 实施步骤

**1. 安装 Ninja**：

```bash
# Ubuntu/Debian
sudo apt install ninja-build

# Arch
sudo pacman -S ninja
```

**2. 使用 Ninja 生成构建文件**：

```bash
# 方式一：命令行指定
cmake -B build -G Ninja
cmake --build build

# 方式二：设置环境变量（推荐，VSCode CMake Tools 自动使用）
export CMAKE_GENERATOR=Ninja
```

**3. VSCode 配置**（可选）：

在 `.vscode/settings.json` 中添加：

```json
{
    "cmake.generator": "Ninja"
}
```

### 效果

| 操作 | Make | Ninja |
|------|------|-------|
| 增量编译启动时间 | ~0.5s | ~0.05s |
| 并行调度效率 | 较低 | 更高 |
| 大项目构建 | 慢 | 快 10-20% |

---

## 4. 完整配置示例

### CMakeLists.txt (整合版)

```cmake
cmake_minimum_required(VERSION 3.16.0)
project(fcitx5-ai VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === 优化 1: ccache ===
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

find_package(Fcitx5Core REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(fcitx5-ai MODULE
    src/ai.cpp
    src/ThreadPool.cpp
)

target_link_libraries(fcitx5-ai
    Fcitx5::Core
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(fcitx5-ai PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# === 优化 2: PCH ===
target_precompile_headers(fcitx5-ai PRIVATE
    <cstdio>
    <cstdlib>
    <memory>
    <string>
    <functional>
    <atomic>
    <chrono>
    <thread>
    "${CMAKE_SOURCE_DIR}/src/httplib.h"
    "${CMAKE_SOURCE_DIR}/src/json.hpp"
)
```

### 构建命令

```bash
# 首次配置（使用 Ninja + ccache）
cmake -B build -G Ninja
cmake --build build

# 增量编译
cmake --build build

# 清理后重新编译（ccache 会让这个非常快）
cmake --build build --target clean
cmake --build build
```

---

## 5. 优化效果总结

| 优化方案 | 首次编译 | 增量编译 | clean 后编译 |
|----------|----------|----------|--------------|
| 无优化 | ~15s | ~15s | ~15s |
| + PCH | ~17s | ~7s | ~17s |
| + PCH + ccache | ~17s | ~7s | **<1s** |
| + PCH + ccache + Ninja | ~17s | **~5s** | **<1s** |

---

## 6. 注意事项

1. **PCH 要求 CMake 3.16+**：旧版本不支持 `target_precompile_headers`
2. **ccache 默认缓存大小 5GB**：可通过 `ccache -M 10G` 调整
3. **Ninja 需要 V3.7+**：旧版本可能有兼容问题
4. **修改 PCH 列表需要重新编译**：首次添加或删除 PCH 中的头文件会触发全量编译
