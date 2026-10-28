#include "mainwindow.h"
#include "plotwidgets.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <exception>

using namespace paleo::data;
namespace {
QString q(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
std::string u(const QString& s){return s.toUtf8().toStdString();}
QLineEdit* edit(QFormLayout* form,const QString& label,const std::string& value={}){auto* e=new QLineEdit(q(value));form->addRow(label,e);return e;}
QLineEdit* number(QFormLayout* form,const QString& label,double value){auto* e=new QLineEdit(std::isfinite(value)?QString::number(value,'g',16):QString());e->setPlaceholderText(QStringLiteral("未知留空"));form->addRow(label,e);return e;}
QComboBox* state(QFormLayout* form,const QString& label,int value){auto* c=new QComboBox;c->addItem(QStringLiteral("未知"),-1);c->addItem(QStringLiteral("否"),0);c->addItem(QStringLiteral("是"),1);c->setCurrentIndex(c->findData(value));form->addRow(label,c);return c;}
void buttons(QDialog& dialog,QVBoxLayout* layout){auto* b=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);QObject::connect(b,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(b,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(b);}
bool parseNumber(QLineEdit* e,double& value){if(e->text().trimmed().isEmpty()){value=Missing;return true;}bool ok=false;value=e->text().toDouble(&ok);return ok&&std::isfinite(value);}
}

void MainWindow::editWell(bool create){if(!ready()||!requireIdle())return;long long id=create?0:currentWellId();if(!create&&!id){showError("请先选择一口井。");return;}WellInfo value;std::string error;if(!create&&!service_.readWellInfo(id,value,error)){showError(error);return;}QDialog d(this);d.setWindowTitle(create?QStringLiteral("新建井"):QStringLiteral("编辑井基础信息"));d.resize(520,500);auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);auto* name=edit(form,QStringLiteral("井名"),value.wellName);auto* field=edit(form,QStringLiteral("油田 / 工区"),value.fieldName);auto* type=edit(form,QStringLiteral("井型"),value.wellType);auto* x=number(form,QStringLiteral("井口 X"),value.wellheadX);auto* y=number(form,QStringLiteral("井口 Y"),value.wellheadY);auto* crs=edit(form,QStringLiteral("坐标系"),value.coordinateSystem);auto* unit=edit(form,QStringLiteral("坐标单位"),value.coordinateUnit);auto* depth=number(form,QStringLiteral("完钻总井深"),value.totalDrilledDepth);auto* remark=edit(form,QStringLiteral("备注"),value.remark);layout->addWidget(new QLabel(QStringLiteral("坐标轴序、单位和基准应来自已核对资料；未知时保持空值。")));buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;value.wellName=u(name->text().trimmed());value.fieldName=u(field->text().trimmed());value.wellType=u(type->text().trimmed());value.coordinateSystem=u(crs->text().trimmed());value.coordinateUnit=u(unit->text().trimmed());value.remark=u(remark->text());if(value.wellName.empty()||!parseNumber(x,value.wellheadX)||!parseNumber(y,value.wellheadY)||!parseNumber(depth,value.totalDrilledDepth)){showError("请填写井名，数值项填写有限数字或留空。");return;}bool ok=create?service_.createWell(value,id,error):service_.saveWellInfo(id,value,error);if(!ok){showError(error);return;}refreshAll();int index=wellBox_->findData(QVariant::fromValue<qlonglong>(id));if(index>=0)wellBox_->setCurrentIndex(index);}

void MainWindow::assignTimeDepthWell(){
    if(!ready()||!requireIdle())return;
    const auto* entry=currentEntry();
    if(!entry||currentKind()!="time_depth"){showError("请先选择一份时深关系资料。");return;}
    const long long id=entry->id,currentWell=entry->wellId;
    std::vector<WellSummary> wells;std::string error;
    if(!service_.listWells(wells,error)){showError(error);return;}
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("确认时深资料所属井"));dialog.resize(500,230);
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    auto* well=new QComboBox;well->addItem(QStringLiteral("待归井 / 撤销现有关联"),QVariant::fromValue<qlonglong>(0));
    for(const auto& candidate:wells)well->addItem(q(candidate.name),QVariant::fromValue<qlonglong>(candidate.id));
    const int index=well->findData(QVariant::fromValue<qlonglong>(currentWell));well->setCurrentIndex(index>=0?index:0);form->addRow(QStringLiteral("所属井"),well);
    auto* note=new QLabel(QStringLiteral("按已经核对的资料选择井名。仅保存归属关系，原始来源与时深数值保持不变。\n需要新增真实井时，请先使用左侧“新建井”入口。"));note->setWordWrap(true);layout->addWidget(note);
    buttons(dialog,layout);if(dialog.exec()!=QDialog::Accepted)return;
    if(!service_.assignTimeDepthWell(id,well->currentData().toLongLong(),error)){showError(error);return;}
    refreshAll();statusBar()->showMessage(QStringLiteral("时深资料的井归属已保存。"));
}

