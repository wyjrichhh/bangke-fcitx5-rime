#ifndef FCITX5_RIME_CONFIG_MAINWINDOW_H_
#define FCITX5_RIME_CONFIG_MAINWINDOW_H_

#include <QMainWindow>

#include "theme/scheme.h"
#include "yamlconfig.h"

class QLabel;
class QStackedWidget;
class QTabBar;
class QResizeEvent;
class QGridLayout;

namespace bangke {
class Sidebar;
class AppearancePage;
class BehaviorPage;
class AiPage;
class SchemasPage;
class AboutPage;
}  // namespace bangke

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  void setPage(int index);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

 private slots:
  void saveAndDeploy();
  void restoreDefaults();

 private:
  void buildUi();
  void updateNavigationLayout();

  RimeData data_;
  bangke::BangkeConfig config_;

  QGridLayout* navigationLayout_ = nullptr;
  QLabel* navigationBrand_ = nullptr;
  QLabel* navigationCaption_ = nullptr;
  QTabBar* navigation_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QLabel* status_ = nullptr;
  bangke::AppearancePage* appearance_ = nullptr;
  bangke::BehaviorPage* behavior_ = nullptr;
  bangke::AiPage* ai_ = nullptr;
  bangke::SchemasPage* schemas_ = nullptr;
};

#endif  // FCITX5_RIME_CONFIG_MAINWINDOW_H_
