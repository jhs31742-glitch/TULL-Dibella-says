#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "PrismaUI_API.h"
#include "Panel.h"
#include "HudSession.h"

#include <array>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <string_view>

namespace tds::panel {
namespace {

using PRISMA_UI_API::IVPrismaUI1;
IVPrismaUI1* api{};
PrismaView view{};
PrismaView hud{};
bool hudReady{};
HudSession hudSession;
auto& hudEnabled=hudSession.enabled;
auto& hudLayout=hudSession.layout;
std::uint64_t uiRevision{};
std::string uiLanguage="ko";
bool opening{};
bool focusRequested{};
bool ready{};
std::string (*makeSnapshot)(){};
void (*onAction)(Action, unsigned, std::string_view){};
std::atomic_uint64_t panelGeneration{};
void (*writeLog)(const char*){};
std::array<bool, 256> held{};
unsigned key = 34;
bool shift = true;
bool control{};
bool alt{};
std::atomic_bool captureHotkey{};

void loadHotkey()
{
    const auto path = std::filesystem::absolute(L"Data/SKSE/Plugins/SexLabLifetimeProgression.ini");
    const auto setting = [&](const wchar_t* name, int fallback) {
        return GetPrivateProfileIntW(L"Hotkey", name, fallback, path.c_str());
    };
    const auto configured = setting(L"Key", 34);
    if (configured > 0 && configured < held.size() &&
        configured != 42 && configured != 54 && configured != 29 && configured != 157 &&
        configured != 56 && configured != 184) {
        key = configured;
    }
    shift = setting(L"Shift", 1) != 0;
    control = setting(L"Ctrl", 0) != 0;
    alt = setting(L"Alt", 0) != 0;
}

bool supportedHotkey(unsigned code)
{
    return (code>=2 && code<=11) || (code>=16 && code<=25) ||
        (code>=30 && code<=38) || (code>=44 && code<=50) ||
        (code>=59 && code<=68) || code==87 || code==88;
}

void sendHotkey()
{
    const auto packed=key+(shift?256u:0)+(control?512u:0)+(alt?1024u:0);
    api->InteropCall(view,"tdsHotkeyChanged",std::to_string(packed).c_str());
}

void acceptHotkey(unsigned packed)
{
    if (!captureHotkey || !api || !view || !ready || !api->IsValid(view) || api->IsHidden(view)) return;
    if (packed>=2048 || !supportedHotkey(packed & 255u)) return;
    captureHotkey=false;
    key=packed & 255u;
    shift=(packed & 256u)!=0;
    control=(packed & 512u)!=0;
    alt=(packed & 1024u)!=0;
    sendHotkey();
    writeLog("TDS_UI status=SESSION_HOTKEY_UPDATED");
}

void show()
{
    opening = false;
    if (!api || !view || !api->IsValid(view)) {
        return;
    }
    api->Show(view);
    api->InteropCall(view,"tdsDismissHudEditor","");
    sendHotkey();
    api->InteropCall(view,"tdsHudPreferences",(std::string(hudEnabled?"1,":"0,")+hudLayout).c_str());
    if (!api->Focus(view, true, false)) {
        api->Hide(view);
        writeLog("TDS_UI status=FOCUS_FAILED");
        return;
    }
    focusRequested = true;
    onAction(Action::Open,0,{});
    publish();
    api->InteropCall(view,"tdsMenuShown","");
}

void domReady(PrismaView loaded)
{
    SKSE::GetTaskInterface()->AddTask([loaded] {
        if (loaded == view && opening) {
            ready = true;
            show();
        }
    });
}

void updateHud()
{
    if (!api) return;
    if (!hudEnabled) {
        if (hud && api->IsValid(hud)) api->Hide(hud);
        return;
    }
    if (!hud || !api->IsValid(hud)) {
        hudReady=false;
        hud=api->CreateView("tds/hud.html",[](PrismaView loaded) {
            SKSE::GetTaskInterface()->AddTask([loaded] {
                if (loaded!=hud) return;
                hudReady=true;
                updateHud();
            });
        });
        return;
    }
    if (hudReady) {
        api->InteropCall(hud,"tdsHudLayout",hudLayout.c_str());
        tickHud();
        publish();
    }
}

void command(const char* input)
{
    if (!input) {
        return;
    }
    const std::string_view action(input);
    if (action=="ui-language:ko" || action=="ui-language:en") {
        const auto generation=panelGeneration.load();
        const std::string language=action=="ui-language:en"?"en":"ko";
        SKSE::GetTaskInterface()->AddTask([generation,language] {
            if(generation==panelGeneration.load() && api && view && api->IsValid(view)) {
                uiLanguage=language;
                if(hud && hudReady && api->IsValid(hud)) api->InteropCall(hud,"tdsHudLanguage",uiLanguage.c_str());
            }
        });
    } else if (action=="snapshot") {
        const auto generation=panelGeneration.load();
        SKSE::GetTaskInterface()->AddTask([generation] {
            if(generation==panelGeneration.load() && api && view && ready && api->IsValid(view) && !api->IsHidden(view)) publish();
        });
    } else if (action.starts_with("snapshot-applied:") || action.starts_with("ui-error:")) {
        if (action.size()>180) return;
        std::string entry="TDS_UI_ACK ";
        for (const unsigned char c:action) if(c>=32 && c<127) entry.push_back(static_cast<char>(c));
        if (writeLog) writeLog(entry.c_str());
    } else if (action == "close") {
        SKSE::GetTaskInterface()->AddTask([] { close(); });
    } else if (action=="hotkey-begin" || action=="hotkey-cancel") {
        SKSE::GetTaskInterface()->AddTask([begin=action=="hotkey-begin"] {
            captureHotkey=begin && api && view && ready && api->IsValid(view) && !api->IsHidden(view);
        });
    } else if (action.starts_with("hotkey:")) {
        const auto payload=action.substr(7);
        unsigned packed{};
        const auto [end,error]=std::from_chars(payload.data(),payload.data()+payload.size(),packed);
        if (error!=std::errc{} || end!=payload.data()+payload.size()) return;
        SKSE::GetTaskInterface()->AddTask([packed] { acceptHotkey(packed); });
    } else if (action=="hud-on" || action=="hud-off" || action.starts_with("hud:")) {
        if (action.size()>32) return;
        const auto generation=panelGeneration.load();
        SKSE::GetTaskInterface()->AddTask([text=std::string(action),generation] {
            if (generation!=panelGeneration.load() || !api || !view || !ready || !api->IsValid(view) || api->IsHidden(view)) return;
            if (text=="hud-on" || text=="hud-off") hudEnabled=text=="hud-on";
            else {
                auto payload=std::string_view(text).substr(4);
                std::array<unsigned,5> fields{};
                for (auto& field : fields) {
                    if (payload.empty()) return;
                    const auto [end,error]=std::from_chars(payload.data(),payload.data()+payload.size(),field);
                    if (error!=std::errc{}) return;
                    const bool last=&field==&fields.back();
                    if (last ? end!=payload.data()+payload.size() : end==payload.data()+payload.size() || *end!=',') return;
                    payload.remove_prefix(end-payload.data()+(last ? 0 : 1));
                }
                if (fields[0]>1 || fields[1]>100 || fields[2]>100 || fields[3]<50 || fields[3]>160 || fields[4]>1) return;
                hudLayout=text.substr(4);
            }
            updateHud();
        });
    } else if (action=="aura-on" || action=="aura-off") {
        const auto type=action=="aura-on" ? Action::AuraOn : Action::AuraOff;
        const auto generation=panelGeneration.load();
        SKSE::GetTaskInterface()->AddTask([type,generation] {
            if (generation==panelGeneration.load()) onAction(type,0,{});
        });
    } else if (action.starts_with("gear-grant:")) {
        if(action.size()!=12 || action.back()<'0' || action.back()>'3') return;
        const auto group=static_cast<unsigned>(action.back()-'0');
        const auto generation=panelGeneration.load();
        SKSE::GetTaskInterface()->AddTask([group,generation] {
            if(generation==panelGeneration.load() && api && view && ready && api->IsValid(view) && !api->IsHidden(view) && api->HasFocus(view)) onAction(Action::GearGrant,group,{});
        });
    } else if (action == "refresh") {
        SKSE::GetTaskInterface()->AddTask([] { onAction(Action::Refresh,0,{}); });
    } else if (action == "hardcore-on" || action == "hardcore-off") {
        const auto type=action=="hardcore-on" ? Action::HardcoreOn : Action::HardcoreOff;
        const auto generation=panelGeneration.load();
        SKSE::GetTaskInterface()->AddTask([type,generation] {
            if (generation==panelGeneration.load()) onAction(type,0,{});
        });
    } else if (action == "nodes-on" || action == "nodes-off" ||
               action == "nodes-reset-confirmed" || action.starts_with("node-buy:") ||
               (action=="qa-enter" || action.starts_with("qa-enter:")) || action.starts_with("qa:")) {
        if (action.size()>160) return;
        const auto purchase=parseNodePurchase(action);
        if (action.starts_with("node-buy:") && !purchase) return;
        const auto type=action=="nodes-on" ? Action::NodesOn : action=="nodes-off" ? Action::NodesOff :
            action=="nodes-reset-confirmed" ? Action::NodesReset : (action=="qa-enter" || action.starts_with("qa-enter:")) ? Action::DeveloperEnter :
            action.starts_with("qa:") ? Action::DeveloperInput : Action::NodeBuy;
        const auto generation=panelGeneration.load();
        const auto payload=type==Action::DeveloperInput ? std::string(action.substr(3)) :
            type==Action::DeveloperEnter && action.starts_with("qa-enter:") ? std::string(action.substr(9)) : std::string{};
        SKSE::GetTaskInterface()->AddTask([type,node=purchase.value_or(0),generation,payload] {
            if (generation==panelGeneration.load() && api && view && ready &&
                api->IsValid(view) && !api->IsHidden(view) && api->HasFocus(view)) {
                onAction(type,node,payload);
            }
        });
    } else if (action == "recheck") {
        SKSE::GetTaskInterface()->AddTask([] { onAction(Action::Recheck,0,{}); });
    }
}

void toggle()
{
    if (!api) {
        api = PRISMA_UI_API::RequestPluginAPI<IVPrismaUI1>();
    }
    if (!api) {
        writeLog("TDS_UI status=PRISMA_V1_UNAVAILABLE");
        return;
    }
    if (view && api->IsValid(view) && !api->IsHidden(view) && ready) {
        close();
        return;
    }
    auto* ui = RE::UI::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (opening || !ui || !player || !player->Is3DLoaded() || ui->GameIsPaused() ||
        api->HasAnyActiveFocus()) {
        return;
    }

    if (view && api->IsValid(view) && ready) {
        show();
        return;
    }

    ready = false;
    opening = true;
    view = api->CreateView("tds/index.html", domReady);
    if (!view) {
        opening = false;
        writeLog("TDS_UI status=CREATE_FAILED");
        return;
    }
    api->RegisterJSListener(view, "tdsCommand", command);
}

class Input final : public RE::BSTEventSink<RE::InputEvent*> {
public:
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* input,
        RE::BSTEventSource<RE::InputEvent*>*) override
    {
        bool pressed{};
        bool escape{};
        unsigned captured{};
        for (auto* event = input ? *input : nullptr; event; event = event->next) {
            const auto* button = event->AsButtonEvent();
            if (button && button->GetDevice()==RE::INPUT_DEVICE::kMouse &&
                button->GetIDCode()==1 && button->IsDown()) {
                const auto generation=panelGeneration.load();
                SKSE::GetTaskInterface()->AddTask([generation] {
                    if(generation==panelGeneration.load() && api && view && ready &&
                       api->IsValid(view) && !api->IsHidden(view)) {
                        api->InteropCall(view,"tdsQaRightClick","");
                    }
                });
            }
            if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }
            const auto code = button->GetIDCode();
            if (code >= held.size()) {
                continue;
            }
            if (button->IsUp()) {
                held[code] = false;
            } else if (button->IsDown()) {
                held[code] = true;
                pressed |= code == key;
                if (captureHotkey && supportedHotkey(code)) captured=code;
                escape |= code==1;
            }
        }
        if (captured) {
            const auto down=[](int vk){return (GetAsyncKeyState(vk) & 0x8000)!=0;};
            const auto packed=captured+(down(VK_SHIFT)?256u:0)+(down(VK_CONTROL)?512u:0)+(down(VK_MENU)?1024u:0);
            SKSE::GetTaskInterface()->AddTask([packed]{acceptHotkey(packed);});
            return RE::BSEventNotifyControl::kContinue;
        }
        if (escape) {
            const auto generation=panelGeneration.load();
            SKSE::GetTaskInterface()->AddTask([generation] {
                if (generation==panelGeneration.load() && api && view && ready &&
                    api->IsValid(view) && !api->IsHidden(view)) {
                    api->InteropCall(view,"tdsNativeEscape","");
                }
            });
        }
        // Use current physical modifier state where Prisma consumed a key-up event.
        if (pressed) {
            const auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
            if (down(VK_SHIFT) == shift && down(VK_CONTROL) == control && down(VK_MENU) == alt) {
                SKSE::GetTaskInterface()->AddTask([] { toggle(); });
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
Input input;

}  // namespace

void install(std::string (*snapshot)(), void (*action)(Action, unsigned, std::string_view), void (*log)(const char*))
{
    makeSnapshot = snapshot;
    onAction = action;
    writeLog = log;
    loadHotkey();
    if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) {
        manager->AddEventSink(&input);
        writeLog("TDS_UI status=INPUT_READY");
    } else {
        writeLog("TDS_UI status=INPUT_UNAVAILABLE");
    }
}

bool hudTickNeeded() { return hudEnabled; }

void tickHud()
{
    if (!api || !hud || !hudReady || !api->IsValid(hud)) return;
    auto* ui=RE::UI::GetSingleton();
    auto* player=RE::PlayerCharacter::GetSingleton();
    bool visible=hudEnabled && ui && player && player->Is3DLoaded() &&
        ui->IsShowingMenus() && !ui->GameIsPaused() && !ui->IsApplicationMenuOpen();
    if (visible) {
        for (const auto& menu : ui->menuStack) {
            if (menu && menu->OnStack() && !menu->AlwaysOpen()) {
                visible=false;
                break;
            }
        }
    }
    if (visible && api->IsHidden(hud)) {
        api->Show(hud);
        const auto data=makeSnapshot();
        api->InteropCall(hud,"tdsHudLanguage",uiLanguage.c_str());
        api->InteropCall(hud,"tdsHudSnapshot",data.c_str());
    } else if (!visible && !api->IsHidden(hud)) {
        api->Hide(hud);
    }
}

void publish()
{
    if (!api || !makeSnapshot) return;
    if (hudEnabled && !hud) {
        updateHud();
    }
    const bool panelVisible=view && ready && api->IsValid(view) && !api->IsHidden(view);
    const bool hudVisible=hudEnabled && hud && hudReady && api->IsValid(hud) && !api->IsHidden(hud);
    if (!panelVisible && !hudVisible) return;
    const auto data=makeSnapshot();
    if (panelVisible) {
        const auto revision=++uiRevision;
        if (!data.empty() && data.front()=='{') {
            const auto framed=std::string("{\"uiRevision\":")+std::to_string(revision)+","+data.substr(1);
            api->InteropCall(view,"tdsSnapshot",framed.c_str());
            if (writeLog) writeLog(("TDS_UI_SNAPSHOT revision="+std::to_string(revision)).c_str());
        }
    }
    if (hudVisible) {
        api->InteropCall(hud,"tdsHudLanguage",uiLanguage.c_str());
        api->InteropCall(hud,"tdsHudSnapshot",data.c_str());
    }
}

void resetSession()
{
    close();
    hudSession.reset();
    if (writeLog) writeLog("TDS_HUD session_reset enabled=0 layout=DEFAULT");
    if (api && view && api->IsValid(view)) api->Destroy(view);
    view=0;
    ready=false;
    if (api && hud && api->IsValid(hud)) api->Destroy(hud);
    hud=0;
    hudReady=false;
}

void close()
{
    ++panelGeneration;
    captureHotkey=false;
    if (api && view && api->IsValid(view)) {
        if (ready && !api->IsHidden(view)) api->InteropCall(view,"tdsDismissHudEditor","");
        if (focusRequested || api->HasFocus(view)) {
            api->Unfocus(view);
        }
        api->Hide(view);
        if (opening) {
            api->Destroy(view);
            view = 0;
            ready = false;
        }
    }
    focusRequested = false;
    opening = false;
}

}
