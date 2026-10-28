#include "mainwindow.h"
#include "plotwidgets.h"
#include "apidemo.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>

using namespace paleo::data;
namespace {
QString q(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
std::string u(const QString& s){return s.toUtf8().toStdString();}
QPushButton* button(const QString& text,QHBoxLayout* layout,const std::function<void()>& fn){auto* b=new QPushButton(text);layout->addWidget(b);QObject::connect(b,&QPushButton::clicked,b,fn);return b;}
QTableWidget* table(QWidget* parent){auto* t=new QTableWidget(parent);t->setAlternatingRowColors(true);t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setSelectionMode(QAbstractItemView::SingleSelection);t->verticalHeader()->setVisible(false);t->horizontalHeader()->setStretchLastSection(true);return t;}
QString statusName(const std::string& s){if(s=="parsed")return QStringLiteral("已结构化");if(s=="review")return QStringLiteral("待核对");if(s=="registered")return QStringLiteral("仅登记");if(s=="failed")return QStringLiteral("失败");if(s=="ignored")return QStringLiteral("已忽略");return q(s);}
QString wellContents(const WellSummary& w){
    QStringList items;
    if(w.curves)items<<QStringLiteral("曲线 %1 条").arg(w.curves);
    if(w.tracks)items<<QStringLiteral("分层 %1 道").arg(w.tracks);
    if(w.samples)items<<QStringLiteral("样品 %1 条").arg(w.samples);
    if(w.timeDepthTables)items<<QStringLiteral("时深 %1 组").arg(w.timeDepthTables);
    if(w.trajectorySurveys)items<<QStringLiteral("测斜 %1 组").arg(w.trajectorySurveys);
    if(w.corePhotos)items<<QStringLiteral("照片 %1 张").arg(w.corePhotos);
    if(w.attachments)items<<QStringLiteral("附件 %1 份").arg(w.attachments);
    if(w.markerTracks)items<<QStringLiteral("界面点 %1 道").arg(w.markerTracks);
    if(w.hasCoordinates)items<<QStringLiteral("井位坐标");
    return items.empty()?QStringLiteral("井基础信息"):items.join(QStringLiteral(" · "));
}
int countForKind(const WellSummary& w,const std::string& kind){
    if(kind=="curves")return w.curves;if(kind=="intervals")return w.tracks;
    if(kind=="samples")return w.samples;if(kind=="time_depth")return w.timeDepthTables;
    if(kind=="trajectories")return w.trajectorySurveys;if(kind=="core_photos")return w.corePhotos;if(kind=="attachments")return w.attachments;if(kind=="markers")return w.markerTracks;return -1;
}
}

MainWindow::MainWindow(QWidget* parent):QMainWindow(parent){setupUi();setWindowTitle(QStringLiteral("多源数据服务 · 独立原型"));resize(1360,900);statusBar()->showMessage(QStringLiteral("新建或打开工区，即可开始导入—核对—保存—修改—查询"));}
MainWindow::~MainWindow(){cancelled_=true;if(worker_&&worker_->isRunning())worker_->wait();}
void MainWindow::closeEvent(QCloseEvent* e){if((worker_&&worker_->isRunning())||(apiDemo_&&apiDemo_->isRunning())){cancelled_=true;apiDemo_->requestCancel();closeWhenIdle_=true;statusBar()->showMessage(QStringLiteral("已请求取消，后台任务结束后自动关闭"));e->ignore();return;}QMainWindow::closeEvent(e);}
void MainWindow::setupUi(){
    auto* bar=addToolBar(QStringLiteral("工区"));bar->setMovable(false);
    auto action=[&](const QString& text,const char* name,const std::function<void()>& fn){auto* a=bar->addAction(text);a->setObjectName(QString::fromLatin1(name));connect(a,&QAction::triggered,this,fn);lockedActions_.push_back(a);return a;};
    action(QStringLiteral("新建工区"),"newProject",[this]{newProject();});
    action(QStringLiteral("打开工区"),"openProject",[this]{chooseOpenProject();});
    action(QStringLiteral("导入目录"),"importDirectory",[this]{importDirectory();});
    action(QStringLiteral("刷新"),"refresh",[this]{if(ready())refreshAll();});
    action(QStringLiteral("备份数据库"),"backupDatabase",[this]{backupDatabase();});
    action(QStringLiteral("更多使用案例"),"usageExamples",[this]{showTutorials();});
    action(QStringLiteral("合成数据示例"),"syntheticExample",[this]{
        if(!requireIdle())return;
        const QStringList paths={QDir(QCoreApplication::applicationDirPath()).filePath("../api_example_project"),QDir(QCoreApplication::applicationDirPath()).filePath("../../api_example_project"),QDir(q(service_.projectPath())).filePath("../api_example_project"),QDir::current().filePath("paleo_data_service/api_example_project")};
        for(const auto& path:paths)if(QFileInfo::exists(QDir(path).filePath("data.sqlite"))){if(openProjectPath(QDir(path).absolutePath()))showApiDemo("section");return;}
        showError("未找到 api_example_project 合成演示工区。可用 create_api_demo_project 工具生成；此示例与真实测试资料分开保存。");
    });
    bar->addSeparator();projectLabel_=new QLabel(QStringLiteral(" 尚未打开工区 "));bar->addWidget(projectLabel_);
    auto* main=new QWidget(this);auto* mainLayout=new QVBoxLayout(main);mainLayout->setContentsMargins(14,12,14,10);
    auto* explanation=new QLabel(QStringLiteral("解析层 → 数据流转层 → 业务层　｜　SQLite 独立工区　｜　保留原始值，修改通过业务服务保存"));explanation->setWordWrap(true);mainLayout->addWidget(explanation);
    auto* split=new QSplitter(Qt::Horizontal,main);mainLayout->addWidget(split,1);
    auto* left=new QWidget;auto* leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,10,0);
    leftLayout->addWidget(new QLabel(QStringLiteral("井与工区资料")));
    wellBox_=new QComboBox;wellBox_->setObjectName("wellSelector");wellBox_->addItem(QStringLiteral("工区全部资料"),QVariant::fromValue<qlonglong>(0));leftLayout->addWidget(wellBox_);
    wellBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);wellBox_->setMinimumContentsLength(20);
    relevantWellsBox_=new QCheckBox(QStringLiteral("仅列出有当前类型资料的井"));relevantWellsBox_->setObjectName("relevantWellsOnly");relevantWellsBox_->setChecked(true);leftLayout->addWidget(relevantWellsBox_);
    wellSummaryLabel_=new QLabel;wellSummaryLabel_->setObjectName("wellCoverage");wellSummaryLabel_->setWordWrap(true);leftLayout->addWidget(wellSummaryLabel_);
    auto* wellButtons=new QHBoxLayout;button(QStringLiteral("新建井"),wellButtons,[this]{editWell(true);});button(QStringLiteral("井属性"),wellButtons,[this]{editWell(false);});leftLayout->addLayout(wellButtons);
    kindBox_=new QComboBox;kindBox_->setObjectName("kindSelector");
    for(const auto& pair:std::vector<std::pair<QString,QString>>{{QStringLiteral("测井曲线"),"curves"},{QStringLiteral("地层 / 岩性分层道"),"intervals"},{QStringLiteral("样品分析表"),"samples"},{QStringLiteral("地震层位"),"horizons"},{QStringLiteral("地质平面图"),"maps"},{QStringLiteral("地震体"),"volumes"},{QStringLiteral("二维地震剖面"),"sections"},{QStringLiteral("时深关系"),"time_depth"},{QStringLiteral("测斜资料"),"trajectories"},{QStringLiteral("岩心照片"),"core_photos"},{QStringLiteral("井相关附件"),"attachments"},{QStringLiteral("标准层 / 界面点"),"markers"},{QStringLiteral("原始工作表"),"tables"}})kindBox_->addItem(pair.first,pair.second);
    leftLayout->addWidget(kindBox_);filterEdit_=new QLineEdit;filterEdit_->setPlaceholderText(QStringLiteral("筛选当前列表名称"));leftLayout->addWidget(filterEdit_);
    entryList_=new QListWidget;entryList_->setObjectName("dataEntries");leftLayout->addWidget(entryList_,1);split->addWidget(left);
    auto* right=new QWidget;auto* rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(8,0,0,0);
    detailLabel_=new QLabel(QStringLiteral("选择资料查看内容"));detailLabel_->setWordWrap(true);rightLayout->addWidget(detailLabel_);
    auto* range=new QHBoxLayout;range->addWidget(new QLabel(QStringLiteral("深度范围")));topEdit_=new QLineEdit;bottomEdit_=new QLineEdit;topEdit_->setPlaceholderText(QStringLiteral("顶深（空为全部）"));bottomEdit_->setPlaceholderText(QStringLiteral("底深（空为全部）"));topEdit_->setMaximumWidth(180);bottomEdit_->setMaximumWidth(180);range->addWidget(topEdit_);range->addWidget(bottomEdit_);processedBox_=new QCheckBox(QStringLiteral("使用处理值"));processedBox_->setChecked(true);range->addWidget(processedBox_);button(QStringLiteral("查询 / 预览"),range,[this]{pageOffset_=0;querySelected();});range->addStretch();rightLayout->addLayout(range);
    auto* edits=new QHBoxLayout;button(QStringLiteral("编辑选中记录"),edits,[this]{if(currentKind()=="intervals")editInterval(false);else if(currentKind()=="curves")editCurveValue();else if(currentKind()=="maps")editMap();else if(currentKind()=="sections")editSection(false);else if(currentKind()=="time_depth")assignTimeDepthWell();else if(currentKind()=="volumes"||currentKind()=="horizons")editSeismicMetadata();else QMessageBox::information(this,QStringLiteral("编辑"),QStringLiteral("请选择层段、曲线、图件、时深关系、地震体、层位或剖面。其他资料保留原始表，可通过核对映射重新导入。"));});
    button(QStringLiteral("新增层段"),edits,[this]{editInterval(true);});button(QStringLiteral("曲线清理"),edits,[this]{cleanCurve();});button(QStringLiteral("恢复原始曲线"),edits,[this]{resetCurve();});button(QStringLiteral("删除选中记录"),edits,[this]{deleteSelected();});rightLayout->addLayout(edits);
    auto* seismic=new QHBoxLayout;button(QStringLiteral("新建剖面路径"),seismic,[this]{editSection(true);});button(QStringLiteral("提取剖面"),seismic,[this]{extractSection();});button(QStringLiteral("时间切片"),seismic,[this]{extractTimeSlice();});button(QStringLiteral("打开来源"),seismic,[this]{openSource();});button(QStringLiteral("报告正文"),seismic,[this]{showDocument();});seismic->addStretch();rightLayout->addLayout(seismic);
    tabs_=new QTabWidget;tabs_->setObjectName("contentTabs");rightLayout->addWidget(tabs_,1);
    auto* tabular=new QWidget;auto* tableLayout=new QVBoxLayout(tabular);dataTable_=table(tabular);dataTable_->setObjectName("queryTable");tableLayout->addWidget(dataTable_,1);
    auto* paging=new QHBoxLayout;button(QStringLiteral("上一页"),paging,[this]{pageOffset_=std::max<long long>(0,pageOffset_-200);querySelected();});pageLabel_=new QLabel(QStringLiteral("每页 200 行"));paging->addWidget(pageLabel_);button(QStringLiteral("下一页"),paging,[this]{if(pageOffset_+200<totalRows_){pageOffset_+=200;querySelected();}});paging->addStretch();tableLayout->addLayout(paging);tabs_->addTab(tabular,QStringLiteral("数据表"));
    curvePlot_=new CurvePlot;tabs_->addTab(curvePlot_,QStringLiteral("曲线预览"));imagePreview_=new ImagePreview;tabs_->addTab(imagePreview_,QStringLiteral("图件 / 剖面"));
    auto* sourcePage=new QWidget;auto* sourceLayout=new QVBoxLayout(sourcePage);sourceLayout->addWidget(new QLabel(QStringLiteral("所有文件均有处理状态；“仅登记”表示内容尚未结构化。")));sourcesTable_=table(sourcePage);sourcesTable_->setObjectName("sourceTable");sourceLayout->addWidget(sourcesTable_,1);auto* sourceActions=new QHBoxLayout;button(QStringLiteral("打开原文件"),sourceActions,[this]{openSource();});button(QStringLiteral("查看正文"),sourceActions,[this]{showDocument();});button(QStringLiteral("重新定位来源"),sourceActions,[this]{relocateSource();});button(QStringLiteral("更正映射并重导入"),sourceActions,[this]{reimportSelectedSource();});sourceActions->addStretch();sourceLayout->addLayout(sourceActions);tabs_->addTab(sourcePage,QStringLiteral("来源清单"));
    auto* reviewPage=new QWidget;auto* reviewLayout=new QVBoxLayout(reviewPage);auto* reviewActions=new QHBoxLayout;unresolvedBox_=new QCheckBox(QStringLiteral("只看未处理"));unresolvedBox_->setChecked(true);reviewActions->addWidget(unresolvedBox_);button(QStringLiteral("记录核对结论"),reviewActions,[this]{resolveIssue();});button(QStringLiteral("更正映射并重导入"),reviewActions,[this]{reimportSelectedSource();});reviewActions->addStretch();reviewLayout->addLayout(reviewActions);issuesTable_=table(reviewPage);issuesTable_->setObjectName("issuesTable");reviewLayout->addWidget(issuesTable_,1);issueDetails_=new QPlainTextEdit;issueDetails_->setReadOnly(true);issueDetails_->setMaximumHeight(130);issueDetails_->setPlaceholderText(QStringLiteral("选择问题查看原始值、来源位置和处理结论。记录结论不会自动修改数据；需要更正数据时请重新导入。"));reviewLayout->addWidget(issueDetails_);tabs_->addTab(reviewPage,QStringLiteral("待核对"));
    split->addWidget(right);split->setStretchFactor(0,0);split->setStretchFactor(1,1);split->setSizes({280,1000});
    modeTabs_=new QTabWidget(this);modeTabs_->setObjectName("modeTabs");apiDemo_=new ApiDemoWidget;apiDemo_->setObjectName("apiDemo");modeTabs_->addTab(apiDemo_,QStringLiteral("接口代码演示"));modeTabs_->addTab(main,QStringLiteral("资料管理"));setCentralWidget(modeTabs_);
    connect(apiDemo_,&ApiDemoWidget::busyChanged,this,[this](bool busy){for(auto* a:lockedActions_)a->setEnabled(!busy);modeTabs_->setTabEnabled(1,!busy);if(!busy&&closeWhenIdle_)close();});
    progress_=new QProgressBar;progress_->setMaximumWidth(260);progress_->setVisible(false);cancelButton_=new QPushButton(QStringLiteral("取消任务"));cancelButton_->setVisible(false);statusBar()->addPermanentWidget(progress_);statusBar()->addPermanentWidget(cancelButton_);
    connect(cancelButton_,&QPushButton::clicked,this,[this]{cancelled_=true;cancelButton_->setEnabled(false);statusBar()->showMessage(QStringLiteral("正在取消，等待当前操作安全结束…"));});
    connect(wellBox_,&QComboBox::currentIndexChanged,this,[this]{if(!refreshing_)refreshEntries();});connect(kindBox_,&QComboBox::currentIndexChanged,this,[this]{if(!refreshing_){refreshWellSelector(currentWellId());refreshEntries();}});
    connect(relevantWellsBox_,&QCheckBox::toggled,this,[this]{if(!refreshing_){refreshWellSelector(currentWellId());refreshEntries();}});
    connect(entryList_,&QListWidget::currentRowChanged,this,[this]{pageOffset_=0;if(!refreshing_){curvePlot_->clear();imagePreview_->clear();querySelected();}});
    connect(filterEdit_,&QLineEdit::textChanged,this,[this](const QString& text){int first=-1;for(int i=0;i<entryList_->count();++i){const bool match=entryList_->item(i)->text().contains(text,Qt::CaseInsensitive);entryList_->item(i)->setHidden(!match);if(match&&first<0)first=i;}if(!entryList_->currentItem()||entryList_->currentItem()->isHidden())entryList_->setCurrentRow(first);});
    connect(unresolvedBox_,&QCheckBox::toggled,this,[this]{if(!service_.projectPath().empty())refreshSourcesAndIssues();});
    connect(issuesTable_,&QTableWidget::itemSelectionChanged,this,[this]{int r=issuesTable_->currentRow();if(r>=0&&r<static_cast<int>(issues_.size())){const auto& i=issues_[r];issueDetails_->setPlainText(QStringLiteral("%1\n工作表：%2　行：%3\n原值：%4\n核对结论：%5").arg(q(i.message),q(i.sheet)).arg(i.row).arg(q(i.originalText),q(i.resolution)));}});
    connect(dataTable_,&QTableWidget::cellClicked,this,[this](int row,int){
        if(currentKind()!="core_photos"||row<0||row>=static_cast<int>(currentTable_.rows.size()))return;
        const auto& cells=currentTable_.rows[size_t(row)];if(cells.size()<4)return;QString error;
        imagePreview_->clear();if(imagePreview_->load(q(cells[3]),error))tabs_->setCurrentIndex(2);else statusBar()->showMessage(QStringLiteral("照片预览失败：")+error);
    });
    connect(dataTable_,&QTableWidget::cellDoubleClicked,this,[this](int,int){if(currentKind()=="intervals")editInterval(false);else if(currentKind()=="curves")editCurveValue();});
}
bool MainWindow::ready(){if(service_.projectPath().empty()){QMessageBox::information(this,QStringLiteral("工区"),QStringLiteral("请先新建或打开一个工区。"));return false;}return true;}
bool MainWindow::requireIdle(){if((worker_&&worker_->isRunning())||(apiDemo_&&apiDemo_->isRunning())){QMessageBox::information(this,QStringLiteral("任务进行中"),QStringLiteral("请等待当前任务结束，或先取消任务。"));return false;}return true;}
void MainWindow::showError(const std::string& error){QMessageBox::warning(this,QStringLiteral("操作未完成"),error.empty()?QStringLiteral("服务没有返回成功结果。"):q(error));}
void MainWindow::newProject(){if(!requireIdle())return;QString parent=QFileDialog::getExistingDirectory(this,QStringLiteral("选择新工区的父目录"));if(parent.isEmpty())return;bool ok=false;QString name=QInputDialog::getText(this,QStringLiteral("新建工区"),QStringLiteral("工区名称（同时作为目录名称）"),QLineEdit::Normal,QStringLiteral("新建工区"),&ok).trimmed();if(!ok||name.isEmpty())return;if(name.contains('/')||name.contains('\\')||name=="."||name==".."){showError("工区名称不能包含路径分隔符。");return;}std::string error;const auto path=QDir(parent).filePath(name);if(!service_.createProject(u(path),u(name),error)){showError(error);return;}refreshAll();}
bool MainWindow::openProjectPath(const QString& path,bool showFailureDialog){if(!requireIdle())return false;std::string error;if(!service_.openProject(u(path),error)){if(showFailureDialog)showError(error);else qWarning()<<q(error);return false;}refreshAll();return true;}
void MainWindow::chooseOpenProject(){QString path=QFileDialog::getExistingDirectory(this,QStringLiteral("打开包含 data.sqlite 的工区目录"));if(!path.isEmpty())openProjectPath(path);}
void MainWindow::selectKind(const QString& kind){const int index=kindBox_->findData(kind);if(index>=0)kindBox_->setCurrentIndex(index);}
void MainWindow::showApiDemo(const QString& example){modeTabs_->setCurrentIndex(0);if(!example.isEmpty())apiDemo_->setExample(example);}
void MainWindow::showDataManager(){modeTabs_->setCurrentIndex(1);}
void MainWindow::refreshWellSelector(long long preferred){
    QSignalBlocker blocker(wellBox_);wellBox_->clear();const auto kind=currentKind();
    const bool byWell=kind=="curves"||kind=="intervals"||kind=="samples"||kind=="time_depth"||kind=="trajectories"||kind=="core_photos"||kind=="attachments"||kind=="markers";
    wellBox_->addItem(byWell?QStringLiteral("工区全部资料"):QStringLiteral("工区级资料（与单井无关）"),QVariant::fromValue<qlonglong>(0));
    wellBox_->setEnabled(byWell);relevantWellsBox_->setEnabled(byWell);
    const auto withCurves=std::count_if(wells_.begin(),wells_.end(),[](const auto& w){return w.curves>0;});
    int available=0;
    for(const auto& w:wells_){if(countForKind(w,kind)>0)++available;if(!byWell||(relevantWellsBox_->isChecked()&&countForKind(w,kind)<=0))continue;wellBox_->addItem(q(w.name)+" · "+wellContents(w),QVariant::fromValue<qlonglong>(w.id));wellBox_->setItemData(wellBox_->count()-1,q(w.name)+"\n"+wellContents(w),Qt::ToolTipRole);}
    const int selected=wellBox_->findData(QVariant::fromValue<qlonglong>(preferred));wellBox_->setCurrentIndex(selected>=0?selected:0);
    wellSummaryLabel_->setText(QStringLiteral("%1 口井：%2 口有曲线，%3 口暂无曲线。\n井可仅含坐标、时深等资料。").arg(wells_.size()).arg(withCurves).arg(wells_.size()-withCurves)+(byWell?QStringLiteral("\n当前类型：%1 口井有资料。").arg(available):QString()));
}
void MainWindow::refreshAll(){if(service_.projectPath().empty())return;const long long selected=currentWellId();std::string error;if(!service_.listWells(wells_,error)){showError(error);return;}refreshWellSelector(selected);projectLabel_->setText("  "+q(service_.projectName())+"  ");projectLabel_->setToolTip(q(service_.projectPath()));setWindowTitle(q(service_.projectName())+QStringLiteral(" — 多源数据服务原型"));refreshEntries();refreshSourcesAndIssues();apiDemo_->setProjectPath(q(service_.projectPath()));statusBar()->showMessage(QStringLiteral("已打开 %1 · %2 口井").arg(q(service_.projectName())).arg(wells_.size()));}
std::string MainWindow::currentKind()const{return u(kindBox_->currentData().toString());}
long long MainWindow::currentWellId()const{return wellBox_->currentData().toLongLong();}
const DataEntry* MainWindow::currentEntry()const{const int row=entryList_->currentRow();return row>=0&&row<static_cast<int>(entries_.size())?&entries_[row]:nullptr;}
void MainWindow::refreshEntries(){if(service_.projectPath().empty())return;std::string error;const auto kind=currentKind();const auto* previous=currentEntry();const auto previousId=previous?previous->id:0;if(!service_.listData(kind,currentWellId(),entries_,error)){showError(error);return;}refreshing_=true;entryList_->clear();int selected=-1;for(size_t i=0;i<entries_.size();++i){const auto& e=entries_[i];QString detail=q(e.detail);if(e.kind=="time_depth"&&e.wellId==0)detail=QStringLiteral("待归井 · ")+detail;for(const auto& well:wells_)if(well.id==e.wellId){detail=q(well.name)+QStringLiteral(" · ")+detail;break;}auto* item=new QListWidgetItem(q(e.name)+QStringLiteral("\n%1 项 · %2").arg(e.count).arg(detail),entryList_);item->setToolTip(detail);if(e.id==previousId)selected=static_cast<int>(i);}entryList_->setCurrentRow(selected>=0?selected:entries_.empty()?-1:0);refreshing_=false;pageOffset_=0;curvePlot_->clear();imagePreview_->clear();int firstVisible=-1;for(int i=0;i<entryList_->count();++i){const bool match=entryList_->item(i)->text().contains(filterEdit_->text(),Qt::CaseInsensitive);entryList_->item(i)->setHidden(!match);if(match&&firstVisible<0)firstVisible=i;}if(entryList_->currentItem()&&entryList_->currentItem()->isHidden()){QSignalBlocker blocker(entryList_);entryList_->setCurrentRow(firstVisible);}querySelected();}
void MainWindow::fillTable(QTableWidget* widget,const DataTable& data){widget->setUpdatesEnabled(false);widget->clear();widget->setColumnCount(static_cast<int>(data.columns.size()));widget->setRowCount(static_cast<int>(data.rows.size()));QStringList headers;for(const auto& c:data.columns)headers<<q(c);widget->setHorizontalHeaderLabels(headers);for(size_t r=0;r<data.rows.size();++r)for(size_t c=0;c<data.rows[r].size()&&c<data.columns.size();++c){auto* item=new QTableWidgetItem(q(data.rows[r][c]));item->setToolTip(item->text());widget->setItem(static_cast<int>(r),static_cast<int>(c),item);}for(int c=0;c<widget->columnCount();++c)widget->setColumnWidth(c,c==0?90:160);widget->setUpdatesEnabled(true);}
bool MainWindow::depthRange(double& top,double& bottom){top=bottom=Missing;bool ok=true;if(!topEdit_->text().trimmed().isEmpty()){top=topEdit_->text().toDouble(&ok);if(!ok||!std::isfinite(top)){showError("顶深应为有限数值，或留空。");return false;}}if(!bottomEdit_->text().trimmed().isEmpty()){bottom=bottomEdit_->text().toDouble(&ok);if(!ok||!std::isfinite(bottom)){showError("底深应为有限数值，或留空。");return false;}}if(std::isfinite(top)&&std::isfinite(bottom)&&top>bottom){showError("顶深不能大于底深。");return false;}return true;}
void MainWindow::querySelected(){const auto* e=currentEntry();if(!e){currentTable_={};totalRows_=0;fillTable(dataTable_,currentTable_);curvePlot_->clear();imagePreview_->clear();QString note=QStringLiteral("当前分类没有已入库的资料。可查看“来源清单 / 待核对”确认文件处理状态。");for(const auto& w:wells_)if(w.id==currentWellId())note=q(w.name)+QStringLiteral("暂无此类资料。已收录：")+wellContents(w)+QStringLiteral("。井存在不代表已导入测井曲线。");detailLabel_->setText(note);pageLabel_->setText(QStringLiteral("0 行"));return;}const auto kind=currentKind();std::string error;detailLabel_->setText(q(e->name)+"  ·  "+q(e->detail));
    if(kind=="intervals"){
        double top,bottom;if(!depthRange(top,bottom))return;std::vector<IntervalRecord> records;
        if(!service_.readIntervals(e->id,top,bottom,records,error)){showError(error);return;}
        currentTable_={};currentTable_.columns={"编号","名称 / 岩性","顶深","记录底深","相类型","顶部不整合","底部不整合","底部未钻穿","原始行"};currentTable_.totalRows=static_cast<long long>(records.size());if(pageOffset_>=currentTable_.totalRows)pageOffset_=currentTable_.totalRows?((currentTable_.totalRows-1)/200)*200:0;
        auto value=[](double d){return std::isfinite(d)?QString::number(d,'g',16).toStdString():std::string();};auto flag=[](int v){return v==1?"是":v==0?"否":"未知";};
        for(long long i=pageOffset_;i<std::min<long long>(pageOffset_+200,records.size());++i){const auto& r=records[static_cast<size_t>(i)];const auto& v=r.value;currentTable_.rows.push_back({std::to_string(r.id),v.intervalName,value(v.topDepth),value(v.bottomDepth),v.faciesType,flag(v.topUnconformity),flag(v.bottomUnconformity),flag(v.bottomNotPenetrated),std::to_string(v.sourceRow)});}
    }else if(!service_.readTable(kind,e->id,pageOffset_,200,currentTable_,error)){showError(error);return;}
    if(pageOffset_>=currentTable_.totalRows&&pageOffset_>0){pageOffset_=currentTable_.totalRows?((currentTable_.totalRows-1)/200)*200:0;querySelected();return;}fillTable(dataTable_,currentTable_);if(kind=="intervals"){const int widths[]={60,180,90,90,100,95,95,105,75};for(int c=0;c<9;++c)dataTable_->setColumnWidth(c,widths[c]);}totalRows_=currentTable_.totalRows;pageLabel_->setText(QStringLiteral("%1–%2 / %3 行").arg(totalRows_?pageOffset_+1:0).arg(pageOffset_+static_cast<long long>(currentTable_.rows.size())).arg(totalRows_));
    if(kind=="curves"){double top,bottom;if(!depthRange(top,bottom))return;if(!service_.readCurveSlice(e->id,top,bottom,processedBox_->isChecked(),currentCurve_,error)){showError(error);return;}curvePlot_->setCurve(currentCurve_);detailLabel_->setText(q(e->name)+QStringLiteral(" · 数据表按原始索引分页；深度范围用于曲线预览。"));}
    if(kind=="core_photos"&&!currentTable_.rows.empty()){
        QString imageError;imagePreview_->clear();if(!imagePreview_->load(q(currentTable_.rows.front()[3]),imageError))statusBar()->showMessage(QStringLiteral("照片预览失败：")+imageError);
        detailLabel_->setText(q(e->name)+QStringLiteral(" · 点击数据表中的照片可预览；空深度表示尚未确认。"));
    }
    if(kind=="maps"){GeologicalMap map;if(!service_.readMap(e->id,map,error)){showError(error);return;}QString path=q(map.imagePath);if(QDir::isRelativePath(path))path=QDir(q(service_.projectPath())).filePath(path);QString imageError;if(!imagePreview_->load(path,imageError))statusBar()->showMessage(QStringLiteral("图件预览失败：")+imageError);else tabs_->setCurrentIndex(2);}
}
void MainWindow::refreshSourcesAndIssues(){std::string error;if(!service_.listSources(sources_,error)){showError(error);return;}DataTable data;data.columns={"编号","状态","类别","来源路径","字节数","说明"};for(const auto& s:sources_)data.rows.push_back({std::to_string(s.id),u(statusName(s.status)),s.kind,s.path,std::to_string(s.bytes),s.message});fillTable(sourcesTable_,data);const int sourceWidths[]={65,85,100,280,90,280};for(int c=0;c<6;++c)sourcesTable_->setColumnWidth(c,sourceWidths[c]);if(!service_.listIssues(unresolvedBox_->isChecked(),issues_,error)){showError(error);return;}data={};data.columns={"问题编号","来源编号","状态","代码","来源行","问题说明"};for(const auto& i:issues_)data.rows.push_back({std::to_string(i.id),std::to_string(i.sourceId),i.resolved?"已核对":i.blocking?"阻止归并":"待核对",i.code,std::to_string(i.row),i.message});fillTable(issuesTable_,data);const int issueWidths[]={70,70,100,180,60,360};for(int c=0;c<6;++c)issuesTable_->setColumnWidth(c,issueWidths[c]);issueDetails_->clear();if(!issues_.empty())issuesTable_->selectRow(0);}
void MainWindow::setBusy(bool busy){for(auto* a:lockedActions_)a->setEnabled(!busy);centralWidget()->setEnabled(!busy);progress_->setVisible(busy);cancelButton_->setVisible(busy);cancelButton_->setEnabled(busy);if(busy){progress_->setRange(0,0);cancelled_=false;}}
void MainWindow::importDirectory(){
    if(!ready()||!requireIdle())return;
    const QString path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择待导入资料目录"));
    if(path.isEmpty())return;
    QDialog choices(this);choices.setWindowTitle(QStringLiteral("导入资料"));choices.setMinimumWidth(560);
    auto* layout=new QVBoxLayout(&choices);
    auto* explanation=new QLabel(QStringLiteral("引用原文件可以省去资料复制，适合大量数据。原文件移动后需要重新定位来源。\n"
        "也可以复制普通资料到工区，便于以后搬移工区。地震体始终只保存外部路径。\n\n"
        "再次导入时，默认快速跳过未变化文件；新增或变化文件仍会完整校验并解析。"));
    explanation->setWordWrap(true);layout->addWidget(explanation);
    auto* form=new QFormLayout;layout->addLayout(form);
    auto* threads=new QComboBox(&choices);threads->setObjectName("importParserThreads");
    threads->addItem(QStringLiteral("1 个线程（串行对照）"),1);
    threads->addItem(QStringLiteral("2 个线程（推荐）"),2);
    threads->addItem(QStringLiteral("4 个线程"),4);threads->setCurrentIndex(1);
    threads->setToolTip(QStringLiteral("多个文件可以同时校验和解析；数据库按文件顺序保存。大文件和待处理结果受到内存预算限制。"));
    form->addRow(QStringLiteral("解析线程数"),threads);
    auto* strict=new QCheckBox(QStringLiteral("完整校验每个文件（较慢）"),&choices);
    strict->setObjectName("verifyUnchangedFiles");
    strict->setToolTip(QStringLiteral("关闭时先检查文件变化信息；打开后重新校验每个文件的身份。地震体仍使用文件头、大小和修改时间核验，不通读整个地震体。"));
    strict->setChecked(false);layout->addWidget(strict);
    auto* buttons=new QDialogButtonBox(&choices);layout->addWidget(buttons);
    auto* reference=buttons->addButton(QStringLiteral("引用原文件（推荐）"),QDialogButtonBox::AcceptRole);
    auto* copy=buttons->addButton(QStringLiteral("复制普通资料"),QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);reference->setDefault(true);
    bool copySources=false;
    connect(reference,&QPushButton::clicked,&choices,[&]{copySources=false;choices.accept();});
    connect(copy,&QPushButton::clicked,&choices,[&]{copySources=true;choices.accept();});
    connect(buttons,&QDialogButtonBox::rejected,&choices,&QDialog::reject);
    if(choices.exec()!=QDialog::Accepted)return;
    runImport(path,0,{},copySources,strict->isChecked(),threads->currentData().toInt());
}
void MainWindow::runImport(const QString& path,long long sourceId,const ParseOptions& mapping,bool copySources,bool verifyUnchangedFiles,int parserThreads){
    if(!requireIdle())return;
    const auto project=service_.projectPath();setBusy(true);
    statusBar()->showMessage(sourceId?QStringLiteral("准备重新导入来源…"):QStringLiteral("正在扫描目录…"));
    QPointer<MainWindow> guard(this);
    worker_=QThread::create([this,guard,project,path,sourceId,mapping,copySources,verifyUnchangedFiles,parserThreads]{
        bool ok=false;ImportReport report;std::string error;
        try{
            DataService workerService;
            if(workerService.openProject(project,error)){
                auto cancel=[this]{return cancelled_.load();};
                if(sourceId)ok=workerService.reimportSource(sourceId,mapping,report,error,cancel);
                else{
                    ImportOptions options;options.copySources=copySources;options.parsing=mapping;
                    options.verifyUnchangedFiles=verifyUnchangedFiles;options.parserThreads=parserThreads;
                    // Only one GUI update may wait in the event queue. A fast batch can produce
                    // many stage changes; replace the waiting value with the latest progress.
                    struct PendingProgress { std::mutex mutex;ImportProgress value;bool queued=false; };
                    auto pending=std::make_shared<PendingProgress>();
                    QElapsedTimer clock;clock.start();qint64 lastPosted=-100;
                    std::string lastStage;
                    auto progress=[guard,pending,&clock,&lastPosted,&lastStage](const ImportProgress& p){
                        if(!guard)return;
                        const qint64 now=clock.elapsed();
                        const bool finished=p.total>0&&p.completed>=p.total;
                        if(!finished&&p.status==lastStage&&now-lastPosted<100)return;
                        lastPosted=now;lastStage=p.status;
                        {
                            std::lock_guard<std::mutex> lock(pending->mutex);
                            pending->value=p;
                            if(pending->queued)return;
                            pending->queued=true;
                        }
                        QMetaObject::invokeMethod(guard.data(),[guard,pending]{
                            ImportProgress current;
                            {
                                std::lock_guard<std::mutex> lock(pending->mutex);
                                current=pending->value;pending->queued=false;
                            }
                            if(!guard)return;
                            if(current.total<=0)guard->progress_->setRange(0,0);
                            else{
                                guard->progress_->setRange(0,current.total);
                                guard->progress_->setValue(current.completed);
                            }
                            const QString counter=current.total>0?
                                QStringLiteral("%1 / %2　").arg(current.completed).arg(current.total):QString();
                            guard->statusBar()->showMessage(counter+statusName(current.status)+QStringLiteral("　")+q(current.file));
                        },Qt::QueuedConnection);
                    };
                    ok=workerService.importDirectory(u(path),options,report,error,progress,cancel);
                }
            }
        }catch(const std::exception& e){error=e.what();}
        catch(...){error="后台导入发生未知错误。";}
        if(guard)QMetaObject::invokeMethod(guard.data(),[guard,ok,report,error]{if(guard)guard->finishImport(ok,report,error);},Qt::QueuedConnection);
    });
    observeWorker();
}
void MainWindow::finishImport(bool ok,const ImportReport& r,const std::string& error){
    setBusy(false);if(closeWhenIdle_)return;refreshAll();
    QString summary=QStringLiteral("扫描 %1　结构化 %2　仅登记 %3　待核对 %4\n失败 %5　忽略 %6　重复跳过 %7（其中快速跳过 %8）")
        .arg(r.seen).arg(r.imported).arg(r.registered).arg(r.review).arg(r.failed).arg(r.ignored).arg(r.skipped).arg(r.cached);
    auto seconds=[](long long ms){return QString::number(ms/1000.0,'f',2);};
    summary+=QStringLiteral("\n解析线程 %1").arg(r.workerCount);
    summary+=QStringLiteral("\n总耗时 %1 秒\n各文件累计：校验 %2 秒 · 解析 %3 秒 · 复制 %4 秒 · 入库 %5 秒")
        .arg(seconds(r.elapsedMs),seconds(r.hashMs),seconds(r.parseMs),seconds(r.copyMs),seconds(r.storeMs));
    if(r.cancelled)summary+=QStringLiteral("\n任务已取消，未提交文件已回滚。");
    if(!error.empty())summary+="\n"+q(error);
    QMessageBox box(ok?QMessageBox::Information:QMessageBox::Warning,QStringLiteral("导入结果"),summary,QMessageBox::Ok,this);
    QStringList errors;for(const auto& e:r.errors)errors<<q(e);
    if(!errors.isEmpty())box.setDetailedText(errors.join('\n'));
    box.exec();tabs_->setCurrentIndex(r.review||r.failed?4:3);
}
long long MainWindow::selectedSourceId()const{if(tabs_->currentIndex()==4){int row=issuesTable_->currentRow();return row>=0&&row<static_cast<int>(issues_.size())?issues_[row].sourceId:0;}if(tabs_->currentIndex()==3){int row=sourcesTable_->currentRow();return row>=0&&row<static_cast<int>(sources_.size())?sources_[row].id:0;}const auto* e=currentEntry();return e?e->sourceId:0;}
void MainWindow::openSource(){if(!ready())return;auto id=selectedSourceId();if(!id){showError("请先选择一条有来源的资料或来源记录。");return;}std::string path,error;if(!service_.sourcePath(id,path,error)){showError(error);return;}QString file=q(path);if(QDir::isRelativePath(file))file=QDir(q(service_.projectPath())).filePath(file);if(!QFileInfo::exists(file)){showError("来源文件不存在，请检查文件是否已移动。");return;}if(!QDesktopServices::openUrl(QUrl::fromLocalFile(file)))showError("系统未能打开该文件，请检查是否安装了相应阅读软件。");}
void MainWindow::showDocument(){if(!ready())return;auto id=selectedSourceId();if(!id){showError("请先选择有来源的资料。");return;}std::string text,error;if(!service_.readDocument(id,text,error)){showError(error);return;}auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("已提取的报告正文"));dialog->resize(800,620);auto* layout=new QVBoxLayout(dialog);auto* view=new QPlainTextEdit;view->setReadOnly(true);view->setPlainText(text.empty()?QStringLiteral("此文件尚未提取正文。可使用“打开来源”查看原文件；这不表示原文件没有内容。"):q(text));layout->addWidget(view);dialog->show();}
void MainWindow::backupDatabase(){if(!ready()||!requireIdle())return;const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("备份业务数据库"),QDir(q(service_.projectPath())).filePath("data-backup.sqlite"),"SQLite (*.sqlite)");if(path.isEmpty())return;std::string error;if(!service_.backupDatabase(u(path),error)){showError(error);return;}QMessageBox::information(this,QStringLiteral("备份完成"),QStringLiteral("数据库已备份。图片、原始资料和外部地震文件需要随工区另行保存。"));}

void MainWindow::observeWorker(){auto* task=worker_;connect(task,&QThread::finished,this,[this,task]{if(worker_==task)worker_=nullptr;task->deleteLater();if(closeWhenIdle_)close();});task->start();}
