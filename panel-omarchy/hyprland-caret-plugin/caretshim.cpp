// 把 Hyprland 内存里的"输入光标矩形"通过 hyprctl 暴露出来 —— 给自绘候选面板用。
//
// 为什么只能在这里做：原生 Wayland 应用的光标矩形（text-input 的 set_cursor_rectangle）
// 是 **app → 合成器** 直发的：合成器只把它转给输入法（input-method-v2 的 popup surface
// 上那个 text_input_rectangle 事件），fcitx5 / 面板都是另一个进程，谁都拿不到。
// hyprctl 也只有鼠标位置（cursorpos），没有文本光标。所以要么在合成器进程里读，
// 要么永远只能猜。这个插件就是那个"在合成器里读"。
//
// 坐标：和 Hyprland 摆输入法弹窗用的是同一套 —— 表面局部矩形 + 该表面的全局位置，
// 结果是对齐到全局逻辑像素（和 hyprctl -j activewindow 给的窗口几何同一套）。
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/version.h>

#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/input/TextInput.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>

#include <hyprland/src/desktop/rule/Engine.hpp>
#include <hyprland/src/desktop/rule/layerRule/LayerRule.hpp>
#include <format>
#include <stdexcept>

inline HANDLE PHANDLE = nullptr;

static std::string caretJSON(eHyprCtlOutputFormat format, std::string request) {
    const auto TI = g_pInputManager ? g_pInputManager->m_relay.getFocusedTextInput() : nullptr;
    if (!TI || !TI->hasCursorRectangle())
        return R"({"ok":false})";

    const CBox local = TI->cursorBox();
    const auto SURF  = TI->focusedSurface();
    const auto WLS   = SURF ? Desktop::View::CWLSurface::fromResource(SURF) : nullptr;
    const CBox origin = WLS ? WLS->getSurfaceBoxGlobal().value_or(CBox{0, 0, 0, 0}) : CBox{0, 0, 0, 0};

    return std::format(R"({{"ok":true,"x":{},"y":{},"w":{},"h":{}}})",
                       (int)(origin.x + local.x), (int)(origin.y + local.y), (int)local.w, (int)local.h);
}

static SP<Desktop::Rule::CLayerRule> panelBlurRule;
static SP<Desktop::Rule::CLayerRule> panelStableRule;
static std::string panelBlur(eHyprCtlOutputFormat, std::string request) {
    using namespace Desktop::Rule;
    const bool enabled = request.ends_with(" 1");
    if (panelBlurRule) ruleEngine()->unregisterRule(panelBlurRule);
    panelBlurRule = makeShared<CLayerRule>("bangke-panel-backdrop");
    panelBlurRule->registerMatch(RULE_PROP_NAMESPACE,"^bangke-panel$");
    const auto result = panelBlurRule->addEffect(LAYER_RULE_EFFECT_BLUR,enabled?"on":"off");
    if (!result) return R"({"ok":false})";
    ruleEngine()->registerRule(SP<IRule>{panelBlurRule});
    ruleEngine()->updateAllRules();
    return enabled ? R"({"ok":true,"enabled":true})" : R"({"ok":true,"enabled":false})";
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (panelBlurRule) Desktop::Rule::ruleEngine()->unregisterRule(panelBlurRule);
    panelBlurRule.reset();
    if (panelStableRule) Desktop::Rule::ruleEngine()->unregisterRule(panelStableRule);
    panelStableRule.reset();
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    // Installed headers may have advanced while the running compositor has not.
    // Refuse that mismatch before touching any compositor objects.
    if (std::string(__hyprland_api_get_hash()) != __hyprland_api_get_client_hash())
        throw std::runtime_error("Bangke caret plugin: restart Hyprland after its upgrade before loading this plugin");
    PHANDLE = handle;
    using namespace Desktop::Rule;
    panelStableRule = makeShared<CLayerRule>("bangke-panel-no-animation");
    panelStableRule->registerMatch(RULE_PROP_NAMESPACE,"^bangke-panel$");
    if (!panelStableRule->addEffect(LAYER_RULE_EFFECT_NO_ANIM,"on"))
        throw std::runtime_error("Bangke: cannot disable candidate surface animations");
    ruleEngine()->registerRule(SP<IRule>{panelStableRule});
    ruleEngine()->updateAllRules();

    HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{"bangkeimcaret", true, caretJSON});

    HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{"bangkeimblur", false, panelBlur});
    HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{"bangkeimidentity", true,
        [](eHyprCtlOutputFormat, std::string) { return R"({"ok":true,"api":3,"blur":true,"stable":true})"; }});

    return {"bangke-im-caret", "把输入光标的矩形暴露给候选面板", "bangke", "0.3"};
}
