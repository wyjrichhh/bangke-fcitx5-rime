#ifndef FCITX5_RIME_CONFIG_THEME_NINEGRID_H_
#define FCITX5_RIME_CONFIG_THEME_NINEGRID_H_

#include <QColor>
#include <QImage>
#include <QMargins>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

// fcitx5 classicui 的面板底/高亮块都是九宫格图片（`Theme::paintTile`）：
// 四角不缩放、四条边单向拉伸、中心双向拉伸。这里生成那些图，并用同一份
// 绘制代码给设置页的预览用 —— 预览与真机同源是刻意为之。
//
// 只用 PNG：引擎的 loadImage 对非 .png 走 GdkPixbuf，SVG 需要 librsvg2，
// 缺了会静默退化成纯色方角面板且不报错。
namespace bangke::ninegrid {

// mac 的连续圆角（BangkeView.drawSmoothLines）：三次贝塞尔，端点距顶点
// beta=1.4r、控制柄 alpha=0.3r。观感比同半径圆弧"更圆润"，因为曲率从
// 直边平滑渐入。半径过大会被矩形的短边钳住。
QPainterPath continuousRoundedPath(const QRectF& rect, qreal radius);

// 圆角与留白需要的九宫格切片宽度：角区在图上占 1.4r，再加上形状外留的
// 边框/阴影宽度。调用方用它填 theme.conf 的 Margin。
int requiredMargin(int radius, int inset, int shadowExtent);

// 形状外留的宽度：有边框时是半个描边线宽（描边以路径为中心画），
// 加阴影外扩。都是像素。
int shapeInset(int borderWidth, int shadowExtent);

// 生成面板底图（无阴影）。fill 与 border 都可以带 alpha —— classicui 的
// 面板窗口是 ARGB，颜色里的 alpha 真的会透出桌面。
QImage makeBackgroundImage(qreal radius, int inset, int margin, const QColor& fill,
                           const QColor& border);

// 生成高亮块图。mac 的 shadow_size 是高亮候选投在面板上的柔影（不是面板
// 投影），所以阴影烘在这张图里。
QImage makeHighlightImage(qreal radius, int inset, int margin,
                          const QColor& fill, const QColor& border,
                          int shadowExtent, bool shadowDown);

// 复刻 Theme::paintTile：把九宫格图铺到 target 矩形。
void paintTile(QPainter* painter, const QImage& image, const QRectF& target,
               const QMargins& margin);

}  // namespace bangke::ninegrid

#endif  // FCITX5_RIME_CONFIG_THEME_NINEGRID_H_
