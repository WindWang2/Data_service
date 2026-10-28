// Read-only, bounded benchmark. Inputs and outputs stay outside the source tree.
#include "paleo_data/service.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace paleo::data;
namespace {
std::string utf8(const QString& value) { return value.toUtf8().toStdString(); }
QString text(const std::string& value) { return QString::fromUtf8(value); }
int integer(const QString& value,const char* name,int minimum,int maximum) {
    bool valid=false;const auto result=value.toLongLong(&valid);
    if(!valid||result<minimum||result>maximum)throw std::runtime_error(std::string(name)+" is outside the allowed integer range");
    return static_cast<int>(result);
}
bool sameFloat(float a,float b) {return (std::isnan(a)&&std::isnan(b))||std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
bool sameDouble(double a,double b) {return (std::isnan(a)&&std::isnan(b))||a==b;}
bool equal(const SeismicTimeSliceResult& a,const SeismicTimeSliceResult& b) {
    if(a.inlineNumbers!=b.inlineNumbers||a.crosslineNumbers!=b.crosslineNumbers||a.actualTime!=b.actualTime||a.timeUnit!=b.timeUnit||
       a.amplitudes.size()!=b.amplitudes.size()||a.x.size()!=b.x.size()||a.y.size()!=b.y.size())return false;
    for(size_t i=0;i<a.amplitudes.size();++i)if(!sameFloat(a.amplitudes[i],b.amplitudes[i]))return false;
    for(size_t i=0;i<a.x.size();++i)if(!sameDouble(a.x[i],b.x[i])||!sameDouble(a.y[i],b.y[i]))return false;
    return true;
}
QJsonObject statistics(const SeismicReadStatistics& s) {
    return {{"serviceElapsedMs",s.elapsedMs},{"workerPositionMs",s.positionMs},{"workerReadMs",s.readMs},
        {"workerValidationMs",s.validationMs},{"workerConversionMs",s.conversionMs},{"logicalReadCalls",double(s.readCalls)},
        {"logicalSeekCalls",double(s.seekCalls)},{"explicitBytesRead",double(s.bytesRead)},{"viewBytesMapped",double(s.mappedBytes)},
        {"tracesRead",double(s.tracesRead)},{"workerCount",s.workerCount},{"cacheHits",s.cacheHits},{"cacheMisses",s.cacheMisses},
        {"completedTiles",s.completedTiles},{"totalTiles",s.totalTiles},{"actualIoMode",text(s.ioMode)}};
}
int defaultLast(int first,int step,int count) {
    const long long result=static_cast<long long>(first)+static_cast<long long>(std::max(0,std::min(count,256)-1))*step;
    if(result<std::numeric_limits<int>::min()||result>std::numeric_limits<int>::max())throw std::runtime_error("line range overflow; specify bounds");
    return static_cast<int>(result);
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QCommandLineParser parser;
    parser.setApplicationDescription("Read-only time-slice benchmark with exact legacy comparison; does not flush the OS file cache.");
    parser.addHelpOption();parser.addPositionalArgument("project","Existing project directory");parser.addPositionalArgument("volume","Volume ID from that project");
    for(const char* key:{"inline-min","inline-max","crossline-min","crossline-max"})parser.addOption({key,"Inclusive actual line number","number"});
    parser.addOption({"time","Source sample-axis time in ms; default is the middle sample","ms"});
    parser.addOption({"deadline-seconds","Cooperative timeout for each individual read (1-300)","seconds","30"});
    parser.addOption({"max-cells","Maximum ROI cells; explicit opt-in needed beyond the default 256x256","count","65536"});
    parser.addOption({"tile-size","Tile edge (32-512); default enables several jobs for a 256x256 ROI","count","64"});
    parser.addOption({"repeats","Number of rounds; mode ordering rotates after the first round (1-5)","count","1"});
    parser.addOption({"no-stage-timings","Disable detailed per-stage instrumentation for a separate throughput run"});
    parser.process(app);QJsonObject output;QJsonArray runs;
    output["osCacheState"]="uncontrolled; later cases may benefit from earlier reads; this is not a cold-cache benchmark";
    output["timingMeaning"]="Stage times sum work across workers, not wall time. bytesRead and mappedBytes are logical counters, not physical disk traffic.";
    output["sourcePolicy"]="external read-only; no volume copying; only requested ROI results are allocated";
    bool allGood=true;
    try {
        const auto args=parser.positionalArguments();if(args.size()!=2)throw std::runtime_error("Expected <project> <volumeId>; see --help");
        bool idValid=false;const long long id=args[1].toLongLong(&idValid);if(!idValid||id<=0)throw std::runtime_error("volumeId must be positive");
        const int deadline=integer(parser.value("deadline-seconds"),"deadline-seconds",1,300);
        const int maxCells=integer(parser.value("max-cells"),"max-cells",1,4000000);
        const int repeats=integer(parser.value("repeats"),"repeats",1,5);
        DataService service;std::string error;if(!service.openProject(utf8(args[0]),error))throw std::runtime_error(error);
        SeismicFileInfo info;if(!service.inspectSeismic(id,info,error))throw std::runtime_error(error);
        if(info.inlineCount<1||info.crosslineCount<1)throw std::runtime_error("No usable line grid");
        if((info.inlineStep<1&&!parser.isSet("inline-max"))||(info.crosslineStep<1&&!parser.isSet("crossline-max")))
            throw std::runtime_error("Irregular axes require explicit line bounds");
        TimeSliceReadOptions base;base.fullResolution=true;base.useCache=false;base.adjacentSamples=1;base.collectTimings=!parser.isSet("no-stage-timings");
        base.maxValues=maxCells;base.tileSize=integer(parser.value("tile-size"),"tile-size",32,512);
        auto bound=[&](const char* name,int fallback){return parser.isSet(name)?integer(parser.value(name),name,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()):fallback;};
        base.inlineMin=bound("inline-min",info.inlineMin);base.inlineMax=bound("inline-max",defaultLast(base.inlineMin,info.inlineStep,info.inlineCount));
        base.crosslineMin=bound("crossline-min",info.crosslineMin);base.crosslineMax=bound("crossline-max",defaultLast(base.crosslineMin,info.crosslineStep,info.crosslineCount));
        bool timeValid=true;const double requested=parser.isSet("time")?parser.value("time").toDouble(&timeValid):info.firstTime+(info.sampleCount/2)*info.sampleInterval;
        if(!timeValid||!std::isfinite(requested))throw std::runtime_error("time must be finite");
        output["request"]=QJsonObject{{"volumeId",double(id)},{"inlineMin",base.inlineMin},{"inlineMax",base.inlineMax},
            {"crosslineMin",base.crosslineMin},{"crosslineMax",base.crosslineMax},{"requestedTimeMs",requested},{"maxCells",maxCells},{"deadlineSeconds",deadline},{"stageTimingsEnabled",base.collectTimings}};
        auto run=[&](QString label,TimeSliceReadOptions options,double time,const SeismicTimeSliceResult* reference,SeismicTimeSliceResult& result){
            QElapsedTimer timer;timer.start();bool timedOut=false;int tiles=0;
            const bool ok=service.readSeismicTimeSlice(id,time,options,result,error,[&]{return timedOut=timer.elapsed()>=deadline*1000LL;},
                [&](const SeismicTimeSliceTile&){++tiles;});
            const double elapsed=timer.nsecsElapsed()/1000000.0;
            const bool exact=ok&&(!reference||equal(*reference,result));
            runs.append(QJsonObject{{"case",label},{"requestedIoMode",text(options.ioMode)},{"requestedWorkers",options.workerCount},
                {"wallMs",elapsed},{"ok",ok},{"deadlineReached",timedOut},{"error",text(error)},{"comparedAllValues",reference!=nullptr&&ok},
                {"exactMatch",exact},{"cells",double(result.amplitudes.size())},{"tileCallbacks",tiles},{"diagnostics",statistics(result.statistics)}});
            allGood=allGood&&ok&&exact;return ok&&exact;
        };
        DataService::clearSeismicSliceCache();base.ioMode="legacy";base.workerCount=1;SeismicTimeSliceResult reference;
        if(!run("legacy-reference",base,requested,nullptr,reference))throw std::runtime_error("Legacy reference failed or timed out; reduce the requested ROI before comparing");
        // The first correctness reference may warm the OS cache. Repeat legacy in every round
        // so its warmed throughput can be compared with the optimized paths too.
        const std::vector<std::pair<std::string,int>> cases={{"legacy",1},{"buffered",1},{"buffered",2},{"buffered",4},{"mapped",1},{"mapped",2},{"mapped",4}};
        for(int round=0;round<repeats;++round)for(size_t item=0;item<cases.size();++item){
            const auto& spec=cases[(item+size_t(round))%cases.size()];auto options=base;options.ioMode=spec.first;options.workerCount=spec.second;
            SeismicTimeSliceResult result;run(QStringLiteral("%1-%2-round%3").arg(text(spec.first)).arg(spec.second).arg(round+1),options,requested,&reference,result);
        }
        // A time band is aligned to its sample count. Choose the adjacent sample inside that band.
        const int sample=static_cast<int>(std::llround((reference.actualTime-info.firstTime)/info.sampleInterval));
        const int adjacent=(sample%4<3&&sample+1<info.sampleCount)?sample+1:(sample%4>0?sample-1:sample);
        const double adjacentTime=info.firstTime+adjacent*info.sampleInterval;SeismicTimeSliceResult adjacentReference;
        base.ioMode="legacy";base.workerCount=1;
        if(run("adjacent-legacy-reference",base,adjacentTime,nullptr,adjacentReference)){
            DataService::clearSeismicSliceCache();auto cached=base;cached.ioMode="auto";cached.workerCount=2;cached.useCache=true;cached.adjacentSamples=4;cached.cacheBudgetMiB=128;
            SeismicTimeSliceResult result;
            run("cache-fill",cached,requested,&reference,result);
            run("cache-same-time",cached,requested,&reference,result);
            run("cache-adjacent-time",cached,adjacentTime,&adjacentReference,result);
        }
        output["allExactAndSuccessful"]=allGood;
    } catch(const std::exception& exception) {output["fatalError"]=QString::fromUtf8(exception.what());allGood=false;}
    output["runs"]=runs;std::cout<<QJsonDocument(output).toJson(QJsonDocument::Indented).toStdString();
    return allGood?0:2;
}
