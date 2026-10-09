#ifndef FCITX5_RIME_CONFIG_THEME_PANELSPEC_H_
#define FCITX5_RIME_CONFIG_THEME_PANELSPEC_H_

#include <QColor>
#include <QFont>
#include <QImage>
#include <QMargins>
#include <QString>

#include "scheme.h"

namespace bangke {

// 候选面板的**唯一**渲染规格。两个消费者共用它：
//   1) themewriter 序列化成 classicui 主题（theme.conf + 九宫格 PNG + classicui.conf）
//   2) 设置页的预览直接画它
// 这是「预览与实机不会各走一套」的保证 —— 预览画的是这里的值，而这里的值
// 就是写进主题的那些，不是另算一遍 mac 的 style。
struct PanelSpec {
  QColor panelBackground;
  QColor panelBorder;             // 无效色 = 不画边框
  QColor normalText;              // 候选文字 + 预编辑（classicui 同一个键）
  QColor highlightText;           // HighLight 标记文字的前景（AI 强调注释）
  QColor highlightFill;           // HighLight 标记文字的背景（透明 = 不铺底）
  QColor highlightCandidateText;
  QColor highlightBackground;     // 高亮候选底

  int cornerRadius = 0;           // 面板圆角（像素）
  int highlightCornerRadius = 0;  // 高亮块圆角（像素）
  int borderWidth = 0;            // ≤0 不画
  int shadowSize = 0;             // 高亮块的柔影（像素）
  QMargins contentMargin;         // 面板内边距
  QMargins textMargin;            // 每段文字四周（决定行高与文字间距）
  QMargins backgroundMargin;      // 面板九宫格切片
  QMargins highlightMargin;       // 高亮九宫格切片 = 相对文字的外扩量

  QFont font;
  bool vertical = false;
  bool fullWidthHighlight = false;
  bool showPaging = false;
  bool inlinePreedit = true;

  int shadowExtent() const { return shadowSize > 0 ? (shadowSize * 3 + 1) / 2 : 0; }
};

// mac 的 pt 与 classicui 的像素：主题边距按 96dpi 基线写，
// 运行时由 fcitx5 的 PerScreenDPI 缩放。
int ptToPx(double pt);

// 圆角半径的安全上限（pt）。
//
// 九宫格的切片宽度 = ceil(1.4×半径) + 边框外扩，而 classicui 的 paintTile 在
// 「面板尺寸 < 切片总和」时会算出负的缩放比，把面板画成碎片 —— 实测过：圆角 13pt
// + 面板只显示一两行时，候选窗变成「一条横条 + 一个灰块 + 大片空白」。
// 面板最小高度约 = 两行边距 + 一行文字，据此反推半径上限，并把它作为设置页滑杆的
// 最大值（不是钳死用户的值，而是根本不让他设到会坏的程度）。
int maxSafeCornerRadiusPt();

PanelSpec buildPanelSpec(const SchemeColors& colors, const StyleOptions& style,
                         const QFont& font);

// 面板底图 / 高亮块图。落盘与预览共用这两个函数 —— 预览画的图和真面板
// 用的是同一份绘制代码，不存在"预览一套、实机一套"。
QImage panelBackgroundImage(const PanelSpec& spec);
QImage panelHighlightImage(const PanelSpec& spec);

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_PANELSPEC_H_