void MainWindow::editInterval(bool create){if(!ready()||!requireIdle())return;const auto* e=currentEntry();if(currentKind()!="intervals"||!e){showError("请先选择一条地层或岩性分层道。");return;}IntervalRecord record;record.trackId=e->id;std::string error;if(!create){int row=dataTable_->currentRow();if(row<0||row>=static_cast<int>(currentTable_.rows.size())||currentTable_.rows[row].empty()){showError("请在数据表中选择需要编辑的层段。");return;}bool ok=false;auto id=q(currentTable_.rows[row][0]).toLongLong(&ok);if(!ok){showError("当前行没有有效记录编号。");return;}std::vector<IntervalRecord> intervals;if(!service_.readIntervals(e->id,Missing,Missing,intervals,error)){showError(error);return;}auto it=std::find_if(intervals.begin(),intervals.end(),[id](const auto& r){return r.id==id;});if(it==intervals.end()){showError("记录已变化，请刷新后重试。");return;}record=*it;}
    QDialog d(this);d.setWindowTitle(create?QStringLiteral("新增层段"):QStringLiteral("编辑层段"));d.resize(540,590);auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);auto* name=edit(form,QStringLiteral("名称 / 岩性描述"),record.value.intervalName);auto* top=number(form,QStringLiteral("顶深"),record.value.topDepth);auto* bottom=number(form,QStringLiteral("记录底深"),record.value.bottomDepth);auto* facies=edit(form,QStringLiteral("相类型"),record.value.faciesType);auto* tvdTop=number(form,QStringLiteral("顶 TVD"),record.value.topTvd);auto* tvdBottom=number(form,QStringLiteral("底 TVD"),record.value.bottomTvd);auto* ssTop=number(form,QStringLiteral("顶 TVDSS"),record.value.topTvdss);auto* ssBottom=number(form,QStringLiteral("底 TVDSS"),record.value.bottomTvdss);auto* topU=state(form,QStringLiteral("顶部不整合"),record.value.topUnconformity);auto* bottomU=state(form,QStringLiteral("底部不整合"),record.value.bottomUnconformity);auto* unpenetrated=state(form,QStringLiteral("底部未钻穿"),record.value.bottomNotPenetrated);layout->addWidget(new QLabel(QStringLiteral("未钻穿时保留实际记录止深，不将它解释为真实地层底界。\n其他层段属性和来源记录保持不变。")));buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;record.value.intervalName=u(name->text().trimmed());record.value.faciesType=u(facies->text().trimmed());if(!parseNumber(top,record.value.topDepth)||!parseNumber(bottom,record.value.bottomDepth)||!parseNumber(tvdTop,record.value.topTvd)||!parseNumber(tvdBottom,record.value.bottomTvd)||!parseNumber(ssTop,record.value.topTvdss)||!parseNumber(ssBottom,record.value.bottomTvdss)){showError("深度须为有限数字或留空。");return;}record.value.topUnconformity=topU->currentData().toInt();record.value.bottomUnconformity=bottomU->currentData().toInt();record.value.bottomNotPenetrated=unpenetrated->currentData().toInt();if(!service_.saveInterval(e->id,record,error)){showError(error);return;}refreshEntries();statusBar()->showMessage(QStringLiteral("层段已保存，关闭重开后仍可读取。"));}

