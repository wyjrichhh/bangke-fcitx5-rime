#include "panelspec.h"

#include <QtMath>

#include "ninegrid.h"

namespace bangke {
namespace {

// 片段文字四周的基础留白（像素）。classicui 的 TextMargin 会同时影响面板
// 尺寸与 label/candidate/comment 之间的间隙。
QColor withAlpha(QColor color, double factor) {
  color.setAlpha(qBound(0, qRound(color.alpha() * factor), 255));
  return color;
}

QMargins uniform(int v) { return QMargins(v, v, v, v); }

}  // namespace

int ptToPx(double pt) { return qRound(pt * 96.0 / 72.0); }

namespace {
// 面板能小到什么程度：一行文字的高度 + 上下两条边距（见 panelspec 里的 cornerBand）。
// 保守取小一点，宁可能设的圆角略小，也不要出现画碎的面板。
constexpr int kMinPanelExtent = 44;
}  // namespace

int maxSafeCornerRadiusPt() {
  for (int pt = 30; pt > 0; --pt) {
    const int radius = ptToPx(pt);
    // 最坏情况：边框拉到上限时的外扩
    const int inset = ninegrid::shapeInset(ptToPx(20), 0);
    if (2 * ninegrid::requiredMargin(radius, inset, 0) <= kMinPanelExtent)
      return pt;
  }
  return 0;
}

PanelSpec buildPanelSpec(const SchemeColors& colors, const StyleOptions& style,
                         const QFont& font) {
  PanelSpec spec;
  spec.font = font;
  spec.vertical = style.textOrientation == QLatin1String("vertical");
  spec.showPaging = style.showPaging;
  spec.inlinePreedit = style.inlinePreedit;
  // classicui 只有一个 Vertical Candidate List 布尔，竖排时把高亮拉满整行 ——
  // 对应 mac 堆叠布局里高亮占满一行的观感。
  spec.fullWidthHighlight = spec.vertical;

  // 透明度只作用在**面板背景**上。
  //
  // mac 的 style/alpha 是整窗不透明度（NSPanel.alphaValue），高亮与背景一起变淡、
  // 相对对比不变；classicui 没有整窗透明度，只能改颜色自己的 alpha —— 而高亮是
  // 画在半透明背景**之上**的：两边都乘 alpha 会让高亮被背景污染
  // （0.6×高亮 + 0.24×背景 + 0.16×桌面），看起来比 mac 暗一截。
  // 所以背景带透明度、高亮与文字保持不透明 —— 这也是 mac 开 translucency 时的观感
  // （底色半透明，高亮仍是实色）。
  spec.panelBackground = withAlpha(colors.back, style.alpha);
  spec.highlightBackground = colors.hilitedCandidateBack;
  spec.panelBorder = colors.border.isValid() ? colors.border : QColor(Qt::transparent);

  spec.normalText = colors.candidateText;
  spec.highlightCandidateText = colors.hilitedCandidateText;
  // accent 只上色不铺底：mac 的强调注释也是只改文字色。
  spec.highlightText = colors.accent;
  spec.highlightFill = QColor(Qt::transparent);

  spec.cornerRadius = ptToPx(qMin(style.cornerRadius, maxSafeCornerRadiusPt()));
  spec.highlightCornerRadius = ptToPx(style.hilitedCornerRadius);
  // mac：borderLineWidth = min(borderHeight, borderWidth)，≤0 就完全不画。
  const int borderPt = qMin(style.borderWidth, style.borderHeight);
  spec.borderWidth = borderPt > 0 ? ptToPx(borderPt) : 0;
  spec.shadowSize = style.shadowSize > 0 ? ptToPx(style.shadowSize) : 0;

  // 关键：**面板的内边距放在单元格里，而不是 contentMargin**。
  //
  // mac / Windows 的模型是「高亮块 = 候选单元格」：高亮贴着面板内缘，边缘那几行
  // 的圆角由面板自己裁出来（Windows 侧为此写了 _SnapHighlightToPanel）。classicui
  // 的高亮外扩量恒等于九宫格切片宽度、每行都一样，所以只要把内边距留在
  // contentMargin 里，高亮就永远够不到上下边缘，看起来是「灰面板圆角 + 方块高亮」。
  //
  // 让 contentMargin = 0、内边距由 textMargin 提供，高亮外扩量就等于单元格尺寸：
  //   第一行的高亮上沿正好压在面板上沿，上圆角被面板形状裁出来；
  //   最后一行同理压在下沿；中间各行就是规规矩矩的方块（mac 的出厂高亮圆角
  //   本来就是 0，中间行也是方块）。
  // 面板**上下各留一条带**，左右不留：高亮永远是一整块矩形。
  //
  // 为什么这么分：classicui 的窗口不是圆角形状（theme.mask() 只用于模糊区域，
  // X11/Wayland 都一样），圆角只来自背景图的透明角，而高亮画在背景之后、没有
  // 任何裁剪拦得住它。于是只有两种活法：
  //   ① 高亮贴到面板上/下沿 → 被圆角裁出缺口，左右侧露出背景（丑）；
  //   ② 上下留一条带 → 高亮碰不到圆角，恒为整块矩形。
  // 用户明确选了 ②。
  //
  // 左右仍然贴边（边缘那一列没有圆角问题），所以高亮是「满宽 + 上下各留一条带」。
  const int lineSpacing = qMax(0, ptToPx(style.lineSpacing));
  // 带高：连续圆角曲线在 x=0 处的高度是 1.4r，但允许亚像素级擦边，
  // 取 0.55r + 2（半径 9px → 7px）就够，肉眼看不到露边。
  const int cornerBand = qCeil(spec.cornerRadius * 0.55) + 2;
  const int pad = qMax(2, lineSpacing / 2);  // 高亮块里文字四周的呼吸位
  spec.contentMargin = QMargins(0, cornerBand, 0, cornerBand);
  spec.textMargin = uniform(pad);
  spec.highlightMargin = uniform(pad);  // 左/右因此正好贴到面板边缘

  const int bgInset = ninegrid::shapeInset(spec.borderWidth, 0);
  spec.backgroundMargin =
      uniform(ninegrid::requiredMargin(spec.cornerRadius, bgInset, 0));

  return spec;
}

QImage panelBackgroundImage(const PanelSpec& spec) {
  const int inset = ninegrid::shapeInset(spec.borderWidth, 0);
  const QColor border =
      spec.borderWidth > 0 ? spec.panelBorder : QColor(Qt::transparent);
  return ninegrid::makeBackgroundImage(spec.cornerRadius, inset,
                                       spec.backgroundMargin.left(),
                                       spec.panelBackground, border);
}

QImage panelHighlightImage(const PanelSpec& spec) {
  const int inset = ninegrid::shapeInset(spec.borderWidth, spec.shadowExtent());
  // 高亮块**不带边框**：边框是面板的属性（mac 侧高亮也没有描边）。带上的话，
  // 边框一宽（比如 3pt=4px）就会把只有几像素的高亮图糊成一块实心色 —— 实测过。
  const QColor border = QColor(Qt::transparent);
  return ninegrid::makeHighlightImage(spec.highlightCornerRadius, inset,
                                      spec.highlightMargin.left(),
                                      spec.highlightBackground,
                                      border, spec.shadowExtent(), true);
}

}  // namespace bangke
