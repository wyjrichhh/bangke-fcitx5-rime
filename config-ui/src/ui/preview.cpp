#include "preview.h"

#include <QFontMetricsF>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtMath>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include "theme/ninegrid.h"
#include "ui/themepalette.h"

namespace bangke {
namespace {

struct Row {
  QString label;
  QString text;
  QString comment;
  bool highlighted = false;
  bool accentComment = false;
  qreal textHeight = 0;
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
    row.textHeight = fm.height();
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
      if (spec.verticalText) row.textHeight = 0;
      const auto append = [&](const QString& text,int role) {
        QString display = text;
        if (spec.verticalText) {
          QStringList chars;for (uint ch : text.toUcs4()) chars << QString::fromUcs4(&ch,1);
          display=chars.join(QLatin1Char('\n'));
          qreal width=0;for (const auto& ch: chars) width=qMax(width,fm.horizontalAdvance(ch));
          row.width = qMax(row.width, width);
          row.textHeight += fm.height() * chars.size();
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

  qreal contentWidth = 0;
  qreal contentHeight = 0;
  for (const Row& row : layout.rows) {
    const qreal lineHeight = row.textHeight + spec.textMargin.top() +
                             spec.textMargin.bottom();
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
            : cursorY + row.textHeight + spec.textMargin.top() + spec.textMargin.bottom();
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

    if (spec.vertical) cursorY += row.textHeight + spec.textMargin.top() + spec.textMargin.bottom();
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
    qreal runBaseline = baseline;
    if (!row.runs.isEmpty()) {
      for (const auto& run : row.runs) {
        QColor color=rowColor;
        if (run.second==0) color=row.highlighted?spec.selectedLabelText:spec.labelText;
        if (run.second==2) color=row.highlighted?spec.selectedCommentText:spec.commentText;
        drawRun(painter,spec,run.first,x,runBaseline,color);
        if (spec.verticalText) {
          if (!run.first.isEmpty()) runBaseline += fm.height() * run.first.count(QLatin1Char('\n')) + fm.height();
        } else x += fm.horizontalAdvance(run.first);
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

namespace {
class PreviewCanvas : public QWidget {
 public:
  explicit PreviewCanvas(QWidget* parent) : QWidget(parent) {}
  void setContent(const PanelSpec& spec, const PreviewSamples& samples) {
    spec_ = spec;
    samples_ = samples;
    const QSize size = panelSize(spec_, samples_).toSize() + QSize(48, 48);
    setMinimumSize(size);
    updateGeometry();
    update();
  }
 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const auto geometry = computeLayout(spec_, samples_);
    if (geometry.panelSize.isEmpty()) return;
    const QRectF panel(QPointF(24, 24), geometry.panelSize);
    drawPanel(&painter, spec_, samples_, geometry, panel);
  }
 private:
  PanelSpec spec_;
  PreviewSamples samples_;
};
}  // namespace

PreviewStrip::PreviewStrip(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("previewScene"));
  setAttribute(Qt::WA_StyledBackground);
  setMinimumWidth(360);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(16);
  auto* heading = new QLabel(QStringLiteral("实时预览 · 当前系统主题"), this);
  heading->setObjectName(QStringLiteral("previewHeading"));
  layout->addWidget(heading);
  composition_ = new QLabel(this);
  composition_->setObjectName(QStringLiteral("previewComposition"));
  composition_->setWordWrap(true);
  layout->addWidget(composition_);
  scroll_ = new QScrollArea(this);
  scroll_->setObjectName(QStringLiteral("candidatePreviewScroll"));
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setWidget(new PreviewCanvas(scroll_));
  layout->addWidget(scroll_, 1);
  auto* note = new QLabel(QStringLiteral("原始字号预览，超出区域可滚动。\n毛玻璃与宽度记忆需在实际输入中验证。"), this);
  note->setObjectName(QStringLiteral("previewNote"));
  note->setWordWrap(true);
  layout->addWidget(note);
  samples_ = PreviewSamples::defaults();
}

void PreviewStrip::setSpecs(const PanelSpec& light, const PanelSpec& dark) {
  light_ = light;
  dark_ = dark;
  refresh();
}

void PreviewStrip::setSamples(const PreviewSamples& samples) {
  samples_ = samples;
  refresh();
}

void PreviewStrip::setInlineCandidate(bool enabled) {
  inlineCandidate_ = enabled;
}

void PreviewStrip::refresh() {
  const auto& spec = palette::dark() ? dark_ : light_;
  QString composing;
  if (spec.inlinePreedit)
    composing = inlineCandidate_ && !samples_.candidates.isEmpty()
                    ? samples_.candidates.first().text : samples_.preedit;
  composition_->setText(QStringLiteral("今天，") + composing + QStringLiteral("▏"));
  static_cast<PreviewCanvas*>(scroll_->widget())->setContent(spec, samples_);
}

QSize PreviewStrip::sizeHint() const { return QSize(480, 460); }

void PreviewStrip::paintEvent(QPaintEvent*) {}

}  // namespace bangke