void MainWindow::editCurveValue(){if(!ready()||!requireIdle())return;const auto* e=currentEntry();if(!e||currentKind()!="curves"){showError("请先选择一条测井曲线。");return;}QString defaultIndex="0";const int row=dataTable_->currentRow();if(row>=0&&row<static_cast<int>(currentTable_.rows.size())&&!currentTable_.rows[row].empty())defaultIndex=q(currentTable_.rows[row][0]);QDialog d(this);d.setWindowTitle(QStringLiteral("修改曲线样点"));auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);auto* index=edit(form,QStringLiteral("原始样点索引（从 0 开始）"),u(defaultIndex));auto* value=edit(form,QStringLiteral("新值（留空表示缺失）"),row>=0&&row<static_cast<int>(currentTable_.rows.size())&&currentTable_.rows[row].size()>3?currentTable_.rows[row][3]:std::string());layout->addWidget(new QLabel(QStringLiteral("修改当前处理值，原始曲线保持可恢复。索引是原始数组位置，与当前分页无关。")));buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;bool ok=false;const auto sample=index->text().toLongLong(&ok);double v;if(!ok||sample<0||!parseNumber(value,v)){showError("请输入非负样点索引，数值填写有限数字或留空。");return;}std::string error;if(!service_.updateCurveValue(e->id,sample,v,error)){showError(error);return;}querySelected();}
void MainWindow::cleanCurve(){
    if(!ready()||!requireIdle())return;
    const auto* entry=currentEntry();
    if(!entry||currentKind()!="curves"){showError("请选择一条测井曲线。");return;}
    const long long curveId=entry->id;
    const bool unitUnknown=currentCurve_.unit.empty();
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("按规则清理曲线"));dialog.resize(560,480);
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    auto* type=edit(form,QStringLiteral("适用曲线"),currentCurve_.name);type->setReadOnly(true);
    auto* unit=edit(form,QStringLiteral("已核实的曲线单位"),currentCurve_.unit);
    unit->setReadOnly(!unitUnknown);
    unit->setPlaceholderText(QStringLiteral("核实资料后填写，系统不会猜测单位"));
    auto* unitNote=new QLabel(unitUnknown
        ?QStringLiteral("当前曲线单位未知。填写单位并保存，将先登记为该曲线的已确认单位，再应用相同单位的清理规则；原数值不会换算。")
        :QStringLiteral("已保存的曲线单位在此处只读。清理规则使用相同单位，此操作不会更改或换算已有单位。"));
    unitNote->setWordWrap(true);layout->addWidget(unitNote);
    auto* nulls=edit(form,QStringLiteral("明确空值（逗号分隔）"));nulls->setPlaceholderText(QStringLiteral("仅填写已确认的空值标记"));
    auto* minimum=new QDoubleSpinBox;minimum->setRange(-1e12,1e12);minimum->setDecimals(8);
    auto* maximum=new QDoubleSpinBox;maximum->setRange(-1e12,1e12);maximum->setDecimals(8);maximum->setValue(1000);
    auto* checkMin=new QCheckBox(QStringLiteral("检查下限"));auto* checkMax=new QCheckBox(QStringLiteral("检查上限"));form->addRow(checkMin,minimum);form->addRow(checkMax,maximum);
    auto* replace=new QCheckBox(QStringLiteral("将违反范围的样点设为缺失（否则仅标记）"));layout->addWidget(replace);
    auto* info=new QLabel(QStringLiteral("SP、中子及部分校正曲线可有合法负值。请依据曲线含义和单位设置规则，系统不统一删除负数。原始值始终保留。"));info->setWordWrap(true);layout->addWidget(info);
    buttons(dialog,layout);if(dialog.exec()!=QDialog::Accepted)return;
    CurveCleaningRule rule;rule.curveType=u(type->text().trimmed());rule.unit=u(unit->text().trimmed());rule.checkMinimum=checkMin->isChecked();rule.minimum=minimum->value();rule.checkMaximum=checkMax->isChecked();rule.maximum=maximum->value();rule.replaceInvalidWithMissing=replace->isChecked();
    for(const auto& token:nulls->text().split(QRegularExpression("[,，;；\\s]+"),Qt::SkipEmptyParts)){bool ok=false;const double n=token.toDouble(&ok);if(!ok||!std::isfinite(n)){showError("空值列表中有无法识别的数值。");return;}rule.nullValues.push_back(n);}
    if(rule.checkMinimum&&rule.checkMaximum&&rule.minimum>rule.maximum){showError("下限不能大于上限。");return;}
    if(rule.unit.empty()){showError("请先核实并填写曲线单位，再应用清理规则。单位未知时请保留原始数据。");return;}
    std::string error;
    if(unitUnknown&&!service_.saveCurveUnit(curveId,rule.unit,error)){showError(error);return;}
    if(!service_.cleanCurve(curveId,rule,error)){
        if(unitUnknown){querySelected();showError("曲线单位已确认保存，但清理规则未能应用："+error);}
        else showError(error);
        return;
    }
    refreshEntries();refreshSourcesAndIssues();
}
void MainWindow::resetCurve(){if(!ready()||!requireIdle())return;const auto* e=currentEntry();if(!e||currentKind()!="curves"){showError("请选择一条曲线。");return;}if(QMessageBox::question(this,QStringLiteral("恢复原始曲线"),QStringLiteral("清除当前曲线的处理与编辑结果，恢复为原始数值？"))!=QMessageBox::Yes)return;std::string error;if(!service_.resetCurve(e->id,error)){showError(error);return;}querySelected();}
void MainWindow::deleteSelected(){if(!ready()||!requireIdle())return;const auto* e=currentEntry();if(!e){showError("请先选择资料。");return;}auto kind=currentKind();long long id=e->id;QString label=q(e->name);if(kind=="intervals"){int row=dataTable_->currentRow();if(row<0||row>=static_cast<int>(currentTable_.rows.size())||currentTable_.rows[row].empty()){showError("请选择要删除的层段行。");return;}id=q(currentTable_.rows[row][0]).toLongLong();kind="interval";label=QStringLiteral("层段记录 %1").arg(id);}if(QMessageBox::question(this,QStringLiteral("删除业务记录"),QStringLiteral("删除“%1”？\n将删除其业务记录及所属数据，外部原始文件不会删除。").arg(label))!=QMessageBox::Yes)return;std::string error;if(!service_.deleteData(kind,id,error)){showError(error);return;}refreshAll();}

