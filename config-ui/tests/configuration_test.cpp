#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <cassert>
#include <cstdio>
#include "yamlconfig.h"
#include "theme/paths.h"
#include "theme/color.h"
#include "theme/migrate.h"
#include "theme/themewriter.h"
#include "ui/pages.h"
#include "ui/preview.h"
static void write(const QString& path,const QByteArray& text) {QFile f(path);assert(f.open(QIODevice::WriteOnly));assert(f.write(text)==text.size());}
int main(int argc,char** argv) {
 QTemporaryDir temp;assert(temp.isValid());
 qputenv("XDG_DATA_HOME",(temp.path()+"/data").toUtf8());qputenv("XDG_CONFIG_HOME",(temp.path()+"/config").toUtf8());qputenv("BANGKE_PANEL_BACKEND","kimpanel");
 QApplication app(argc,argv);
 auto platform=bangke::loadPlatformTheme(QStringLiteral(BANGKE_TEST_FIXTURES "/omarchy-tokyo-night"));bangke::setAppPlatformTheme(platform);
 const auto dir=YamlConfig::rimeUserDir();QDir().mkpath(dir);
 assert(QFile::copy(QStringLiteral(BANGKE_SOURCE_YAML),dir+"/bangke.yaml"));
 const QByteArray schema("switches:\n  - name: ascii_mode\n    reset: 0\n  - name: simplification\n    states: [漢字, 汉字]\n");
 for(const auto name:{"luna_pinyin","rime_ice"}) write(dir+"/"+name+".schema.yaml",schema);
 write(dir+"/luna_pinyin.custom.yaml","patch:\n  custom/unmanaged: keep-me\n  switches/@0/reset: 1\n");
 auto data=YamlConfig::load();data.pageSize=9;data.shiftLAction="clear";data.shiftRAction="noop";data.spellingHints=true;data.preeditInApp=false;
 data.schemas[0].simplified=false;data.schemas[0].userDict=false;data.schemas[1].enabled=false;
 auto& a=data.ai;a.enabled=false;a.filterPunctuation=false;a.device="cpu";a.modelPath="/tmp/a model # custom";a.maxTokens=128;a.debounceMs=400;a.minInputLength=20;a.minHanzi=4;a.targetIndex=3;a.searchRange=25;a.contextWindowSize=30;a.minContextPromptLength=6;
 auto& s=data.style;s.colorScheme="lost_temple";s.colorSchemeDark="native";s.candidateListLayout="stacked";s.textOrientation="vertical";s.fontFace="Noto Sans CJK SC";s.fontPoint=24;s.alpha=.65;s.cornerRadius=25;s.hilitedCornerRadius=11;s.borderWidth=4;s.borderHeight=3;s.lineSpacing=15;s.spacing=12;s.shadowSize=9;s.inlinePreedit=false;s.inlineCandidate=true;s.memorizeSize=false;s.mutualExclusive=true;s.showPaging=true;s.candidateFormat="[candidate] # [label]: [comment]";
 assert(YamlConfig::save(data));auto loaded=YamlConfig::load();
 assert(bangke::styleToPatch(loaded.style)==bangke::styleToPatch(s));
 assert(loaded.pageSize==9 && loaded.shiftLAction=="clear" && loaded.shiftRAction=="noop" && loaded.spellingHints && !loaded.preeditInApp);
 assert(!loaded.schemas[0].simplified && !loaded.schemas[0].userDict && !loaded.schemas[1].enabled);
 auto b=loaded.ai;assert(!b.enabled && !b.filterPunctuation && b.modelPath==a.modelPath && b.maxTokens==128 && b.debounceMs==400 && b.minInputLength==20 && b.minHanzi==4 && b.targetIndex==3 && b.searchRange==25 && b.contextWindowSize==30 && b.minContextPromptLength==6);
 QFile custom(dir+"/luna_pinyin.custom.yaml");assert(custom.open(QIODevice::ReadOnly));const auto preserved=custom.readAll();assert(preserved.contains("custom/unmanaged: keep-me") && preserved.contains("switches/@0/reset: 1"));
 auto config=bangke::BangkeConfig::load(dir+"/bangke.yaml",dir+"/bangke.custom.yaml",platform);
 bangke::AppearancePage page(config,s);assert(bangke::styleToPatch(page.style())==bangke::styleToPatch(s));
 page.resize(900,700);page.show();app.processEvents();
 auto* scroll=page.findChild<QScrollArea*>();auto* preview=page.findChild<bangke::PreviewStrip*>();assert(scroll && preview);
 assert(!scroll->isAncestorOf(preview));const auto previewPosition=preview->mapTo(&page,QPoint());
 scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());app.processEvents();
 assert(preview->isVisible() && preview->mapTo(&page,QPoint())==previewPosition);
 auto spec=bangke::panelSpecFor(config,s,false);assert(spec.cornerRadius==bangke::ptToPx(25) && spec.highlightCornerRadius==bangke::ptToPx(11));
 auto payload=QJsonDocument::fromJson(bangke::customPanelAppearance(config,s).toUtf8()).object();assert(payload["version"].toInt()==1 && payload["style"].toObject()["candidate_format"].toString()==s.candidateFormat);
 for(bool dark:{false,true}) {
   auto broken=platform;broken.panelRoles["candidate_text_color"]=broken.panelRoles["back_color"];broken.panelRoles["hilited_candidate_text_color"]=QColor(0,0,0,0);
   auto c=bangke::BangkeConfig::load(dir+"/bangke.yaml",QString(),broken);auto colors=c.resolve("native",dark);assert(colors.candidateText!=colors.back && colors.hilitedCandidateText.alpha()>200);
   QImage image(720,280,QImage::Format_ARGB32);image.fill(Qt::gray);QPainter painter(&image);auto native=s;native.colorScheme="native";native.colorSchemeDark="native";bangke::renderPanel(&painter,bangke::panelSpecFor(c,native,dark),bangke::PreviewSamples::defaults(),QRectF(0,0,720,280));painter.end();assert(image.save(temp.path()+"/native.png"));
 }
 // Reproduce the exact transparent custom palette collected from the VM.
 write(dir+"/bangke.custom.yaml", "patch:\n  style/color_scheme: bangke_custom\n  preset_color_schemes/bangke_custom/name: 自定义\n  preset_color_schemes/bangke_custom/back_color: 0xffffff\n  preset_color_schemes/bangke_custom/text_color: 0x00d90000\n  preset_color_schemes/bangke_custom/candidate_text_color: 0x00d90000\n  preset_color_schemes/bangke_custom/hilited_candidate_text_color: 0x00d90000\n  preset_color_schemes/bangke_custom/hilited_candidate_back_color: 0xffd7b3\n");
 auto imported=bangke::BangkeConfig::load(dir+"/bangke.yaml",dir+"/bangke.custom.yaml",platform);
 assert(imported.style().colorScheme=="bangke_custom");
 for(bool dark:{false,true}) {
   const auto colors=imported.resolve("bangke_custom",dark);
   assert(colors.candidateText.alpha()==255 && colors.text.alpha()==255 && colors.hilitedCandidateText.alpha()==255);
   assert(colors.candidateText!=colors.back && colors.hilitedCandidateText!=colors.hilitedCandidateBack);
   QImage image(720,280,QImage::Format_ARGB32);image.fill(Qt::gray);QPainter painter(&image);
   bangke::renderPanel(&painter,bangke::panelSpecFor(imported,dark),bangke::PreviewSamples::defaults(),QRectF(0,0,720,280));painter.end();
   assert(image.save(temp.path()+"/custom.png"));
 }
 assert(bangke::parseFcitxColor("#000000D9")==QColor(0,0,0,217));
 assert(bangke::parseFcitxColor("#FFFFFF80")==QColor(255,255,255,128));
 assert(!bangke::parseFcitxColor("#ZZFFFFFF").isValid());
 auto factory=bangke::BangkeConfig::load(dir+"/bangke.yaml",QString());
 assert(QFile::remove(dir+"/bangke.custom.yaml"));
 const auto theme=bangke::paths::themeDir("BangKe");QDir().mkpath(theme);
 // A real old theme must import RGBA correctly, including alpha and RGB order.
 write(theme+"/theme.conf","[InputPanel]\nNormalColor=#112233D9\nHighlightCandidateColor=#000000D9\n[InputPanel/Background]\nColor=#FFFFFF\n[InputPanel/Highlight]\nColor=#B3D7FF\n");
 assert(bangke::migrateLegacyAppearance(factory));
 auto legacy=bangke::BangkeConfig::load(dir+"/bangke.yaml",dir+"/bangke.custom.yaml");
 assert(legacy.resolve("bangke_custom",false).candidateText==QColor(17,34,51,217));
 assert(!bangke::migrateLegacyAppearance(factory));
 // Fresh install: generated fallback themes must not be re-imported as custom.
 assert(QFile::remove(dir+"/bangke.custom.yaml"));
 assert(bangke::applyTheme(factory,factory.style(),false).ok);
 assert(!bangke::migrateLegacyAppearance(factory));
 assert(bangke::BangkeConfig::load(dir+"/bangke.yaml",dir+"/bangke.custom.yaml").style().colorScheme==factory.style().colorScheme);
 // Every custom-panel palette must fade its entire fill stack, not only
 // the panel base. Test real preview pixels with glyphs made transparent.
 const auto catalogue=bangke::BangkeConfig::load(dir+"/bangke.yaml",QString(),platform);
 for(const auto& id:catalogue.schemeIds()) {
   if(id!="native" && !id.startsWith("omarchy_")) continue;
   QList<QImage> frames;
   for(double alpha:{1.0,.5,0.0}) {
     auto style=catalogue.style();style.colorScheme=id;style.colorSchemeDark=id;
     style.alpha=alpha;style.shadowSize=7;style.borderWidth=0;style.borderHeight=0;
     style.inlinePreedit=true;style.showPaging=false;
     auto spec=bangke::panelSpecFor(catalogue,style,false);
     spec.normalText=spec.highlightCandidateText=spec.highlightText=QColor(Qt::transparent);
     spec.labelText=spec.selectedLabelText=spec.commentText=spec.selectedCommentText=QColor(Qt::transparent);
     QImage frame(720,280,QImage::Format_ARGB32_Premultiplied);frame.fill(Qt::transparent);
     QPainter painter(&frame);painter.setRenderHint(QPainter::Antialiasing);
     bangke::renderPanel(&painter,spec,bangke::PreviewSamples::defaults(),QRectF(0,0,720,280));painter.end();frames<<frame;
   }
   int painted=0;
   for(int y=0;y<280;++y) for(int x=0;x<720;++x) {
     const QRgb full=frames[0].pixel(x,y),half=frames[1].pixel(x,y),zero=frames[2].pixel(x,y);
     assert(zero==0);
     assert(qAbs(qAlpha(half)-qAlpha(full)*.5)<=2);
     assert(qAbs(qRed(half)-qRed(full)*.5)<=2);
     assert(qAbs(qGreen(half)-qGreen(full)*.5)<=2);
     assert(qAbs(qBlue(half)-qBlue(full)*.5)<=2);
     painted+=qAlpha(full)>0;
   }
   assert(painted>100);qInfo("PASS: complete preview background opacity for %s",qPrintable(id));
 }
 puts("PASS: VM custom palette readable, legacy RGBA import correct, fresh-install palette stable");
 puts("PASS: every style/UI field, behavior, schemas and AI settings round-trip; unknown keys preserved; native preview contrast and payload verified");
}
