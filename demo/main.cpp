#include "mainwindow.h"
#include "apidemo.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QTabWidget>
#include <QDebug>
#include <QSaveFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QAction>
#include <QComboBox>
#include <QDialog>

int main(int argc,char** argv){
    QApplication app(argc,argv);
    QCoreApplication::setApplicationName("paleo-data-demo");
    QCoreApplication::setOrganizationName("PaleoDataPrototype");
    QCommandLineParser parser;parser.setApplicationDescription(QStringLiteral("多源数据服务平台 · 独立原型演示"));parser.addHelpOption();
    parser.addOption({"project",QStringLiteral("打开现有工区目录"),"directory"});
    parser.addOption({"view",QStringLiteral("初始资料分类：curves / intervals / maps / sections 等"),"kind"});
    parser.addOption({"tab",QStringLiteral("初始页面：data / curve / image / sources / issues"),"name"});
    parser.addOption({"smoke-ui",QStringLiteral("显示后保存截图并退出（用于界面验证）"),"png"});
    parser.addOption({"example",QStringLiteral("接口示例：wells / curve / intervals / time_depth / section / wall / slice / seismic_info / seismic_trace / inline / crossline / seismic_slice / processed_section"),"kind"});
    parser.addOption({"run-example",QStringLiteral("执行所选接口示例；与 smoke-ui 配合时等执行结束后截图")});
    parser.addOption({"example-resource",QStringLiteral("接口示例的曲线 / 剖面 / 地震体编号"),"id"});
    parser.addOption({"example-first",QStringLiteral("接口示例顶深 / 起始时间 / 切片时间 / 地震道位置 / 测线号"),"number"});
    parser.addOption({"example-last",QStringLiteral("接口示例底深 / 结束时间；原生地震示例为样点数 / 最大道数 / 每轴位置数"),"number"});
    parser.addOption({"example-points",QStringLiteral("剖面墙有序坐标：X,Y;X,Y;X,Y，坐标约定必须与所选地震体一致"),"points"});
    parser.addOption({"preview",QStringLiteral("将地震示例切换为抽样预览；默认读取原始分辨率")});
    parser.addOption({"slice-inline-min",QStringLiteral("切片 IL 起点，按实际测线号"),"number"});
    parser.addOption({"slice-inline-max",QStringLiteral("切片 IL 终点"),"number"});
    parser.addOption({"slice-crossline-min",QStringLiteral("切片 XL 起点"),"number"});
    parser.addOption({"slice-crossline-max",QStringLiteral("切片 XL 终点"),"number"});
    parser.addOption({"slice-workers",QStringLiteral("切片读取线程：1 / 2 / 4"),"number"});
    parser.addOption({"slice-io",QStringLiteral("切片读取方式：auto / buffered / mapped / legacy"),"mode"});
    parser.addOption({"no-slice-cache",QStringLiteral("禁用切片相邻时间缓存" )});
    parser.addOption({"slice-timings",QStringLiteral("记录切片分阶段耗时" )});
    parser.addOption({"code-tab",QStringLiteral("接口左侧页签：0导包 / 1代码 / 2返回类型"),"index"});
    parser.addOption({"result-tab",QStringLiteral("接口右侧页签：0图形 / 1JSON"),"index"});
    parser.addOption({"export-code",QStringLiteral("保存当前可编译的 C++ 示例"),"cpp"});
    parser.addOption({"export-result",QStringLiteral("保存本次真实返回结果的 JSON 摘要"),"json"});
    parser.addOption({"tutorial-case",QStringLiteral("打开使用案例：0入门 / 1目录 / 2到9对应案例01到08"),"index"});
    parser.process(app);
    MainWindow window;window.show();
    if(parser.isSet("project")&&!window.openProjectPath(parser.value("project"),!parser.isSet("smoke-ui")))return 2;
    if(parser.isSet("view")){window.showDataManager();window.selectKind(parser.value("view"));}
    if(parser.isSet("tab")){window.showDataManager();const QStringList names={"data","curve","image","sources","issues"};const int index=names.indexOf(parser.value("tab"));if(index<0)return 4;if(auto* tabs=window.findChild<QTabWidget*>("contentTabs"))tabs->setCurrentIndex(index);}
    if(parser.isSet("example"))window.showApiDemo(parser.value("example"));
    if(parser.isSet("example-resource")){bool ok=false;const auto id=parser.value("example-resource").toLongLong(&ok);if(!ok||!window.apiDemo()->selectResource(id))return 4;}
    if(parser.isSet("example-first"))window.apiDemo()->findChild<QLineEdit*>("apiFirstValue")->setText(parser.value("example-first"));
    if(parser.isSet("example-last"))window.apiDemo()->findChild<QLineEdit*>("apiLastValue")->setText(parser.value("example-last"));
    if(parser.isSet("example-points"))window.apiDemo()->findChild<QPlainTextEdit*>("apiWallCoordinates")->setPlainText(parser.value("example-points").replace(';','\n'));
    if(parser.isSet("preview"))window.apiDemo()->findChild<QCheckBox*>("apiFullResolution")->setChecked(false);
    const char* rangeFlags[]={"slice-inline-min","slice-inline-max","slice-crossline-min","slice-crossline-max"};
    const char* rangeControls[]={"apiInlineMin","apiInlineMax","apiCrosslineMin","apiCrosslineMax"};
    for(int n=0;n<4;++n)if(parser.isSet(rangeFlags[n]))window.apiDemo()->findChild<QLineEdit*>(rangeControls[n])->setText(parser.value(rangeFlags[n]));
    if(parser.isSet("slice-workers")){bool valid=false;int value=parser.value("slice-workers").toInt(&valid);auto* control=window.apiDemo()->findChild<QComboBox*>("apiSliceWorkers");int index=control->findData(value);if(!valid||index<0)return 4;control->setCurrentIndex(index);}
    if(parser.isSet("slice-io")){auto* control=window.apiDemo()->findChild<QComboBox*>("apiSliceIoMode");int index=control->findData(parser.value("slice-io"));if(index<0)return 4;control->setCurrentIndex(index);}
    if(parser.isSet("no-slice-cache"))window.apiDemo()->findChild<QCheckBox*>("apiSliceCache")->setChecked(false);
    if(parser.isSet("slice-timings"))window.apiDemo()->findChild<QCheckBox*>("apiSliceTimings")->setChecked(true);
    auto saveText=[](const QString& path,const QString& value){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);const auto bytes=value.toUtf8();return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();};
    if(parser.isSet("export-code")&&!saveText(parser.value("export-code"),window.apiDemo()->generatedCode()))return 6;
    if(parser.isSet("tutorial-case")){
        bool valid=false;const int index=parser.value("tutorial-case").toInt(&valid);
        auto* action=window.findChild<QAction*>("usageExamples");if(!valid||!action)return 4;action->trigger();
        auto* dialog=window.findChild<QDialog*>("tutorialHelp");if(!dialog)return 4;
        auto* choice=dialog->findChild<QComboBox*>("tutorialChoice");if(!choice||index<0||index>=choice->count())return 4;choice->setCurrentIndex(index);
    }
    auto finish=[&app,&window,&parser,saveText](bool success){
        bool saved=true;if(parser.isSet("export-result"))saved=saveText(parser.value("export-result"),window.apiDemo()->lastResultJson());
        if(parser.isSet("code-tab"))window.apiDemo()->findChild<QTabWidget*>("apiCodeTabs")->setCurrentIndex(parser.value("code-tab").toInt());
        if(parser.isSet("result-tab"))window.apiDemo()->findChild<QTabWidget*>("apiResultTabs")->setCurrentIndex(parser.value("result-tab").toInt());
        if(parser.isSet("smoke-ui")){const auto output=parser.value("smoke-ui");QDir().mkpath(QFileInfo(output).absolutePath());QWidget* capture=&window;if(parser.isSet("tutorial-case")){if(auto* dialog=window.findChild<QDialog*>("tutorialHelp"))capture=dialog;}saved=capture->grab().save(output)&&saved;qInfo()<<"UI screenshot"<<output<<saved;app.exit(!success?5:saved?0:3);}
    };
    if(parser.isSet("run-example")){
        window.showApiDemo();QObject::connect(window.apiDemo(),&ApiDemoWidget::runFinished,&window,[&window,finish](bool success){QTimer::singleShot(250,&window,[finish,success]{finish(success);});});
        QTimer::singleShot(100,&window,[&window]{window.apiDemo()->runExample();});
    }else if(parser.isSet("smoke-ui"))QTimer::singleShot(1200,&window,[finish]{finish(true);});
    return app.exec();
}