void MainWindow::resolveIssue(){if(!ready()||!requireIdle())return;const int row=issuesTable_->currentRow();if(row<0||row>=static_cast<int>(issues_.size())){showError("请先选择一条待核对问题。");return;}const auto issue=issues_[row];bool ok=false;QString conclusion=QInputDialog::getMultiLineText(this,QStringLiteral("记录核对结论"),QStringLiteral("%1\n\n说明核对依据和结论。此操作只记录结论，不自动改写数据。\n需调整归井或坐标映射时，请使用“更正映射并重导入”。").arg(q(issue.message)),q(issue.resolution),&ok);if(!ok||conclusion.trimmed().isEmpty())return;std::string error;if(!service_.resolveIssue(issue.id,u(conclusion.trimmed()),error)){showError(error);return;}refreshSourcesAndIssues();}
void MainWindow::reimportSelectedSource(){if(!ready()||!requireIdle())return;const auto id=selectedSourceId();if(!id){showError("请在来源或待核对列表中选择一个文件。");return;}QDialog d(this);d.setWindowTitle(QStringLiteral("核对映射并重新导入"));d.resize(540,440);auto* layout=new QVBoxLayout(&d);auto* info=new QLabel(QStringLiteral("只填写已有依据的映射。重新导入将生成明确的处理批次，原始文件不修改。\n井名覆盖用于解决文件名与内容井名冲突，不能仅凭相似名称合井。"));info->setWordWrap(true);layout->addWidget(info);auto* form=new QFormLayout;layout->addLayout(form);auto* well=edit(form,QStringLiteral("确认的井名（可留空）"));auto* order=new QComboBox;order->addItem(QStringLiteral("坐标轴序未确认"),"");order->addItem(QStringLiteral("原第 2 列为 X，第 3 列为 Y"),"xy");order->addItem(QStringLiteral("原第 2 列为 Y，第 3 列为 X"),"yx");form->addRow(QStringLiteral("无表头坐标表轴序"),order);auto* crs=edit(form,QStringLiteral("确认的坐标系"));auto* coordUnit=edit(form,QStringLiteral("坐标单位"));auto* depthUnit=edit(form,QStringLiteral("深度单位"));buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;ParseOptions mapping;mapping.wellNameOverride=u(well->text().trimmed());mapping.coordinateOrder=u(order->currentData().toString());mapping.coordinateSystem=u(crs->text().trimmed());mapping.coordinateUnit=u(coordUnit->text().trimmed());mapping.depthUnit=u(depthUnit->text().trimmed());runImport({},id,mapping,true);}

