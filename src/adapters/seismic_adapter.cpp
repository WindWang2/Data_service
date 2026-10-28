// Business adapter: metadata stays in SQLite; SEG-Y bytes are read on demand via segyio.
#include "paleo_data/service.h"
#include "segy_reader.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace paleo::data {
namespace {
constexpr long long ValueLimit=8000000;
bool reject(std::string& error,const std::string& text){error=text;return false;}
bool stopped(const CancelCheck& cancel,std::string& error){return cancel&&cancel()?(error="地震读取已取消",true):false;}
bool sourceVolume(const DataService& service,long long id,SeismicVolume& volume,std::string& error){
    if(!service.readVolume(id,volume,error))return false;
    std::vector<DataEntry> entries;if(!service.listData("volumes",0,entries,error))return false;
    for(const auto& entry:entries)if(entry.id==id)return service.verifySource(entry.sourceId,error);
    return reject(error,"地震体来源不存在");
}
double milliseconds(const SeismicSampling& s){return s.timeUnit=="s"?1000.0:s.timeUnit=="ms"?1.0:Missing;}
bool conventions(const SeismicVolume& volume,std::string& error){
    const auto& s=volume.sampling;
    if(volume.geometry.coordinateSystem.empty()||volume.geometry.coordinateUnit.empty())return reject(error,"地震体坐标系和坐标单位尚未确认");
    if(volume.geometry.coordinateUnit!="m"&&volume.geometry.coordinateUnit!="ft")return reject(error,"按XY提取仅支持已确认的平面m/ft坐标");
    if(!std::isfinite(s.firstTime)||!std::isfinite(s.sampleInterval)||s.sampleInterval<=0||s.sampleCount<=0||!std::isfinite(milliseconds(s)))return reject(error,"请先确认共同采样轴与时间单位ms/s");
    if((s.timeType!="TWT"&&s.timeType!="OWT")||s.timeReference.empty())return reject(error,"请先确认单/双程时间与时间基准");
    return true;
}
bool checkSavedAxis(const SeismicVolume& volume,const segy_detail::Reader& reader,std::string& error){
    const auto& facts=reader.info();const auto& s=volume.sampling;const double factor=milliseconds(s);
    if(s.sampleCount!=facts.sampleCount||std::abs(s.sampleInterval*factor-facts.sampleInterval)>1e-8||std::abs(s.firstTime*factor-facts.firstTime)>1e-8)
        return reject(error,"保存的地震采样轴与实际SEG-Y不一致，请先核对元信息");
    if(!reader.hasCoordinateMap()||(facts.coordinateUnitsCode!=0&&facts.coordinateUnitsCode!=1))return reject(error,"SEG-Y缺少可验证的平面XY坐标，无法按坐标提取");
    if(reader.maxCoordinateResidual()>(volume.geometry.coordinateUnit=="ft"?9.84252:3.0))return reject(error,"地震坐标偏离规则网格过大，无法保证XY定位精度");
    return true;
}
bool geometry(const segy_detail::Reader& reader,std::string& error){
    return (!reader.inlines().empty()&&!reader.crosslines().empty())||reject(error,"无法确认规则测线网格；大型文件未被全量扫描，仍可按文件道序号读取");
}
int nearest(const std::vector<int>& axis,double value){
    const auto it=std::lower_bound(axis.begin(),axis.end(),value);
    if(it==axis.begin())return *it;if(it==axis.end())return axis.back();
    return value-*(it-1)<=*it-value?*(it-1):*it;
}
std::vector<int> selectedAxis(const std::vector<int>& axis,int maximum){
    const int count=std::min(static_cast<int>(axis.size()),maximum);std::vector<int> output;output.reserve(count);
    for(int n=0;n<count;++n){const size_t at=count==1?axis.size()/2:static_cast<size_t>(static_cast<long long>(n)*(axis.size()-1)/(count-1));output.push_back(axis[at]);}return output;
}
bool sectionOptions(const SectionReadOptions& options,std::string& error){
    return (options.maxColumns>=2&&options.maxColumns<=8192&&options.maxValues>=1&&options.maxValues<=ValueLimit)||reject(error,"剖面上限需为2–8192列、1–8000000个振幅");
}
bool timeWindow(const SeismicFileInfo& facts,double first,double last,int& begin,int& count,std::string& error){
    const double end=facts.firstTime+(facts.sampleCount-1)*facts.sampleInterval;
    if(std::isnan(first))first=facts.firstTime;if(std::isnan(last))last=end;
    if(!std::isfinite(first)||!std::isfinite(last)||first>last||last<facts.firstTime||first>end)return reject(error,"时间范围无效或与SEG-Y不相交");
    begin=static_cast<int>(std::ceil((std::max(first,facts.firstTime)-facts.firstTime)/facts.sampleInterval-1e-9));
    const int finish=static_cast<int>(std::floor((std::min(last,end)-facts.firstTime)/facts.sampleInterval+1e-9));
    count=finish-begin+1;return count>0||reject(error,"时间范围内没有采样点");
}
bool checkCoordinate(const segy_detail::Reader& reader,int il,int xl,const SeismicTraceResult& trace,double limit,std::string& error){
    double x=Missing,y=Missing;
    if(!reader.mapLine(il,xl,x,y)||!std::isfinite(trace.x)||!std::isfinite(trace.y)||std::hypot(trace.x-x,trace.y-y)>limit)
        return reject(error,"所选道的实际XY坐标偏离抽样网格，已停止避免错误定位");
    return true;
}
bool timeSliceFromReader(segy_detail::Reader& reader,double time,const TimeSliceReadOptions& options,
                          SeismicTimeSliceResult& result,std::string& error,const CancelCheck& cancel,bool strictCoordinates=false,double residualLimit=3.0){
    if(!std::isfinite(time)||options.maxInlineCount<1||options.maxInlineCount>4096||options.maxCrosslineCount<1||options.maxCrosslineCount>4096||options.maxValues<1||options.maxValues>ValueLimit)
        return reject(error,"时间切片参数无效：轴上限1–4096，振幅预算最多8000000");
    if(!geometry(reader,error))return false;
    const auto& facts=reader.info();const double sample=(time-facts.firstTime)/facts.sampleInterval;
    if(sample<0||sample>facts.sampleCount-1)return reject(error,"请求时间超出地震体采样范围");
    const int sampleIndex=static_cast<int>(std::round(sample));
    SeismicTimeSliceResult output;output.inlineNumbers=selectedAxis(reader.inlines(),options.maxInlineCount);output.crosslineNumbers=selectedAxis(reader.crosslines(),options.maxCrosslineCount);
    const long long cells=static_cast<long long>(output.inlineNumbers.size())*output.crosslineNumbers.size();
    if(cells<1||cells>options.maxValues)return reject(error,"切片点数超过振幅预算，请降低测线数量上限");
    output.actualTime=facts.firstTime+sampleIndex*facts.sampleInterval;output.timeUnit="ms";
    output.x.reserve(cells);output.y.reserve(cells);output.amplitudes.reserve(cells);bool any=false;
    for(int il:output.inlineNumbers)for(int xl:output.crosslineNumbers){
        if(stopped(cancel,error))return false;
        double x=Missing,y=Missing;reader.mapLine(il,xl,x,y);float amplitude=std::numeric_limits<float>::quiet_NaN();
        const long long ordinal=reader.traceIndex(il,xl);
        if(ordinal>=0){SeismicTraceResult trace;if(!reader.readTrace(ordinal,sampleIndex,1,trace,error,cancel))return false;
            if(strictCoordinates&&!checkCoordinate(reader,il,xl,trace,residualLimit,error))return false;
            x=trace.x;y=trace.y;amplitude=trace.amplitudes.front();any|=std::isfinite(amplitude);}
        output.x.push_back(x);output.y.push_back(y);output.amplitudes.push_back(amplitude);
    }
    if(!any)return reject(error,"时间切片没有有限振幅");if(stopped(cancel,error))return false;result=std::move(output);return true;
}
}

