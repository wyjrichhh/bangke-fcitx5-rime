#include "mainwindow.h"
#include <QProcess>
#include <QTextStream>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDateTime>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
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
  config_ = bangke::BangkeConfig::load(bangke::paths::sharedBangkeYaml(),
                                       bangke::paths::customBangkeYaml(),
                                       bangke::appPlatformTheme());
  buildUi();
}

void MainWindow::buildUi() {
  setWindowTitle(YamlConfig::brandName() + QStringLiteral(" 设置"));
  // mac 的窗口是 740×680、最小 700×620；这里对齐，另外给侧栏与页脚留出空间。
  setMinimumSize(980, 700);
  resize(1080, 800);

  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* body = new QWidget(central);
  auto* bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  bodyLayout->setSpacing(0);

  auto* navigation = new QWidget(body);
  navigation->setObjectName(QStringLiteral("brandNavigation"));
  navigation->setFixedWidth(248);
  auto* nav = new QVBoxLayout(navigation);
  nav->setContentsMargins(24, 32, 24, 24);
  nav->setSpacing(12);
  auto* mark = new QLabel(QStringLiteral("蚌"), navigation);
  mark->setObjectName(QStringLiteral("brandMark"));
  mark->setAlignment(Qt::AlignCenter);
  mark->setFixedHeight(108);
  auto* brand = new QLabel(YamlConfig::brandName(), navigation);
  brand->setObjectName(QStringLiteral("brandTitle"));
  brand->setAlignment(Qt::AlignCenter);
  auto* caption = new QLabel(QStringLiteral("本地 AI · 自在输入"), navigation);
  caption->setObjectName(QStringLiteral("brandCaption"));
  caption->setAlignment(Qt::AlignCenter);
  nav->addWidget(mark);
  nav->addWidget(brand);
  nav->addWidget(caption);
  nav->addSpacing(30);
  sidebar_ = new bangke::Sidebar(navigation);
  sidebar_->addItems(kPageTitles);
  nav->addWidget(sidebar_, 1);
  auto* privacy = new QLabel(QStringLiteral("离线推理\n输入内容留在本机"), navigation);
  privacy->setObjectName(QStringLiteral("brandCaption"));
  privacy->setWordWrap(true);
  nav->addWidget(privacy);
  bodyLayout->addWidget(navigation);

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
  footer->setObjectName(QStringLiteral("settingsFooter"));
  footerLayout->setContentsMargins(24, 16, 24, 16);
  footerLayout->setSpacing(8);
  status_ = new QLabel(footer);
  status_->setWordWrap(true);
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
  // 「出厂值」取的是**叠了桌面缺省**的那一份（load 传了 platform、补丁传空），
  // 与设置页里控件的默认显示值是同一个东西 —— 否则「恢复默认」会恢复成一个
  // 用户从没见过的值，而且和「保存时什么算没改过」对不上。
  const bangke::StyleOptions factory = bangke::BangkeConfig::load(
                                           bangke::paths::sharedBangkeYaml(),
                                           QString(),
                                           bangke::appPlatformTheme())
                                           .style();
  bangke::resetStylePatch(factory);
  appearance_->setStyle(factory);
  status_->setText(QStringLiteral("外观设置已恢复出厂值，点「保存并应用」生效。"));
}