void MainWindow::editMap(){if(!ready()||!requireIdle())return;const auto* e=currentEntry();if(!e||currentKind()!="maps"){showError("请先选择一张平面图。");return;}long long id=e->id;GeologicalMap value;std::string error;if(!service_.readMap(id,value,error)){showError(error);return;}QDialog d(this);d.setWindowTitle(QStringLiteral("编辑地质平面图"));d.resize(600,570);auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);auto* name=edit(form,QStringLiteral("图名"),value.mapName);auto* type=edit(form,QStringLiteral("类型"),value.mapType);auto* area=edit(form,QStringLiteral("覆盖地区"),value.areaName);QStringList strata;for(const auto& s:value.stratigraphicNames)strata<<q(s);auto* strat=edit(form,QStringLiteral("对应地层（分号分隔）"),u(strata.join(';')));auto* image=edit(form,QStringLiteral("预览图片路径"),value.imagePath);auto* source=edit(form,QStringLiteral("原始文件路径"),value.sourceFile);auto* location=edit(form,QStringLiteral("原图号 / 页号"),value.sourceLocation);auto* crs=edit(form,QStringLiteral("坐标系"),value.coordinateSystem);auto* georef=new QComboBox;georef->addItem(QStringLiteral("未核实"),-1);georef->addItem(QStringLiteral("无可用配准"),0);georef->addItem(QStringLiteral("配准资料已核实"),1);georef->setCurrentIndex(georef->findData(value.georeferenceStatus));form->addRow(QStringLiteral("配准状态"),georef);auto* georefFile=edit(form,QStringLiteral("配准资料路径"),value.georeferenceFile);auto* desc=edit(form,QStringLiteral("图件说明"),value.description);layout->addWidget(new QLabel(QStringLiteral("配准已核实需要明确坐标系和配准依据；图上刻度本身不等于配准。")));buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;value.mapName=u(name->text().trimmed());value.mapType=u(type->text().trimmed());value.areaName=u(area->text().trimmed());value.stratigraphicNames.clear();for(const auto& s:strat->text().split(QRegularExpression("[;；]"),Qt::SkipEmptyParts))value.stratigraphicNames.push_back(u(s.trimmed()));value.imagePath=u(image->text().trimmed());value.sourceFile=u(source->text().trimmed());value.sourceLocation=u(location->text().trimmed());value.coordinateSystem=u(crs->text().trimmed());value.georeferenceStatus=georef->currentData().toInt();value.georeferenceFile=u(georefFile->text().trimmed());value.description=u(desc->text());if(!service_.saveMap(id,value,error)){showError(error);return;}refreshEntries();}

void MainWindow::editSection(bool create){if(!ready()||!requireIdle())return;long long id=0;SeismicSection value;std::string error;if(!create){const auto* e=currentEntry();if(!e||currentKind()!="sections"){showError("请先选择一条二维剖面。");return;}id=e->id;if(!service_.readSection(id,value,error)){showError(error);return;}}std::vector<DataEntry> volumes;if(!service_.listData("volumes",0,volumes,error)){showError(error);return;}QDialog d(this);d.setWindowTitle(create?QStringLiteral("新建二维剖面路径"):QStringLiteral("编辑二维剖面路径"));d.resize(600,570);auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);auto* name=edit(form,QStringLiteral("剖面名称"),value.sectionName);auto* volume=new QComboBox;volume->addItem(QStringLiteral("待关联地震体"),QVariant::fromValue<qlonglong>(0));for(const auto& v:volumes)volume->addItem(q(v.name),QVariant::fromValue<qlonglong>(v.id));int index=volume->findData(QVariant::fromValue<qlonglong>(value.parentVolumeId));volume->setCurrentIndex(index>=0?index:0);form->addRow(QStringLiteral("所属地震体"),volume);auto* crs=edit(form,QStringLiteral("路径坐标系"),value.coordinateSystem);auto* unit=edit(form,QStringLiteral("路径坐标单位"),value.coordinateUnit);layout->addWidget(new QLabel(QStringLiteral("按起点到终点的顺序，每行填写 X, Y。两点为直线，多点为折线。")));auto* points=new QPlainTextEdit;QStringList lines;for(const auto& p:value.pathPoints)lines<<QString::number(p.x,'g',16)+", "+QString::number(p.y,'g',16);points->setPlainText(lines.join('\n'));points->setPlaceholderText("1000, 2000\n1100, 2100");layout->addWidget(points,1);buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;value.sectionName=u(name->text().trimmed());value.parentVolumeId=volume->currentData().toInt();value.coordinateSystem=u(crs->text().trimmed());value.coordinateUnit=u(unit->text().trimmed());value.pathPoints.clear();for(const auto& line:points->toPlainText().split('\n',Qt::SkipEmptyParts)){auto fields=line.trimmed().split(QRegularExpression("[,，\\s]+"),Qt::SkipEmptyParts);bool ok1=false,ok2=false;if(fields.size()!=2){showError("每行必须恰好包含 X 和 Y 两个数。");return;}double x=fields[0].toDouble(&ok1),y=fields[1].toDouble(&ok2);if(!ok1||!ok2||!std::isfinite(x)||!std::isfinite(y)){showError("路径坐标必须是有限数值。");return;}value.pathPoints.push_back({x,y});}if(!service_.saveSection(id,value,error)){showError(error);return;}selectKind("sections");refreshEntries();}

