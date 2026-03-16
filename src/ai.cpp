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
            printf("AI ver1.6 addon hello\n");

            auto *instance = manager_->instance();

            handler_ = instance->watchEvent(
                EventType::InputContextKeyEvent,
                EventWatcherPhase::PreInputMethod,
                [this](Event &event)
                {
                    auto &keyEvent = static_cast<KeyEvent &>(event);

                    if (keyEvent.isRelease())
                    {
                        return;
                    }
                    auto key = keyEvent.key().toString();

                    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z')
                    {
                        buffer.push_back(key[0]);
                    }
                    else
                    {
                        buffer.clear();
                    }

                    if (buffer == "zhihu")
                    {
                        printf("zhihuDesu\n");
                        // auto *ic = keyEvent.inputContext();
                        // // 使用
                        // auto list = std::make_unique<CommonCandidateList>();
                        // auto candidate = std::make_unique<MyCandidate>("🤖知乎",
                        //                                                [ic]()
                        //                                                { ic->commitString("🤖知乎"); });

                        // list->append(std::move(candidate));
                        // ic->inputPanel().setCandidateList(std::move(list));
                        // ic->updateUserInterface(UserInterfaceComponent::InputPanel);

                        this->printCandidates(keyEvent.inputContext());
                        buffer.clear();
                        keyEvent.filterAndAccept();
                    }

                    printf("key: %s\n", key.c_str());
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
        std::string getPinyin(fcitx::InputContext *ic)
        {
            return ic->inputPanel().preedit().toString();
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