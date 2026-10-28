#include "mainwindow.h"
#include "plotwidgets.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <exception>
#include <cmath>

using namespace paleo::data;
namespace {
QString q(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
std::string u(const QString& s){return s.toUtf8().toStdString();}
QLineEdit* field(QFormLayout* form,const QString& label,const std::string& value){auto* edit=new QLineEdit(q(value));form->addRow(label,edit);return edit;}
QLineEdit* numberField(QFormLayout* form,const QString& label,double value){auto* edit=new QLineEdit(std::isfinite(value)?QString::number(value,'g',16):QString());edit->setPlaceholderText(QStringLiteral("未知留空，不推断为 0"));form->addRow(label,edit);return edit;}
bool parseNumber(QLineEdit* edit,double& value){if(edit->text().trimmed().isEmpty()){value=Missing;return true;}bool ok=false;value=edit->text().toDouble(&ok);return ok&&std::isfinite(value);}
void buttons(QDialog& d,QVBoxLayout* layout){auto* b=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);QObject::connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);layout->addWidget(b);}
}

void MainWindow::editSeismicMetadata(){
    if(!ready()||!requireIdle())return;
    const auto* entry=currentEntry();
    if(!entry||(currentKind()!="volumes"&&currentKind()!="horizons")){showError("请先选择地震体或层位。");return;}
    const long long id=entry->id;
    const bool isVolume=currentKind()=="volumes";
    SeismicVolume volume;SeismicHorizon horizon;std::string error;
    if(isVolume){if(!service_.readVolume(id,volume,error)){showError(error);return;}}
    else if(!service_.readHorizon(id,0,1,horizon,error)){showError(error);return;}

    QDialog d(this);d.resize(630,560);d.setWindowTitle(isVolume?QStringLiteral("地震体信息与约定"):QStringLiteral("地震层位信息与关联"));
    auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);
    auto* name=field(form,QStringLiteral("名称"),isVolume?volume.volumeName:horizon.horizonName);
    QComboBox* parent=nullptr;
    if(!isVolume){
        std::vector<DataEntry> volumes;if(!service_.listData("volumes",0,volumes,error)){showError(error);return;}
        parent=new QComboBox;parent->addItem(QStringLiteral("未关联"),0);
        for(const auto& v:volumes)parent->addItem(q(v.name),QVariant::fromValue<qlonglong>(v.id));
        const int index=parent->findData(QVariant::fromValue<qlonglong>(horizon.parentVolumeId));parent->setCurrentIndex(index>=0?index:0);form->addRow(QStringLiteral("所属地震体"),parent);
    }
    auto* crs=field(form,QStringLiteral("平面坐标系"),isVolume?volume.geometry.coordinateSystem:horizon.coordinateSystem);
    auto* coordinateUnit=field(form,QStringLiteral("坐标单位"),isVolume?volume.geometry.coordinateUnit:horizon.coordinateUnit);
    auto* timeUnit=field(form,QStringLiteral("时间单位"),isVolume?volume.sampling.timeUnit:horizon.timeUnit);
    auto* timeType=new QComboBox;timeType->addItem(QStringLiteral("单 / 双程未确认"),"");timeType->addItem(QStringLiteral("TWT · 双程时间"),"TWT");timeType->addItem(QStringLiteral("OWT · 单程时间"),"OWT");
    const auto type=isVolume?volume.sampling.timeType:horizon.timeType;const int index=timeType->findData(q(type));
    if(index<0&&!type.empty()){timeType->addItem(q(type),q(type));timeType->setCurrentIndex(timeType->count()-1);}else timeType->setCurrentIndex(index>=0?index:0);
    form->addRow(QStringLiteral("时间含义"),timeType);
    auto* reference=field(form,QStringLiteral("时间零点 / 参考面"),isVolume?volume.sampling.timeReference:horizon.timeReference);
    QLineEdit *amplitude=nullptr,*firstTime=nullptr,*sampleInterval=nullptr,*sampleCount=nullptr;
    if(isVolume){
        amplitude=field(form,QStringLiteral("振幅单位 / 说明"),volume.amplitudeUnit);
        firstTime=numberField(form,QStringLiteral("首样点时间"),volume.sampling.firstTime);
        sampleInterval=numberField(form,QStringLiteral("采样时间间隔"),volume.sampling.sampleInterval);
        sampleCount=field(form,QStringLiteral("每道样点数"),volume.sampling.sampleCount>0?std::to_string(volume.sampling.sampleCount):std::string());sampleCount->setPlaceholderText(QStringLiteral("未知留空；须与文件道头一致"));
        auto* details=new QLabel(QStringLiteral("来源：%1\n道数：%2　IL：%3–%4　XL：%5–%6\n每道 %7 样点　起始时间 %8　间隔 %9 %10")
            .arg(q(volume.dataFile)).arg(volume.geometry.traceCount).arg(volume.geometry.inlineMin).arg(volume.geometry.inlineMax).arg(volume.geometry.crosslineMin).arg(volume.geometry.crosslineMax)
            .arg(volume.sampling.sampleCount).arg(volume.sampling.firstTime).arg(volume.sampling.sampleInterval).arg(q(volume.sampling.timeUnit)));
        details->setWordWrap(true);details->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(details);
    }
    auto* note=new QLabel(QStringLiteral("按已经确认的资料填写，未知项留空。修改单位名称不会换算数值。\n此处仅保存名称、关联和坐标 / 时间约定，层位点与地震振幅保持原值。"));note->setWordWrap(true);layout->addWidget(note);buttons(d,layout);
    if(d.exec()!=QDialog::Accepted)return;
    if(isVolume){
        volume.volumeName=u(name->text().trimmed());volume.geometry.coordinateSystem=u(crs->text().trimmed());volume.geometry.coordinateUnit=u(coordinateUnit->text().trimmed());
        volume.sampling.timeUnit=u(timeUnit->text().trimmed());volume.sampling.timeType=u(timeType->currentData().toString());volume.sampling.timeReference=u(reference->text().trimmed());volume.amplitudeUnit=u(amplitude->text().trimmed());
        bool countOk=true;volume.sampling.sampleCount=sampleCount->text().trimmed().isEmpty()?0:sampleCount->text().toInt(&countOk);
        if(!countOk||volume.sampling.sampleCount<0||!parseNumber(firstTime,volume.sampling.firstTime)||!parseNumber(sampleInterval,volume.sampling.sampleInterval)||(std::isfinite(volume.sampling.sampleInterval)&&volume.sampling.sampleInterval<=0)){showError("首样点时间须为有限数值或留空，间隔须为正数或留空，样点数须为正整数或留空。请核对这些值与文件中的道头一致。");return;}
        if(!service_.saveVolume(id,volume,error)){showError(error);return;}
    }else{
        horizon.horizonName=u(name->text().trimmed());horizon.parentVolumeId=parent->currentData().toInt();horizon.coordinateSystem=u(crs->text().trimmed());horizon.coordinateUnit=u(coordinateUnit->text().trimmed());
        horizon.timeUnit=u(timeUnit->text().trimmed());horizon.timeType=u(timeType->currentData().toString());horizon.timeReference=u(reference->text().trimmed());
        if(!service_.saveHorizonInfo(id,horizon,error)){showError(error);return;}
    }
    refreshEntries();statusBar()->showMessage(QStringLiteral("地震资料信息已保存。"));
}