void MainWindow::extractSection(){
    if(!ready()||!requireIdle())return;
    const auto* entry=currentEntry();
    if(!entry||currentKind()!="sections"){showError("请先选择剖面路径；可用“新建剖面路径”按顺序输入多个 X,Y 点。");return;}
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("提取高清剖面 / 多点剖面墙"));
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    auto* first=number(form,QStringLiteral("开始时间（按地震体时间单位）"),Missing);
    auto* last=number(form,QStringLiteral("结束时间（空为全部）"),Missing);
    auto* hd=new QCheckBox(QStringLiteral("原始分辨率：按原始网格取道，保留全部时间样点"));hd->setChecked(true);form->addRow(hd);
    auto* columns=new QSpinBox;columns->setRange(2,8192);columns->setValue(1000);columns->setEnabled(false);
    form->addRow(QStringLiteral("快速预览最大道数"),columns);
    connect(hd,&QCheckBox::toggled,columns,[columns](bool checked){columns->setEnabled(!checked);});
    auto* note=new QLabel(QStringLiteral("按多个坐标点的顺序生成折线剖面，保留转折点；振幅取自最近原始道。\n单次最多 6400 万个振幅；超限请缩小时间范围，不会自动降低高清精度。\n图形区可切换“剖面墙”，缩放至 1:1，并导出原尺寸剖面或剖面墙 PNG。"));
    note->setWordWrap(true);layout->addWidget(note);buttons(dialog,layout);if(dialog.exec()!=QDialog::Accepted)return;
    SectionReadOptions options;options.fullResolution=hd->isChecked();options.maxValues=64000000;options.maxColumns=columns->value();
    if(!parseNumber(first,options.firstTime)||!parseNumber(last,options.lastTime)){showError("时间请输入有限数字或留空。");return;}
    const auto id=entry->id;const auto project=service_.projectPath();setBusy(true);QPointer<MainWindow> guard(this);
    worker_=QThread::create([this,guard,id,project,options]{
        std::string error;SeismicSectionResult result;bool ok=false;
        try{DataService service;ok=service.openProject(project,error)&&service.extractSection(id,options,result,error,[this]{return cancelled_.load();});}
        catch(const std::exception& e){error=e.what();}catch(...){error="后台剖面读取发生未知错误。";}
        if(guard)QMetaObject::invokeMethod(guard.data(),[guard,ok,result=std::move(result),error]{
            if(!guard)return;guard->setBusy(false);if(guard->closeWhenIdle_)return;if(!ok){guard->showError(error);return;}
            guard->imagePreview_->setSection(result);guard->tabs_->setCurrentIndex(2);
            guard->statusBar()->showMessage(QStringLiteral("剖面：%1 列 × %2 样点，%3；可切换剖面墙或导出原尺寸图像。")
                .arg(result.columns).arg(result.samples).arg(result.decimated?QStringLiteral("抽样预览"):QStringLiteral("原始分辨率")));
        },Qt::QueuedConnection);
    });observeWorker();
}
