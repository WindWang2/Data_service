// All inputs are generated in a temporary directory. No survey data is bundled.
#include "paleo_data/service.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace paleo::data;
namespace {
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
std::string utf8(const QString& value){return value.toUtf8().toStdString();}
template<class T> void put(QByteArray& bytes,int offset,T value){qToBigEndian<T>(value,bytes.data()+offset);}
float amplitude(int il,int xl,int sample){return float(il*100000+xl*2+sample%17-8);}
void write(QFile& file,const QByteArray& bytes){check(file.write(bytes)==bytes.size(),"write generated fixture");}
void fixture(const QString& path,int rows,int columns,int samples){
    QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"create generated fixture");
    write(file,QByteArray(3200,' '));QByteArray binary(400,'\0');
    put(binary,16,quint16(2000));put(binary,20,quint16(samples));put(binary,24,quint16(5));
    put(binary,300,quint16(0x100));put(binary,302,quint16(1));write(file,binary);
    for(int il=0;il<rows;++il)for(int xl=0;xl<columns;++xl){
        QByteArray header(240,'\0');put(header,0,qint32(il*columns+xl+1));put(header,4,qint32(il*columns+xl+1));
        put(header,70,qint16(1));put(header,88,qint16(1));put(header,108,qint16(100));
        put(header,114,quint16(samples));put(header,116,quint16(2000));
        put(header,188,qint32(100+il*2));put(header,192,qint32(200+xl*2));
        put(header,180,qint32(1000+xl*10));put(header,184,qint32(2000+il*20));write(file,header);
        QByteArray values(samples*4,'\0');
        for(int sample=0;sample<samples;++sample){const float value=amplitude(il,xl,sample);quint32 bits=0;std::memcpy(&bits,&value,4);put(values,sample*4,bits);}
        write(file,values);
    }
}
long long importVolume(DataService& service,const QString& path,int samples,std::string& error){
    ImportOptions options;options.copySources=false;ImportReport report;
    check(service.importFile(utf8(path),options,report,error),error);check(report.failed==0,"fixture import succeeds");
    std::vector<DataEntry> entries;check(service.listData("volumes",0,entries,error),error);
    for(const auto& entry:entries){SeismicVolume volume;check(service.readVolume(entry.id,volume,error),error);
        if(volume.dataFile!=utf8(path))continue;
        volume.geometry.coordinateSystem="SYNTHETIC_LOCAL_GRID";volume.geometry.coordinateUnit="m";
        volume.sampling={samples,100,2,"ms","TWT","synthetic time origin"};
        check(service.saveVolume(entry.id,volume,error),error);return entry.id;
    }
    throw std::runtime_error("generated volume not registered");
}
void verifyColumns(const SeismicSectionResult& result,int sourceColumns){
    check(result.amplitudes.size()==size_t(result.columns)*result.samples,"matrix shape");
    check(result.pathX.size()==size_t(result.columns)&&result.distances.size()==size_t(result.columns),"wall geometry aligned with amplitudes");
    for(int col=0;col<result.columns;++col){
        const int il=(result.inlineNumbers[col]-100)/2,xl=(result.crosslineNumbers[col]-200)/2;
        check(result.traceIndexes[col]==static_cast<long long>(il)*sourceColumns+xl,"physical trace identity");
        check(result.x[col]==1000+xl*10&&result.y[col]==2000+il*20,"actual trace coordinate");
        for(int sample=0;sample<result.samples;++sample)
            check(result.amplitudes[size_t(col)*result.samples+sample]==amplitude(il,xl,sample),"exact un-interpolated source amplitude");
    }
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("seismic-hd-tests");
    try{
        QTemporaryDir temp;check(temp.isValid(),"temporary directory");DataService service;std::string error;
        check(service.createProject(utf8(temp.path()+"/project"),"Synthetic HD tests",error),error);
        const auto largePath=temp.path()+"/wide-generated.sgy";fixture(largePath,2,9001,1000);
        const auto volume=importVolume(service,largePath,1000,error);const QFileInfo sourceBefore(largePath);
        SeismicSectionResult line;SectionReadOptions options;options.maxColumns=8;options.maxValues=10000000;options.fullResolution=true;
        check(service.readSeismicLine(volume,"inline",100,options,line,error),error);
        check(line.columns==9001&&line.samples==1000&&line.nativeColumns==9001&&!line.decimated,"HD exceeds old 8192-column and 8M-value limits");
        verifyColumns(line,9001);check(line.amplitudes.front()==-8&&line.amplitudes[8]==0,"negative and zero amplitudes preserved");
        check(line.vertexColumns==std::vector<int>({0,9000})&&line.distances.back()==90000,"native line endpoint geometry");
        options.maxValues=8000000;
        check(!service.readSeismicLine(volume,"inline",100,options,line,error)&&line.amplitudes.empty()&&error.find("9001000")!=std::string::npos,"HD budget fails explicitly without lower resolution or partial output");
        options.fullResolution=false;
        check(service.readSeismicLine(volume,"inline",100,options,line,error),error);
        check(line.columns==8&&line.decimated&&line.nativeColumns==9001,"preview truthfully reports spatial decimation");
        options.fullResolution=true;options.maxValues=10000000;int calls=0;
        check(!service.readSeismicLine(volume,"inline",100,options,line,error,[&]{return ++calls>300;})&&line.amplitudes.empty(),"mid-read cancellation publishes no partial matrix");
        TimeSliceReadOptions sliceOptions;sliceOptions.fullResolution=true;sliceOptions.maxInlineCount=1;sliceOptions.maxCrosslineCount=2;sliceOptions.maxValues=20000;
        SeismicTimeSliceResult slice;check(service.readSeismicTimeSlice(volume,104,sliceOptions,slice,error),error);
        check(slice.inlineNumbers.size()==2&&slice.crosslineNumbers.size()==9001&&!slice.decimated,"HD slice exceeds old 4096 axis limit and ignores preview caps");
        check(slice.nativeInlineCount==2&&slice.nativeCrosslineCount==9001,"full slice native dimensions");
        for(int il=0;il<2;++il)for(int xl=0;xl<9001;++xl)check(slice.amplitudes[size_t(il)*9001+xl]==amplitude(il,xl,2),"slice exact sample values");
        sliceOptions.maxValues=18001;check(!service.readSeismicTimeSlice(volume,104,sliceOptions,slice,error)&&slice.amplitudes.empty(),"full slice budget rejected");
        sliceOptions.fullResolution=false;check(service.readSeismicTimeSlice(volume,104,sliceOptions,slice,error),error);
        check(slice.decimated&&slice.amplitudes.size()==2,"slice preview resolution reported");
        check(QFileInfo(largePath).size()==sourceBefore.size()&&QFileInfo(largePath).lastModified()==sourceBefore.lastModified(),"SEG-Y remains untouched");

        const auto smallPath=temp.path()+"/wall-generated.sgy";fixture(smallPath,5,7,8);const auto small=importVolume(service,smallPath,8,error);
        SeismicSection wall;wall.sectionName="three connected wall segments";wall.parentVolumeId=static_cast<int>(small);
        wall.coordinateSystem="SYNTHETIC_LOCAL_GRID";wall.coordinateUnit="m";
        wall.pathPoints={{1000,2000},{1060,2000},{1060,2080},{1020,2080}};
        options={};options.fullResolution=true;options.maxColumns=2;options.maxValues=1000;
        check(service.extractSection(wall,options,line,error),error);
        check(line.columns==15&&line.nativeColumns==15&&!line.decimated,"native grid wall retains all crossed cells");
        check(line.vertexColumns==std::vector<int>({0,6,10,14}),"all four input vertices retained in original order");
        check(line.distances.back()==180,"wall cumulative distance");verifyColumns(line,7);
        for(size_t vertex=0;vertex<wall.pathPoints.size();++vertex){const int column=line.vertexColumns[vertex];
            check(line.pathX[column]==wall.pathPoints[vertex].x&&line.pathY[column]==wall.pathPoints[vertex].y,"precise requested bend coordinates");}
        long long sectionId=0;check(service.saveSection(sectionId,wall,error),error);SeismicSectionResult saved;
        check(service.extractSection(sectionId,options,saved,error),error);
        check(saved.amplitudes==line.amplitudes&&saved.pathX==line.pathX&&saved.vertexColumns==line.vertexColumns,"saved and direct path APIs agree");
        options.fullResolution=false;options.maxColumns=4;
        check(service.extractSection(wall,options,line,error)&&line.columns==4&&line.decimated&&line.vertexColumns==std::vector<int>({0,1,2,3}),"preview preserves every bend");
        options.maxColumns=3;check(!service.extractSection(wall,options,line,error)&&line.amplitudes.empty(),"cannot silently remove a bend to fit preview budget");
        options.fullResolution=true;options.maxValues=119;
        check(!service.extractSection(wall,options,line,error)&&line.amplitudes.empty(),"wall full resolution budget enforced");
        options.maxValues=1000;
        wall.pathPoints={{1000,2000},{1060,2080}};
        check(service.extractSection(wall,options,line,error),error);verifyColumns(line,7);
        check(line.columns>7,"oblique traversal retains cells missed by fixed endpoint sampling");
        wall.pathPoints={{1001,2001},{1002,2002}};
        check(service.extractSection(wall,options,line,error)&&line.columns==2&&line.traceIndexes[0]==line.traceIndexes[1],"sub-cell path preserves both endpoints without fabricating source detail");
        wall.pathPoints={{1000,2000},{1000,2000}};check(!service.extractSection(wall,options,line,error),"coincident vertices rejected");
        wall.pathPoints={{1000,2000},{999,2000}};check(!service.extractSection(wall,options,line,error),"out-of-coverage wall rejected");
        wall.pathPoints={{1000,2000},{1060,2080}};wall.coordinateSystem="UNRELATED_GRID";check(!service.extractSection(wall,options,line,error),"CRS mismatch rejected");
        wall.coordinateSystem="SYNTHETIC_LOCAL_GRID";check(!service.extractSection(wall,options,line,error,[]{return true;})&&line.amplitudes.empty(),"wall cancellation publishes nothing");
        service.closeProject();check(service.openProject(utf8(temp.path()+"/project"),error),error);
        check(service.extractSection(sectionId,options,line,error)&&line.vertexColumns==std::vector<int>({0,6,10,14}),"saved wall survives project reopening");
        std::cout<<"HD seismic checks passed: source resolution, explicit budgets, exact values, connected XY walls, vertices, cancellation and persistence\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
