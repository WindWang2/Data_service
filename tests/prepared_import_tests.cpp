#include <paleo_data/service.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>
#include <stdexcept>
using namespace paleo::data;
static void check(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }
static std::string u(const QString& value) { return value.toUtf8().toStdString(); }
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  try {
    QTemporaryDir temp; const auto source=temp.filePath("source.las");
    const QByteArray original="host normalized source\n";
    QFile file(source); check(file.open(QIODevice::WriteOnly),"write source"); file.write(original); file.close();
    const auto hash=QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex().toStdString();
    ParsedFile model; model.kind="well_log"; model.status="parsed";
    WellDataset well; well.wellInfo.wellName="SYNTH-1";
    LogSet logs; logs.samplingAxis.depthValues={100,101,102};
    LogCurve curve; curve.curveName="SP"; curve.sampleValues={-999,-99999,Missing};
    logs.logCurves.push_back(curve); well.wellbore.logSets.push_back(logs); model.wells.push_back(well);
    const auto project=temp.filePath("model"); DataService service; std::string error; long long id=0;
    check(service.createProject(u(project),"prepared",error),error);
    check(!service.importPreparedFile(u(source),u(source),"../source.las",std::string(64,'0'),model,id,error),"wrong hash rejected");
    check(!service.importPreparedFile(u(source),u(source),"../source.las",hash,model,id,error,[]{return true;}),"cancel rejected");
    auto broken=model; broken.wells[0].wellbore.logSets[0].logCurves[0].sampleValues.pop_back();
    check(!service.importPreparedFile(u(source),u(source),"../source.las",hash,broken,id,error),"invalid shape rejected");
    std::vector<SourceEntry> sources; check(service.listSources(sources,error)&&sources.empty(),"failed transaction leaves no source");
    check(service.importPreparedFile(u(source),u(source),"../source.las",hash,model,id,error),error);
    const auto publishedId=id;
    check(service.verifySource(id,error),error);
    std::vector<DataEntry> curves; check(service.listData("curves",0,curves,error)&&curves.size()==1,"one curve");
    const auto curveId=curves.front().id;
    CurveSlice slice; check(service.readCurveSlice(curveId,Missing,Missing,true,slice,error),error);
    check(slice.values[0]==-999&&slice.values[1]==-99999&&std::isnan(slice.values[2]),"resolved nulls retain finite negatives");
    service.closeProject();
    check(QDir(temp.path()).rename("model","moved-model"),"relocate project");
    check(service.openProjectReadOnly(u(temp.filePath("moved-model")),error),error);
    check(service.verifySource(id,error),error);
    check(service.readCurveSlice(curveId,Missing,Missing,true,slice,error),error);
    check(!service.importPreparedFile(u(source),u(source),"../source.las",hash,model,id,error),"readonly rejected");
    check(file.open(QIODevice::ReadOnly)&&file.readAll()==original,"source unchanged"); file.close();
    check(file.open(QIODevice::Append),"change source"); file.write("changed"); file.close();
    check(!service.verifySource(publishedId,error),"changed source rejected");
    std::cout<<"prepared import contract passed\n"; return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
