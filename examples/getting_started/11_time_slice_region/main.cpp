// 按实际测线号读取局部高清时间切片，边读取边接收完整分块。
// 本例只读使用用户指定的工区，原始 SEG-Y 不复制、不修改。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace paleo::data;

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=8){
        std::cerr<<"用法：example_11_time_slice_region <工区> <地震体编号> <时间ms> <IL起> <IL止> <XL起> <XL止>\n";
        return 1;
    }
    bool idOk=false,timeOk=false,il0Ok=false,il1Ok=false,xl0Ok=false,xl1Ok=false;
    const long long volumeId=args[2].toLongLong(&idOk);
    const double timeMs=args[3].toDouble(&timeOk);
    TimeSliceReadOptions options;
    options.inlineMin=args[4].toInt(&il0Ok);options.inlineMax=args[5].toInt(&il1Ok);
    options.crosslineMin=args[6].toInt(&xl0Ok);options.crosslineMax=args[7].toInt(&xl1Ok);
    if(!idOk||volumeId<=0||!timeOk||!std::isfinite(timeMs)||!il0Ok||!il1Ok||!xl0Ok||!xl1Ok){
        std::cerr<<"编号和测线号须为整数，时间须为有限数值。\n";return 1;
    }
    DataService service;std::string error;
    if(!service.openProject(args[1].toUtf8().toStdString(),error)){
        std::cerr<<error<<'\n';return 2;
    }

    options.fullResolution=true;   // 保留所选区域的每个实际网格点；裁剪范围不等于抽稀。
    options.maxValues=4000000;      // 本例最多约400万点；超限报错，不自动降低精度。
    options.ioMode="auto";          // 也可显式选 buffered / mapped；legacy 仅用于基准对照。
    options.workerCount=2;          // 可选1、2、4，每个任务独立只读访问文件。
    options.tileSize=128;           // 每块最多128²点；规则体可为长条块，图像分辨率不变。
    options.useCache=true;
    options.adjacentSamples=4;      // 一次读取当前时间所在的4样点时间带，附近时间可复用。
    options.cacheBudgetMiB=128;     // 进程内有界缓存，不生成地震体副本，不写磁盘缓存。
    options.collectTimings=true;   // 演示诊断；日常读取可关闭逐阶段计时。

    SeismicTimeSliceResult result;
    auto cancelled=[] {return false;}; // 实际GUI可读取自己的原子取消标志。
    auto tileReady=[](const SeismicTimeSliceTile& tile){
        // 这里运行在调用readSeismicTimeSlice的线程。
        // 若服务调用来自后台线程，应把块复制/移动到GUI消息队列后再绘图。
        std::cout<<"分块 "<<tile.completedTiles<<'/'<<tile.totalTiles
                 <<"：起始行列="<<tile.inlineOffset<<','<<tile.crosslineOffset
                 <<"，尺寸="<<tile.inlineCount<<'x'<<tile.crosslineCount
                 <<"，缓存="<<tile.fromCache<<'\n';
        // 此块行优先：tile.amplitudes[row * tile.crosslineCount + column]。
        // 只有完整校验过的块才回调；完整请求仍可能在后续失败/取消。
    };
    if(!service.readSeismicTimeSlice(volumeId,timeMs,options,result,error,cancelled,tileReady)){
        // 失败时result为空。GUI应清掉此前显示的临时块，不导出为完整结果。
        std::cerr<<error<<'\n';return 3;
    }
    const auto& stats=result.statistics;
    std::cout<<"完整区域="<<result.inlineNumbers.size()<<'x'<<result.crosslineNumbers.size()
             <<"，实际时间="<<result.actualTime<<' '<<result.timeUnit
             <<"，耗时ms="<<stats.elapsedMs<<"，命中块="<<stats.cacheHits
             <<"，读取模式="<<stats.ioMode<<'\n';
    // 返回的完整数组可以直接计算；无需重新解析文件，也无需先转成JSON。
    for(size_t i=0;i<std::min<size_t>(result.amplitudes.size(),10);++i)
        std::cout<<"XY="<<result.x[i]<<','<<result.y[i]<<"，振幅="<<result.amplitudes[i]<<'\n';
    // 同一进程随后请求相邻时间会复用命中的时间带。
    // DataService::clearSeismicSliceCache() 可显式清空缓存，不改变源文件。
    return 0;
}
