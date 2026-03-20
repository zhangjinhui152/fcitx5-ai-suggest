#ifndef FCITX5_AI_ADDON_H
#define FCITX5_AI_ADDON_H

#include <memory>
#include <string>
#include <functional>
#include <list>
#include <unordered_map>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/candidateaction.h>
#include <fcitx/event.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/inputpanel.h>
#include <fcitx-utils/i18n.h>

namespace fcitx
{
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

    class MyCandidate : public fcitx::CandidateWord
    {
    public:
        MyCandidate(const std::string &text, std::function<void()> cb);
        void select(fcitx::InputContext *) const override;

    private:
        std::function<void()> callback_;
    };

    // LRU 缓存
    class AICache
    {
    public:
        AICache(size_t maxSize = 100) : maxSize_(maxSize) {}
        
        std::string get(const std::string &key);
        void put(const std::string &key, const std::string &value);
        void clear();
        size_t size() const { return cacheMap_.size(); }

    private:
        size_t maxSize_;
        std::list<std::pair<std::string, std::string>> cacheList_;
        std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> cacheMap_;
    };

    class AIAddon : public AddonInstance
    {
    public:
        AIAddon(AddonManager *manager);

    private:
        AddonManager *manager_;
        std::unique_ptr<HandlerTableEntry<EventHandler>> preeditHandler_;
        EventDispatcher dispatcher_;
        AIAddonConfig config_;
        AICache cache_;

        // 配置读取
        std::string getApiKey();
        int getTimeout();
        int getDebounceTime();
        int getMinimumLength();
        int getCacheSize();

        // 拼音获取
        std::string getPinyin(fcitx::InputContext *ic);

        // 候选词操作
        void insertCandidate(fcitx::InputContext *ic, int position, 
                            const std::string &text, std::function<void()> callback);
        void insertAICandidate(fcitx::InputContext *ic, const std::string &answer);

        // 请求相关
        std::string buildPrompt(const std::string &pinyin);
        bool shouldRequest(const std::string &pinyin);
        void doRequest(const std::string &pinyin, fcitx::InputContext *ic);
        void handleResponse(const std::string &pinyin, const std::string &response,
                           fcitx::TrackableObjectReference<fcitx::InputContext> icRef);

        // 主入口
        void onPreeditUpdate(fcitx::InputContext *ic);

        // 配置接口
        const fcitx::Configuration *getConfig() const override;
        void setConfig(const fcitx::RawConfig &config) override;
    };

    class AIAddonFactory : public AddonFactory
    {
    public:
        AddonInstance *create(AddonManager *manager) override;
    };

} // namespace fcitx

#endif // FCITX5_AI_ADDON_H
