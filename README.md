# Fcitx5 AI 拼音插件

基于阿里云通义千问 API 的 fcitx5 拼音预测插件。

## 安装

```bash
mkdir -p out/build/linux-gcc-debug
cd out/build/linux-gcc-debug
cmake ../..
make -j4
sudo cp libfcitx5-ai.so /usr/lib/x86_64-linux-gnu/fcitx5/

# 这里看调试信息
sudo cp /home/xxx/Workspace/cpp/fcitxAddon/out/build/linux-gcc-debug/libfcitx5-ai.so /usr/lib/x86_64-linux-gnu/fcitx5/ ;fcitx5 -r


```

重启 fcitx5 生效。

## 配置

配置文件位置: `~/.config/fcitx5/conf/aiaddon.conf`

```ini
[AIAddon]
Enabled=True
InsertPosition=3
APIKey=your-api-key-here
ApiUrl=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
Model=qwen-plus
MinimumLength=3
DebounceTime=300
CacheSize=100
TimeOut=10
Prompt=拼音'{input}'最可能的词语是？只返回词语
```

| 配置项         | 说明                                     | 默认值                                    |
| -------------- | ---------------------------------------- | ----------------------------------------- |
| Enabled        | 是否启用 AI 云拼音                       | True                                      |
| InsertPosition | 候选词插入位置（0=最前面）               | 3                                         |
| APIKey         | API Key                                  | 空                                        |
| ApiUrl         | OpenAI 兼容的 API 地址                   | 阿里云 DashScope                          |
| Model          | 模型名称                                 | qwen-plus                                 |
| MinimumLength  | 最小拼音长度（小于此值不发请求）         | 3                                         |
| DebounceTime   | 防抖等待时间（毫秒）                     | 300                                       |
| CacheSize      | LRU 缓存容量（条）                       | 100                                       |
| TimeOut        | 请求超时时间（秒）                       | 10                                        |
| Prompt         | 提示词模板，`{input}` 会被替换为实际拼音 | `拼音'{input}'最可能的词语是？只返回词语` |

**常见 API 配置示例：**

```ini
# 阿里云 DashScope
ApiUrl=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
Model=qwen-plus

# OpenAI
ApiUrl=https://api.openai.com/v1/chat/completions
Model=gpt-3.5-turbo

# 本地 Ollama
ApiUrl=http://localhost:11434/v1/chat/completions
Model=llama3
```

---

## 代码详解

### 文件结构

```
src/
├── ai.h          # 类定义、配置宏
├── ai.cpp        # 主逻辑
└── ThreadPool.cpp # 线程池（当前未使用）
```

---

### 1. 配置系统 (`ai.h:24-35`)

```cpp
FCITX_CONFIGURATION(
    AIAddonConfig,
    fcitx::Option<bool> enabled{this, "Enabled", _("Enable AI"), true};
    fcitx::Option<int> insertposition{this, "InsertPosition", _("Insert Position"), 3};
    fcitx::Option<std::string> apiKey{this, "APIKey", _("API Key"), ""};
    fcitx::Option<std::string> apiUrl{this, "ApiUrl", _("API URL"), "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"};
    fcitx::Option<std::string> model{this, "Model", _("Model"), "qwen-plus"};
    fcitx::Option<int> minimumLength{this, "MinimumLength", _("Minimum Pinyin Length"), 3};
    fcitx::Option<int> debounceTime{this, "DebounceTime", _("Debounce Time (ms)"), 300};
    fcitx::Option<int> cacheSize{this, "CacheSize", _("Cache Size"), 100};
    fcitx::Option<std::string> timeout{this, "TimeOut", _("TimeOut"), "10"};
    fcitx::Option<std::string> prompt{this, "Prompt", _("Prompt"), "拼音'{input}'最可能的词语是？只返回词语"};
);
```

**原理：** `FCITX_CONFIGURATION` 是 fcitx5 提供的宏，自动生成一个配置类。每个 `Option` 包含：

- `this` - 父对象指针
- `"Enabled"` - 配置文件中的键名
- `_("Enable AI")` - UI 显示名称（支持国际化）
- `true` - 默认值

读取配置：`config_.enabled.value()`

---

### 2. 插件入口 (`ai.cpp:143-184`)

```cpp
AIAddon::AIAddon(AddonManager *manager) : manager_(manager)
{
    auto *instance = manager_->instance();
    fcitx::readAsIni(config_, "conf/aiaddon.conf");  // 读取配置文件

    dispatcher_.attach(&instance->eventLoop());      // 关键！绑定事件调度器

    preeditHandler_ = instance->watchEvent(
        EventType::InputContextUpdatePreedit,        // 监听：用户输入变化
        EventWatcherPhase::Default,
        [this, instance](Event &event) {
            auto *ic = ...;  // 获取输入上下文
            std::string pinyin = getPinyin(ic);
        
            if (!config_.enabled.value()) return;    // 检查是否启用
        
            if (pinyin.length() >= 3) {
                fetchAIAsyncDebug(pinyin, ic);       // 触发 AI 请求
            }
        });
}
```