void MainWindow::extractTimeSlice(){
    if(!ready()||!requireIdle())return;const auto* entry=currentEntry();
    if(!entry||currentKind()!="volumes"){showError("请先选择一个地震体，再读取时间切片。");return;}
    SeismicVolume volume;std::string error;if(!service_.readVolume(entry->id,volume,error)){showError(error);return;}
    QDialog d(this);d.setWindowTitle(QStringLiteral("读取水平时间切片"));auto* layout=new QVBoxLayout(&d);auto* form=new QFormLayout;layout->addLayout(form);
    auto* time=new QDoubleSpinBox;time->setRange(-1e12,1e12);time->setDecimals(6);time->setValue(std::isfinite(volume.sampling.firstTime)?volume.sampling.firstTime:0);form->addRow(QStringLiteral("时间（%1）").arg(volume.sampling.timeUnit.empty()?QStringLiteral("单位未确认"):q(volume.sampling.timeUnit)),time);
    auto* hd=new QCheckBox(QStringLiteral("原始分辨率（保留全部 IL / XL 网格）"));hd->setChecked(true);form->addRow(hd);
    auto* note=new QLabel(QStringLiteral("按最近时间样点读取。高清模式保留全部网格，超过 6400 万点时明确报错。\n取消勾选则读取最多 512 × 512 的快速预览；完整切片的磁盘读取可能较慢，可取消。\n坐标与时间约定需先确认。图形区支持滚轮缩放、拖动、1:1 和原尺寸 PNG 导出。"));note->setWordWrap(true);layout->addWidget(note);buttons(d,layout);if(d.exec()!=QDialog::Accepted)return;
    TimeSliceReadOptions options;options.fullResolution=hd->isChecked();options.maxValues=64000000;
    const auto id=entry->id;const auto project=service_.projectPath();const double requested=time->value();setBusy(true);QPointer<MainWindow> guard(this);
    worker_=QThread::create([this,guard,id,project,requested,options]{
        bool ok=false;std::string error;SeismicTimeSliceResult result;
        try{DataService service;ok=service.openProject(project,error)&&service.readTimeSlice(id,requested,options,result,error,[this]{return cancelled_.load();});}catch(const std::exception& e){error=e.what();}catch(...){error="后台时间切片读取发生未知错误。";}
        if(guard)QMetaObject::invokeMethod(guard.data(),[guard,ok,result=std::move(result),error]{
            if(!guard)return;guard->setBusy(false);if(guard->closeWhenIdle_)return;if(!ok){guard->showError(error);return;}
            guard->imagePreview_->setTimeSlice(result);guard->tabs_->setCurrentIndex(2);guard->statusBar()->showMessage(QStringLiteral("时间切片：%1 × %2，%3；可用 1:1 查看或导出原尺寸图像。")
                .arg(result.crosslineNumbers.size()).arg(result.inlineNumbers.size()).arg(result.decimated?QStringLiteral("抽样预览"):QStringLiteral("原始网格")));
        },Qt::QueuedConnection);
    });observeWorker();
}

void MainWindow::relocateSource(){
    if(!ready()||!requireIdle())return;const auto id=selectedSourceId();if(!id){showError("请在来源列表中选择一条记录。");return;}
    const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("定位已移动的原始文件"));if(path.isEmpty())return;std::string error;
    if(!service_.relocateSource(id,u(path),error)){showError(error);return;}refreshAll();statusBar()->showMessage(QStringLiteral("来源位置已更新。"));
}
