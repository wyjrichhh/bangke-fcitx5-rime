#include "preview.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include "theme/ninegrid.h"

namespace bangke {
namespace {

// 预览背板（mac 用固定灰，不随系统深浅漂移，便于并排比对）。
const QColor kLightBackdrop(0xC0, 0xC0, 0xC0);
const QColor kDarkBackdrop(0x69, 0x69, 0x69);
constexpr int kStripHeight = 280;
constexpr int kBackdropRadius = 10;

struct Row {
  QString label;
  QString text;
  QString comment;
  bool highlighted = false;
  bool accentComment = false;
  qreal width = 0;  // 不含 textMargin
};

struct Layout {
  QList<Row> rows;
  qreal fontHeight = 0;
  qreal gap = 0;
  qreal rowWidth = 0;    // 竖排取最宽行；横排取总宽
  qreal rowHeight = 0;   // 竖排取总高；横排取最高行
  QSizeF panelSize;
};

Layout computeLayout(const PanelSpec& spec, const PreviewSamples& samples) {
  Layout layout;
  const QFontMetricsF fm(spec.font);
  layout.fontHeight = fm.height();
  layout.gap = fm.horizontalAdvance(QLatin1Char(' '));

  const bool showPreedit = !spec.inlinePreedit && !samples.preedit.isEmpty();
  if (showPreedit) {
    Row row;
    row.text = samples.preedit;
    row.width = fm.horizontalAdvance(row.text);
    layout.rows << row;
  }
  const int count = qMin(samples.pageSize, samples.candidates.size());
  for (int i = 0; i < count; ++i) {
    const PreviewCandidate& candidate = samples.candidates.at(i);
    Row row;
    row.label = candidate.label;
    row.text = candidate.text;
    row.comment = candidate.comment;
    row.highlighted = (i == 0);
    row.accentComment = candidate.accent;
    row.width = fm.horizontalAdvance(row.label) + layout.gap +
                fm.horizontalAdvance(row.text);
    if (!row.comment.isEmpty()) {
      row.width += layout.gap + fm.horizontalAdvance(row.comment);
    }
    layout.rows << row;
  }

  const qreal lineHeight = layout.fontHeight + spec.textMargin.top() +
                           spec.textMargin.bottom();
  qreal contentWidth = 0;
  qreal contentHeight = 0;
  for (const Row& row : layout.rows) {
    const qreal w = row.width + spec.textMargin.left() + spec.textMargin.right();
    if (spec.vertical) {
      contentWidth = qMax(contentWidth, w);
      contentHeight += lineHeight;
    } else {
      contentWidth += w;
      contentHeight = qMax(contentHeight, lineHeight);
    }
  }
  layout.rowWidth = contentWidth;
  layout.rowHeight = contentHeight;
  layout.panelSize = QSizeF(
      contentWidth + spec.contentMargin.left() + spec.contentMargin.right(),
      contentHeight + spec.contentMargin.top() + spec.contentMargin.bottom());
  return layout;
}

void drawRun(QPainter* painter, const PanelSpec& spec, const QString& text,
             qreal x, qreal baseline, const QColor& color) {
  if (text.isEmpty()) return;
  painter->setFont(spec.font);
  painter->setPen(color);
  painter->drawText(QPointF(x, baseline), text);
}

void drawPanel(QPainter* painter, const PanelSpec& spec,
               const PreviewSamples& samples, const Layout& layout,
               const QRectF& panel) {
  const QFontMetricsF fm(spec.font);
  const qreal ascent = fm.ascent();

  // 面板底：与落盘同一份九宫格绘制。
  ninegrid::paintTile(painter, panelBackgroundImage(spec), panel,
                      spec.backgroundMargin);

  qreal cursorX = panel.left() + spec.contentMargin.left();
  qreal cursorY = panel.top() + spec.contentMargin.top();

  for (const Row& row : layout.rows) {
    const qreal textX = cursorX + spec.textMargin.left();
    const qreal textTop = cursorY + spec.textMargin.top();

    if (row.highlighted) {
      QRectF box(textX, textTop, row.width, layout.fontHeight);
      box.adjust(-spec.highlightMargin.left(), -spec.highlightMargin.top(),
                 spec.highlightMargin.right(), spec.highlightMargin.bottom());
      if (spec.fullWidthHighlight) {
        box.setLeft(panel.left() + spec.contentMargin.left() -
                    spec.highlightMargin.left());
        box.setRight(panel.right() - spec.contentMargin.right() +
                     spec.highlightMargin.right());
      }
      // 不裁剪：真面板的窗口不是圆角形状（theme.mask() 只用于模糊），
      // 高亮就是照实画上去的。预览也不要裁，否则两边不一致。
      ninegrid::paintTile(painter, panelHighlightImage(spec), box,
                          spec.highlightMargin);
    }

    const QColor rowColor =
        row.highlighted ? spec.highlightCandidateText : spec.normalText;
    const QColor commentColor = row.accentComment ? spec.highlightText : rowColor;

    qreal x = textX;
    const qreal baseline = textTop + ascent;
    drawRun(painter, spec, row.label, x, baseline, rowColor);
    x += fm.horizontalAdvance(row.label);
    if (!row.label.isEmpty()) x += layout.gap;
    drawRun(painter, spec, row.text, x, baseline, rowColor);
    x += fm.horizontalAdvance(row.text);
    if (!row.comment.isEmpty()) {
      x += layout.gap;
      drawRun(painter, spec, row.comment, x, baseline, commentColor);
    }

    if (spec.vertical) {
      cursorY += layout.fontHeight + spec.textMargin.top() +
                 spec.textMargin.bottom();
    } else {
      cursorX += row.width + spec.textMargin.left() + spec.textMargin.right();
    }
  }
}

}  // namespace

PreviewSamples PreviewSamples::defaults() {
  PreviewSamples samples;
  samples.candidates = {
      {QStringLiteral("1."), QStringLiteral("蚌壳"), QStringLiteral("bang ke")},
      {QStringLiteral("2."), QStringLiteral("半个"), QStringLiteral("ban ge")},
      {QStringLiteral("3."), QStringLiteral("帮客"), QStringLiteral("bang ke")},
      {QStringLiteral("4."), QStringLiteral("棒喝"), QStringLiteral("bang he")},
  };
  return samples;
}

QSizeF panelSize(const PanelSpec& spec, const PreviewSamples& samples) {
  return computeLayout(spec, samples).panelSize;
}

void renderPanel(QPainter* painter, const PanelSpec& spec,
                 const PreviewSamples& samples, const QRectF& area) {
  const Layout layout = computeLayout(spec, samples);
  if (layout.panelSize.isEmpty() || area.isEmpty()) return;

  // 放不下就等比缩小（只缩不放）：整体缩放，行高与间距的比例与真机一致。
  const qreal zoom = qMin(1.0, qMin(area.width() / layout.panelSize.width(),
                                    area.height() / layout.panelSize.height()));

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setRenderHint(QPainter::TextAntialiasing, true);
  painter->translate(area.center());
  painter->scale(zoom, zoom);
  painter->translate(-area.center());
  const QRectF panel(area.center().x() - layout.panelSize.width() / 2.0,
                     area.center().y() - layout.panelSize.height() / 2.0,
                     layout.panelSize.width(), layout.panelSize.height());
  drawPanel(painter, spec, samples, layout, panel);
  painter->restore();
}

PreviewStrip::PreviewStrip(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(kStripHeight);
  light_ = PanelSpec();
  dark_ = PanelSpec();
  samples_ = PreviewSamples::defaults();
}

void PreviewStrip::setSpecs(const PanelSpec& light, const PanelSpec& dark) {
  light_ = light;
  dark_ = dark;
  update();
}

void PreviewStrip::setSamples(const PreviewSamples& samples) {
  samples_ = samples;
  update();
}

QSize PreviewStrip::sizeHint() const { return QSize(640, kStripHeight); }

void PreviewStrip::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QRectF frame = rect().adjusted(0, 0, -1, -1);