**流程：**

1. 读取配置文件
2. **`dispatcher_.attach()`** - 把 EventDispatcher 绑定到主事件循环（必须在主线程调用）
3. 注册事件监听器，用户每输入一个字符都会触发

---

### 3. Debounce 防抖机制 (`ai.cpp:314-343`)

```cpp
void AIAddon::fetchAIAsyncDebug(const std::string &pinyin, InputContext *ic)
{
    static std::atomic<int> debounceId{0};  // 全局计数器，原子变量

    int id = ++debounceId;  // 每次调用 id 递增

    std::thread([=]() {
        sleep_for(300ms);  // 等待 300ms

        if (id != debounceId) {  // 期间有新输入？
            return;              // 取消本次请求
        }

        // 继续执行 HTTP 请求...
    }).detach();
}
```

**为什么需要防抖？**

用户输入 `zhongguo` 时，事件触发顺序：

```
z → zh → zho → zhon → zhong → zhongg → ... → zhongguo
```

如果不防抖，会发起 8 次 HTTP 请求，浪费资源且结果混乱。

**防抖原理：**

```
输入 z    → id=1, sleep(300ms)
输入 zh   → id=2, sleep(300ms)
输入 zho  → id=3, sleep(300ms)
... 300ms 后 ...
id=1 醒来 → debounceId=8 → 取消
id=2 醒来 → debounceId=8 → 取消
...
id=8 醒来 → debounceId=8 → 继续执行 ✓
```

---

### 4. 跨线程调度：EventDispatcher (`ai.cpp:388-412`)

**问题：** HTTP 请求在 worker 线程执行，但 `insertCandidate()` 操作 UI，必须在主线程执行。

**解决方案：**

```cpp
// 主线程初始化（构造函数中）
dispatcher_.attach(&instance->eventLoop());

// worker 线程中调用
std::thread([=]() {
    // ... HTTP 请求 ...
    std::string answer = parseAnswer(responseStr);

    dispatcher_.schedule([=]() {
        // 👈 这里的代码会在主线程执行！
        insertCandidate(ic, 0, "🤖AI: " + answer, callback);
    });
}).detach();
```

**EventDispatcher 原理：**

1. `attach()` - 在主线程注册，内部创建一个 pipe/eventfd
2. `schedule(functor)` - 线程安全地将 functor 放入队列，并通知主线程
3. 主线程事件循环检测到通知，取出 functor 执行

---

### 5. 为什么用 `.detach()`？ (`ai.cpp:413`)

```cpp
std::thread([=]() {
    // ...
}).detach();  // 👈 关键
```

`std::thread` 对象销毁时，如果线程还在运行且没有 `join()` 或 `detach()`，会调用 `std::terminate()` 导致程序崩溃。


| 方式       | 效果                                         |
| ---------- | -------------------------------------------- |
| `join()`   | 阻塞等待线程结束（不适合这里，会卡住主线程） |
| `detach()` | 让线程独立运行，主线程继续执行               |

---

### 6. InputContext 弱引用 (`ai.cpp:322, 389`)

```cpp
auto icRef = ic->watch();  // 创建弱引用

// worker 线程中
dispatcher_.schedule([=]() {
    auto ic = icRef.get();  // 尝试获取
    if (!ic) {
        return;  // IC 已销毁（用户关闭了输入框）
    }
    // 安全使用 ic
});
```

**为什么需要弱引用？**

用户可能在我们 HTTP 请求期间关闭输入框，此时 `InputContext` 已被销毁。如果直接使用裸指针会崩溃。

`ic->watch()` 返回一个 `TrackableObjectReference`，当对象销毁时 `get()` 返回 `nullptr`。

---

### 7. 候选词插入 (`ai.cpp:217-230`)

```cpp
void AIAddon::insertCandidate(InputContext *ic, int position, 
                              const std::string &text, 
                              std::function<void()> callback)
{
    auto candidateList = ic->inputPanel().candidateList();
    auto modifiableList = candidateList->toModifiable();  // 转为可修改列表

    auto candidate = std::make_unique<MyCandidate>(text, callback);
    modifiableList->insert(position, std::move(candidate));  // 插入到指定位置
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);  // 刷新 UI
}
```

---

### 8. 自定义候选词类 (`ai.h:29-37, ai.cpp:131-141`)

```cpp
class MyCandidate : public fcitx::CandidateWord {
public:
    MyCandidate(const std::string &text, std::function<void()> cb);
    void select(InputContext *) const override;  // 用户选中时调用

private:
    std::function<void()> callback_;  // 回调函数
};

void MyCandidate::select(InputContext *) const {
    if (callback_)
        callback_();  // 执行回调，把答案提交到输入框
}
```

当用户按数字键选择这个候选词时，`select()` 被调用，执行 `ic->commitString(answer)`。

---

### 完整流程图

