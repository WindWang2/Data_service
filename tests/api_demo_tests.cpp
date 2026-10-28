// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include "apidemo.h"
#include "paleo_data/service.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace paleo::data;
namespace {
std::string utf8(const QString& value){return value.toUtf8().toStdString();}
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void writeFile(const QString& path,const QByteArray& contents){QFile file(path);check(file.open(QIODevice::WriteOnly),"open fixture");check(file.write(contents)==contents.size(),"write fixture");}
template<class T>T* control(ApiDemoWidget& widget,const char* name){auto* result=widget.findChild<T*>(name);check(result!=nullptr,std::string("test control missing: ")+name);return result;}
QJsonObject resultObject(const ApiDemoWidget& widget){
    QJsonParseError error;const auto document=QJsonDocument::fromJson(widget.lastResultJson().toUtf8(),&error);
    check(error.error==QJsonParseError::NoError&&document.isObject(),"service result is not a JSON object");return document.object();
}
bool runAndWait(ApiDemoWidget& widget,bool cancelBeforeWorkerStarts=false){
    QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);bool delivered=false,success=false,timedOut=false;
    const auto finished=QObject::connect(&widget,&ApiDemoWidget::runFinished,&loop,[&](bool value){delivered=true;success=value;loop.quit();});
    const auto cancelled=QObject::connect(&widget,&ApiDemoWidget::busyChanged,&loop,[&](bool busy){
        // busyChanged is emitted after the worker is created but before it starts.
        // This makes cancellation deterministic even for a three-point fixture.
        if(busy&&cancelBeforeWorkerStarts)widget.requestCancel();
    });
    QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{timedOut=true;widget.requestCancel();loop.quit();});
    timeout.start(5000);widget.runExample();if(!delivered)loop.exec();
    QObject::disconnect(finished);QObject::disconnect(cancelled);
    check(!timedOut&&delivered,"API example did not finish within five seconds");
    check(!widget.isRunning(),"completed API example kept its worker active");
    check(widget.lastRunSucceeded()==success,"lastRunSucceeded differs from runFinished");return success;
}
void checkCleared(const ApiDemoWidget& widget,const char* reason){check(widget.lastResultJson().isEmpty()&&!widget.lastRunSucceeded(),reason);}
}

