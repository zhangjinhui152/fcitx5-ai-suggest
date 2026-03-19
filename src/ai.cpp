#include "ai.h"
#include "ThreadPool.cpp"
#include <atomic>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <fcitx-config/iniparser.h>
namespace fcitx
{

    static ThreadPool pool{4};
    static std::atomic<int> debounceId{0};

    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        ((std::string *)userp)->append((char *)contents, size * nmemb);
        return size * nmemb;
    }

    static std::string httpPost(const std::string &url, const std::string &body,
                                const std::vector<std::pair<std::string, std::string>> &headers,
                                long timeoutSec = 5)
    {
        std::string readBuffer;
        CURL *curl = curl_easy_init();
        if (!curl)
            return "";

        struct curl_slist *chunk = nullptr;
        for (const auto &h : headers)
        {
            std::string headerLine = h.first + ": " + h.second;
            chunk = curl_slist_append(chunk, headerLine.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode res = curl_easy_perform(curl);

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || httpCode != 200)
        {
            printf("[AI] curl error: %s, http: %ld\n", curl_easy_strerror(res), httpCode);
            return "";
        }

        return readBuffer;
    }

    static std::string buildRequestBody(const std::string &prompt, const std::string &model)
    {
        rapidjson::Document doc;
        doc.SetObject();
        auto &alloc = doc.GetAllocator();

        doc.AddMember("model", rapidjson::Value(model.c_str(), alloc), alloc);

        rapidjson::Value messages(rapidjson::kArrayType);

        // system message
        rapidjson::Value systemMsg(rapidjson::kObjectType);
        systemMsg.AddMember("role", "system", alloc);
        systemMsg.AddMember("content", "You are a helpful assistant that converts pinyin to Chinese words. Only return the most likely word, no explanations.", alloc);
        messages.PushBack(systemMsg, alloc);

        // user message
        rapidjson::Value userMsg(rapidjson::kObjectType);
        userMsg.AddMember("role", "user", alloc);
        userMsg.AddMember("content", rapidjson::Value(prompt.c_str(), alloc), alloc);
        messages.PushBack(userMsg, alloc);

        doc.AddMember("messages", messages, alloc);

        // 可选参数
        doc.AddMember("temperature", 0.3, alloc);
        doc.AddMember("max_tokens", 50, alloc);

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        return buffer.GetString();
    }

    static std::string parseAnswer(const std::string &jsonStr)
    {
        rapidjson::Document doc;
        doc.Parse(jsonStr.c_str());

        if (doc.HasParseError())
            return "";

        // 检查是否有 choices 数组
        if (!doc.HasMember("choices") || !doc["choices"].IsArray() || doc["choices"].Size() == 0)
            return "";

        const auto &choices = doc["choices"];
        const auto &firstChoice = choices[0];

        // 检查是否有 message
        if (!firstChoice.HasMember("message") || !firstChoice["message"].IsObject())
            return "";

        const auto &message = firstChoice["message"];

        // 检查是否有 content
        if (!message.HasMember("content") || !message["content"].IsString())
            return "";

        return message["content"].GetString();
    }

    MyCandidate::MyCandidate(const std::string &text, std::function<void()> cb)
        : callback_(cb)
    {
        setText(fcitx::Text(text));
    }

    void MyCandidate::select(fcitx::InputContext *) const
    {
        if (callback_)
            callback_();
    }

    AIAddon::AIAddon(AddonManager *manager) : manager_(manager)
    {
        printf("AI ver2.0 addon hello\n");

        auto *instance = manager_->instance();
        fcitx::readAsIni(config_, "conf/aiaddon.conf");

        dispatcher_.attach(&instance->eventLoop());

        preeditHandler_ = instance->watchEvent(
            EventType::InputContextUpdatePreedit,
            EventWatcherPhase::Default,
            [this, instance](Event &event)
            {
                auto *ic = static_cast<InputContextEvent &>(event).inputContext();

                std::string pinyin = getPinyin(ic);
                printf("Preedit更新: %s\n", pinyin.c_str());

                if (!config_.enabled.value())
                {
                    return;
                }

                if (pinyin.length() >= 3)
                {
                    try
                    {
                        fetchAIAsyncDebug(pinyin, ic);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << e.what() << '\n';
                    }
                }
                else
                {
                    hasInserted = false;
                    lastInsertedPinyin.clear();
                }
            });
    }

    void AIAddon::printCandidates(fcitx::InputContext *ic)
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

    std::string AIAddon::getPinyin(fcitx::InputContext *ic)
    {
        auto &clientPreedit = ic->inputPanel().clientPreedit();
        printf("clientPreedit empty: %d\n", clientPreedit.empty());
        printf("clientPreedit text: [%s]\n", clientPreedit.toString().c_str());
        return clientPreedit.toString();
    }

    std::string AIAddon::getApiKey()
    {
        return config_.apiKey.value(); // 直接从配置对象获取
    }

    std::string AIAddon::getConfigKeyValue(std::string key)
    {
        return config_.timeout.value(); // 直接从配置对象获取
    }

    void AIAddon::insertCandidate(fcitx::InputContext *ic, int position, const std::string &text, std::function<void()> callback)
    {
        auto candidateList = ic->inputPanel().candidateList();
        if (!candidateList)
            return;

        auto modifiableList = candidateList->toModifiable();
        if (modifiableList)
        {
            auto candidate = std::make_unique<MyCandidate>(text, callback);
            modifiableList->insert(position, std::move(candidate));
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
    }

    void AIAddon::insertAICandidate(fcitx::InputContext *ic, const std::string &pinyin)
    {
        if (hasInserted && lastInsertedPinyin == pinyin)
        {
            return;
        }

        insertCandidate(ic, 3, "🤖AI: " + pinyin, [ic, pinyin]()
                        { ic->commitString("AI结果: " + pinyin); });

        lastInsertedPinyin = pinyin;
        hasInserted = true;
    }

    void AIAddon::fetchAIAsyncDebug(const std::string &pinyin,
                                    fcitx::InputContext *ic)
    {
        static std::atomic<int> debounceId{0};
        static ThreadPool pool{4};

        int id = ++debounceId;

        auto icRef = ic->watch();

        std::string apiKey = getApiKey();
        std::string promptTemplate = config_.prompt.value();
        std::string apiUrl = config_.apiUrl.value();
        std::string model = config_.model.value();
        int timeout = 5;
        try {
            timeout = std::stoi(config_.timeout.value());
        } catch (...) {}

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

        printf("[AI] debounce 通过，开始请求: id=%d\n", id);

        if (apiKey.empty())
        {
            printf("[AI] ❌ API Key 为空\n");
            return;
        }

        std::string promptText = promptTemplate;
        size_t pos = promptText.find("{input}");
        if (pos != std::string::npos) {
            promptText.replace(pos, 7, pinyin);
        }
        std::string body = buildRequestBody(promptText, model);

        std::vector<std::pair<std::string, std::string>> headers = {
            {"Authorization", "Bearer " + apiKey},
            {"Content-Type", "application/json"}};

        printf("[AI] timeout=%d\n", timeout);

        std::string responseStr = httpPost(apiUrl, body, headers, timeout);

        if (responseStr.empty())
        {
            printf("[AI] ❌ 请求失败\n");
            return;
        }

        printf("[AI] ✅ 请求成功: id=%d\n", id);

        std::string answer = parseAnswer(responseStr);

        if (answer.empty())
        {
            printf("[AI] ⚠️ 没有拿到答案\n");
            return;
        }

        printf("[AI] 🎯 AI结果: %s (id=%d)\n", answer.c_str(), id);

        dispatcher_.schedule([=]() {
            auto ic = icRef.get();

            if (!ic)
            {
                printf("[AI] ⚠️ IC 已失效，跳过 (id=%d)\n", id);
                return;
            }

            printf("[AI] 📌 插入候选: %s (id=%d)\n",
                   answer.c_str(), id);

            insertCandidate(
                ic,
                config_.insertposition.value(),
                "🤖AI: " + answer,
                [icRef, answer]() {
                    auto ic = icRef.get();
                    if (!ic)
                        return;

                    printf("[AI] ✅ commit: %s\n", answer.c_str());
                    ic->commitString(answer);
                });
        });
    }).detach();
    }

    const fcitx::Configuration *AIAddon::getConfig() const
    {
        return &config_;
    }

    void AIAddon::setConfig(const fcitx::RawConfig &config)
    {
        config_.load(config, true);
        // 保存到文件
        fcitx::safeSaveAsIni(config_, "conf/aiaddon.conf");
    }

    AddonInstance *AIAddonFactory::create(AddonManager *manager)
    {
        return new AIAddon(manager);
    }

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::AIAddonFactory)
