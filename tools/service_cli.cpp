#include "paleo_data/service.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <iostream>
using namespace paleo::data;
static QString q(const std::string& s){return QString::fromUtf8(s);}
static std::string u(const QString& s){return s.toUtf8().toStdString();}
int main(int argc,char** argv){QCoreApplication app(argc,argv);auto args=app.arguments();const bool referenceOnly=args.removeAll("--reference-sources")>0;const bool verifyAll=args.removeAll("--verify-all")>0;int threads=2;const int ti=args.indexOf("--threads");if(ti>=0){bool valid=false;if(ti+1>=args.size())return 1;threads=args[ti+1].toInt(&valid);if(!valid||threads<1||threads>4)return 1;args.removeAt(ti+1);args.removeAt(ti);}if(args.size()!=4||args[1]!="audit"){std::cerr<<"usage: paleo_data_cli audit <input directory> <project directory> [--reference-sources] [--verify-all] [--threads 1..4]\n";return 1;}
    DataService service;std::string error;bool exists=QFileInfo::exists(QDir(args[3]).filePath("data.sqlite"));if(!(exists?service.openProject(u(args[3]),error):service.createProject(u(args[3]),"测试数据演示工区",error))){std::cerr<<error<<'\n';return 1;}
    ImportOptions options;options.copySources=!referenceOnly;options.verifyUnchangedFiles=verifyAll;options.parserThreads=threads;
    ImportReport report;QElapsedTimer timer;timer.start();int lastPrinted=-1;bool ok=service.importDirectory(u(args[2]),options,report,error,[&](const ImportProgress& p){if(p.total>0&&p.completed!=lastPrinted&&(p.completed%10==0||p.completed==p.total)){lastPrinted=p.completed;std::cout<<p.completed<<'/'<<p.total<<' '<<p.status<<std::endl;}});
    QJsonObject result{{"input",args[2]},{"project",args[3]},{"elapsedSeconds",timer.elapsed()/1000.0},{"seen",report.seen},{"parsed",report.imported},{"review",report.review},{"registered",report.registered},{"ignored",report.ignored},{"failed",report.failed},{"skipped",report.skipped},{"cancelled",report.cancelled},{"message",q(error)}};
    result["performance"]=QJsonObject{{"cached",report.cached},{"workerCount",report.workerCount},{"elapsedMs",double(report.elapsedMs)},{"hashMs",double(report.hashMs)},{"parseMs",double(report.parseMs)},{"copyMs",double(report.copyMs)},{"storeMs",double(report.storeMs)},{"hashedBytes",double(report.hashedBytes)},{"referenceOnly",referenceOnly},{"verifyAll",verifyAll}};
    std::vector<SourceEntry> sources;service.listSources(sources,error);QJsonArray sourceItems;for(auto& s:sources)sourceItems.append(QJsonObject{{"id",double(s.id)},{"path",q(s.path)},{"kind",q(s.kind)},{"status",q(s.status)},{"message",q(s.message)},{"bytes",double(s.bytes)}});result["sources"]=sourceItems;
    std::vector<WellSummary> wells;service.listWells(wells,error);QJsonArray wellItems;for(auto& w:wells)wellItems.append(QJsonObject{{"id",double(w.id)},{"name",q(w.name)},{"curves",w.curves},{"tracks",w.tracks},{"samples",w.samples}});result["wells"]=wellItems;
    QJsonObject counts;for(const std::string kind:{"curves","intervals","samples","horizons","volumes","maps","tables","time_depth","trajectories","core_photos","attachments","markers"}){std::vector<DataEntry> items;service.listData(kind,0,items,error);long long rows=0;for(auto& item:items)rows+=item.count;counts[q(kind)]=QJsonObject{{"objects",int(items.size())},{"rows",double(rows)}};}result["counts"]=counts;
    std::vector<IssueEntry> issues;service.listIssues(false,issues,error);QJsonArray issueItems;for(auto& i:issues)issueItems.append(QJsonObject{{"id",double(i.id)},{"sourceId",double(i.sourceId)},{"code",q(i.code)},{"message",q(i.message)},{"row",i.row},{"sheet",q(i.sheet)},{"blocking",i.blocking},{"resolved",i.resolved}});result["issues"]=issueItems;
    QDir dir(args[3]);dir.mkpath("exports");QSaveFile out(dir.filePath("exports/import-audit.json"));if(!out.open(QIODevice::WriteOnly)){std::cerr<<"cannot save audit\n";return 1;}out.write(QJsonDocument(result).toJson());if(!out.commit())return 1;
    std::cout<<"seen="<<report.seen<<" parsed="<<report.imported<<" review="<<report.review<<" registered="<<report.registered<<" ignored="<<report.ignored<<" failed="<<report.failed<<" skipped="<<report.skipped<<" wells="<<wells.size()<<" elapsed="<<timer.elapsed()/1000.0<<"s\n";
    return ok?0:2;
}