bool DataService::inspectSeismic(long long volumeId,SeismicFileInfo& result,std::string& error,const CancelCheck& cancel){
    result={};error.clear();try{if(stopped(cancel,error))return false;SeismicVolume volume;if(!sourceVolume(*this,volumeId,volume,error))return false;
        return segy_detail::inspectSegyFile(volume.dataFile,result,error,cancel);
    }catch(const std::exception& e){return reject(error,std::string("地震检查失败：")+e.what());}
}
bool DataService::readSeismicTrace(long long volumeId,long long traceIndex,const SeismicTraceReadOptions& options,SeismicTraceResult& result,std::string& error,const CancelCheck& cancel){
    result={};error.clear();try{
        if(stopped(cancel,error))return false;
        if(options.firstSample<0||options.sampleCount<0||options.maxSamples<1||options.maxSamples>ValueLimit)return reject(error,"单道采样参数无效");
        SeismicVolume volume;if(!sourceVolume(*this,volumeId,volume,error))return false;
        segy_detail::Reader reader;if(!reader.open(volume.dataFile,error,cancel,false))return false;
        const auto& facts=reader.info();if(options.firstSample>=facts.sampleCount)return reject(error,"起始样点超出本道范围");
        const int count=options.sampleCount==0?facts.sampleCount-options.firstSample:options.sampleCount;
        if(count>options.maxSamples)return reject(error,"单道结果超过maxSamples预算");
        return reader.readTrace(traceIndex,options.firstSample,count,result,error,cancel);
    }catch(const std::exception& e){return reject(error,std::string("地震单道读取失败：")+e.what());}
}
bool DataService::readSeismicLine(long long volumeId,const std::string& direction,int lineNumber,const SectionReadOptions& options,SeismicSectionResult& result,std::string& error,const CancelCheck& cancel){
    result={};error.clear();try{
        if(stopped(cancel,error)||!sectionOptions(options,error))return false;
        if(direction!="inline"&&direction!="crossline")return reject(error,"direction应为inline或crossline");
        SeismicVolume volume;if(!sourceVolume(*this,volumeId,volume,error))return false;
        segy_detail::Reader reader;if(!reader.open(volume.dataFile,error,cancel)||!geometry(reader,error))return false;
        const auto& fixed=direction=="inline"?reader.inlines():reader.crosslines();
        if(!std::binary_search(fixed.begin(),fixed.end(),lineNumber))return reject(error,"请求测线编号不存在");
        const auto& axis=direction=="inline"?reader.crosslines():reader.inlines();
        int begin=0,count=0;if(!timeWindow(reader.info(),options.firstTime,options.lastTime,begin,count,error))return false;
        const int maxColumns=std::min(options.maxColumns,static_cast<int>(options.maxValues/count));
        if(maxColumns<1)return reject(error,"剖面时间范围超过振幅预算");
        const auto selected=selectedAxis(axis,maxColumns);SeismicSectionResult output;
        output.columns=static_cast<int>(selected.size());output.samples=count;output.firstTime=reader.info().firstTime+begin*reader.info().sampleInterval;
        output.sampleInterval=reader.info().sampleInterval;output.timeUnit="ms";
        output.amplitudes.assign(size_t(output.columns)*count,std::numeric_limits<float>::quiet_NaN());bool any=false;
        for(int column=0;column<output.columns;++column){
            if(stopped(cancel,error))return false;const int il=direction=="inline"?lineNumber:selected[column],xl=direction=="inline"?selected[column]:lineNumber;
            const auto ordinal=reader.traceIndex(il,xl);double x=Missing,y=Missing;reader.mapLine(il,xl,x,y);
            if(ordinal>=0){SeismicTraceResult trace;if(!reader.readTrace(ordinal,begin,count,trace,error,cancel))return false;x=trace.x;y=trace.y;
                std::copy(trace.amplitudes.begin(),trace.amplitudes.end(),output.amplitudes.begin()+size_t(column)*count);
                for(float value:trace.amplitudes)any|=std::isfinite(value);}
            output.x.push_back(x);output.y.push_back(y);output.traceIndexes.push_back(ordinal);output.inlineNumbers.push_back(il);output.crosslineNumbers.push_back(xl);
        }
        if(!any)return reject(error,"测线没有有限振幅");if(stopped(cancel,error))return false;result=std::move(output);return true;
    }catch(const std::exception& e){return reject(error,std::string("地震测线读取失败：")+e.what());}
}
bool DataService::readSeismicTimeSlice(long long volumeId,double timeMs,const TimeSliceReadOptions& options,SeismicTimeSliceResult& result,std::string& error,const CancelCheck& cancel){
    result={};error.clear();try{if(stopped(cancel,error))return false;SeismicVolume volume;if(!sourceVolume(*this,volumeId,volume,error))return false;
        segy_detail::Reader reader;if(!reader.open(volume.dataFile,error,cancel))return false;
        return timeSliceFromReader(reader,timeMs,options,result,error,cancel);
    }catch(const std::exception& e){return reject(error,std::string("原生时间切片读取失败：")+e.what());}
}
bool DataService::readTimeSlice(long long volumeId,double time,const TimeSliceReadOptions& options,SeismicTimeSliceResult& result,std::string& error,const CancelCheck& cancel){
    result={};error.clear();try{if(stopped(cancel,error))return false;SeismicVolume volume;if(!sourceVolume(*this,volumeId,volume,error)||!conventions(volume,error))return false;
        segy_detail::Reader reader;if(!reader.open(volume.dataFile,error,cancel)||!checkSavedAxis(volume,reader,error))return false;
        SeismicTimeSliceResult output;const double factor=milliseconds(volume.sampling);
        if(!timeSliceFromReader(reader,time*factor,options,output,error,cancel,true,volume.geometry.coordinateUnit=="ft"?9.84252:3.0))return false;
        output.actualTime/=factor;output.timeUnit=volume.sampling.timeUnit;result=std::move(output);return true;
    }catch(const std::exception& e){return reject(error,std::string("时间切片读取失败：")+e.what());}
}
bool DataService::extractSection(long long sectionId,const SectionReadOptions& options,SeismicSectionResult& result,std::string& error,const CancelCheck& cancel){
    result={};error.clear();try{
        if(stopped(cancel,error)||!sectionOptions(options,error))return false;
        SeismicSection section;if(!readSection(sectionId,section,error))return false;
        if(section.parentVolumeId<=0)return reject(error,"二维剖面尚未关联地震体");
        SeismicVolume volume;if(!sourceVolume(*this,section.parentVolumeId,volume,error)||!conventions(volume,error))return false;
        if(section.coordinateSystem.empty()||section.coordinateUnit.empty()||section.coordinateSystem!=volume.geometry.coordinateSystem||section.coordinateUnit!=volume.geometry.coordinateUnit)return reject(error,"剖面与地震体必须确认相同坐标系及单位；当前不自动投影转换");
        if(section.pathPoints.size()<2||section.pathPoints.size()>10000)return reject(error,"剖面需要2–10000个有序路径点");
        segy_detail::Reader reader;if(!reader.open(volume.dataFile,error,cancel)||!geometry(reader,error)||!checkSavedAxis(volume,reader,error))return false;
        std::vector<double> lengths{0};double total=0;
        for(size_t n=0;n<section.pathPoints.size();++n){const auto& point=section.pathPoints[n];if(!reader.covers(point.x,point.y))return reject(error,"剖面路径存在无效坐标或超出测区范围");
            if(n){const auto& previous=section.pathPoints[n-1];const double distance=std::hypot(point.x-previous.x,point.y-previous.y);if(distance<=0||!std::isfinite(distance))return reject(error,"剖面相邻点不能重合");total+=distance;lengths.push_back(total);}}
        const double factor=milliseconds(volume.sampling);int begin=0,count=0;
        if(!timeWindow(reader.info(),options.firstTime*factor,options.lastTime*factor,begin,count,error))return false;
        const int columns=std::min(options.maxColumns,static_cast<int>(options.maxValues/count));if(columns<2)return reject(error,"剖面时间范围超过振幅预算");
        SeismicSectionResult output;output.columns=columns;output.samples=count;output.firstTime=(reader.info().firstTime+begin*reader.info().sampleInterval)/factor;output.sampleInterval=reader.info().sampleInterval/factor;output.timeUnit=volume.sampling.timeUnit;
        output.amplitudes.assign(size_t(columns)*count,std::numeric_limits<float>::quiet_NaN());size_t segment=1;bool any=false;
        const double residualLimit=volume.geometry.coordinateUnit=="ft"?9.84252:3.0;
        for(int column=0;column<columns;++column){if(stopped(cancel,error))return false;
            const double distance=total*column/(columns-1);while(segment+1<lengths.size()&&distance>lengths[segment])++segment;
            const double fraction=(distance-lengths[segment-1])/(lengths[segment]-lengths[segment-1]);const auto& a=section.pathPoints[segment-1];const auto& b=section.pathPoints[segment];
            double il=0,xl=0;if(!reader.mapXY(a.x+(b.x-a.x)*fraction,a.y+(b.y-a.y)*fraction,il,xl))return reject(error,"剖面坐标转换失败");
            const int actualIl=nearest(reader.inlines(),il),actualXl=nearest(reader.crosslines(),xl);double x=Missing,y=Missing;reader.mapLine(actualIl,actualXl,x,y);
            const auto ordinal=reader.traceIndex(actualIl,actualXl);
            if(ordinal>=0){SeismicTraceResult trace;if(!reader.readTrace(ordinal,begin,count,trace,error,cancel)||!checkCoordinate(reader,actualIl,actualXl,trace,residualLimit,error))return false;
                x=trace.x;y=trace.y;std::copy(trace.amplitudes.begin(),trace.amplitudes.end(),output.amplitudes.begin()+size_t(column)*count);for(float value:trace.amplitudes)any|=std::isfinite(value);}
            output.x.push_back(x);output.y.push_back(y);output.traceIndexes.push_back(ordinal);output.inlineNumbers.push_back(actualIl);output.crosslineNumbers.push_back(actualXl);
        }
        if(!any)return reject(error,"剖面没有有限振幅；缺失道保留NaN");if(stopped(cancel,error))return false;result=std::move(output);return true;
    }catch(const std::exception& e){return reject(error,std::string("剖面提取失败：")+e.what());}
}
}
