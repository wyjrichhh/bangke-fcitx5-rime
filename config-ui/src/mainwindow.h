#ifndef FCITX5_RIME_CONFIG_MAINWINDOW_H_
#define FCITX5_RIME_CONFIG_MAINWINDOW_H_

#include <QMainWindow>

#include "theme/scheme.h"
#include "yamlconfig.h"

class QLabel;
class QStackedWidget;

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

 private slots:
  void saveAndDeploy();
  void restoreDefaults();

 private:
  void buildUi();

  RimeData data_;
  bangke::BangkeConfig config_;

  bangke::Sidebar* sidebar_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QLabel* status_ = nullptr;
  bangke::AppearancePage* appearance_ = nullptr;
  bangke::BehaviorPage* behavior_ = nullptr;
  bangke::AiPage* ai_ = nullptr;
  bangke::SchemasPage* schemas_ = nullptr;
};

#endif  // FCITX5_RIME_CONFIG_MAINWINDOW_H_