// Save diagnostics belong in the user's cache, independent of test shares.
namespace {
QString saveLogPath() {
  QString cache = QString::fromLocal8Bit(qgetenv("XDG_CACHE_HOME"));
  if (cache.isEmpty()) cache = QDir::homePath() + QStringLiteral("/.cache");
  const QString dir = cache + QStringLiteral("/bangke-pinyin");
  QDir().mkpath(dir);
  return dir + QStringLiteral("/settings.log");
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
  data_.preeditInApp = RimeDeploy::usesCustomPanel() ? data_.style.inlinePreedit : true;
  behavior_->collect(&data_);
  ai_->collect(&data_);
  schemas_->collect(&data_);

  if (RimeDeploy::usesCustomPanel() && !data_.style.candidateFormat.contains(QStringLiteral("[candidate]"))) {
    QMessageBox::warning(this,QStringLiteral("候选格式无效"),QStringLiteral("候选格式需要包含 [candidate]，否则无法显示候选文字。"));return;
  }
  bool hasSchema = false;
  for (const auto& schema : data_.schemas) hasSchema |= schema.enabled;
  if (!hasSchema) {
    QMessageBox::warning(this,QStringLiteral("无法保存"),QStringLiteral("请至少启用一个输入方案。"));return;
  }
  if (data_.ai.enabled && !data_.ai.modelPath.isEmpty() &&
      (!QFileInfo::exists(data_.ai.modelPath+"/model.bin") || !QFileInfo::exists(data_.ai.modelPath+"/config.json") || !QFileInfo::exists(data_.ai.modelPath+"/tokenizer.json") || !QFileInfo::exists(data_.ai.modelPath+"/shared_vocabulary.json"))) {
    QMessageBox::warning(this,QStringLiteral("模型目录无效"),QStringLiteral("请选择包含 model.bin、config.json、tokenizer.json 和 shared_vocabulary.json 的完整模型目录。"));return;
  }
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

  if (RimeDeploy::usesCustomPanel()) {
    saveLog(QStringLiteral("面板后端: bangke-panel；跳过 classicui 写入、推送和 fcitx5 重启"));
    QStringList errors;
    ensureRimeInputMethod();
    const QString rimeError = RimeDeploy::setAddonConfig(
        QStringLiteral("rime-ai"),
        {{QStringLiteral("PreeditInApplication"), data_.preeditInApp},
         {QStringLiteral("InlineCandidate"), data_.style.inlineCandidate}});
    if (!rimeError.isEmpty()) errors << rimeError;
    const auto currentConfig = bangke::BangkeConfig::load(bangke::paths::sharedBangkeYaml(), bangke::paths::customBangkeYaml(), bangke::appPlatformTheme());
    const QString panelError = RimeDeploy::setCustomPanelAppearance(bangke::customPanelAppearance(currentConfig, data_.style));
    saveLog(QStringLiteral("SetAppearance(%1) 返回: [%2]")
                .arg(data_.style.textOrientation, panelError));
    if (!panelError.isEmpty()) errors << panelError;
    const QString deployError = RimeDeploy::triggerDeploy();
    saveLog(QStringLiteral("triggerDeploy 返回: [%1]").arg(deployError));
    if (!deployError.isEmpty()) errors << deployError;
    saveLog(QStringLiteral("========== 保存结束（自绘面板，无重启）=========="));
    if (errors.isEmpty()) {
      status_->setText(QStringLiteral("已保存并应用。"));
      close();
    } else {
      status_->setText(errors.join(QLatin1Char('\n')));
      QMessageBox::information(this, QStringLiteral("已保存"), status_->text());
    }
    return;
  }

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
      QStringLiteral("rime-ai"),
      {{QStringLiteral("PreeditInApplication"), data_.preeditInApp},
         {QStringLiteral("InlineCandidate"), data_.style.inlineCandidate}});
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
  // 回读：fcitx5 自己认为这几个键是什么。和文件里的值一比，就能分清是
  // 「fcitx5 拒了我们的值」还是「写文件那条路另有其人」。
  saveLog(QStringLiteral("fcitx5 回读: %1")
              .arg(RimeDeploy::addonConfigDump(
                  QStringLiteral("classicui"),
                  {QStringLiteral("Theme"), QStringLiteral("DarkTheme"),
                   QStringLiteral("UseDarkTheme"), QStringLiteral("Font"),
                   QStringLiteral("Vertical Candidate List")})));
  if (!classicError.isEmpty()) pushErrors << classicError;

  saveLog(QStringLiteral("推送之后 conf（这一步最关键）: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));

  // **推送之后**再写一遍 classicui.conf。
  //
  // 真机日志的时序：SetConfig 返回成功，但同一秒里文件被重置成「全是默认值 +
  // 空的 Font=」。而 applyTheme 写文件是在推送**之前** —— 写好的值被推送冲掉了。
  // 所以这里补一次，让文件以我们的值为准。日志里同时会记下 fcitx5 自己认为的值，
  // 用来分清是「它拒了我们的值」还是「写文件那条路另有其人」。
  bangke::writeClassicUiConf(data_.style);
  saveLog(QStringLiteral("补写之后 conf: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));

  // rime 侧配置还要走部署（编译词典/方案）。
  const QString hint = RimeDeploy::triggerDeploy();
  saveLog(QStringLiteral("triggerDeploy 返回: [%1]").arg(hint));
  saveLog(QStringLiteral("部署之后 conf: %1")
              .arg(readClassicUiKeys(bangke::paths::classicUiConf())));
  // **让 fcitx5 重启一次**。
  //
  // 为什么必须这样：日志证明 SetConfig 之后 fcitx5 的运行时状态变成了空（它随即把
  // classicui.conf 写成了「全是默认 + 空 Font=」），而 fcitx5 **没有文件监视器**，
  // 只在启动时读一次 classicui.conf —— 所以光把文件写对救不了已经在跑的那个进程。
  // 这也解释了为什么每次手动重启输入法都能恢复。
  // `-r` 是"替换运行中的实例"：新进程会用当前会话环境起来，并读到我们刚写好的文件。
  saveLog(QStringLiteral("重启 fcitx5 让它读回写好的配置..."));
  if (QProcess::startDetached(QStringLiteral("fcitx5"),
                              {QStringLiteral("-r"), QStringLiteral("-d")})) {
    saveLog(QStringLiteral("已发出重启请求"));
  } else {
    saveLog(QStringLiteral("!! 重启请求发送失败，请手动跑 fcitx5 -r -d"));
  }
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
