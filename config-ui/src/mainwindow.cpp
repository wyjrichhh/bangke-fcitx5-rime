#include "mainwindow.h"
#include <QTextStream>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDateTime>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "fcitxprofile.h"
#include "rimedeploy.h"
#include "theme/paths.h"
#include "theme/stylepatch.h"
#include "theme/themewriter.h"
#include "ui/pages.h"
#include "ui/themepalette.h"
#include "ui/widgets.h"

namespace {
const QStringList kPageTitles = {
    QStringLiteral("外观"), QStringLiteral("行为"), QStringLiteral("智能联想"),
    QStringLiteral("方案与词典"), QStringLiteral("关于")};
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  data_ = YamlConfig::load();
  // Linux（尤其 Wayland）下「拼音显示在候选面板里」走不通：那里的面板由**客户端**
  // 渲染（fcitx5 文档：Wayland 下面板可能由客户端画、Qt 用原生 Qt API），它不认我们
  // 主题里的图片/九宫格，有 preedit 时只画出一个灰块 + 大片空白。所以恒用 mac 的
  // 出厂值（拼音在输入框内），设置页也不再暴露这一项。
  config_ = bangke::BangkeConfig::load(bangke::paths::sharedBangkeYaml(),
                                       bangke::paths::customBangkeYaml());
  buildUi();
}

void MainWindow::buildUi() {
  setWindowTitle(YamlConfig::brandName() + QStringLiteral(" 设置"));
  // mac 的窗口是 740×680、最小 700×620；这里对齐，另外给侧栏与页脚留出空间。
  setMinimumSize(700, 620);
  resize(760, 680);

  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* body = new QWidget(central);
  auto* bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  bodyLayout->setSpacing(0);

  sidebar_ = new bangke::Sidebar(body);
  sidebar_->addItems(kPageTitles);
  bodyLayout->addWidget(sidebar_);

  auto* divider = new QFrame(body);
  divider->setFrameShape(QFrame::VLine);
  divider->setFixedWidth(1);
  QPalette dividerPalette = divider->palette();
  dividerPalette.setColor(QPalette::WindowText, bangke::palette::separator(body));
  divider->setPalette(dividerPalette);
  bodyLayout->addWidget(divider);

  stack_ = new QStackedWidget(body);
  appearance_ = new bangke::AppearancePage(config_, data_.style, stack_);
  behavior_ = new bangke::BehaviorPage(data_, stack_);
  ai_ = new bangke::AiPage(data_, stack_);
  schemas_ = new bangke::SchemasPage(data_, stack_);
  for (QWidget* page : {static_cast<QWidget*>(appearance_),
                        static_cast<QWidget*>(behavior_),
                        static_cast<QWidget*>(ai_),
                        static_cast<QWidget*>(schemas_),
                        static_cast<QWidget*>(new bangke::AboutPage(stack_))})
    stack_->addWidget(page);
  bodyLayout->addWidget(stack_, 1);
  connect(sidebar_, &QListWidget::currentRowChanged, stack_,
          &QStackedWidget::setCurrentIndex);
  sidebar_->setCurrentRow(0);

  root->addWidget(body, 1);

  auto* footer = new QWidget(central);
  auto* footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(16, 10, 16, 10);
  footerLayout->setSpacing(8);
  status_ = new QLabel(footer);
  QPalette statusPalette = status_->palette();
  statusPalette.setColor(QPalette::WindowText,
                         bangke::palette::secondaryText(status_));
  status_->setPalette(statusPalette);
  footerLayout->addWidget(status_, 1);

  auto* restore = new QPushButton(QStringLiteral("恢复默认"), footer);
  auto* cancel = new QPushButton(QStringLiteral("取消"), footer);
  auto* save = new QPushButton(QStringLiteral("保存并应用"), footer);
  save->setProperty("primary", true);
  save->setDefault(true);
  footerLayout->addWidget(restore);
  footerLayout->addWidget(cancel);
  footerLayout->addWidget(save);
  connect(restore, &QPushButton::clicked, this, &MainWindow::restoreDefaults);
  connect(cancel, &QPushButton::clicked, this, &QWidget::close);
  connect(save, &QPushButton::clicked, this, &MainWindow::saveAndDeploy);

  root->addWidget(footer);

  auto* line = new QFrame(central);
  line->setFrameShape(QFrame::HLine);
  line->setFixedHeight(1);
  root->insertWidget(1, line);

  setCentralWidget(central);
}

