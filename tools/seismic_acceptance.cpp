// Acceptance probe uses only the public service; convention labels below are test inputs, not inferred geology.
#include "paleo_data/service.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
using namespace paleo::data;
void check(bool v,const std::string& error){if(!v)throw std::runtime_error(error);}
int main(int argc,char**argv){QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("paleo-seismic-acceptance");try{
    check(app.arguments().size()>1,"usage: seismic_acceptance <SEG-Y file>");const QString path=app.arguments()[1];QFile file(path);check(file.open(QIODevice::ReadOnly),"cannot open input");const QByteArray binary=file.read(3600);check(binary.size()==3600,"header missing");
    const bool little=qFromBigEndian<quint16>(binary.constData()+3224)>16;auto read16=[&](const char* p){return little?qFromLittleEndian<quint16>(p):qFromBigEndian<quint16>(p);};auto read32=[&](const char* p){return little?qFromLittleEndian<qint32>(p):qFromBigEndian<qint32>(p);};
    const int samples=read16(binary.constData()+3220),format=read16(binary.constData()+3224);check(format==1||format==5,"acceptance endpoint probe supports IBM/IEEE32 fixtures");const qint64 traceBytes=240+qint64(samples)*4;check((file.size()-3600)%traceBytes==0,"unexpected fixed length");const qint64 traceCount=(file.size()-3600)/traceBytes;
    auto xy=[&](qint64 trace){check(file.seek(3600+trace*traceBytes),"seek header");const QByteArray h=file.read(240);check(h.size()==240,"trace header missing");const int scalar=static_cast<qint16>(read16(h.constData()+70));const double factor=scalar>0?double(scalar):scalar<0?1.0/-scalar:1.0;int x=read32(h.constData()+180),y=read32(h.constData()+184);if(x==0&&y==0){x=read32(h.constData()+72);y=read32(h.constData()+76);}return SeismicSectionPoint{x*factor,y*factor};};
    const auto first=xy(0),last=xy(traceCount-1);file.seek(3600);const QByteArray firstHeader=file.read(240);const int delay=static_cast<qint16>(read16(firstHeader.constData()+108));file.close();
    QTemporaryDir temp;check(temp.isValid(),"temp failed");DataService service;std::string error;check(service.createProject(temp.path().toUtf8().toStdString(),"Acceptance",error),error);ImportOptions import;import.copySources=false;ImportReport report;check(service.importFile(path.toUtf8().toStdString(),import,report,error),error);std::vector<DataEntry> entries;check(service.listData("volumes",0,entries,error)&&entries.size()==1,error);const auto id=entries[0].id;
    SeismicVolume volume;check(service.readVolume(id,volume,error),error);volume.geometry.coordinateSystem="TEST_LOCAL_XY_NO_EPSG";volume.geometry.coordinateUnit="m";volume.sampling.firstTime=delay;volume.sampling.timeType="TWT";volume.sampling.timeReference="TEST_SOURCE_DELAY_NO_DATUM_CONFIRMATION";check(service.saveVolume(id,volume,error),error);
    SeismicSection section;section.sectionName="Acceptance diagonal";section.parentVolumeId=int(id);section.coordinateSystem=volume.geometry.coordinateSystem;section.coordinateUnit=volume.geometry.coordinateUnit;
    // Keep the probe inside survey edges; original XY may differ slightly from a valid affine fit.
    section.pathPoints={{first.x+(last.x-first.x)*0.01,first.y+(last.y-first.y)*0.01},{first.x+(last.x-first.x)*0.99,first.y+(last.y-first.y)*0.99}};
    if(app.arguments().contains("--edges"))section.pathPoints={first,last};
    long long sectionId=0;check(service.saveSection(sectionId,section,error),error);
    SectionReadOptions sectionOptions;sectionOptions.maxColumns=16;sectionOptions.maxValues=64000;sectionOptions.firstTime=delay+(samples/2)*volume.sampling.sampleInterval;sectionOptions.lastTime=std::min(delay+(samples-1)*volume.sampling.sampleInterval,sectionOptions.firstTime+40);SeismicSectionResult result;QElapsedTimer timer;timer.start();check(service.extractSection(sectionId,sectionOptions,result,error),error);const auto sectionMs=timer.elapsed();
    SeismicTimeSliceResult slice;TimeSliceReadOptions sliceOptions;sliceOptions.maxInlineCount=8;sliceOptions.maxCrosslineCount=8;sliceOptions.maxValues=64;timer.restart();check(service.readTimeSlice(id,sectionOptions.firstTime,sliceOptions,slice,error),error);const auto sliceMs=timer.elapsed();int finiteSection=0,finiteTime=0;for(float v:result.amplitudes)finiteSection+=std::isfinite(v);for(float v:slice.amplitudes)finiteTime+=std::isfinite(v);
    std::cout<<"file_bytes="<<QFile(path).size()<<" trace_count="<<traceCount<<" samples="<<samples<<"\nsection_columns="<<result.columns<<" section_samples="<<result.samples<<" finite_section_values="<<finiteSection<<" elapsed_ms="<<sectionMs<<"\ntime_values="<<slice.amplitudes.size()<<" finite_time_values="<<finiteTime<<" elapsed_ms="<<sliceMs<<'\n';
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);if(K32GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))std::cout<<"peak_working_set_bytes="<<memory.PeakWorkingSetSize<<" peak_pagefile_bytes="<<memory.PeakPagefileUsage<<'\n';
#endif
    std::cout<<"CRS, coordinate unit, TWT and time datum were EXPLICIT TEST SETTINGS; no survey EPSG or geological datum inferred.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
