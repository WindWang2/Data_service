#include "segy_window_io.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace paleo::data::segy_detail {
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point first){return std::chrono::duration<double,std::milli>(Clock::now()-first).count();}
bool fail(std::string& error,const std::string& message){error=message;return false;}
#ifdef _WIN32
// No C++ objects with destructors in this function: MSVC SEH must remain isolated.
bool mappedCopy(void* destination,const void* source,size_t length){
#ifdef _MSC_VER
    __try {std::memcpy(destination,source,length);return true;}
    __except(GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
#else
    std::memcpy(destination,source,length);return true;
#endif
}
#endif
}
struct WindowAccess::Impl {
    QFile file;
    std::string mode="buffered";
    long long fileBytes=0,windowBytes=0,begin=-1,available=0;
    std::vector<unsigned char> buffer;
#ifdef _WIN32
    HANDLE nativeFile=INVALID_HANDLE_VALUE,mapping=nullptr;
    const unsigned char* view=nullptr;
    unsigned long granularity=65536;
    ~Impl(){if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);if(nativeFile!=INVALID_HANDLE_VALUE)CloseHandle(nativeFile);}
#endif
};
WindowAccess::WindowAccess():impl_(std::make_unique<Impl>()){}
WindowAccess::~WindowAccess()=default;
const std::string& WindowAccess::mode()const{return impl_->mode;}
bool WindowAccess::open(const QString& path,long long expectedBytes,const std::string& requested,int windowMiB,std::string& error){
    error.clear();impl_=std::make_unique<Impl>();auto& p=*impl_;
    if(expectedBytes<0||windowMiB<1||windowMiB>32)return fail(error,"批量窗口大小应为1–32 MiB");
    p.windowBytes=static_cast<long long>(windowMiB)*1024*1024;p.fileBytes=expectedBytes;
    if(requested!="auto"&&requested!="buffered"&&requested!="mapped")return fail(error,"批量读取模式无效");
#ifdef _WIN32
    if(requested!="buffered"){
        DWORD mappingError=ERROR_SUCCESS;
        p.nativeFile=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,
                                 nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(p.nativeFile!=INVALID_HANDLE_VALUE){
            LARGE_INTEGER size{};
            if(!GetFileSizeEx(p.nativeFile,&size)||size.QuadPart!=expectedBytes)
                return fail(error,"来源大小在批量打开时发生变化");
            p.mapping=CreateFileMappingW(p.nativeFile,nullptr,PAGE_READONLY,0,0,nullptr);
            if(p.mapping){SYSTEM_INFO info{};GetSystemInfo(&info);p.granularity=info.dwAllocationGranularity;p.mode="mapped";return true;}
            mappingError=GetLastError();
            CloseHandle(p.nativeFile);p.nativeFile=INVALID_HANDLE_VALUE;
        }else mappingError=GetLastError();
        if(requested=="mapped")return fail(error,"Windows只读文件映射创建失败，错误码="+std::to_string(mappingError));
    }
#else
    if(requested=="mapped")return fail(error,"此平台未实现映射读取，请选择buffered或auto");
#endif
    p.file.setFileName(path);if(!p.file.open(QIODevice::ReadOnly))return fail(error,p.file.errorString().toUtf8().toStdString());
    if(p.file.size()!=expectedBytes)return fail(error,"来源大小在批量打开时发生变化");
    p.mode="buffered";p.buffer.resize(static_cast<size_t>(p.windowBytes));return true;
}
bool WindowAccess::read(long long offset,void* destination,size_t length,long long requestEnd,
                        SeismicReadStatistics& stats,bool timings,std::string& error){
    auto& p=*impl_;
    if(offset<0||requestEnd<offset||requestEnd>p.fileBytes||length>static_cast<unsigned long long>(requestEnd-offset))
        return fail(error,"批量读取范围越过文件或请求边界");
    auto* target=static_cast<unsigned char*>(destination);
    while(length){
        if(p.begin<0||offset<p.begin||offset>=p.begin+p.available){
            auto started=timings?Clock::now():Clock::time_point{};
#ifdef _WIN32
            if(p.mode=="mapped"){
                if(p.view){UnmapViewOfFile(p.view);p.view=nullptr;}
                p.begin=offset-offset%p.granularity;
                p.available=std::min(p.windowBytes,requestEnd-p.begin);
                const auto position=static_cast<unsigned long long>(p.begin);
                p.view=static_cast<const unsigned char*>(MapViewOfFile(p.mapping,FILE_MAP_READ,
                    static_cast<DWORD>(position>>32),static_cast<DWORD>(position),static_cast<SIZE_T>(p.available)));
                ++stats.seekCalls;
                if(timings)stats.positionMs+=milliseconds(started);
                if(!p.view){p.begin=-1;return fail(error,"Windows窗口映射失败，错误码="+std::to_string(GetLastError()));}
                stats.mappedBytes+=p.available;
            }else
#endif
            {
                p.begin=offset;p.available=std::min(p.windowBytes,requestEnd-offset);
                ++stats.seekCalls;
                const bool positioned=p.file.seek(offset);
                if(timings)stats.positionMs+=milliseconds(started);
                if(!positioned){p.begin=-1;return fail(error,p.file.errorString().toUtf8().toStdString());}
                started=timings?Clock::now():Clock::time_point{};
                const qint64 received=p.file.read(reinterpret_cast<char*>(p.buffer.data()),p.available);
                ++stats.readCalls;if(received>0)stats.bytesRead+=received;
                if(timings)stats.readMs+=milliseconds(started);
                if(received!=p.available){p.begin=-1;return fail(error,"来源读取不足或发生变化："+p.file.errorString().toUtf8().toStdString());}
            }
        }
        const auto count=static_cast<size_t>(std::min<unsigned long long>(length,p.begin+p.available-offset));
        const auto started=timings?Clock::now():Clock::time_point{};
#ifdef _WIN32
        if(p.mode=="mapped"){
            if(!mappedCopy(target,p.view+(offset-p.begin),count))return fail(error,"映射页读取失败，来源可能不可访问或已变化");
        }else
#endif
            std::memcpy(target,p.buffer.data()+(offset-p.begin),count);
        // Mapped copy includes page faults, but mappedBytes is not measured physical disk I/O.
        if(timings)stats.readMs+=milliseconds(started);
        target+=count;offset+=static_cast<long long>(count);length-=count;
    }
    return true;
}
}