void MainWindow::setPage(int index) {
  if (sidebar_) sidebar_->setCurrentRow(index);
}

void MainWindow::restoreDefaults() {
  // 出厂值单取（不带用户补丁），据此把补丁里的受管键全删掉。
  const bangke::StyleOptions factory = bangke::BangkeConfig::load(
                                           bangke::paths::sharedBangkeYaml(),
                                           QString())
                                           .style();
  bangke::resetStylePatch(factory);
  appearance_->setStyle(factory);
  status_->setText(QStringLiteral("外观设置已恢复出厂值，点「保存并应用」生效。"));
}

// 保存过程的逐步日志。
//
// 为什么要有它：候选面板在真机上「一保存就消失」，而症状完全由 classicui.conf 里
// 某个键的最终取值决定。光看结果猜是哪一步写坏的已经猜错好几轮了 —— 让它自己把
// 中间值打出来，比外面猜准得多。写到共享目录（Mac 侧能直接读到），没有就写家目录。
namespace {
const char* kSaveLogName = "save-log.txt";

QString saveLogPath() {
  const QString share = QDir::homePath() + QStringLiteral("/bangke-share");
  if (QFileInfo::exists(share)) return share + QLatin1Char('/') + kSaveLogName;
  return QDir::homePath() + QLatin1Char('/') + kSaveLogName;
}

void saveLog(const QString& line) {
  QFile f(saveLogPath());
  if (!f.open(QIODevice::Append | QIODevice::Text)) return;
  QTextStream ts(&f);
  ts << QDateTime::currentDateTime().toString(Qt::ISODate) << "  " << line << "\n";
}

// classicui.conf 里我们关心的那几行（读不到就记 <无>）
QString readClassicUiKeys(const QString& conf) {
  QFile f(conf);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringLiteral("<读不到>");
  const QStringList keys{QStringLiteral("Theme"), QStringLiteral("DarkTheme"),
                         QStringLiteral("UseDarkTheme"), QStringLiteral("Font"),
                         QStringLiteral("Vertical Candidate List")};
  QStringList out;
  QTextStream ts(&f);
  while (!ts.atEnd()) {
    const QString line = ts.readLine();
    for (const QString& k : keys) {
      if (line.startsWith(k + QLatin1Char('=')))
        out << QStringLiteral("%1=[%2]").arg(k, line.mid(k.size() + 1));
    }
  }
  return out.isEmpty() ? QStringLiteral("<没有这些键>") : out.join(QStringLiteral("  "));
}
}  // namespace

