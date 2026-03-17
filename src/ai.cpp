#include <cstdio>
#include <memory>
#include <string>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/candidateaction.h>
#include <fcitx/event.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx-utils/event.h>
#include <fcitx/inputpanel.h> // 添加这个头文件
#include <cstdlib>            // for getenv
#include <atomic>
#include <chrono>
#include <thread>

#include "httplib.h"
#include "json.hpp" // 需要引入JSON库，或者自己拼字符串
#include "ThreadPool.cpp"
#include <chrono>
namespace fcitx
{
    static ThreadPool pool{4};
    static std::atomic<int> debounceId{0};
    // 正确的方式 ✅
    class MyCandidate : public fcitx::CandidateWord
    {
    public:
        MyCandidate(const std::string &text, std::function<void()> cb) : callback_(cb)
        {
            setText(fcitx::Text(text));
        }
        void select(fcitx::InputContext *) const override
        {
            if (callback_)
                callback_();
        }

    private:
        std::function<void()> callback_;
    };
    class AIAddon : public AddonInstance
    {
    public:
        std::string buffer;

        AIAddon(AddonManager *manager) : manager_(manager)
        {
            printf("AI ver1.9 addon hello\n");

            auto *instance = manager_->instance();

            // 添加preedit更新监听
            preeditHandler_ = instance->watchEvent(
                EventType::InputContextUpdatePreedit,
                EventWatcherPhase::Default,
                [this, &instance](Event &event)
                {
                    auto *ic = static_cast<InputContextEvent &>(event).inputContext();

                    std::string pinyin = getPinyin(ic);
                    printf("Preedit更新: %s\n", pinyin.c_str());

                    if (pinyin.length() >= 3)
                    {
                        // insertAICandidate(ic, pinyin); // 自动处理防重复
                        fetchAIAsyncDebug(pinyin, ic, &instance->eventLoop()); // 自动处理防重复
                    }
                    else
                    {
                        // 拼音变短时重置状态
                        hasInserted = false;
                        lastInsertedPinyin.clear();
                    }
                });
        }

    private:
        AddonManager *manager_;
        std::string lastPinyin;
        bool zhihuInserted = false; // 添加标志
        std::unique_ptr<HandlerTableEntry<EventHandler>> handler_;
        std::unique_ptr<HandlerTableEntry<EventHandler>> preeditHandler_;
        void printCandidates(fcitx::InputContext *ic)
        {
            auto candidateList = ic->inputPanel().candidateList();
            if (!candidateList)
                return;

            for (int i = 0; i < candidateList->size(); i++)
            {
                auto &candidate = candidateList->candidate(i);
                printf("候选词 %d: %s\n", i, candidate.text().toString().c_str());
            }
        }

        std::string getPinyin(fcitx::InputContext *ic)
        {
            auto &clientPreedit = ic->inputPanel().clientPreedit();
            printf("clientPreedit empty: %d\n", clientPreedit.empty());
            printf("clientPreedit text: [%s]\n", clientPreedit.toString().c_str());
            return clientPreedit.toString();
        }
        // 包装函数：防止重复插入
        bool hasInserted = false;       // 标记当前拼音是否已插入
        std::string lastInsertedPinyin; // 记录最后一次插入的拼音

        // 读取环境变量
        std::string getApiKey()
        {
            const char *key = std::getenv("FCITX5_AI_APIKEY");
            return key ? std::string(key) : "";
        }

        void insertCandidate(fcitx::InputContext *ic, int position, const std::string &text, std::function<void()> callback)
        {
            auto candidateList = ic->inputPanel().candidateList();
            if (!candidateList)
                return;

            auto modifiableList = candidateList->toModifiable();
            if (modifiableList)
            {
                auto candidate = std::make_unique<MyCandidate>(text, callback);
                modifiableList->insert(position, std::move(candidate));

                // 更新界面
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            }
        }
        void insertAICandidate(fcitx::InputContext *ic, const std::string &pinyin)
        {
            // 如果已经为这个拼音插入过了，就不再插入
            if (hasInserted && lastInsertedPinyin == pinyin)
            {
                return;
            }

            // 插入新候选词
            insertCandidate(ic, 3, "🤖AI: " + pinyin, [ic, pinyin]()
                            { ic->commitString("AI结果: " + pinyin); });

            // 更新状态
            lastInsertedPinyin = pinyin;
            hasInserted = true;
        }

