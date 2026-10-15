#ifndef FCITX5_RIME_CONFIG_THEME_NATIVE_H_
#define FCITX5_RIME_CONFIG_THEME_NATIVE_H_

#include <QColor>
#include <QHash>
#include <QString>

// native（系统配色）方案的**烘焙**两套色。
//
// macOS 上 native 由 AppKit 语义色动态解析（windowBackgroundColor /
// selectedTextBackgroundColor / labelColor / secondaryLabelColor）；其它平台没有
// 对应的动态通道，于是 2026-09 在 macOS 26 上实测后固化为常量，且不跟随用户
// 自定义强调色 —— 三端一致。常量沿用 Windows 侧 include/BangkeNativeTheme.h，
// 语义是 AARRGGBB。
//
// 这里是**兜底**：桌面自己能给颜色时（Omarchy 的 colors.toml/shell.toml，
// 见 platformtheme.h）优先用桌面的，这套常量是「桌面没得读」时的结果。
namespace bangke {

struct NativePalette {
  quint32 back;
  quint32 text;
  quint32 candidateText;
  quint32 hilitedText;
  quint32 hilitedCandidateText;
  quint32 hilitedCandidateBack;
  quint32 label;
  quint32 comment;
  quint32 hilitedLabel;
  quint32 hilitedComment;
};

inline constexpr NativePalette kNativeLight{
    0xFFFFFFFF, 0xD9000000, 0xD9000000, 0xD9000000, 0xD9000000, 0xFFB3D7FF,
    0x80000000, 0x80000000, 0x80000000, 0x80000000};

inline constexpr NativePalette kNativeDark{
    0xFF1E1E1E, 0xD9FFFFFF, 0xD9FFFFFF, 0xD9FFFFFF, 0xD9FFFFFF, 0xFF3F638B,
    0x8CFFFFFF, 0x8CFFFFFF, 0x8CFFFFFF, 0x8CFFFFFF};

inline QColor argb(quint32 v) {
  return QColor(int((v >> 16) & 0xFF), int((v >> 8) & 0xFF), int(v & 0xFF),
                int((v >> 24) & 0xFF));
}

// 以 yaml 方案键的形态给出颜色表，让 native 与普通方案走同一条解析路径。
inline QHash<QString, QColor> bakedNativeSchemeColors(bool dark) {
  const NativePalette& p = dark ? kNativeDark : kNativeLight;
  return {
      {QStringLiteral("back_color"), argb(p.back)},
      {QStringLiteral("text_color"), argb(p.text)},
      {QStringLiteral("candidate_text_color"), argb(p.candidateText)},
      {QStringLiteral("hilited_text_color"), argb(p.hilitedText)},
      {QStringLiteral("hilited_candidate_text_color"), argb(p.hilitedCandidateText)},
      {QStringLiteral("hilited_candidate_back_color"), argb(p.hilitedCandidateBack)},
      {QStringLiteral("label_color"), argb(p.label)},
      {QStringLiteral("comment_text_color"), argb(p.comment)},
      {QStringLiteral("hilited_candidate_label_color"), argb(p.hilitedLabel)},
      {QStringLiteral("hilited_comment_text_color"), argb(p.hilitedComment)},
  };
}

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_NATIVE_H_