```
用户输入拼音
    │
    ▼
┌─────────────────────────────────────┐
│  主线程: Preedit 事件触发            │
│  - 获取拼音                          │
│  - 检查 enabled 配置                 │
│  - 检查长度 >= 3                     │
│  - 调用 fetchAIAsyncDebug()          │
│    - id = ++debounceId               │
│    - 创建 icRef 弱引用               │
│    - 创建 std::thread + detach()     │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  worker 线程:                        │
│  - sleep(300ms) 防抖                 │
│  - if (id != debounceId) return     │
│  - HTTP 请求阿里云 API               │
│  - 解析 JSON 获取 answer             │
│  - dispatcher_.schedule(callback)    │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  主线程: 执行 schedule 的回调        │
│  - ic = icRef.get()                  │
│  - if (!ic) return                   │
│  - insertCandidate() 插入候选词      │
│  - updateUI() 刷新界面               │
└─────────────────────────────────────┘
    │
    ▼
用户按数字键选择候选词
    │
    ▼
MyCandidate::select() → ic->commitString(answer)
```

---

## 关键技术点总结


| 技术                    | 作用                                          |
| ----------------------- | --------------------------------------------- |
| `EventDispatcher`       | fcitx5 原生线程安全调度器，worker→主线程通信 |
| `std::thread::detach()` | 让线程独立运行，不阻塞主线程                  |
| `std::atomic<int>`      | 原子计数器，实现防抖                          |
| `ic->watch()`           | 弱引用，防止访问已销毁的 InputContext         |
| `FCITX_CONFIGURATION`   | 配置系统宏，自动生成配置类                    |

---

## Q&A

### Q: 为什么 EventDispatcher 需要"线程安全"？不是主线程干自己的，另开线程请求，请求完主线程拿到数据渲染就行了吗？

**你的理解是对的！** "线程安全"解决的是另一个问题。

#### 问题：worker 线程怎么"告诉"主线程？

```
主线程：在跑事件循环，不知道 worker 什么时候完成
worker线程：请求完了，想告诉主线程"数据好了，你来渲染"
```

**最直觉的想法：** worker 直接调用主线程的函数

```cpp
std::thread([=]() {
    // 请求完成
    insertCandidate(ic, 0, answer);  // ❌ 直接调用
}).detach();
```

**问题：** 主线程可能正在干别的事（处理键盘输入、刷新界面等），两个线程同时操作同一个东西会崩溃。

#### 解决方案：一个"信箱"

想象一个信箱：
- worker 线程：把信（回调函数）放进信箱
- 主线程：定期检查信箱，有信就拿出来执行

```
worker线程                     主线程
    │                           │
    │  把回调放入队列            │  正在跑事件循环
    │  ───────────────► [队列]  │
    │                           │  检查队列 → 发现任务 → 执行
```

#### 为什么需要"线程安全"？

队列被两个人同时访问：
- worker 写入
- 主线程读取

**如果同时操作：**
```
worker: 正在放入任务...
主线程: 正在取出任务...
结果: 队列乱了，程序崩溃
```

**线程安全** = 不管谁先谁后，队列内部会用锁保证不会乱。

#### EventDispatcher 做了什么

```cpp
// worker 线程调用
dispatcher_.schedule([=]() {
    insertCandidate(...);  // 这段代码
});
// ↑ 只是放入队列，不会立即执行
// ↑ 队列是线程安全的，不会和主线程冲突

// 主线程稍后自动取出执行
```

**所以"线程安全"是指：**
- `schedule()` 这个函数本身可以被任何线程安全调用
- 内部的队列不会因为多线程访问而出问题

#### 总结

| 你的理解 | 实际情况 |
|---------|---------|
| worker 请求完 → 主线程拿到数据 | worker 请求完 → **放入队列** → 主线程从队列拿到数据 |

中间多了一个"队列"，这个队列必须是线程安全的，否则两个线程同时访问会崩溃。`EventDispatcher` 就是这个线程安全的队列。

---

### Q: EventDispatcher 是从哪里看到的？头文件在哪？

**头文件位置：** `/usr/include/Fcitx5/Utils/fcitx-utils/eventdispatcher.h`

**引入方式：**
```cpp
#include <fcitx-utils/eventdispatcher.h>
```

**源码关键部分：**
```cpp
/**
 * A thread safe class to post event to a certain EventLoop.
 */
class FCITXUTILS_EXPORT EventDispatcher {
public:
    /**
     * Attach EventDispatcher to an EventLoop. Must be called in the same thread
     * of EventLoop.
     */
    void attach(EventLoop *event);

    /**
     * A thread-safe function to schedule a functor to be call from event loop.
     */
    void schedule(std::function<void()> functor);
};
```

**使用要点：**
- `attach()` 必须在主线程（EventLoop 所在的线程）调用
- `schedule()` 是线程安全的，可以从任意线程调用
- 内部实现使用 pipe/eventfd + IOEvent，跨线程通知主线程

---