        void fetchAI(const std::string &pinyin, fcitx::InputContext *ic) // 添加 ic 参数
        {
            std::string apiKey = getApiKey();
            if (apiKey.empty())
            {
                printf("错误: 未设置 FCITX5_AI_APIKEY 环境变量\n");
                return;
            }

            // 创建 HTTPS 客户端（注意是 https）
            httplib::SSLClient cli("https://dashscope.aliyuncs.com");

            // 设置请求头
            httplib::Headers headers = {
                {"Authorization", "Bearer " + apiKey},
                {"Content-Type", "application/json"}};

            // 构造请求体 - 使用你的问题示例
            using namespace nlohmann;
            std::string prompt = "拼音'" + pinyin + "'最可能的词语是？只返回词语";
            json body = {
                {"model", "qwen3.5-flash"},
                {"input", prompt}, // 这里可以用 pinyin 或自定义问题
                {"extra_body", {{"enable_thinking", false}}}};

            // 发送 POST 请求
            auto res = cli.Post("/api/v2/apps/protocols/compatible-mode/v1/responses",
                                headers,
                                body.dump(),
                                "application/json");
            if (!res)
            {
                printf("[AI] ❌ 请求失败: %d\n", res.error());
                return;
            }
            if (res && res->status == 200)
            {
                // 解析响应
                json response = json::parse(res->body);

                // 遍历输出项
                for (const auto &item : response["output"])
                {
                    if (item["type"] == "reasoning")
                    {
                        printf("【推理过程】\n");
                        for (const auto &summary : item["summary"])
                        {
                            std::string text = summary["text"];
                            printf("%.500s\n", text.c_str()); // 截取前500字符
                        }
                        printf("\n");
                    }
                    else if (item["type"] == "message")
                    {
                        printf("【最终答案】\n");
                        std::string answer = item["content"][0]["text"];
                        printf("%s\n", answer.c_str());

                        // 这里可以把 answer 插入到候选词列表
                        // 显示 "🤖AI: 9.11 > 9.9"，但选中后只上屏 "9.11 > 9.9"
                        insertCandidate(ic, 0, "🤖AI: " + answer, [ic, answer]()
                                        { ic->commitString(answer); });
                    }
                }
            }
            else
            {
                printf("请求失败: %d - %s\n",
                       res ? res->status : -1,
                       res ? res->body.c_str() : "连接失败");
            }
        }

