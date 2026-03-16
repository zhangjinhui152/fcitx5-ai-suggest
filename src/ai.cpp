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
namespace fcitx
{
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
            printf("AI ver1.7 addon hello\n");

            auto *instance = manager_->instance();

            handler_ = instance->watchEvent(
                EventType::InputContextKeyEvent,
                EventWatcherPhase::PreInputMethod,
                [this](Event &event)
                {
                    auto &keyEvent = static_cast<KeyEvent &>(event);
                    auto key = keyEvent.key().toString();
                    auto ic = keyEvent.inputContext();

                    printf("===== Debug Info =====\n");

                    // 打印原始按键
                    printf("Key pressed: %s\n", key.c_str());

                    // 打印 preedit 内容
                    std::string pinyin = getPinyin(ic);
                    printf("Preedit string: '%s'\n", pinyin.c_str());
                    printf("Preedit length: %zu\n", pinyin.length());

                    // 检查 preedit 是否真的存在
                    auto &preedit = ic->inputPanel().preedit();
                    printf("Preedit empty: %d\n", preedit.empty());
                    printf("Preedit text: '%s'\n", preedit.toString().c_str());

                    // 打印 buffer 内容
                    printf("Buffer: '%s'\n", buffer.c_str());

                    if (keyEvent.isRelease())
                    {
                        return;
                    }

                    printf("this->getPinyin(ic) \n");
                    printf(this->getPinyin(ic).c_str());
                    if (this->getPinyin(ic) == "zhihu")
                    {
                        printf("this->getPinyin(ic) \n");
                        this->printCandidates(ic);
                    }
                });
        }

    private:
        AddonManager *manager_;
        std::unique_ptr<HandlerTableEntry<EventHandler>> handler_;
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
        // 有些输入法可能把拼音存在其他地方
        // std::string getPinyin(fcitx::InputContext *ic)
        // {
        //     // 方法1: 尝试获取整个输入的字符串
        //     auto &inputPanel = ic->inputPanel();
        //     printf("ClientPreedit empty: %d\n", inputPanel.clientPreedit().empty());
        //     printf("ClientPreedit: '%s'\n", inputPanel.clientPreedit().toString().c_str());

        //     // 方法2: 获取候选词列表的第一个词（如果有）
        //     auto candidateList = inputPanel.candidateList();
        //     if (candidateList && candidateList->size() > 0)
        //     {
        //         printf("First candidate: %s\n", candidateList->candidate(0).text().toString().c_str());
        //     }

        //     return inputPanel.preedit().toString();
        // }
        std::string getPinyin(fcitx::InputContext *ic)
        {
            auto &clientPreedit = ic->inputPanel().clientPreedit();
            if (!clientPreedit.empty())
            {
                return clientPreedit.toString();
            }
            return "";
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