  // 单行（横排）时上下切分，竖排时左右切分 —— mac 用测量决定，这里直接
  // 读配置值，避免测量误差导致预览与设置不一致。
  const bool stackVertically = !light_.vertical;
  const QRectF lightRect =
      stackVertically ? QRectF(frame.left(), frame.top(), frame.width(),
                               frame.height() / 2.0)
                      : QRectF(frame.left(), frame.top(), frame.width() / 2.0,
                               frame.height());
  const QRectF darkRect =
      stackVertically ? QRectF(frame.left(), frame.center().y(), frame.width(),
                               frame.height() / 2.0)
                      : QRectF(frame.center().x(), frame.top(), frame.width() / 2.0,
                               frame.height());

  QPainterPath clip;
  clip.addRoundedRect(frame, kBackdropRadius, kBackdropRadius);
  painter.setClipPath(clip);

  for (const auto& half :
       {qMakePair(lightRect, qMakePair(kLightBackdrop, light_)),
        qMakePair(darkRect, qMakePair(kDarkBackdrop, dark_))}) {
    painter.fillRect(half.first, half.second.first);
    renderPanel(&painter, half.second.second, samples_,
                half.first.adjusted(16, 16, -16, -16));
  }

  painter.setClipping(false);

  // 角标：mac 的预览条左上有「浅色 / 深色」小胶囊。
  const QFont caption = [this]() {
    QFont f = font();
    f.setPointSizeF(qMax(7.0, f.pointSizeF() - 2.0));
    return f;
  }();
  painter.setFont(caption);
  const QFontMetricsF captionMetrics(caption);
  for (const auto& half :
       {qMakePair(lightRect, QStringLiteral("浅色")),
        qMakePair(darkRect, QStringLiteral("深色"))}) {
    const qreal w = captionMetrics.horizontalAdvance(half.second) + 12;
    const qreal h = captionMetrics.height() + 3;
    const QRectF pill(half.first.left() + 7, half.first.top() + 6, w, h);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 40));
    painter.drawRoundedRect(pill, h / 2.0, h / 2.0);
    painter.setPen(QColor(0, 0, 0, 140));
    painter.drawText(pill, Qt::AlignCenter, half.second);
  }

  painter.setPen(QPen(QColor(0, 0, 0, 51), 1));
  painter.drawRoundedRect(frame, kBackdropRadius, kBackdropRadius);
  if (!stackVertically) {
    painter.drawLine(QPointF(frame.center().x(), frame.top()),
                     QPointF(frame.center().x(), frame.bottom()));
  } else {
    painter.drawLine(QPointF(frame.left(), frame.center().y()),
                     QPointF(frame.right(), frame.center().y()));
  }
}

}  // namespace bangke