        void fetchAIAsync(const std::string &pinyin,
                          fcitx::InputContext *ic,
                          fcitx::EventLoop *eventLoop)
        {
            static std::atomic<int> debounceId{0};
            static ThreadPool pool{4};

            int id = ++debounceId;

            std::thread([=]()
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(300));

                            if (id != debounceId)
                                return;

                            pool.enqueue([=]()
                                         {
                                             std::string apiKey = getApiKey();
                                             if (apiKey.empty())
                                                 return;

                                             httplib::Client cli("https://dashscope.aliyuncs.com");
                                             cli.set_connection_timeout(2);
                                             cli.set_read_timeout(3);

                                             httplib::Headers headers = {
                                                 {"Authorization", "Bearer " + apiKey},
                                                 {"Content-Type", "application/json"}};

                                             using json = nlohmann::json;
                                             std::string prompt = "拼音'" + pinyin + "'最可能的词语是？只返回词语";

                                             json body = {
                                                 {"model", "qwen3.5-flash"},
                                                 {"input", prompt},
                                                 {"extra_body", {{"enable_thinking", false}}}};

                                             auto res = cli.Post(
                                                 "/api/v2/apps/protocols/compatible-mode/v1/responses",
                                                 headers,
                                                 body.dump(),
                                                 "application/json");

                                             if (!res || res->status != 200)
                                                 return;

                                             json response = json::parse(res->body);

                                             std::string answer;
                                             for (const auto &item : response["output"])
                                             {
                                                 if (item["type"] == "message")
                                                 {
                                                     answer = item["content"][0]["text"];
                                                     break;
                                                 }
                                             }

                                             if (answer.empty())
                                                 return;

                                             // ✅ 正确回主线程
                                             auto _ = eventLoop->addPostEvent([=](fcitx::EventSource *) {
                                                insertCandidate(
                                                    ic,
                                                    0,
                                                    "🤖AI: " + answer,
                                                    [ic, answer]() {
                                                        ic->commitString(answer);
                                                    }
                                                );
                                                return true;
                                            });
                                         }); })
                .detach();
        }

        void fetchAIAsyncDebug(const std::string &pinyin,
                               fcitx::InputContext *ic,
                               fcitx::EventLoop *eventLoop)
        {
            static std::atomic<int> debounceId{0};
            static ThreadPool pool{4};

            int id = ++debounceId;

            printf("[AI] 输入: %s | debounceId=%d\n", pinyin.c_str(), id);

            std::thread([=]()
                        {
                            printf("[AI] 进入 debounce 等待: id=%d\n", id);

                            std::this_thread::sleep_for(std::chrono::milliseconds(300));

                            if (id != debounceId)
                            {
                                printf("[AI] debounce 被取消: id=%d (最新=%d)\n", id, (int)debounceId.load());
                                return;
                            }

                            printf("[AI] debounce 通过，进入线程池: id=%d\n", id);

                            pool.enqueue([=]()
                                         {
                                             printf("[AI] 开始请求: id=%d, pinyin=%s\n", id, pinyin.c_str());

                                             std::string apiKey = getApiKey();
                                             if (apiKey.empty())
                                             {
                                                 printf("[AI] ❌ API Key 为空\n");
                                                 return;
                                             }

                                             httplib::Client cli("https://dashscope.aliyuncs.com");
                                             cli.set_connection_timeout(2);
                                             cli.set_read_timeout(3);

                                             httplib::Headers headers = {
                                                 {"Authorization", "Bearer " + apiKey},
                                                 {"Content-Type", "application/json"}};

                                             using json = nlohmann::json;
                                             std::string prompt = "拼音'" + pinyin + "'最可能的词语是？只返回词语";

                                             json body = {
                                                 {"model", "qwen3.5-flash"},
                                                 {"input", prompt},
                                                 {"extra_body", {{"enable_thinking", false}}}};

                                             auto res = cli.Post(
                                                 "/api/v2/apps/protocols/compatible-mode/v1/responses",
                                                 headers,
                                                 body.dump(),
                                                 "application/json");

                                             if (!res)
                                             {
                                                 printf("[AI] ❌ 请求失败: 无响应\n");
                                                 return;
                                             }

                                             if (res->status != 200)
                                             {
                                                 printf("[AI] ❌ HTTP错误: %d\n%s\n", res->status, res->body.c_str());
                                                 return;
                                             }

                                             printf("[AI] ✅ 请求成功: id=%d\n", id);

                                             json response;
                                             try
                                             {
                                                 response = json::parse(res->body);
                                             }
                                             catch (...)
                                             {
                                                 printf("[AI] ❌ JSON解析失败\n");
                                                 return;
                                             }

                                             std::string answer;
                                             for (const auto &item : response["output"])
                                             {
                                                 if (item["type"] == "message")
                                                 {
                                                     answer = item["content"][0]["text"];
                                                     break;
                                                 }
                                             }

                                             if (answer.empty())
                                             {
                                                 printf("[AI] ⚠️ 没有拿到答案\n");
                                                 return;
                                             }

                                             printf("[AI] 🎯 AI结果: %s (id=%d)\n", answer.c_str(), id);

                                             // 👉 回主线程
                                             auto _ = eventLoop->addPostEvent([=](fcitx::EventSource *)
                                                                              {
                printf("[AI] 📌 插入候选: %s (id=%d)\n", answer.c_str(), id);

                insertCandidate(
                    ic,
                    0,
                    "🤖AI: " + answer,
                    [ic, answer]() {
                        printf("[AI] ✅ commit: %s\n", answer.c_str());
                        ic->commitString(answer);
                    }
                );

                return true; });
                                         }); })
                .detach();
        }
    };

    class AIAddonFactory : public AddonFactory
    {
    public:
        AddonInstance *create(AddonManager *manager) override
        {
            return new AIAddon(manager);
        }
    };

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::AIAddonFactory)