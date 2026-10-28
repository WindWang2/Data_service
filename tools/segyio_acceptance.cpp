// 只通过公开服务调用大文件，并用少量原始IEEE字节独立核对返回振幅。
// 不复制SEG-Y，不做全文件哈希，不把振幅写入SQLite。
#include "paleo_data/service.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
using namespace paleo::data;
static void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
static std::string u(const QString& text) { return text.toUtf8().toStdString(); }
static QJsonValue number(double value) { return std::isfinite(value) ? QJsonValue(value) : QJsonValue(); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QCommandLineParser args; args.addHelpOption();
        args.addOptions({{"file","SEG-Y source (read only)","file"},{"project","New or existing project directory","dir"},{"output","Acceptance JSON","json"}});
        args.process(app); check(args.isSet("file") && args.isSet("project") && args.isSet("output"), "required: --file --project --output");
        const QString sourcePath = QFileInfo(args.value("file")).absoluteFilePath();
        const QFileInfo before(sourcePath); const auto sourceSize = before.size(); const auto sourceModified = before.lastModified();
        DataService service; std::string error; const auto project = args.value("project");
        check(QFileInfo::exists(QDir(project).filePath("data.sqlite")) ? service.openProject(u(project),error) : service.createProject(u(project),"真实SEG-Y按需读取验证",error), error);
        ImportOptions import; import.copySources = false; ImportReport imported;
        QElapsedTimer clock; clock.start();
        check(service.importFile(u(sourcePath),import,imported,error),error); const auto importMs = clock.elapsed();
        std::vector<DataEntry> entries; check(service.listData("volumes",0,entries,error),error);
        long long id = 0; for(const auto& entry:entries) { SeismicVolume v; check(service.readVolume(entry.id,v,error),error); if(QFileInfo(QString::fromUtf8(v.dataFile)).absoluteFilePath()==sourcePath) id=entry.id; }
        check(id>0,"import must register selected external volume");
        clock.restart(); SeismicFileInfo info; check(service.inspectSeismic(id,info,error),error); const auto inspectMs=clock.elapsed();
        check(info.sampleFormat==5 && info.sampleCount>0,"independent acceptance comparator currently requires IEEE32 source");
        QFile raw(sourcePath); check(raw.open(QIODevice::ReadOnly),"open independent read-only comparator");
        long long verified=0;
        auto compare=[&](long long traceIndex,int firstSample,const float* values,int count) {
            const qint64 stride=240LL+info.sampleCount*4LL;
            check(raw.seek(info.firstTraceOffset+traceIndex*stride+240+firstSample*4LL),"independent 64bit seek");
            const auto bytes=raw.read(count*4LL); check(bytes.size()==count*4LL,"independent bounded sample read");
            for(int i=0;i<count;++i) {
                const auto bits=info.byteOrder=="little-endian" ? qFromLittleEndian<quint32>(bytes.constData()+i*4) : qFromBigEndian<quint32>(bytes.constData()+i*4);
                float expected; std::memcpy(&expected,&bits,4);
                check((std::isfinite(expected)&&expected==values[i]) || (!std::isfinite(expected)&&std::isnan(values[i])),"segyio amplitude differs from independent source bytes"); ++verified;
            }
        };
        QJsonArray traces; clock.restart();
        for(long long index : {0LL,info.traceCount/3,info.traceCount/2,info.traceCount-1}) {
            SeismicTraceResult trace; check(service.readSeismicTrace(id,index,{},trace,error),error);
            compare(index,0,trace.amplitudes.data(),int(trace.amplitudes.size()));
            SeismicStatistics stats; check(service.seismicStatistics(trace.amplitudes,stats,error),error);
            traces.append(QJsonObject{{"traceIndex",index},{"inline",trace.inlineNumber},{"crossline",trace.crosslineNumber},{"x",number(trace.x)},{"y",number(trace.y)},{"samples",int(trace.amplitudes.size())},{"rms",number(stats.rms)}});
        }
        const auto traceMs=clock.elapsed();
        SectionReadOptions window; window.maxColumns=256; window.maxValues=2000000;
        window.firstTime=info.firstTime+info.sampleInterval*(info.sampleCount/4);
        window.lastTime=info.firstTime+info.sampleInterval*(info.sampleCount/2);
        const int inlineNo=info.inlineMin+(info.inlineCount/2)*info.inlineStep;
        const int crosslineNo=info.crosslineMin+(info.crosslineCount/2)*info.crosslineStep;
        SeismicSectionResult line,crossline; clock.restart();
        check(service.readSeismicLine(id,"inline",inlineNo,window,line,error),error);
        check(service.readSeismicLine(id,"crossline",crosslineNo,window,crossline,error),error);
        const auto lineMs=clock.elapsed();
        for(const auto* section : {&line,&crossline}) {
            check(section->traceIndexes.size()==size_t(section->columns),"line returns original ordinal per column");
            const int firstSample=int(std::llround((section->firstTime-info.firstTime)/info.sampleInterval));
            for(int col : {0,section->columns/2,section->columns-1}) compare(section->traceIndexes[col],firstSample,section->amplitudes.data()+size_t(col)*section->samples,section->samples);
        }
        TimeSliceReadOptions sliceOptions; sliceOptions.maxInlineCount=64; sliceOptions.maxCrosslineCount=64; sliceOptions.maxValues=4096;
        SeismicTimeSliceResult slice; clock.restart();
        check(service.readSeismicTimeSlice(id,window.firstTime,sliceOptions,slice,error),error); const auto sliceMs=clock.elapsed();
        // 已独立核验该真实样本按INLINE主序排列；本断言不是服务对任意文件的假设。
        check(info.traceCount==static_cast<long long>(info.inlineCount)*info.crosslineCount,"acceptance source must be a rectangular regular grid");
        for(size_t i=0;i<slice.inlineNumbers.size();++i) for(size_t j=0;j<slice.crosslineNumbers.size();++j) {
            const auto ordinal=static_cast<long long>((slice.inlineNumbers[i]-info.inlineMin)/info.inlineStep)*info.crosslineCount+(slice.crosslineNumbers[j]-info.crosslineMin)/info.crosslineStep;
            compare(ordinal,int(std::llround((slice.actualTime-info.firstTime)/info.sampleInterval)),&slice.amplitudes[i*slice.crosslineNumbers.size()+j],1);
        }
        SeismicProcessingOptions processing; processing.removeMean=true; processing.agcWindowSamples=101;
        SeismicSectionResult processed; check(service.processSeismicSection(line,processing,processed,error),error);
        SeismicStatistics stats; check(service.seismicStatistics(processed.amplitudes,stats,error),error);
        check(std::isfinite(stats.rms),"processed real section should contain finite data");
        // 取消不能留下上一次成功调用的数据。
        check(!service.readSeismicLine(id,"inline",inlineNo,window,processed,error,[]{return true;})&&processed.amplitudes.empty(),"cancellation clears result");
        service.closeProject(); check(service.openProject(u(project),error),error);
        SeismicVolume metadata; check(service.readVolume(id,metadata,error)&&metadata.traces.empty(),"reopen metadata does not load amplitudes");
        const QFileInfo after(sourcePath); check(after.size()==sourceSize&&after.lastModified()==sourceModified,"source file metadata changed");
        QJsonArray warnings; for(const auto& w:info.warnings)warnings.append(QString::fromUtf8(w));
        QJsonObject result{{"status","passed"},{"engine",QString::fromUtf8(info.engine)},{"source",sourcePath},{"sourceBytes",sourceSize},{"sourceCopied",false},{"sourceMetadataUnchanged",true},{"volumeId",id},{"traceCount",info.traceCount},{"sampleCount",info.sampleCount},{"firstTimeMs",info.firstTime},{"sampleIntervalMs",info.sampleInterval},{"format",info.sampleFormat},{"geometryStatus",QString::fromUtf8(info.geometryStatus)},{"checkedHeaders",info.checkedHeaders},{"inlineMin",info.inlineMin},{"inlineMax",info.inlineMax},{"crosslineMin",info.crosslineMin},{"crosslineMax",info.crosslineMax},{"verifiedAmplitudes",verified},{"traces",traces},{"warnings",warnings},
            {"importMs",importMs},{"inspectMs",inspectMs},{"fourTracesMs",traceMs},{"twoLinesMs",lineMs},{"timeSliceMs",sliceMs},{"lineColumns",line.columns},{"lineSamples",line.samples},{"timeSliceValues",int(slice.amplitudes.size())},{"processedRms",number(stats.rms)},{"databaseBytes",QFileInfo(QDir(project).filePath("data.sqlite")).size()}};
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
        if(K32GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))result["peakWorkingSetBytes"]=qint64(memory.PeakWorkingSetSize);
#endif
        const auto bytes=QJsonDocument(result).toJson(); QSaveFile output(args.value("output")); check(output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit(),"save acceptance JSON");
        std::cout<<bytes.toStdString(); return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