void MainWindow::saveAndDeploy() {
  data_.style = appearance_->style();
  data_.preeditInApp = true;  // 见构造函数里的说明：这一项在 Linux 上固定
  behavior_->collect(&data_);
  ai_->collect(&data_);
  schemas_->collect(&data_);

  saveLog(QStringLiteral("========== 保存开始 =========="));
  saveLog(QStringLiteral("style: fontFace=[%1] fontPoint=%2 colorScheme=[%3] dark=[%4] "
                         "orientation=[%5] corner=%6 shadow=%7 border=%8 alpha=%9")
              .arg(data_.style.fontFace)
              .arg(data_.style.fontPoint)
              .arg(data_.style.colorScheme, data_.style.colorSchemeDark,
                   data_.style.textOrientation)
              .arg(data_.style.cornerRadius)
              .arg(data_.style.shadowSize)
              .arg(data_.style.borderWidth)
              .arg(data_.style.alpha));
  saveLog(QStringLiteral("算出的字体规格 classicUiFontSpec=[%1]")
              .arg(bangke::classicUiFontSpec(data_.style)));
  saveLog(QStringLiteral("保存前 conf: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));

  if (!YamlConfig::save(data_)) {
    saveLog(QStringLiteral("!! YamlConfig::save 失败"));
    QMessageBox::warning(
        this, QStringLiteral("保存失败"),
        QStringLiteral("无法写入 %1").arg(YamlConfig::rimeUserDir()));
    return;
  }
  saveLog(QStringLiteral("YamlConfig::save 成功"));

  const bangke::ThemeResult theme = bangke::applyTheme(config_, data_.style);
  saveLog(QStringLiteral("applyTheme ok=%1 msg=[%2]")
              .arg(theme.ok ? QStringLiteral("true") : QStringLiteral("false"),
                   theme.message));
  if (!theme.ok) {
    QMessageBox::warning(this, QStringLiteral("候选面板主题写入失败"),
                         theme.message);
    return;
  }
  saveLog(QStringLiteral("applyTheme 之后 conf: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));

  ensureRimeInputMethod();  // 万一 profile 被重置过

  // fcitx5 只在启动时读 conf/<addon>.conf、也没有文件监视，所以直接写文件
  // 对正在运行的插件无效 —— 必须用 SetConfig 推过去，改完即生效（否则用户要重启）。
  QStringList pushErrors;
  const QString rimeError = RimeDeploy::setAddonConfig(
      QStringLiteral("rime"),
      {{QStringLiteral("PreeditInApplication"), data_.preeditInApp}});
  if (!rimeError.isEmpty()) pushErrors << rimeError;
  // 不启用「跟随系统深色」（UseDarkTheme=False，DarkTheme 指回同一个主题）。
  //
  // 原因：生成器目前给 BangKe 和 BangKe-Dark 产出的是**完全相同**的一份主题
  // （颜色逐行一致），所以这个开关本来就没在起作用。而一旦置 True，fcitx5 会转而
  // 走 DarkTheme 那条分支去加载主题 —— 实测在 Hyprland/Wayland 下**候选面板会整个
  // 画不出来**，且只在保存设置之后才发作（保存会把这个开关推过去）。
  // 两个主题内容一致，指回同一个没有任何损失；等深色主题真的生成出不同颜色了，
  // 再把这条路打开也不迟。
  QVariantMap classicConfig{
      {QStringLiteral("Theme"), QStringLiteral("BangKe")},
      {QStringLiteral("DarkTheme"), QStringLiteral("BangKe")},
      {QStringLiteral("UseDarkTheme"), false},
      {QStringLiteral("Vertical Candidate List"),
       data_.style.textOrientation == QLatin1String("vertical")}};

  // 字体为空时**整项都不推**。
  //
  // 空字符串会让 fcitx5 解析出 0 号字 —— 面板高度跟着文字高度走，于是归零、整个
  // 看不见。真机上「在设置页保存后候选面板就消失」就是这个：保存之后
  // classicui.conf 里唯一的非默认键就是空的 `Font=`。
  // 来源是字体按钮取不到家族名时返回空串（pages.cpp 从控件读 family()）。
  // 不推的话 fcitx5 保持它原有的字体，比推一个空值安全得多。
  const QString fontSpec = bangke::classicUiFontSpec(data_.style);
  if (!fontSpec.isEmpty()) {
    classicConfig.insert(QStringLiteral("Font"), fontSpec);
  }

  {
    QStringList kv;
    for (auto it = classicConfig.constBegin(); it != classicConfig.constEnd(); ++it)
      kv << QStringLiteral("%1=[%2]").arg(it.key(), it.value().toString());
    saveLog(QStringLiteral("要推给 classicui 的内容: %1").arg(kv.join(QStringLiteral("  "))));
  }

  const QString classicError =
      RimeDeploy::setAddonConfig(QStringLiteral("classicui"), classicConfig);
  saveLog(QStringLiteral("SetConfig(classicui) 返回: [%1]").arg(classicError));
  if (!classicError.isEmpty()) pushErrors << classicError;

  saveLog(QStringLiteral("推送之后 conf（这一步最关键）: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));

  // rime 侧配置还要走部署（编译词典/方案）。
  const QString hint = RimeDeploy::triggerDeploy();
  saveLog(QStringLiteral("triggerDeploy 返回: [%1]").arg(hint));
  saveLog(QStringLiteral("部署之后 conf: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));
  saveLog(QStringLiteral("========== 保存结束 =========="));
  if (hint.isEmpty() && pushErrors.isEmpty()) {
    status_->setText(QStringLiteral("已保存并应用。"));
    close();
  } else {
    QStringList all;
    if (!hint.isEmpty()) all << hint;
    all << pushErrors;
    const QString message = all.join(QStringLiteral("\n"));
    status_->setText(message);
    QMessageBox::information(this, QStringLiteral("已保存"), message);
  }
}
