#include "ninegrid.h"

#include <QtMath>

namespace bangke::ninegrid {
namespace {

// 图上中央留的纯色区（会被拉伸）。≥2 是为了避免边缘像素参与插值。
// 九宫格的「中心切片」宽度。原来只有 2px —— 在 X11 上勉强能画，但在 Wayland
// （面板是 layer surface，缓冲区要经过合成器缩放）上会导致面板整个画不出来：
// 实测 Hyprland 下带 Image 的主题面板消失、去掉 Image 改纯色就正常。
// 16px 让中心切片有足够余量，圆角尺寸不受影响（圆角只由 margin 决定）。
constexpr int kCenter = 16;

// 阴影的同心环层数上限（层数越多越平滑，但生成越慢）。
constexpr int kMaxShadowRings = 24;

QImage newImage(int size) {
  QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  return image;
}

void drawShape(QPainter* painter, const QRectF& shape, qreal radius,
               const QColor& fill, const QColor& border, int borderWidth) {
  const QPainterPath path = continuousRoundedPath(shape, radius);
  if (fill.alpha() > 0) painter->fillPath(path, fill);
  if (border.alpha() > 0 && borderWidth > 0) {
    QPen pen(border);
    pen.setWidthF(borderWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
  }
}

}  // namespace

QPainterPath continuousRoundedPath(const QRectF& rect, qreal radius) {
  QPainterPath path;
  const qreal limit = qMin(rect.width(), rect.height()) / 2.0;
  qreal r = qBound(0.0, radius, limit);
  if (r <= 0.5) {
    path.addRect(rect);
    return path;
  }

  // 端点距顶点 beta、控制柄 alpha；半径大到会把角区撑过短边时按比例收，
  // 保持角的形状不变（只变小）。
  const qreal beta = qMin(1.4 * r, limit);
  const qreal alpha = 0.3 * r * (beta / (1.4 * r));
  const qreal l = rect.left(), t = rect.top(), rr = rect.right(),
              b = rect.bottom();

  path.moveTo(l + beta, t);
  path.lineTo(rr - beta, t);
  path.cubicTo(rr - alpha, t, rr, t + alpha, rr, t + beta);
  path.lineTo(rr, b - beta);
  path.cubicTo(rr, b - alpha, rr - alpha, b, rr - beta, b);
  path.lineTo(l + beta, b);
  path.cubicTo(l + alpha, b, l, b - alpha, l, b - beta);
  path.lineTo(l, t + beta);
  path.cubicTo(l, t + alpha, l + alpha, t, l + beta, t);
  path.closeSubpath();
  return path;
}

int shapeInset(int borderWidth, int shadowExtent) {
  int inset = shadowExtent;
  if (borderWidth > 0) {
    inset = qMax(inset, (borderWidth + 1) / 2);  // 描边以路径为中心，取一半
  }
  return inset;
}

int requiredMargin(int radius, int inset, int shadowExtent) {
  const int corner = qCeil(1.4 * qMax(0, radius)) + 1;
  return qMax(corner, 2) + qMax(inset, shapeInset(0, shadowExtent));
}

QImage makeBackgroundImage(qreal radius, int inset, int margin,
                           const QColor& fill, const QColor& border) {
  const int size = 2 * margin + kCenter;
  QImage image = newImage(size);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF shape = QRectF(inset, inset, size - 2.0 * inset, size - 2.0 * inset);
  drawShape(&painter, shape, radius, fill, border,
            border.alpha() > 0 ? qMax(1, 2 * inset) : 0);
  painter.end();
  return image;
}

QImage makeHighlightImage(qreal radius, int inset, int margin,
                          const QColor& fill, const QColor& border,
                          int shadowExtent, bool shadowDown) {
  const int size = 2 * margin + kCenter;
  QImage image = newImage(size);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF shape = QRectF(inset, inset, size - 2.0 * inset, size - 2.0 * inset);

  if (shadowExtent > 0) {
    // 同心环叠影：mac 的柔影是 shadowRadius=size、透明度 0.2、沿 y 偏 half。
    // 用多层递减透明度的圆角矩形环近似高斯模糊（与 Windows 侧同法）。
    const int rings = qMin(shadowExtent, kMaxShadowRings);
    const qreal shift = shadowDown ? shadowExtent / 2.0 : -shadowExtent / 2.0;
    painter.save();
    painter.translate(0, shift);
    for (int i = rings - 1; i >= 0; --i) {
      const qreal fall = 1.0 - qreal(i) / rings;
      QColor ring(0, 0, 0, qRound(0.2 * 255 * fall * fall));
      const qreal grow = i + 1;
      const QRectF outer = shape.adjusted(-grow, -grow, grow, grow);
      painter.fillPath(continuousRoundedPath(outer, radius + grow), ring);
    }
    painter.restore();
  }

  drawShape(&painter, shape, radius, fill, border,
            border.alpha() > 0 ? qMax(1, 2 * inset) : 0);
  painter.end();
  return image;
}

void paintTile(QPainter* painter, const QImage& image, const QRectF& target,
               const QMargins& margin) {
  if (image.isNull() || target.isEmpty()) return;
  const qreal l = margin.left(), t = margin.top(), r = margin.right(),
              b = margin.bottom();
  const qreal sw = image.width() - l - r;
  const qreal sh = image.height() - t - b;
  const qreal tw = target.width() - l - r;
  const qreal th = target.height() - t - b;
  if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0) return;

  // 四角不缩放、边单向、中心双向（引擎侧对应 cairo 的 scale + NEAREST）。
  const bool smooth = painter->renderHints() & QPainter::SmoothPixmapTransform;
  painter->setRenderHint(QPainter::SmoothPixmapTransform, false);

  struct Tile {
    QRectF src, dst;
  };
  const QRectF srcC(l, t, sw, sh);
  const QRectF dstC(target.left() + l, target.top() + t, tw, th);

  const Tile tiles[] = {
      {QRectF(0, 0, l, t), QRectF(target.left(), target.top(), l, t)},
      {QRectF(image.width() - r, 0, r, t),
       QRectF(target.right() - r, target.top(), r, t)},
      {QRectF(0, image.height() - b, l, b),
       QRectF(target.left(), target.bottom() - b, l, b)},
      {QRectF(image.width() - r, image.height() - b, r, b),
       QRectF(target.right() - r, target.bottom() - b, r, b)},
      {QRectF(l, 0, sw, t),
       QRectF(target.left() + l, target.top(), tw, t)},
      {QRectF(l, image.height() - b, sw, b),
       QRectF(target.left() + l, target.bottom() - b, tw, b)},
      {QRectF(0, t, l, sh), QRectF(target.left(), target.top() + t, l, th)},
      {QRectF(image.width() - r, t, r, sh),
       QRectF(target.right() - r, target.top() + t, r, th)},
      {srcC, dstC},
  };
  for (const Tile& tile : tiles) {
    if (tile.src.isEmpty() || tile.dst.isEmpty()) continue;
    painter->drawImage(tile.dst, image, tile.src);
  }
  painter->setRenderHint(QPainter::SmoothPixmapTransform, smooth);
}

}  // namespace bangke::ninegrid
