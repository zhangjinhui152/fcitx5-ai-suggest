#ifndef FCITX5_AI_ADDON_H
#define FCITX5_AI_ADDON_H

#include <memory>
#include <string>
#include <functional>
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

    class AIAddon : public AddonInstance
    {
    public:
        std::string buffer;

        AIAddon(AddonManager *manager);

    private:
        AddonManager *manager_;
        std::string lastPinyin;
        bool zhihuInserted = false;
        std::unique_ptr<HandlerTableEntry<EventHandler>> handler_;
        std::unique_ptr<HandlerTableEntry<EventHandler>> preeditHandler_;
        EventDispatcher dispatcher_;
        AIAddonConfig config_; // 宏生成的类

        bool hasInserted = false;
        std::string lastInsertedPinyin;

        void printCandidates(fcitx::InputContext *ic);
        std::string getPinyin(fcitx::InputContext *ic);
        std::string getApiKey();

        std::string getConfigKeyValue(std::string key);

        void insertCandidate(fcitx::InputContext *ic, int position, const std::string &text, std::function<void()> callback);
        void insertAICandidate(fcitx::InputContext *ic, const std::string &pinyin);
        void fetchAIAsyncDebug(const std::string &pinyin, fcitx::InputContext *ic);

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
