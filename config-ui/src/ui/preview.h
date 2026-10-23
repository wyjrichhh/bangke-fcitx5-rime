#ifndef FCITX5_RIME_CONFIG_UI_PREVIEW_H_
#define FCITX5_RIME_CONFIG_UI_PREVIEW_H_

#include <QList>
#include <QRectF>
#include <QString>
#include <QWidget>

#include "theme/panelspec.h"

class QPainter;
class QLabel;
class QScrollArea;
class QTabBar;

namespace bangke {

// 预览里的样例候选。mac 的 BangkePreviewView 用同一组词，便于两端并排比对。
struct PreviewCandidate {
  QString label;
  QString text;
  QString comment;
  bool accent = false;  // AI 强调注释（_comment_highlight）
};

struct PreviewSamples {
  QString preedit = QStringLiteral("bangke");
  QList<PreviewCandidate> candidates;
  int pageSize = 5;

  static PreviewSamples defaults();
};

// 按 classicui 的绘制顺序把面板画到 area 里（居中）。预览与真机对账时，
// 这个函数也是"预览侧算出来的几何"的唯一来源。
void renderPanel(QPainter* painter, const PanelSpec& spec,
                 const PreviewSamples& samples, const QRectF& area);

// 面板在给定缩放下的自然尺寸（供对账断言用）。
QSizeF panelSize(const PanelSpec& spec, const PreviewSamples& samples);

// Current-system-theme preview with an unscaled, scrollable candidate canvas.
class PreviewStrip : public QWidget {
  Q_OBJECT
 public:
  explicit PreviewStrip(QWidget* parent = nullptr);

  void setSpecs(const PanelSpec& light, const PanelSpec& dark);
  void setSamples(const PreviewSamples& samples);
  void setInlineCandidate(bool enabled);
  void enableSampleSelection();

  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void refresh();
  QScrollArea* scroll_ = nullptr;
  QTabBar* sampleTabs_ = nullptr;
  QLabel* composition_ = nullptr;
  bool inlineCandidate_ = false;
  PanelSpec light_;
  PanelSpec dark_;
  PreviewSamples samples_;
};

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_UI_PREVIEW_H_