int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        QTemporaryDir temporary;check(temporary.isValid(),"temporary directory");
        const auto project=temporary.path()+"/first_project";
        const auto secondProject=temporary.path()+"/second_project";
        DataService service;std::string error;check(service.createProject(utf8(project),"API fixture",error),error);
        const auto las=temporary.path()+"/HZ1-1-1.las";
        writeFile(las,"~Version\nVERS. 2.0 : version\nWRAP. NO : wrap\n~Well\nWELL. HZ1-1-1 : well\nNULL. -999.25 : null\n~Curve\nDEPT.M : depth\nGR.API : gamma\nSP.MV : potential\n~Ascii\n100 40 -20\n101 -999.25 -30\n102 80 -40\n");
        ImportReport imported;check(service.importFile(utf8(las),{},imported,error),error);
        WellInfo coordinateOnly;coordinateOnly.wellName="HZ9-9-9";coordinateOnly.wellheadX=300000;coordinateOnly.wellheadY=2300000;coordinateOnly.coordinateSystem="TEST_CRS";coordinateOnly.coordinateUnit="m";
        long long coordinateWell=0;check(service.createWell(coordinateOnly,coordinateWell,error),error);
        std::vector<DataEntry> curves;check(service.listData("curves",0,curves,error)&&curves.size()==2,"fixture should have GR and SP");
        long long gr=0,sp=0;for(const auto& curve:curves){if(curve.name=="GR")gr=curve.id;if(curve.name=="SP")sp=curve.id;}check(gr&&sp,"fixture curve IDs");

        // New tutorial scenarios use actual parsers and persisted records, not fabricated JSON.
        const auto layers=temporary.path()+"/layers.csv";
        writeFile(layers,QString::fromUtf8("井号,地层,顶深,底深,相类型,未钻穿\nHZ1-1-1,文昌组一段,100,110,湖相,False\nHZ1-1-1,文昌组二段,110,130,三角洲,False\nHZ1-1-1,恩平组,130,150,湖相,True\n").toUtf8());
        ImportOptions layerOptions;layerOptions.parsing.depthUnit="m";
        check(service.importFile(utf8(layers),layerOptions,imported,error),error);
        std::vector<DataEntry> tracks;check(service.listData("intervals",0,tracks,error)&&tracks.size()==1,"fixture should have one geological interval track");
        const auto trackId=tracks[0].id;
        const auto checkshots=temporary.path()+"/checkshots";
        QByteArray timeFile="# Petrel checkshots format\n# Unit in depth: m\nVERSION 1\nBEGIN HEADER\nX\nY\nZ\nTWT picked\nMD\nWell\nAverage velocity\nInterval velocity\nEND HEADER\n1 2 -100 -50 110 \"HZ1-1-1\" 2000 2000\n1 2 -200 -100 210 \"HZ1-1-1\" 2000 2000\n";
        writeFile(checkshots,timeFile);check(service.importFile(utf8(checkshots),{},imported,error),error);
        timeFile.replace("HZ1-1-1","Well 1");
        const auto pendingCheckshots=temporary.path()+"/unassigned-checkshots";
        writeFile(pendingCheckshots,timeFile);check(service.importFile(utf8(pendingCheckshots),{},imported,error),error);
        std::vector<DataEntry> relations;check(service.listData("time_depth",0,relations,error)&&relations.size()==2,"fixture should contain assigned and pending time data");
        long long assignedTimeId=0,pendingTimeId=0;
        for(const auto& relation:relations){if(relation.wellId==0)pendingTimeId=relation.id;else assignedTimeId=relation.id;}
        check(assignedTimeId&&pendingTimeId,"fixture time relationships must retain their ownership state");

        ApiDemoWidget widget;widget.setProjectPath(project);widget.setExample("wells");
        check(runAndWait(widget),"listWells example failed");auto json=resultObject(widget);
        check(json["ok"].toBool()&&json["method"].toString()=="listWells","actual well-list method result");
        check(json["totalWells"].toInt()==2&&json["wellsWithCurves"].toInt()==1&&json["wellsWithoutCurves"].toInt()==1,"zero-curve well must not be presented as a missing imported log");
        bool coordinateFound=false,logsFound=false;
        for(const auto& row:json["preview"].toArray()){
            const auto well=row.toObject();
            if(well["name"].toString()=="HZ9-9-9")coordinateFound=well["hasCoordinates"].toBool()&&well["curves"].toInt()==0;
            if(well["name"].toString()=="HZ1-1-1")logsFound=well["curves"].toInt()==2&&well["logSets"].toInt()==1;
        }
        check(coordinateFound&&logsFound,"well JSON lost meaningful data-availability fields");

        widget.setExample("curve");checkCleared(widget,"changing examples retained old successful result");
        auto* wellChoice=control<QComboBox>(widget,"apiWellSelector");
        check(wellChoice->count()==1&&wellChoice->itemText(0).contains("HZ1-1-1"),"curve selector must exclude coordinate-only wells");
        check(widget.selectResource(gr),"select actual GR curve");check(runAndWait(widget),"curve example failed");json=resultObject(widget);
        check(json["returnType"].toString()=="CurveSlice"&&json["curveId"].toInteger()==gr&&json["totalSamples"].toInt()==3&&json["finiteValues"].toInt()==2,"curve JSON dimensions or identity differ from actual query");
        const auto preview=json["preview"].toArray();check(preview.size()==3,"all three fixture points should be visible");
        check(preview[0].toObject()["depth"].toDouble()==100&&preview[0].toObject()["value"].toDouble()==40,"first returned curve sample");
        check(preview[1].toObject()["rawValue"].toDouble()==-999.25&&preview[1].toObject()["value"].isNull()&&preview[1].toObject()["quality"].toInt()==1,"NaN must serialize as null while preserving the declared raw sentinel");
        check(preview[2].toObject()["depth"].toDouble()==102&&preview[2].toObject()["value"].toDouble()==80,"last returned curve sample");
        check(widget.generatedCode().contains("#include <paleo_data/service.h>")&&widget.generatedCode().contains("service.listData")&&widget.generatedCode().contains("service.readCurveSlice"),"displayed tutorial must contain the public SDK calls");

        auto* first=control<QLineEdit>(widget,"apiFirstValue");auto* last=control<QLineEdit>(widget,"apiLastValue");
        first->setText("101");checkCleared(widget,"depth change retained stale curve results");
        check(runAndWait(widget),"cropped curve example failed");json=resultObject(widget);
        check(json["totalSamples"].toInt()==2&&json["preview"].toArray()[0].toObject()["depth"].toDouble()==101,"range parameters did not affect the executed query");

        first->setText("102");last->setText("100");checkCleared(widget,"invalid range retained old successful results");
        check(!runAndWait(widget),"reversed range unexpectedly succeeded");checkCleared(widget,"failed validation retained stale output");
        check(control<QLabel>(widget,"apiStatus")->text().contains(QStringLiteral("起始值不能大于结束值")),"reversed range failure not explained");
        first->setText("not-a-number");check(!runAndWait(widget),"nonnumeric range unexpectedly succeeded");checkCleared(widget,"nonnumeric validation retained stale output");

        first->clear();last->clear();check(widget.selectResource(sp),"select SP");check(runAndWait(widget),"SP example failed");json=resultObject(widget);
        check(json["name"].toString()=="SP"&&json["finiteValues"].toInt()==3&&json["preview"].toArray()[0].toObject()["value"].toDouble()==-20,"valid signed SP data must survive through the API demo");
        control<QCheckBox>(widget,"apiProcessed")->setChecked(false);checkCleared(widget,"raw/processed parameter retained previous result");

        widget.setExample("intervals");checkCleared(widget,"switching to intervals retained the previous curve result");
        check(widget.selectResource(trackId),"select actual geological interval track");
        check(runAndWait(widget),"geological interval example failed");json=resultObject(widget);
        check(json["returnType"].toString()=="std::vector<IntervalRecord>"&&json["totalIntervals"].toInt()==3,"interval result must return the three persisted records");
        check(json["depthUnit"].toString()=="m","interval metadata lost its confirmed depth unit");
        auto intervalPreview=json["preview"].toArray();
        check(intervalPreview.size()==3&&intervalPreview[0].toObject()["name"].toString()==QStringLiteral("文昌组一段")&&intervalPreview[2].toObject()["name"].toString()==QStringLiteral("恩平组"),"interval tutorial must return actual layer names");
        check(intervalPreview[0].toObject()["topDepth"].toDouble()==100&&intervalPreview[0].toObject()["bottomDepth"].toDouble()==110,"first interval boundaries differ from persisted data");
        first->setText("112");last->setText("125");checkCleared(widget,"interval filter retained stale results");
        check(runAndWait(widget),"filtered interval example failed");json=resultObject(widget);intervalPreview=json["preview"].toArray();
        // A query window selects overlapping layers; it must not rewrite their boundaries.
        check(json["totalIntervals"].toInt()==1&&intervalPreview.size()==1&&intervalPreview[0].toObject()["name"].toString()==QStringLiteral("文昌组二段"),"interval depth filter did not select the overlapping layer");
        check(intervalPreview[0].toObject()["topDepth"].toDouble()==110&&intervalPreview[0].toObject()["bottomDepth"].toDouble()==130,"query bounds must not replace original layer boundaries");
        first->setText("200");last->setText("210");check(runAndWait(widget),"empty interval query should succeed");json=resultObject(widget);
        check(json["ok"].toBool()&&json["totalIntervals"].toInt()==0&&json["preview"].toArray().isEmpty(),"empty successful interval query was confused with stale data or an error");

        widget.setExample("time_depth");checkCleared(widget,"switching to time-depth retained interval data");
        check(first->isHidden()&&last->isHidden(),"time-depth tutorial should not imply an unsupported range conversion");
        check(widget.selectResource(assignedTimeId),"select actual assigned time-depth relation");
        // Hidden controls must not accidentally filter or reject the complete time-depth request.
        first->setText("not-a-number");last->setText("-9999");
        check(runAndWait(widget),"assigned time-depth example failed");json=resultObject(widget);
        check(json["returnType"].toString()=="TimeDepthTable"&&json["totalPoints"].toInt()==2,"time-depth result lost complete persisted point data");
        check(json["depthType"].toString()=="MD"&&json["depthUnit"].toString()=="m"&&json["timeType"].toString()=="TWT"&&json["timeUnit"].toString().isEmpty(),"time-depth tutorial guessed or dropped source conventions");
        auto timePreview=json["preview"].toArray();
        check(timePreview.size()==2&&timePreview[0].toObject()["depth"].toDouble()==110&&timePreview[0].toObject()["time"].toDouble()==-50&&timePreview[1].toObject()["time"].toDouble()==-100,"signed time-depth values differ from source data");
        check(widget.selectResource(pendingTimeId),"pending time-depth data must remain selectable");checkCleared(widget,"selecting pending time-depth retained the assigned record result");
        auto* resourceChoice=control<QComboBox>(widget,"apiResourceSelector");
        check(resourceChoice->count()==1&&resourceChoice->currentText().contains(QStringLiteral("待归井")),"pending-only selector mixed assigned data or omitted pending ownership");
        check(runAndWait(widget),"pending time-depth example failed");json=resultObject(widget);
        check(json["totalPoints"].toInt()==2&&json["preview"].toArray()[0].toObject()["time"].toDouble()==-50,"pending time-depth values must be read without inventing a well");
        const auto allTimeIndex=wellChoice->findData(QVariant::fromValue<qlonglong>(0));
        check(allTimeIndex>=0,"time-depth selector must expose all project records");wellChoice->setCurrentIndex(allTimeIndex);checkCleared(widget,"changing time-depth well scope retained stale data");
        check(resourceChoice->count()==2,"all-project time-depth scope must include assigned and pending records");

        widget.setExample("section");checkCleared(widget,"switching to missing seismic resource retained curve result");
        check(control<QComboBox>(widget,"apiResourceSelector")->count()==0,"fixture unexpectedly has sections");
        check(!runAndWait(widget),"missing section cannot return a fake success");
        check(control<QLabel>(widget,"apiStatus")->text().contains(QStringLiteral("没有可调用的资料")),"missing section is not explained");

        widget.setExample("curve");check(widget.selectResource(gr),"select curve for cancellation");
        check(!runAndWait(widget,true),"cancelled API request published a successful result");json=resultObject(widget);
        check(!json["ok"].toBool()&&json["error"].toString().contains(QStringLiteral("取消"))&&!json.contains("preview"),"cancelled request must not expose partial/stale successful data");
        check(runAndWait(widget),"widget did not recover after cancellation");

        DataService second;check(second.createProject(utf8(secondProject),"Other fixture",error),error);
        coordinateOnly.wellName="HZ8-8-8";long long secondWell=0;check(second.createWell(coordinateOnly,secondWell,error),error);
        widget.setProjectPath(secondProject);checkCleared(widget,"switching projects retained prior successful result");
        check(control<QComboBox>(widget,"apiWellSelector")->count()==0&&control<QComboBox>(widget,"apiResourceSelector")->count()==0&&!widget.selectResource(gr),"project switch retained curve resources from another database");
        check(widget.generatedCode().contains(secondProject)&&!widget.generatedCode().contains(project),"generated code retained the old project path");
        widget.setExample("wells");check(runAndWait(widget),"second project list failed");json=resultObject(widget);
        check(json["project"].toString()==secondProject&&json["totalWells"].toInt()==1&&json["wellsWithCurves"].toInt()==0&&json["preview"].toArray()[0].toObject()["name"].toString()=="HZ8-8-8","second project result contains old database data");
        std::cout<<"PASS API demo: real well/curve/interval/time-depth results, zero-curve availability, JSON nulls, interval filtering, pending time records, parameter invalidation, missing resources, cancellation, project isolation\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL API demo: "<<error.what()<<'\n';return 1;}
}
