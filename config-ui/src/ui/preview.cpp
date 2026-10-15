#include "preview.h"

#include <QFontMetricsF>
#include <QtMath>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include "theme/ninegrid.h"
#include "ui/themepalette.h"

namespace bangke {
namespace {

// 预览背板（mac 用固定灰，不随系统深浅漂移，便于并排比对）。
const QColor kLightBackdrop(0xC0, 0xC0, 0xC0);
const QColor kDarkBackdrop(0x69, 0x69, 0x69);
constexpr int kStripHeight = 176;
constexpr int kBackdropRadius = 10;

struct Row {
  QString label;
  QString text;
  QString comment;
  bool highlighted = false;
  bool accentComment = false;
  qreal width = 0;  // 不含 textMargin
  QList<QPair<QString,int>> runs;
};

struct Layout {
  QList<Row> rows;
  qreal fontHeight = 0;
  qreal gap = 0;
  qreal rowWidth = 0;    // 竖排取最宽行；横排取总宽
  qreal rowHeight = 0;   // 竖排取总高；横排取最高行
  QSizeF panelSize;
  qreal preeditHeight = 0;
  qreal pagingHeight = 0;
  QString preedit;
};

Layout computeLayout(const PanelSpec& spec, const PreviewSamples& samples) {
  Layout layout;
  const QFontMetricsF fm(spec.font);
  layout.fontHeight = fm.height();
  layout.gap = fm.horizontalAdvance(QLatin1Char(' '));

  const bool showPreedit = !spec.inlinePreedit && !samples.preedit.isEmpty();
  if (showPreedit) {
    layout.preedit = samples.preedit;
    layout.preeditHeight = qMax(fm.height()+2,fm.height()+spec.preeditSpacing);
  }
  layout.pagingHeight = spec.showPaging ? 24 : 0;
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
    if (spec.customPanel) {
      QString label = row.label;
      if (label.endsWith(QLatin1Char('.'))) label.chop(1);
      const QRegularExpression token(QStringLiteral("\\[(label|candidate|comment)\\]"));
      auto matches = token.globalMatch(spec.candidateFormat);
      int end = 0;
      row.width = 0;
      const auto append = [&](const QString& text,int role) {
        QString display = text;
        if (spec.verticalText) {
          QStringList chars;for (uint ch : text.toUcs4()) chars << QString::fromUcs4(&ch,1);
          display=chars.join(QLatin1Char('\n'));
          qreal width=0;for (const auto& ch: chars) width=qMax(width,fm.horizontalAdvance(ch));
          row.width += width;
          layout.fontHeight=qMax(layout.fontHeight,fm.height()*chars.size());
        } else row.width += fm.horizontalAdvance(text);
        row.runs.append({display,role});
      };
      while (matches.hasNext()) {
        const auto m=matches.next();append(spec.candidateFormat.mid(end,m.capturedStart()-end),1);
        const QString key=m.captured(1);
        append(key=="label"?label:key=="comment"?row.comment:row.text,key=="label"?0:key=="comment"?2:1);
        end=m.capturedEnd();
      }
      append(spec.candidateFormat.mid(end),1);
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
      qMax(contentWidth,layout.preedit.isEmpty()?0:fm.horizontalAdvance(layout.preedit)+24) + spec.contentMargin.left() + spec.contentMargin.right(),
      contentHeight + layout.preeditHeight + layout.pagingHeight + spec.contentMargin.top() + spec.contentMargin.bottom());
  return layout;
}

void drawRun(QPainter* painter, const PanelSpec& spec, const QString& text,
             qreal x, qreal baseline, const QColor& color) {
  if (text.isEmpty()) return;
  painter->setFont(spec.font);
  painter->setPen(color);
  if (spec.verticalText) {
    const QFontMetricsF fm(spec.font);qreal y=baseline;
    for (const auto& line:text.split(QLatin1Char('\n'))) {painter->drawText(QPointF(x,y),line);y+=fm.height();}
  } else painter->drawText(QPointF(x, baseline), text);
}

QPainterPath highlightPath(const QRectF& box, const QRectF& panel, qreal outer, qreal inner) {
  const auto near=[](qreal a,qreal b) {return qAbs(a-b)<.01;};
  const qreal limit=qMin(box.width(),box.height())/2;
  inner=0;outer=qMin(outer,limit);  // Continuous attached highlight has square internal boundaries.
  const bool l=near(box.left(),panel.left()),r=near(box.right(),panel.right());
  const bool t=near(box.top(),panel.top()),b=near(box.bottom(),panel.bottom());
  const qreal tl=l&&t?outer:inner,tr=r&&t?outer:inner,br=r&&b?outer:inner,bl=l&&b?outer:inner;
  QPainterPath path;
  path.moveTo(box.left()+tl,box.top());path.lineTo(box.right()-tr,box.top());
  if(tr) path.arcTo(QRectF(box.right()-2*tr,box.top(),2*tr,2*tr),90,-90);
  path.lineTo(box.right(),box.bottom()-br);
  if(br) path.arcTo(QRectF(box.right()-2*br,box.bottom()-2*br,2*br,2*br),0,-90);
  path.lineTo(box.left()+bl,box.bottom());
  if(bl) path.arcTo(QRectF(box.left(),box.bottom()-2*bl,2*bl,2*bl),270,-90);
  path.lineTo(box.left(),box.top()+tl);
  if(tl) path.arcTo(QRectF(box.left(),box.top(),2*tl,2*tl),180,-90);
  path.closeSubpath();return path;
}

void drawPanel(QPainter* painter, const PanelSpec& spec,
               const PreviewSamples& samples, const Layout& layout,
               const QRectF& panel) {
  const QFontMetricsF fm(spec.font);
  const qreal ascent = fm.ascent();

  painter->save();
  QPainterPath outline;
  outline.addRoundedRect(panel, spec.cornerRadius, spec.cornerRadius);
  painter->setClipPath(outline, Qt::IntersectClip);
  const qreal ratio = painter->device()->devicePixelRatioF();
  QImage background(QSize(qCeil(panel.width()*ratio),qCeil(panel.height()*ratio)),QImage::Format_ARGB32_Premultiplied);
  background.setDevicePixelRatio(ratio);
  background.fill(Qt::transparent);
  QPainter fills(&background);
  fills.setRenderHints(painter->renderHints());
  fills.translate(-panel.topLeft());
  fills.setClipPath(outline);
  fills.fillPath(outline,spec.panelBackground);

  qreal cursorX = panel.left() + spec.contentMargin.left();
  qreal cursorY = panel.top() + spec.contentMargin.top() + layout.preeditHeight;


  int rowIndex = 0;
  for (const Row& row : layout.rows) {

    if (row.highlighted) {
      QRectF box;
      if (spec.vertical) {
        const qreal top = rowIndex == 0 ? panel.top()+layout.preeditHeight : cursorY;
        const qreal bottom = rowIndex == layout.rows.size() - 1 ? panel.bottom()-layout.pagingHeight
            : cursorY + layout.fontHeight + spec.textMargin.top() + spec.textMargin.bottom();
        box = QRectF(panel.left(), top, panel.width(), bottom - top);
      } else {
        const qreal left = rowIndex == 0 ? panel.left() : cursorX;
        const qreal right = rowIndex == layout.rows.size() - 1 ? panel.right()
            : cursorX + row.width + spec.textMargin.left() + spec.textMargin.right();
        box = QRectF(left,panel.top()+layout.preeditHeight,right-left,panel.height()-layout.preeditHeight-layout.pagingHeight);
      }
      if (spec.shadowSize > 0) {
        for (int spread=qMin(30,spec.shadowSize);spread>0;--spread) {
          fills.setPen(Qt::NoPen);fills.setBrush(QColor(0,0,0,6));
          fills.drawRoundedRect(box.adjusted(-spread/2.0,-spread/2.0,spread/2.0,spread/2.0),spec.highlightCornerRadius+spread/2.0,spec.highlightCornerRadius+spread/2.0);
        }
      }
      if (spec.mutualExclusive) fills.setCompositionMode(QPainter::CompositionMode_Source);
      fills.setPen(Qt::NoPen);fills.setBrush(spec.highlightBackground);
      fills.drawPath(highlightPath(box,panel,spec.cornerRadius,spec.highlightCornerRadius));
      fills.setCompositionMode(QPainter::CompositionMode_SourceOver);

    }

    if (spec.vertical) cursorY += layout.fontHeight + spec.textMargin.top() + spec.textMargin.bottom();
    else cursorX += row.width + spec.textMargin.left() + spec.textMargin.right();
    ++rowIndex;
  }
  fills.end();
  painter->save();
  painter->setOpacity(painter->opacity()*spec.backgroundOpacity);
  painter->drawImage(panel.topLeft(),background);
  painter->restore();
  if (!layout.preedit.isEmpty()) drawRun(painter,spec,layout.preedit,panel.left()+12,panel.top()+fm.ascent()+2,spec.normalText);
  cursorX = panel.left() + spec.contentMargin.left();
  cursorY = panel.top() + spec.contentMargin.top() + layout.preeditHeight;
  for (const Row& row : layout.rows) {
    const qreal textX = cursorX + spec.textMargin.left();
    const qreal textTop = cursorY + spec.textMargin.top();
    const QColor rowColor =
        row.highlighted ? spec.highlightCandidateText : spec.normalText;
    const QColor commentColor = row.accentComment ? spec.highlightText : rowColor;

    qreal x = textX;
    const qreal baseline = textTop + ascent;
    if (!row.runs.isEmpty()) {
      for (const auto& run : row.runs) {
        QColor color=rowColor;
        if (run.second==0) color=row.highlighted?spec.selectedLabelText:spec.labelText;
        if (run.second==2) color=row.highlighted?spec.selectedCommentText:spec.commentText;
        drawRun(painter,spec,run.first,x,baseline,color);
        qreal width=0;for (const auto& line:run.first.split(QLatin1Char('\n'))) width=qMax(width,fm.horizontalAdvance(line));x+=width;
      }
    } else {
    drawRun(painter, spec, row.label, x, baseline, rowColor);
    x += fm.horizontalAdvance(row.label);
    if (!row.label.isEmpty()) x += layout.gap;
    drawRun(painter, spec, row.text, x, baseline, rowColor);
    x += fm.horizontalAdvance(row.text);
    if (!row.comment.isEmpty()) {
      x += layout.gap;
      drawRun(painter, spec, row.comment, x, baseline, commentColor);
    }

    }
    if (spec.vertical) {
      cursorY += layout.fontHeight + spec.textMargin.top() +
                 spec.textMargin.bottom();
    } else {
      cursorX += row.width + spec.textMargin.left() + spec.textMargin.right();
    }
    ++rowIndex;
  }
  if (layout.pagingHeight) drawRun(painter,spec,QStringLiteral("‹     ›"),panel.left()+12,panel.bottom()-5,spec.normalText);
  if (spec.borderWidth > 0) {
    painter->setPen(QPen(spec.panelBorder,spec.borderWidth));painter->setBrush(Qt::NoBrush);painter->drawPath(outline);
  }
  painter->restore();
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
       {qMakePair(lightRect, qMakePair(palette::card(this), light_)),
        qMakePair(darkRect, qMakePair(palette::sidebar(this), dark_))}) {
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
