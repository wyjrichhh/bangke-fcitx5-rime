/*
 * SPDX-FileCopyrightText: 2026 RIME Developers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_RIME_RESERVEDPROPERTY_H_
#define _FCITX_RIME_RESERVEDPROPERTY_H_

#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fcitx::reservedproperty {

// Reserved property keys: cross-frontend contract between librime plugins and
// frontends (rime/squirrel#1124). A plugin calls ctx->set_property("_<key>",
// "<value>"), librime forwards it as a notification of type "property" whose
// value is "<key>=<value>". Only keys in the leading "_" namespace are part of
// this protocol; unrecognised "_*" keys are ignored so the table can grow.
inline constexpr std::string_view kRefreshUI = "_refresh_ui";
inline constexpr std::string_view kCommentHighlight = "_comment_highlight";
inline constexpr std::string_view kCommentWarning = "_comment_warning";

// AI 插件自己的键空间，刻意不带 "_" 前缀 —— 它镜像的是 schema 里的配置键名，
// 不是这份跨前端协议的一部分。值 "1" 表示有一次推理在途，结果还没落进缓存。
// 同样靠 "property" 通知送达，所以 splitKeyValue() 之外要单独认一次。
inline constexpr std::string_view kAIPredictPending = "ai_predict/pending";

// Splits a "property" notification value at the first '='. Returns the
// (key, value) pair only when the key lives in the reserved "_" namespace.
inline std::optional<std::pair<std::string_view, std::string_view>>
splitKeyValue(std::string_view message) {
    if (message.empty() || message.front() != '_') {
        return std::nullopt;
    }
    const auto eq = message.find('=');
    if (eq == std::string_view::npos) {
        return std::nullopt;
    }
    return std::make_pair(message.substr(0, eq), message.substr(eq + 1));
}

// Parses the comma-separated non-negative index list carried by
// _comment_highlight / _comment_warning. Accepts both the bare-list shorthand
// ("0,2") and the explicit form ("indices=0,2"); malformed and negative
// entries are skipped. Mirrors ReservedPropertyValue::indices() in Squirrel.
inline std::unordered_set<int> parseIndices(std::string_view value) {
    constexpr std::string_view prefix = "indices=";
    if (value.size() >= prefix.size() &&
        value.substr(0, prefix.size()) == prefix) {
        value.remove_prefix(prefix.size());
    }
    std::unordered_set<int> out;
    size_t pos = 0;
    while (pos <= value.size()) {
        const auto comma = value.find(',', pos);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        std::string_view part = value.substr(pos, end - pos);
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
            part.remove_prefix(1);
        }
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
            part.remove_suffix(1);
        }
        if (!part.empty()) {
            int n = 0;
            bool valid = true;
            for (const char c : part) {
                if (c < '0' || c > '9') {
                    valid = false;
                    break;
                }
                n = n * 10 + (c - '0');
            }
            if (valid) {
                out.insert(n);
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return out;
}

} // namespace fcitx::reservedproperty

#endif // _FCITX_RIME_RESERVEDPROPERTY_H_
