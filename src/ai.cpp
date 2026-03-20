#include "ai.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <fcitx-config/iniparser.h>

namespace fcitx
{

// ==================== HTTP 工具函数 ====================

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

static std::string httpPost(const std::string &url, const std::string &body,
                            const std::vector<std::pair<std::string, std::string>> &headers,
                            long timeoutSec)
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
        printf("[AI] HTTP error: %s, code: %ld\n", curl_easy_strerror(res), httpCode);
        return "";
    }

    return readBuffer;
}

// ==================== JSON 工具函数 ====================

static std::string buildRequestBody(const std::string &prompt, const std::string &model)
{
    rapidjson::Document doc;
    doc.SetObject();
    auto &alloc = doc.GetAllocator();

    doc.AddMember("model", rapidjson::Value(model.c_str(), alloc), alloc);

    rapidjson::Value messages(rapidjson::kArrayType);

    rapidjson::Value systemMsg(rapidjson::kObjectType);
    systemMsg.AddMember("role", "system", alloc);
    systemMsg.AddMember("content", "You are a helpful assistant that converts pinyin to Chinese words. Only return the most likely word, no explanations.", alloc);
    messages.PushBack(systemMsg, alloc);

    rapidjson::Value userMsg(rapidjson::kObjectType);
    userMsg.AddMember("role", "user", alloc);
    userMsg.AddMember("content", rapidjson::Value(prompt.c_str(), alloc), alloc);
    messages.PushBack(userMsg, alloc);

    doc.AddMember("messages", messages, alloc);
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

    if (!doc.HasMember("choices") || !doc["choices"].IsArray() || doc["choices"].Size() == 0)
        return "";

    const auto &firstChoice = doc["choices"][0];

    if (!firstChoice.HasMember("message") || !firstChoice["message"].IsObject())
        return "";

    const auto &message = firstChoice["message"];

    if (!message.HasMember("content") || !message["content"].IsString())
        return "";

    return message["content"].GetString();
}

// ==================== MyCandidate 实现 ====================

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

// ==================== AICache 实现 ====================

std::string AICache::get(const std::string &key)
{
    auto it = cacheMap_.find(key);
    if (it == cacheMap_.end())
        return "";

    cacheList_.splice(cacheList_.begin(), cacheList_, it->second);
    return it->second->second;
}

void AICache::put(const std::string &key, const std::string &value)
{
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end())
    {
        cacheList_.erase(it->second);
        cacheMap_.erase(it);
    }

    cacheList_.push_front({key, value});
    cacheMap_[key] = cacheList_.begin();

    while (cacheMap_.size() > maxSize_)
    {
        auto last = cacheList_.end();
        --last;
        cacheMap_.erase(last->first);
        cacheList_.pop_back();
    }
}

void AICache::clear()
{
    cacheList_.clear();
    cacheMap_.clear();
}

// ==================== AIAddon 构造函数 ====================

AIAddon::AIAddon(AddonManager *manager) : manager_(manager)
{
    printf("[AI] Addon initialized\n");

    auto *instance = manager_->instance();
    fcitx::readAsIni(config_, "conf/aiaddon.conf");

    dispatcher_.attach(&instance->eventLoop());

    cache_ = AICache(getCacheSize());

    preeditHandler_ = instance->watchEvent(
        EventType::InputContextUpdatePreedit,
        EventWatcherPhase::Default,
        [this](Event &event)
        {
            auto *ic = static_cast<InputContextEvent &>(event).inputContext();
            onPreeditUpdate(ic);
        });
}

// ==================== 配置读取 ====================

std::string AIAddon::getApiKey()
{
    return config_.apiKey.value();
}

int AIAddon::getTimeout()
{
    try {
        return std::stoi(config_.timeout.value());
    } catch (...) {
        return 10;
    }
}

int AIAddon::getDebounceTime()
{
    return config_.debounceTime.value();
}

int AIAddon::getMinimumLength()
{
    return config_.minimumLength.value();
}

int AIAddon::getCacheSize()
{
    return config_.cacheSize.value();
}

// ==================== 拼音获取 ====================

std::string AIAddon::getPinyin(fcitx::InputContext *ic)
{
    return ic->inputPanel().clientPreedit().toString();
}

// ==================== 候选词操作 ====================

void AIAddon::insertCandidate(fcitx::InputContext *ic, int position,
                              const std::string &text, std::function<void()> callback)
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

void AIAddon::insertAICandidate(fcitx::InputContext *ic, const std::string &answer)
{
    insertCandidate(ic, config_.insertposition.value(), "🤖AI: " + answer,
        [icRef = ic->watch(), answer]()
        {
            auto ic = icRef.get();
            if (ic)
            {
                printf("[AI] Commit: %s\n", answer.c_str());
                ic->commitString(answer);
            }
        });
}

// ==================== 请求相关 ====================

std::string AIAddon::buildPrompt(const std::string &pinyin)
{
    std::string prompt = config_.prompt.value();
    size_t pos = prompt.find("{input}");
    if (pos != std::string::npos)
    {
        prompt.replace(pos, 7, pinyin);
    }
    return prompt;
}

bool AIAddon::shouldRequest(const std::string &pinyin)
{
    if (!config_.enabled.value())
        return false;

    if (pinyin.length() < static_cast<size_t>(getMinimumLength()))
        return false;

    return true;
}

void AIAddon::doRequest(const std::string &pinyin, fcitx::InputContext *ic)
{
    static std::atomic<int> debounceId{0};

    int id = ++debounceId;
    int debounceMs = getDebounceTime();

    auto icRef = ic->watch();
    std::string apiKey = getApiKey();
    std::string apiUrl = config_.apiUrl.value();
    std::string model = config_.model.value();
    std::string promptText = buildPrompt(pinyin);
    int timeout = getTimeout();

    printf("[AI] Input: %s | id=%d\n", pinyin.c_str(), id);

    std::thread([this, id, debounceMs, apiKey, apiUrl, model, promptText, timeout, pinyin, icRef]() mutable
    {
        printf("[AI] Debounce: id=%d (%dms)\n", id, debounceMs);

        std::this_thread::sleep_for(std::chrono::milliseconds(debounceMs));

        if (id != debounceId)
        {
            printf("[AI] Canceled: id=%d (latest=%d)\n", id, (int)debounceId.load());
            return;
        }

        printf("[AI] Request: id=%d, pinyin=%s\n", id, pinyin.c_str());

        if (apiKey.empty())
        {
            printf("[AI] Error: API Key empty\n");
            return;
        }

        std::string body = buildRequestBody(promptText, model);

        std::vector<std::pair<std::string, std::string>> headers = {
            {"Authorization", "Bearer " + apiKey},
            {"Content-Type", "application/json"}
        };

        std::string responseStr = httpPost(apiUrl, body, headers, timeout);

        if (responseStr.empty())
        {
            printf("[AI] Failed: id=%d\n", id);
            return;
        }

        std::string answer = parseAnswer(responseStr);

        if (answer.empty())
        {
            printf("[AI] No answer: id=%d\n", id);
            return;
        }

        printf("[AI] Answer: %s (id=%d)\n", answer.c_str(), id);

        dispatcher_.schedule([this, pinyin, answer, icRef, id]()
        {
            auto ic = icRef.get();
            if (!ic)
            {
                printf("[AI] IC invalid: id=%d\n", id);
                return;
            }

            cache_.put(pinyin, answer);
            insertAICandidate(ic, answer);
            printf("[AI] Inserted: %s (id=%d)\n", answer.c_str(), id);
        });

    }).detach();
}

// ==================== 主入口 ====================

void AIAddon::onPreeditUpdate(fcitx::InputContext *ic)
{
    std::string pinyin = getPinyin(ic);

    printf("[Preedit] %s\n", pinyin.c_str());

    if (!shouldRequest(pinyin))
        return;

    std::string cached = cache_.get(pinyin);
    if (!cached.empty())
    {
        printf("[AI] Cache hit: %s -> %s\n", pinyin.c_str(), cached.c_str());
        insertAICandidate(ic, cached);
        return;
    }

    doRequest(pinyin, ic);
}

// ==================== 配置接口 ====================

const fcitx::Configuration *AIAddon::getConfig() const
{
    return &config_;
}

void AIAddon::setConfig(const fcitx::RawConfig &config)
{
    config_.load(config, true);
    fcitx::safeSaveAsIni(config_, "conf/aiaddon.conf");
}

// ==================== Factory ====================

AddonInstance *AIAddonFactory::create(AddonManager *manager)
{
    return new AIAddon(manager);
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::AIAddonFactory)
