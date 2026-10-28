#include "apidemo.h"
#include "plotwidgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>

using namespace paleo::data;

// 单道使用自己的时间轴绘图，避免把地震时间错误标成“井深”。
class SeismicTracePlot final : public QWidget {
public:
    explicit SeismicTracePlot(QWidget* parent=nullptr):QWidget(parent){setMinimumSize(280,320);}
    void setTrace(const SeismicTraceResult& trace){trace_=trace;update();}
    void clear(){trace_={};update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().base());p.setPen(palette().text().color());
        if(trace_.amplitudes.empty()){p.drawText(rect(),Qt::AlignCenter,QStringLiteral("执行单道示例后显示真实振幅"));return;}
        double peak=0;for(float value:trace_.amplitudes)if(std::isfinite(value))peak=std::max(peak,std::abs(double(value)));if(peak==0)peak=1;
        const auto count=trace_.amplitudes.size();QRectF area(86,75,std::max(40,width()-112),std::max(40,height()-125));
        p.drawText(QRectF(8,10,width()-16,26),Qt::AlignCenter,QStringLiteral("地震单道 · 文件位置 %1 · IL %2 / XL %3").arg(trace_.traceIndex).arg(trace_.inlineNumber).arg(trace_.crosslineNumber));
        p.drawText(QRectF(8,38,width()-16,24),Qt::AlignCenter,QStringLiteral("横轴：原始振幅　纵轴：来源采样时间（%1）").arg(QString::fromStdString(trace_.timeUnit)));
        p.drawRect(area);
        for(int i=0;i<=4;++i){const double fraction=i/4.0;double y=area.top()+fraction*area.height();double x=area.left()+fraction*area.width();p.setPen(palette().mid().color());p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));p.setPen(palette().text().color());p.drawText(QRectF(2,y-10,76,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(trace_.firstTime+fraction*(count-1)*trace_.sampleInterval,'g',7));p.drawText(QRectF(x-37,area.bottom()+4,74,20),Qt::AlignCenter,QString::number(-peak+2*peak*fraction,'g',4));}
        QPainterPath path;bool active=false;const size_t stride=std::max<size_t>(1,count/12000);
        for(size_t i=0;i<count;++i){const float value=trace_.amplitudes[i];if(!std::isfinite(value)){active=false;continue;}if(i%stride!=0&&i+1!=count)continue;QPointF point(area.left()+(value+peak)/(2*peak)*area.width(),area.top()+(count>1?double(i)/(count-1):0)*area.height());if(active)path.lineTo(point);else path.moveTo(point);active=true;}
        p.save();p.setClipRect(area);p.setPen(QPen(QColor(45,125,210),1.1));p.drawPath(path);p.restore();p.setPen(palette().text().color());p.drawText(QRectF(5,height()-24,width()-10,20),Qt::AlignCenter,QStringLiteral("%1 个样点 · 负振幅保留 · 不改写原文件").arg(count));
    }
private:
    SeismicTraceResult trace_;
};

namespace {
bool nativeLine(const QString& kind){return kind=="inline"||kind=="crossline"||kind=="processed_section";}
bool nativeSeismic(const QString& kind){return nativeLine(kind)||kind=="seismic_info"||kind=="seismic_trace"||kind=="seismic_slice";}
QString q(const std::string& s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
std::string utf8(const QString& s){return s.toUtf8().toStdString();}
QString number(double v){return std::isfinite(v)?QString::number(v,'g',17):QStringLiteral("Missing");}
QJsonValue numeric(double v){return std::isfinite(v)?QJsonValue(v):QJsonValue(QJsonValue::Null);}
QString cppString(const QString& text){
    // Raw literals keep UTF-8 paths readable and do not interpret backslashes.
    // Choose a delimiter that cannot close inside the input string.
    QString delimiter="pds";
    while(text.contains(")"+delimiter+"\""))delimiter+="x";
    if(delimiter.size()>16){
        QByteArray bytes=text.toUtf8();QString escaped="\"";
        for(unsigned char c:bytes){if(c>=32&&c<127&&c!='\\'&&c!='\"')escaped+=QChar(c);else escaped+="\\"+QString::number(c,8).rightJustified(3,'0');}
        return escaped+"\"";
    }
    return "R\""+delimiter+"("+text+")"+delimiter+"\"";
}
QPlainTextEdit* editor(const char* name,QWidget* parent){
    auto* result=new QPlainTextEdit(parent);result->setObjectName(name);result->setReadOnly(true);
    result->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));result->setLineWrapMode(QPlainTextEdit::NoWrap);
    return result;
}
QWidget* editorPage(QPlainTextEdit* edit,const QString& label){
    auto* page=new QWidget;auto* layout=new QVBoxLayout(page);layout->setContentsMargins(6,6,6,6);
    auto* row=new QHBoxLayout;auto* caption=new QLabel(label);caption->setWordWrap(true);row->addWidget(caption,1);
    auto* copy=new QPushButton(QStringLiteral("复制"));row->addWidget(copy);
    QObject::connect(copy,&QPushButton::clicked,edit,[edit]{QApplication::clipboard()->setText(edit->toPlainText());});
    layout->addLayout(row);layout->addWidget(edit,1);return page;
}
QString sdkInstructions(){return QString::fromUtf8(R"PDS(# CMakeLists.txt —— C++ 中“导包”分为包含头文件和链接库
# 头文件告诉编译器“有哪些方法和数据类型”。
# 链接库提供这些方法真正的实现；只写 #include 还不能运行服务。
cmake_minimum_required(VERSION 3.24)
project(DataServiceConsumer LANGUAGES CXX)
# 与 SDK 使用相同的 C++20 标准。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 找到已安装的 SDK 配置，以及控制台程序需要的 Qt Core。
find_package(PaleoDataService CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)
# 把下一页复制出的源码保存为 main.cpp，生成可执行程序 data_demo。
add_executable(data_demo main.cpp)
# 把本程序与 SDK 静态库链接起来；数据库细节由库内部处理。
target_link_libraries(data_demo PRIVATE
    Paleo::paleo_data_service Qt6::Core)
if(MSVC)
    target_compile_options(data_demo PRIVATE /utf-8)
endif()

# 配置 / 编译（路径换成实际 SDK、Qt、Zlib 路径）：
# cmake -S . -B build -DCMAKE_PREFIX_PATH="<sdk>;<Qt6>" -DZLIB_ROOT=<Zlib>
# cmake --build build --config Release

# main.cpp 的引入方式（下一页有完整源码）：
#   #include <paleo_data/service.h>
#   #include <QCoreApplication>
#   using namespace paleo::data;
#
# SDK 公开数据字段采用标准 C++ 类型；内部依赖 Qt Core/Gui/Sql 和 Zlib。
# std::vector 可以理解为“能自动管理长度的数组”，std::string 是文本。
# bool 返回值只说明调用成功/失败；result 参数接收真正的数据对象。
# result 是结构化的 C++ 数据；本页的 JSON 是为了便于查看而生成的摘要。
# 一个进程建立一个 QCoreApplication（Qt 界面程序已有 QApplication）。
# 每个调用线程使用自己的 DataService，不能跨线程共享同一实例。
# 运行时带上 Qt 平台/SQLite 插件和库依赖；本项目 dist 已提供完整运行目录。
)PDS");}
QString schemaFor(const QString& kind){
    if(kind=="seismic_info")return QString::fromUtf8(R"PDS(inspectSeismic(volumeId, SeismicFileInfo& result, error)

result.engine                   实际读取引擎及版本
result.fileBytes / traceCount    文件字节数、固定布局中的道数
result.sampleCount              每道样点数
result.sampleFormat / byteOrder  样点格式、字节序
result.firstTime / sampleInterval / timeUnit
                                道头采样轴；不代表已确认TWT或基准面
result.firstTraceOffset         首道字节位置，含扩展文本头
result.geometryStatus           sampled-regular表示抽样规则网格
                                full-header-index表示完整道头索引
                                unavailable表示不能确定网格
result.inlineMin / inlineMax / inlineStep / inlineCount
result.crosslineMin / crosslineMax / crosslineStep / crosslineCount
result.checkedHeaders           本次检查过的道头数
result.textHeader / warnings    文本头和限制说明

结果是文件事实，不是完整振幅数据。大文件不隐式全量扫描。
抽样发现规则网格不等于逐道校验；实际请求道还需检查真实道头。
本调用不自动确认CRS、TWT、OWT或时间零点的地质含义。
)PDS");
    if(kind=="seismic_trace")return QString::fromUtf8(R"PDS(readSeismicTrace(volumeId, traceIndex, options, result, error)

SeismicTraceReadOptions options;
options.firstSample = 0;          第一个样点的下标
options.sampleCount = 0;          0表示读到本道末尾
options.maxSamples = 1000000;     超限报错，不偷偷截断

SeismicTraceResult result;
result.traceIndex                文件中从0开始的道位置
result.traceNumber               来源道号，两者不能混用
result.inlineNumber / crosslineNumber
result.x / y                     应用原文件缩放码后的坐标
result.firstTime / sampleInterval / timeUnit
result.amplitudes                真实的float振幅数组

第i点时间 = firstTime + i * sampleInterval。
可直接读取第0道，也能读取文件末尾，文件偏移使用64位。
读取原生单道不要求先填CRS；不将原文件复制到工区。
负振幅和0均是有效值。图形显示时间，不是井深。
)PDS");
    if(nativeLine(kind))return QString::fromUtf8(R"PDS(readSeismicLine(volumeId, direction, lineNumber, options, result, error)

direction: "inline" 或 "crossline"
lineNumber: 真实测线号；本示例留空时从探测网格选择中间线
SectionReadOptions options;
options.firstTime / lastTime     时间窗，Missing表示不限
options.maxColumns              最大输出道数（本例默认512）
options.maxValues               最大振幅数（本例2000000）

SeismicSectionResult result;
result.columns / samples        列数、每列样点数
result.traceIndexes             各列的原文件道位置
result.inlineNumbers / crosslineNumbers
result.x / y                     实际道位置；未知则NaN
result.amplitudes               列优先：column * samples + sample
result.firstTime / sampleInterval / timeUnit

无需先确认CRS即可看原生测线；网格定位与实际道头逐道核对。
“基础处理”示例额外调用 processSeismicSection：
removeMean=false，gain=1，agcWindowSamples=101，clipAmplitude=3。
默认仅RMS自动增益→限幅，保留原有0振幅；窗口不跨道，缺失值保留。
可选removeMean=true会先逐道减均值，也会改变原本为0的样点；需明确选择。
只处理本次小数组，原始剖面和源文件不改写，不代表完整地震处理流程。
)PDS");
    if(kind=="seismic_slice")return QString::fromUtf8(R"PDS(readSeismicTimeSlice(volumeId, timeMs, options, result, error)

TimeSliceReadOptions options;
options.maxInlineCount / maxCrosslineCount  每个轴的最大位置数
options.maxValues                         返回振幅数量上限

SeismicTimeSliceResult result;
result.actualTime / timeUnit                最近采样时间，单位ms
result.inlineNumbers / crosslineNumbers     真实测线编号
result.x / y / amplitudes                   同位置的坐标与振幅

行优先：index = inlineIndex * crosslineNumbers.size() + crosslineIndex。
本例默认每轴128个位置；读取相应道的一个样点，不读取每道全部振幅。
时间留空时从文件采样轴选择中间时间。网格或采样不一致会明确报错。
这是来源原生网格预览；不要求事先确认CRS、TWT、OWT，也不替用户猜测。
)PDS");
    if(kind=="intervals")return QString::fromUtf8(R"PDS(读取分层道信息：readIntervalTrackInfo(trackId, track, error)
读取具体层段：readIntervals(trackId, topDepth, bottomDepth, result, error)

IntervalTrack track;                 // 道名称、类型、深度单位及基准
std::vector<IntervalRecord> result;   // 实际层段数组，不是文件目录

每个 IntervalRecord 含：
  id                 数据库记录编号，不是数组下标
  trackId            所属分层道编号
  value.intervalName 层名或岩性描述
  value.topDepth     顶深
  value.bottomDepth  原记录止深，未钻穿时不能当成真实底界
  value.faciesType   相类型
  value.sourceRow    来源表格行号

范围筛选返回与查询范围相交的完整层段，不把顶底界裁切成查询边界。
深度类型、单位由 track.depthInfo 给出；空字符串表示尚未确认。
不自动把 MD、TVD、TVDSS 混为同一种深度，也不自动换算单位。
JSON 仅展示前 10 个层段；表格最多显示前 200 行，结果数组保留全部。
)PDS");
    if(kind=="time_depth")return QString::fromUtf8(R"PDS(readTimeDepth(relationId, result, error)

TimeDepthTable result;               // 已读取的完整时深关系
result.relationName                  // 时深表名称
result.depthInfo.depthType           // MD、TVD 等，空表示未确认
result.depthInfo.depthUnit           // 深度单位，空表示未确认
result.timeType                      // TWT 双程 / OWT 单程，空表示未知
result.timeUnit                      // 时间单位，空表示未确认
result.timeReference                 // 时间参考面，空表示未确认
result.depthTimePairs                // 实际配对点数组

result.depthTimePairs[i].depthValue  // 第 i 个点的深度
result.depthTimePairs[i].timeValue   // 同一个点的时间

这里读取完整的时深点，不执行插值、时深转换或正负号翻转。
负时间按来源保留；单位和参考面不清楚时不能擅自转换。
“工区全部时深资料”包括已归井和待归井；“仅待归井”不会创建假井。
JSON 仅展示前 10 个点；表格最多显示前 200 行，完整数组仍已返回。
)PDS");
    if(kind=="wells")return QString::fromUtf8(R"PDS(bool listWells(std::vector<WellSummary>& result, std::string& error) const;

struct WellSummary {
    long long id;
    std::string name;
    int logSets, curves, tracks, samples;
    int timeDepthTables, trajectorySurveys;
    bool hasCoordinates;
};

返回 true 表示调用成功；对象数组通过 result 返回。
返回 false 时先看 error，不把空数组当成成功结果。

curves == 0 只表示该井没有已入库的测井曲线。
该井仍可能有时深、井位、样品或地层资料。
这里显示所有井，曲线示例的井选择器只列出有曲线的井。
)PDS");
    if(kind=="curve")return QString::fromUtf8(R"PDS(bool listData("curves", wellId, std::vector<DataEntry>&, error) const;
bool readCurveSlice(curveId, topDepth, bottomDepth, processed,
                    CurveSlice& result, error) const;

struct CurveSlice {
    long long curveId;
    std::string name, unit, depthUnit;
    std::vector<double> depths;     // 深度轴
    std::vector<double> rawValues;  // 原始值
    std::vector<double> values;     // 本次请求的原始值或处理值
    std::vector<unsigned char> quality;
};

depths[i]、rawValues[i]、values[i]、quality[i] 一一对应。
topDepth/bottomDepth 留空表示 Missing，即不限制该端深度。
深度使用资料原生数值，不擅自换算；空单位表示未确认。
quality: 0 正常，1 空值，2 阈值异常，4 人工修改。
绘图使用同一次调用返回的 CurveSlice，不重新读取文件。
JSON 仅展示前 10 个位置；NaN 在 JSON 中显示 null。
)PDS");
    if(kind=="section")return QString::fromUtf8(R"PDS(bool extractSection(sectionId, const SectionReadOptions& options,
                    SeismicSectionResult& result, error, cancelled={});

struct SeismicSectionResult {
    int columns, samples;
    double firstTime, sampleInterval;
    std::string timeUnit;
    std::vector<double> x, y;      // 每列实际道位置
    std::vector<float> amplitudes; // 振幅：列优先
};

amplitudes[column * samples + sample]
time = firstTime + sample * sampleInterval

sectionId 是已保存的 XY 路径，已关联地震体。
路径至少两点；坐标系、单位、时间约定需要先确认。
本示例最多 512 列、200 万个振幅值，后台执行，可取消。
读取时访问地震文件，数据库只持久化路径和元信息。
缺道为 NaN；0 是有效振幅，不作为缺失值。
JSON 仅展示前 10 个位置；图形使用同一次调用的返回数组。
)PDS");
    return QString::fromUtf8(R"PDS(bool readTimeSlice(volumeId, time, const TimeSliceReadOptions& options,
                   SeismicTimeSliceResult& result, error, cancelled={});

struct SeismicTimeSliceResult {
    std::vector<int> inlineNumbers, crosslineNumbers;
    std::vector<double> x, y;
    std::vector<float> amplitudes; // 行优先
    double actualTime;
    std::string timeUnit;
};

index = inlineIndex * crosslineNumbers.size() + crosslineIndex
amplitudes[index]、x[index]、y[index] 对应同一网格位置。
inlineNumbers / crosslineNumbers 是实际测线编号，不是数组索引。

time 使用地震体已经确认的时间单位。
返回 actualTime 为最近样点时间，不隐式插值。
本示例最多 256 × 256 网格；缺道为 NaN，JSON 显示 null。
绘图来自同一次读取的返回结果，不把整个地震体载入数据库。
)PDS");
}
}

struct ApiDemoWidget::Request {
    QString kind,project;
    long long wellId=0,resourceId=0;
    double first=Missing,last=Missing;
    bool processed=true;
};
struct ApiDemoWidget::Result {
    bool ok=false;
    std::string error;
    qint64 milliseconds=0;
    std::vector<WellSummary> wells;
    std::vector<DataEntry> entries;
    CurveSlice curve;
    IntervalTrack intervalTrack;
    std::vector<IntervalRecord> intervals;
    TimeDepthTable timeDepth;
    SeismicSectionResult section;
    SeismicTimeSliceResult slice;
    SeismicFileInfo seismicInfo;
    SeismicTraceResult trace;
    SeismicStatistics statistics;
    SeismicStatistics rawStatistics;
    int actualLineNumber=0;
    double requestedTime=Missing;
};

ApiDemoWidget::ApiDemoWidget(QWidget* parent):QWidget(parent){
    setObjectName("apiDemoWidget");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(16,12,16,10);
    auto* title=new QLabel(QStringLiteral("接口代码演示  ·  选参数 → 看调用代码 → 执行真实服务 → 查看返回数据与图形"));
    QFont titleFont=title->font();titleFont.setPointSize(titleFont.pointSize()+2);titleFont.setBold(true);title->setFont(titleFont);title->setWordWrap(true);layout->addWidget(title);
    auto* selectors=new QHBoxLayout;selectors->addWidget(new QLabel(QStringLiteral("业务示例")));
    example_=new QComboBox;example_->setObjectName("apiExampleSelector");
    example_->addItem(QStringLiteral("井资料清单 · listWells"),"wells");example_->addItem(QStringLiteral("测井曲线 · readCurveSlice"),"curve");example_->addItem(QStringLiteral("地层 / 岩性 · readIntervals"),"intervals");example_->addItem(QStringLiteral("时深关系 · readTimeDepth"),"time_depth");example_->addItem(QStringLiteral("地震剖面 · extractSection"),"section");example_->addItem(QStringLiteral("时间切片 · readTimeSlice"),"slice");selectors->addWidget(example_,1);
    example_->addItem(QStringLiteral("SEG-Y 文件检查 · inspectSeismic"),"seismic_info");
    example_->addItem(QStringLiteral("地震单道 · readSeismicTrace"),"seismic_trace");
    example_->addItem(QStringLiteral("纵测线 INLINE · readSeismicLine"),"inline");
    example_->addItem(QStringLiteral("横测线 XLINE · readSeismicLine"),"crossline");
    example_->addItem(QStringLiteral("原生时间切片 · readSeismicTimeSlice"),"seismic_slice");
    example_->addItem(QStringLiteral("剖面基础处理 · AGC / 限幅"),"processed_section");
    wellLabel_=new QLabel(QStringLiteral("井"));selectors->addWidget(wellLabel_);well_=new QComboBox;well_->setObjectName("apiWellSelector");well_->setMinimumWidth(150);selectors->addWidget(well_,1);
    resourceLabel_=new QLabel(QStringLiteral("曲线"));selectors->addWidget(resourceLabel_);resource_=new QComboBox;resource_->setObjectName("apiResourceSelector");resource_->setMinimumWidth(170);selectors->addWidget(resource_,2);
    refresh_=new QPushButton(QStringLiteral("刷新可用资料"));refresh_->setObjectName("apiRefresh");selectors->addWidget(refresh_);layout->addLayout(selectors);
    auto* parameters=new QHBoxLayout;firstLabel_=new QLabel(QStringLiteral("顶深"));parameters->addWidget(firstLabel_);first_=new QLineEdit;first_->setObjectName("apiFirstValue");first_->setPlaceholderText(QStringLiteral("空为不限"));first_->setMaximumWidth(150);parameters->addWidget(first_);lastLabel_=new QLabel(QStringLiteral("底深"));parameters->addWidget(lastLabel_);last_=new QLineEdit;last_->setObjectName("apiLastValue");last_->setPlaceholderText(QStringLiteral("空为不限"));last_->setMaximumWidth(150);parameters->addWidget(last_);
    processed_=new QCheckBox(QStringLiteral("读取处理值"));processed_->setObjectName("apiProcessed");processed_->setChecked(true);parameters->addWidget(processed_);parameters->addStretch();
    run_=new QPushButton(QStringLiteral("▶ 执行示例"));run_->setObjectName("apiRun");run_->setMinimumWidth(130);parameters->addWidget(run_);cancel_=new QPushButton(QStringLiteral("取消"));cancel_->setObjectName("apiCancel");cancel_->setVisible(false);parameters->addWidget(cancel_);layout->addLayout(parameters);
    summary_=new QLabel;summary_->setObjectName("apiAvailability");summary_->setWordWrap(true);layout->addWidget(summary_);
    auto* split=new QSplitter(Qt::Horizontal,this);split->setChildrenCollapsible(false);layout->addWidget(split,1);
    codeTabs_=new QTabWidget;codeTabs_->setObjectName("apiCodeTabs");codeTabs_->setMinimumWidth(300);
    auto* sdk=editor("apiSdkInstructions",codeTabs_);sdk->setPlainText(sdkInstructions());codeTabs_->addTab(editorPage(sdk,QStringLiteral("先在项目中链接 SDK，再包含服务头文件。")),QStringLiteral("① 如何导包"));
    code_=editor("apiGeneratedCode",codeTabs_);codeTabs_->addTab(editorPage(code_,QStringLiteral("此代码与当前参数一致，可复制为 main.cpp 编译。按钮调用同一公开接口，不执行任意代码。")),QStringLiteral("② 调用代码"));
    schema_=editor("apiReturnSchema",codeTabs_);schema_->setLineWrapMode(QPlainTextEdit::WidgetWidth);codeTabs_->addTab(editorPage(schema_,QStringLiteral("返回对象的字段、数组顺序及缺失值约定。")),QStringLiteral("③ 返回类型"));split->addWidget(codeTabs_);
    resultTabs_=new QTabWidget;resultTabs_->setObjectName("apiResultTabs");resultTabs_->setMinimumWidth(320);
    visuals_=new QStackedWidget;wellTable_=new QTableWidget;wellTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);wellTable_->setAlternatingRowColors(true);wellTable_->verticalHeader()->setVisible(false);wellTable_->horizontalHeader()->setStretchLastSection(true);visuals_->addWidget(wellTable_);
    curve_=new CurvePlot;curve_->setObjectName("apiCurvePlot");visuals_->addWidget(curve_);image_=new ImagePreview;image_->setObjectName("apiSeismicPreview");visuals_->addWidget(image_);resultTabs_->addTab(visuals_,QStringLiteral("④ 结果可视化"));
    tracePlot_=new SeismicTracePlot;tracePlot_->setObjectName("apiSeismicTracePlot");visuals_->addWidget(tracePlot_);
    json_=editor("apiResultJson",resultTabs_);resultTabs_->addTab(editorPage(json_,QStringLiteral("这是服务真实返回值的摘要；数组只列前 10 项，完整数据用于绘图。")),QStringLiteral("返回数据 · JSON"));split->addWidget(resultTabs_);split->setStretchFactor(0,1);split->setStretchFactor(1,1);split->setSizes({620,680});
    status_=new QLabel;status_->setObjectName("apiStatus");status_->setWordWrap(true);layout->addWidget(status_);
    connect(example_,&QComboBox::currentIndexChanged,this,[this]{if(!updating_)exampleChanged();});
    connect(well_,&QComboBox::currentIndexChanged,this,[this]{if(!updating_){reloadResources();parametersChanged();}});
    connect(resource_,&QComboBox::currentIndexChanged,this,[this]{if(!updating_){
        if(example_->currentData().toString()=="slice"&&resource_->currentData().toLongLong()>0){DataService s;std::string e;SeismicVolume v;if(s.openProject(utf8(projectPath_),e)&&s.readVolume(resource_->currentData().toLongLong(),v,e)&&std::isfinite(v.sampling.firstTime)){QSignalBlocker b(first_);first_->setText(number(v.sampling.firstTime));}}
        parametersChanged();
    }});
    connect(first_,&QLineEdit::textChanged,this,[this]{if(!updating_)parametersChanged();});connect(last_,&QLineEdit::textChanged,this,[this]{if(!updating_)parametersChanged();});connect(processed_,&QCheckBox::toggled,this,[this]{if(!updating_)parametersChanged();});
    connect(run_,&QPushButton::clicked,this,&ApiDemoWidget::runExample);connect(cancel_,&QPushButton::clicked,this,&ApiDemoWidget::requestCancel);connect(refresh_,&QPushButton::clicked,this,[this]{reloadChoices();});
    exampleChanged();
}
ApiDemoWidget::~ApiDemoWidget(){cancelRequested_=true;if(worker_){worker_->wait();delete worker_;worker_=nullptr;}}
bool ApiDemoWidget::isRunning()const{return worker_!=nullptr;}
bool ApiDemoWidget::lastRunSucceeded()const{return succeeded_;}
QString ApiDemoWidget::lastResultJson()const{return json_->toPlainText();}
QString ApiDemoWidget::generatedCode()const{return code_->toPlainText();}
void ApiDemoWidget::requestCancel(){if(worker_){cancelRequested_=true;cancel_->setEnabled(false);status_->setText(QStringLiteral("已请求取消，等待当前服务调用安全结束。"));}}
void ApiDemoWidget::setProjectPath(const QString& path){if(isRunning())return;projectPath_=path;reloadChoices();}
void ApiDemoWidget::setExample(const QString& kind){if(isRunning())return;int i=example_->findData(kind);if(i>=0){if(i==example_->currentIndex())exampleChanged();else example_->setCurrentIndex(i);}}
bool ApiDemoWidget::selectResource(long long id){
    if(isRunning())return false;
    const auto kind=example_->currentData().toString();
    if(kind=="curve"||kind=="intervals"||kind=="time_depth"){
        DataService service;std::string error;std::vector<DataEntry> all;
        const auto type=kind=="curve"?"curves":utf8(kind);
        if(!service.openProject(utf8(projectPath_),error)||!service.listData(type,0,all,error))return false;
        for(const auto& item:all)if(item.id==id){const auto wellId=kind=="time_depth"&&item.wellId==0?-1:item.wellId;int w=well_->findData(QVariant::fromValue<qlonglong>(wellId));if(w<0)return false;well_->setCurrentIndex(w);break;}
    }
    int index=resource_->findData(QVariant::fromValue<qlonglong>(id));if(index<0)return false;resource_->setCurrentIndex(index);return true;
}
void ApiDemoWidget::reloadChoices(){
    if(isRunning())return;const auto selected=well_->currentData().toLongLong();const auto kind=example_->currentData().toString();updating_=true;well_->clear();wells_.clear();
    if(kind=="time_depth"){
        well_->addItem(QStringLiteral("工区全部时深资料"),QVariant::fromValue<qlonglong>(0));
        well_->addItem(QStringLiteral("仅待归井时深资料"),QVariant::fromValue<qlonglong>(-1));
    }
    DataService service;std::string error;
    if(!projectPath_.isEmpty()&&service.openProject(utf8(projectPath_),error)&&service.listWells(wells_,error)){
        for(const auto& w:wells_){
            int count=w.curves;QString type=QStringLiteral("条曲线");
            if(kind=="intervals"){count=w.tracks;type=QStringLiteral("道分层");}
            if(kind=="time_depth"){count=w.timeDepthTables;type=QStringLiteral("组时深");}
            if(count>0)well_->addItem(q(w.name)+QStringLiteral(" · %1 %2").arg(count).arg(type),QVariant::fromValue<qlonglong>(w.id));
        }
    }
    int index=well_->findData(QVariant::fromValue<qlonglong>(selected));
    well_->setCurrentIndex(index>=0?index:(well_->count()?0:-1));updating_=false;reloadResources();parametersChanged();
    if(!error.empty())status_->setText(QStringLiteral("无法读取工区：")+q(error));
}
void ApiDemoWidget::reloadResources(){
    const auto selected=resource_->currentData().toLongLong();updating_=true;resource_->clear();const auto kind=example_->currentData().toString();
    DataService service;std::string error;std::vector<DataEntry> entries;
    if(kind!="wells"&&!projectPath_.isEmpty()&&service.openProject(utf8(projectPath_),error)){
        const std::string type=kind=="curve"?"curves":kind=="intervals"?"intervals":kind=="time_depth"?"time_depth":kind=="section"?"sections":"volumes";
        const bool byWell=kind=="curve"||kind=="intervals"||kind=="time_depth";
        const auto selectedWell=byWell?well_->currentData().toLongLong():0;
        if(!byWell||well_->count()>0)service.listData(type,selectedWell>0?selectedWell:0,entries,error);
        if(kind=="time_depth"&&selectedWell==-1)entries.erase(std::remove_if(entries.begin(),entries.end(),[](const DataEntry& item){return item.wellId!=0;}),entries.end());
        for(const auto& e:entries){QString title=q(e.name);if(kind=="time_depth"&&e.wellId==0)title+=QStringLiteral(" · 待归井");resource_->addItem(title+QStringLiteral("  [id=%1]").arg(e.id),QVariant::fromValue<qlonglong>(e.id));resource_->setItemData(resource_->count()-1,q(e.detail),Qt::ToolTipRole);}
    }
    int index=resource_->findData(QVariant::fromValue<qlonglong>(selected));
    if(index<0&&kind=="curve")for(size_t i=0;i<entries.size();++i)if(entries[i].name=="GR"){index=int(i);break;}
    resource_->setCurrentIndex(index>=0?index:(resource_->count()?0:-1));
    if(kind=="slice"&&resource_->count()>0){SeismicVolume volume;if(service.readVolume(resource_->currentData().toLongLong(),volume,error)&&std::isfinite(volume.sampling.firstTime))first_->setText(number(volume.sampling.firstTime));}
    updating_=false;
}
void ApiDemoWidget::exampleChanged(){
    const auto kind=example_->currentData().toString();const bool curve=kind=="curve",wells=kind=="wells",slice=kind=="slice",intervals=kind=="intervals",timeDepth=kind=="time_depth",info=kind=="seismic_info",trace=kind=="seismic_trace",nativeSlice=kind=="seismic_slice";
    const bool byWell=curve||intervals||timeDepth;const bool hasRange=!wells&&!timeDepth&&!info;
    wellLabel_->setVisible(byWell);well_->setVisible(byWell);resourceLabel_->setVisible(!wells);resource_->setVisible(!wells);resourceLabel_->setText(curve?QStringLiteral("曲线"):intervals?QStringLiteral("分层道"):timeDepth?QStringLiteral("时深表"):slice||nativeSeismic(kind)?QStringLiteral("地震体"):QStringLiteral("已存剖面"));
    firstLabel_->setVisible(hasRange);first_->setVisible(hasRange);lastLabel_->setVisible(hasRange&&!slice);last_->setVisible(hasRange&&!slice);processed_->setVisible(curve);
    firstLabel_->setText(curve||intervals?QStringLiteral("顶深"):slice||nativeSlice?QStringLiteral("切片时间（ms）"):trace?QStringLiteral("道位置（从0开始）"):nativeLine(kind)?QStringLiteral("测线号"):QStringLiteral("起始时间"));
    // 旧切片使用已保存的单位，新原生切片明确采用文件时间轴的毫秒。
    if(slice)firstLabel_->setText(QStringLiteral("切片时间"));
    lastLabel_->setText(curve||intervals?QStringLiteral("底深"):trace?QStringLiteral("读取样点数"):nativeLine(kind)?QStringLiteral("最大输出道数"):nativeSlice?QStringLiteral("每轴最大位置数"):QStringLiteral("结束时间"));
    updating_=true;first_->clear();last_->clear();
    first_->setPlaceholderText(slice?QStringLiteral("需填写时间"):nativeSlice||nativeLine(kind)?QStringLiteral("空为中间位置"):trace?QStringLiteral("空为第0道"):QStringLiteral("空为不限"));
    last_->setPlaceholderText(trace?QStringLiteral("空或0为全部"):nativeLine(kind)?QStringLiteral("空为512"):nativeSlice?QStringLiteral("空为128"):QStringLiteral("空为不限"));updating_=false;
    visuals_->setCurrentIndex(wells||intervals||timeDepth||info?0:curve?1:trace?3:2);schema_->setPlainText(schemaFor(kind));reloadChoices();
}
void ApiDemoWidget::invalidateResult(){succeeded_=false;setProperty("apiLastSucceeded",false);json_->clear();curve_->clear();image_->clear();tracePlot_->clear();wellTable_->clear();wellTable_->setRowCount(0);wellTable_->setColumnCount(0);}
void ApiDemoWidget::parametersChanged(){
    invalidateResult();updateCode();const auto kind=example_->currentData().toString();
    if(projectPath_.isEmpty()){summary_->setText(QStringLiteral("请先新建或打开工区；本页不会使用虚构数据填充结果。"));run_->setEnabled(false);}
    else if(kind=="curve"){
        summary_->setText(QStringLiteral("当前工区 %1 口井，其中 %2 口有曲线；此选择器只显示有曲线的井。深度与曲线单位沿用来源，未知单位不猜测。").arg(wells_.size()).arg(well_->count()));run_->setEnabled(resource_->count()>0);
    }else if(kind=="intervals"){
        summary_->setText(QStringLiteral("读取所选分层道的真实层段，范围查询保留完整顶底界。类型、单位与基准见返回对象；未知项不自动补全。"));run_->setEnabled(resource_->count()>0);
    }else if(kind=="time_depth"){
        summary_->setText(QStringLiteral("读取完整的深度—时间配对点。可选择全部资料或仅待归井；不做插值、正负号翻转和单位推断。"));run_->setEnabled(resource_->count()>0);
    }else if(kind=="section"){
        summary_->setText(resource_->count()>0?QStringLiteral("从已保存的 XY 路径现场提取剖面。代码与返回结果使用相同的参数；大地震体只按需读取。"):
            QStringLiteral("工区尚无可用剖面。请在“资料管理”导入非空 SEG-Y、核对坐标与时间信息，再新建剖面路径，回此页刷新。"));run_->setEnabled(resource_->count()>0);
    }else if(kind=="slice"){
        summary_->setText(resource_->count()>0?QStringLiteral("时间值采用所选地震体已确认的时间单位；返回最近样点的实际时间。"):
            QStringLiteral("工区尚无地震体。测试目录中的空 SEG-Y 不能生成剖面；请导入真实非空文件并核对元信息后刷新。"));run_->setEnabled(resource_->count()>0);
    }else if(nativeSeismic(kind)){
        QString description=QStringLiteral("直接读取外部 SEG-Y，不复制地震体，也不把完整振幅载入数据库。原生道和网格预览无需先确认 CRS / TWT。");
        if(kind=="seismic_info")description+=QStringLiteral("本例展示文件头、布局、探测网格和限制；抽样规则网格不等于全文件校验。");
        if(kind=="seismic_trace")description+=QStringLiteral("道位置从0开始；样点数留空读取本道全部样点。");
        if(nativeLine(kind))description+=QStringLiteral("测线号留空自动选中间线；最大道数默认512，输出最多200万振幅。");
        if(kind=="seismic_slice")description+=QStringLiteral("时间留空选中间样点，每轴默认128个位置，只读取对应时间的样点。");
        if(kind=="processed_section")description+=QStringLiteral("本例对INLINE剖面做101样点RMS自动增益，再限幅到±3，保留原有0振幅。未开启去均值；源文件保持不变。");
        if(resource_->count()==0)description=QStringLiteral("请先在“资料管理”导入非空 SEG-Y。服务只登记原始路径；导入后回到本页刷新即可测试。");
        summary_->setText(description);run_->setEnabled(resource_->count()>0);
    }else{summary_->setText(QStringLiteral("读取工区所有井及其资料数量：没有曲线的井仍可能具有井位、样品、时深或分层资料。"));run_->setEnabled(true);}
    status_->setText(QStringLiteral("参数已就绪，点击“执行示例”取得真实返回值。修改参数后会清空旧结果。"));
}
bool ApiDemoWidget::requestValues(Request& request,QString& error)const{
    request.kind=example_->currentData().toString();request.project=projectPath_;request.wellId=well_->currentData().toLongLong();request.resourceId=resource_->currentData().toLongLong();request.processed=processed_->isChecked();
    if(request.project.isEmpty()){error=QStringLiteral("请先打开工区。");return false;}
    if(request.kind!="wells"&&request.resourceId<=0){error=QStringLiteral("当前示例没有可调用的资料，请先导入并核对资料。");return false;}
    if(request.kind!="wells"&&request.kind!="time_depth"&&request.kind!="seismic_info"){
        auto read=[&](QLineEdit* edit,double& target){if(edit->text().trimmed().isEmpty())return true;bool ok=false;target=edit->text().toDouble(&ok);return ok&&std::isfinite(target);};
        if(!read(first_,request.first)||(request.kind!="slice"&&!read(last_,request.last))){error=QStringLiteral("范围参数必须是有限数值，或留空。");return false;}
        if(request.kind=="slice"&&!std::isfinite(request.first)){error=QStringLiteral("请填写切片时间，单位采用地震体已确认的时间单位。");return false;}
        if(nativeSeismic(request.kind)){
            auto integer=[](double value,double minimum,double maximum){return std::isnan(value)||(value>=minimum&&value<=maximum&&std::floor(value)==value);};
            if(request.kind=="seismic_trace"&&(!integer(request.first,0,2147483646)||!integer(request.last,0,1000000))){error=QStringLiteral("道位置须为非负整数；样点数为0到1000000，0表示读到本道末尾。");return false;}
            if(nativeLine(request.kind)&&(!integer(request.first,-2147483647,2147483647)||!integer(request.last,2,8192))){error=QStringLiteral("测线号须为整数；最大输出道数为2到8192，留空使用默认值。");return false;}
            if(request.kind=="seismic_slice"&&!integer(request.last,1,512)){error=QStringLiteral("每轴最大位置数须为1到512的整数，留空使用128。");return false;}
        }else if(std::isfinite(request.first)&&std::isfinite(request.last)&&request.first>request.last){error=QStringLiteral("起始值不能大于结束值。");return false;}
    }return true;
}
void ApiDemoWidget::updateCode(){
    Request request;QString error;const bool valid=requestValues(request,error);
    // Raw templates keep the tutorial itself readable. Only typed parameter
    // values are substituted; strings are escaped by cppString above.
    QString code=QString::fromUtf8(R"PDS(// main.cpp：完整、可编译的 C++ 使用案例。请以 UTF-8 保存。
// 第 1 步：引入头文件。还要按“如何导包”页配置 CMake 链接库。
#include <paleo_data/service.h>  // DataService 和各种返回数据类型
#include <QCoreApplication>     // 服务内部使用 Qt；进程需有一个应用对象
#include <cmath>                // std::isfinite：判断数字是否有效
#include <iostream>             // 向控制台输出结果
#include <string>               // std::string：保存文本
#include <vector>               // std::vector：自动管理长度的数组
using namespace paleo::data;    // 后面可直接写 DataService，不必反复写命名空间

int main(int argc, char** argv) {
    // 第 2 步：创建应用和服务。已有 QApplication 的界面程序不用再创建 app。
    // 一个线程使用自己的 DataService，不把同一实例交给多个线程共同使用。
    QCoreApplication app(argc, argv);
    DataService service;
    std::string error;          // 调用失败时，服务会把原因写到这里

    // 第 3 步：连接已存在的工区。这里不会重新导入原文件。
    // success 是成功标记，不是业务数据；真正的数据通过后面的 result 返回。
    const std::string projectPath = %1;
    bool success = service.openProject(projectPath, error);
    if (!success) {
        std::cerr << "打开工区失败：" << error << '\n';
        return 1;              // 提前结束，避免继续使用失败的调用结果
    }

)PDS").arg(cppString(projectPath_));
    if(!valid)code+=QStringLiteral("    // 当前参数尚未就绪：%1\n").arg(error.replace('\n',' '));
    if(!valid&&nativeSeismic(request.kind)){request.first=Missing;request.last=Missing;}
    if(request.kind=="wells")code+=QString::fromUtf8(R"PDS(    // 第 4 步：准备接收容器。每个 WellSummary 描述一口井的资料情况。
    std::vector<WellSummary> result;
    success = service.listWells(result, error);
    if (!success) {
        std::cerr << "读取井清单失败：" << error << '\n';
        return 2;
    }

    // 第 5 步：使用返回数组。size() 是井的数量，i 是从 0 开始的数组下标。
    std::cout << "wells=" << result.size() << '\n';
    for (size_t i = 0; i < result.size(); ++i) {
        const WellSummary& well = result[i]; // 引用这一口井，不复制整个对象
        std::cout << well.id << ' ' << well.name
                  << " curves=" << well.curves
                  << " timeDepthTables=" << well.timeDepthTables << '\n';
        // well.id 是稳定业务编号，不等于数组下标 i。
        // 曲线数为 0，仍可能有时深、样品、分层或井坐标，不能据此判定导入失败。
        // 后续可把 well.id 传给 listData("curves", ...) 查这口井的曲线。
    }
)PDS");
    else if(request.kind=="curve")code+=QString::fromUtf8(R"PDS(    // 第 4 步：确定井和曲线。本页选择器已给出当前工区内的真实编号。
    // 编号只在当前工区有意义，不要把下面的编号直接拿去查询另一个工区。
    const long long wellId = %1;
    const long long curveId = %2;

    // listData 返回“曲线索引”，让我们核对编号属于哪口井；它不包含采样值。
    std::vector<DataEntry> curves;
    success = service.listData("curves", wellId, curves, error);
    if (!success) {
        std::cerr << "查询曲线索引失败：" << error << '\n';
        return 2;
    }
    bool belongsToWell = false;
    for (size_t i = 0; i < curves.size(); ++i) {
        if (curves[i].id == curveId) {
            belongsToWell = true;
            break;             // 找到后停止循环
        }
    }
    if (!belongsToWell) {
        std::cerr << "选中的曲线不属于这口井，请重新选择。\n";
        return 3;
    }

    // 第 5 步：真正取数。Missing 表示这一端不限制深度，不是数值 0。
    // 数字范围沿用资料原生深度单位；单位未知时不要默认当作米。
    const double topDepth = %3;
    const double bottomDepth = %4;
    const bool useProcessedValues = %5; // true 取处理值，false 取原始值
    CurveSlice result;          // 接收完整曲线数据，而不是目录或文件路径
    success = service.readCurveSlice(
        curveId, topDepth, bottomDepth, useProcessedValues, result, error);
    if (!success) {
        std::cerr << "读取曲线数值失败：" << error << '\n';
        return 4;
    }

    // 第 6 步：理解返回对象。以下四个数组长度相同，按下标一一对应：
    // result.depths[i]    第 i 个点的深度
    // result.rawValues[i] 第 i 个点的原始值（始终保留，可能包含源文件空值码）
    // result.values[i]    本次请求使用的值，可以直接用于绘图/算法
    // result.quality[i]   0正常、1空值、2阈值异常、4人工修改
    // result.unit / depthUnit 是单位；空字符串表示尚未确认。
    std::cout << result.name << " points=" << result.depths.size() << '\n';

    // 全部请求范围的数据已经返回；这里只打印前 10 点，避免刷满控制台。
    size_t previewCount = result.depths.size();
    if (previewCount > 10) previewCount = 10;
    for (size_t i = 0; i < previewCount; ++i) {
        std::cout << result.depths[i] << ',' << result.rawValues[i] << ',';
        if (std::isfinite(result.values[i])) {
            std::cout << result.values[i];
        } else {
            std::cout << "缺失"; // NaN 不是 0；不能把缺失当作真实曲线值
        }
        std::cout << ',' << int(result.quality[i]) << '\n';
    }
    // 绘图时把 depths 和 values 配对使用；缺失位置应断线。
    // 读取原始值时，还要结合 quality==1 识别源文件空值码。
    // 当前 result 已是 C++ 数据对象；JSON 是可选的序列化形式，不必为了计算先转 JSON。
)PDS").arg(request.wellId).arg(request.resourceId).arg(number(request.first),number(request.last),request.processed?"true":"false");
    else if(request.kind=="intervals")code+=QString::fromUtf8(R"PDS(    // 第 4 步：确定一个分层道。一个井可以有地层、岩性、相等多种道。
    const long long trackId = %1; // 本页选中的道编号，不是数组下标
    const double topDepth = %2;   // Missing 表示不限制查询上界
    const double bottomDepth = %3;// Missing 表示不限制查询下界

    // 先读道说明，确认深度类型、单位和参考面，避免混用 MD/TVD/TVDSS。
    IntervalTrack track;
    success = service.readIntervalTrackInfo(trackId, track, error);
    if (!success) {
        std::cerr << "读取分层道信息失败：" << error << '\n';
        return 2;
    }
    std::cout << "分层道：" << track.trackName
              << "，深度类型：" << track.depthInfo.depthType
              << "，深度单位：" << track.depthInfo.depthUnit << '\n';
    // 空字符串表示未确认，不自动当作 MD 或米。

    // 第 5 步：读取实际层段。result 是层段数组，已经包含顶深、底深、层名等。
    std::vector<IntervalRecord> result;
    success = service.readIntervals(trackId, topDepth, bottomDepth, result, error);
    if (!success) {
        std::cerr << "读取层段失败：" << error << '\n';
        return 3;
    }
    std::cout << "intervals=" << result.size() << '\n';

    // 第 6 步：逐条使用返回层段。这里只显示前 10 条，数组仍包含全部命中层段。
    size_t previewCount = result.size();
    if (previewCount > 10) previewCount = 10;
    for (size_t i = 0; i < previewCount; ++i) {
        const IntervalRecord& record = result[i];
        const GeologicalInterval& layer = record.value;
        std::cout << record.id << ',' << layer.intervalName << ','
                  << layer.topDepth << ',' << layer.bottomDepth << ','
                  << layer.faciesType << '\n';
        // 范围查询保留完整层段边界，不把顶底界裁剪成输入的查询范围。
        // bottomNotPenetrated==1 时，bottomDepth 是记录止深，不能当作真实地层底界。
        // sourceRow 可追溯来源表格行号；TVD/TVDSS 字段缺失时为 NaN，不自动推算。
    }
)PDS").arg(request.resourceId).arg(number(request.first),number(request.last));
    else if(request.kind=="time_depth")code+=QString::fromUtf8(R"PDS(    // 第 4 步：选择一张时深表。编号对应当前工区的具体来源和版本。
    const long long relationId = %1;
    TimeDepthTable result;     // 完整时深关系对象，不只是表名或文件路径
    success = service.readTimeDepth(relationId, result, error);
    if (!success) {
        std::cerr << "读取时深关系失败：" << error << '\n';
        return 2;
    }

    // 第 5 步：先理解约定，再使用数值。空字符串表示尚未确认。
    std::cout << "关系表：" << result.relationName << '\n';
    std::cout << "深度：" << result.depthInfo.depthType
              << "，单位：" << result.depthInfo.depthUnit << '\n';
    std::cout << "时间：" << result.timeType << "，单位：" << result.timeUnit
              << "，参考面：" << result.timeReference << '\n';
    // TWT 是双程时间，OWT 是单程时间；不能不看类型就混用。
    // 负时间原样保留，不在这里取绝对值，也不擅自推断毫秒或秒。
    std::cout << "timeDepthPoints=" << result.depthTimePairs.size() << '\n';

    // 第 6 步：数组中的每个元素直接包含一对深度、时间。
    // 已经返回全部配对点；这里只打印前 10 个点。
    size_t previewCount = result.depthTimePairs.size();
    if (previewCount > 10) previewCount = 10;
    for (size_t i = 0; i < previewCount; ++i) {
        const TimeDepthPoint& point = result.depthTimePairs[i];
        std::cout << point.depthValue << ',' << point.timeValue << '\n';
        // 使用前可通过 std::isfinite 检查数值；NaN 表示缺失。
    }
    // 本例只负责取数；没有自动插值或执行深度转时间的计算。
    // 待归井记录也能读取，但不能据此假定它属于当前正在解释的井。
)PDS").arg(request.resourceId);
    else if(request.kind=="seismic_info")code+=QString::fromUtf8(R"PDS(    // 第4步：选定工区中已登记的地震体，只读原始文件，不复制原始地震体。
    const long long volumeId = %1;
    SeismicFileInfo result;
    success = service.inspectSeismic(volumeId, result, error);
    if (!success) { std::cerr << error << '\n'; return 2; }

    // 第5步：查看文件事实。traceCount是固定道长布局中的道数，不是全量振幅。
    std::cout << result.engine << '\n';
    std::cout << "bytes=" << result.fileBytes << " traces=" << result.traceCount
              << " samples/trace=" << result.sampleCount << '\n';
    std::cout << "format=" << result.sampleFormat << " " << result.byteOrder << '\n';
    std::cout << "firstTime=" << result.firstTime << " interval=" << result.sampleInterval
              << " " << result.timeUnit << '\n';
    std::cout << "geometry=" << result.geometryStatus << '\n';
    // sampled-regular仅表示抽样道头符合规则；每次取数仍会核对目标道头。
    // unavailable时仍可按文件道位置读单道，但不能假定拥有规则三维网格。
    std::cout << result.textHeader << '\n';
    for (const std::string& warning : result.warnings) std::cout << warning << '\n';
    // 不根据文件名、坐标数量级自动指定CRS、TWT、OWT或地质时间参考面。
)PDS").arg(request.resourceId);
    else if(request.kind=="seismic_trace")code+=QString::fromUtf8(R"PDS(    // 第4步：文件位置从0开始，和道头中的来源道号是两回事。
    const long long volumeId = %1;
    const long long traceIndex = %2;
    SeismicTraceReadOptions options;
    options.firstSample = 0;     // 从本道的第0个样点开始
    options.sampleCount = %3;   // 0表示读到本道末尾
    options.maxSamples = 1000000;
    SeismicTraceResult result;
    success = service.readSeismicTrace(volumeId, traceIndex, options, result, error);
    if (!success) { std::cerr << error << '\n'; return 2; }

    // 第5步：result.amplitudes已是真实float数组，可直接交给算法或绘图控件。
    std::cout << "traceIndex=" << result.traceIndex << " sourceTrace=" << result.traceNumber
              << " IL=" << result.inlineNumber << " XL=" << result.crosslineNumber << '\n';
    std::cout << "samples=" << result.amplitudes.size() << '\n';
    for (size_t i = 0; i < result.amplitudes.size() && i < 10; ++i) {
        const double time = result.firstTime + i * result.sampleInterval;
        std::cout << time << ',' << result.amplitudes[i] << '\n';
    }
    // 负振幅和0均有意义，不能套用测井曲线的异常值处理规则。
    SeismicStatistics statistics;
    success = service.seismicStatistics(result.amplitudes, statistics, error);
    if (!success) { std::cerr << error << '\n'; return 3; }
    std::cout << "mean=" << statistics.mean << " rms=" << statistics.rms << '\n';
)PDS").arg(request.resourceId).arg(std::isfinite(request.first)?QString::number(qlonglong(request.first)):"0").arg(std::isfinite(request.last)?QString::number(int(request.last)):"0");
    else if(nativeLine(request.kind)){
        code+=QString::fromUtf8(R"PDS(    // 第4步：按真实测线号选原生剖面，无需先创建XY剖面路径。
    const long long volumeId = %1;
    const std::string direction = "%2";
)PDS").arg(request.resourceId).arg(request.kind=="crossline"?"crossline":"inline");
        if(std::isfinite(request.first))code+=QStringLiteral("    const int lineNumber = %1; // 当前选择的真实测线号\n").arg(int(request.first));
        else code+=QString::fromUtf8(R"PDS(    // 未填写测线号：先快速探测网格，再选择网格中间的一条测线。
    SeismicFileInfo info;
    success = service.inspectSeismic(volumeId, info, error);
    if (!success) { std::cerr << error << '\n'; return 2; }
    const int count = direction == "inline" ? info.inlineCount : info.crosslineCount;
    const int first = direction == "inline" ? info.inlineMin : info.crosslineMin;
    const int step = direction == "inline" ? info.inlineStep : info.crosslineStep;
    if (count < 1 || step < 1) {
        std::cerr << "无法自动选择规则网格的中间线，请查看文件检查结果。\n";
        return 3;
    }
    const int lineNumber = first + (count / 2) * step;
)PDS");
        code+=QString::fromUtf8(R"PDS(    SectionReadOptions options;
    options.maxColumns = %1;    // 超过时沿另一测线轴抽样，不改变原文件
    options.maxValues = 2000000;
    options.firstTime = Missing; // 全部时间；需要时可改成具体的毫秒数
    options.lastTime = Missing;
    SeismicSectionResult raw;
    success = service.readSeismicLine(volumeId, direction, lineNumber, options, raw, error);
    if (!success) { std::cerr << error << '\n'; return 4; }
    // raw.amplitudes列优先：第column列、第sample样点的位置为
    // column * raw.samples + sample。负振幅保留，缺失道为NaN。
)PDS").arg(std::isfinite(request.last)?int(request.last):512);
        if(request.kind=="processed_section")code+=QString::fromUtf8(R"PDS(    // 第5步：仅处理本次剖面数组，保留raw作为原始结果，不写回SEG-Y。
    SeismicProcessingOptions processing;
    processing.removeMean = false;  // 默认保留原有0振幅；不逐道减均值
    // 若明确需要去均值，可设true；这也会使原本为0的样点减去本道均值。
    processing.gain = 1.0;          // 固定增益，本例不改变倍数
    processing.agcWindowSamples = 101; // 滑窗RMS自动增益，101个样点，窗口不跨道
    processing.clipAmplitude = 3.0; // 对称限幅到[-3,+3]
    SeismicSectionResult result;
    success = service.processSeismicSection(raw, processing, result, error);
    if (!success) { std::cerr << error << '\n'; return 5; }
    // 本例只做AGC→限幅。可选去均值在固定增益前执行；并非完整偏移/反演流程。
)PDS");
        else code+=QStringLiteral("    const SeismicSectionResult& result = raw; // 原始返回结果直接绘图\n");
        code+=QString::fromUtf8(R"PDS(    std::cout << "line=" << lineNumber << " columns=" << result.columns
              << " samples=" << result.samples << '\n';
    for (size_t i = 0; i < result.amplitudes.size() && i < 10; ++i)
        std::cout << result.amplitudes[i] << '\n';
    SeismicStatistics statistics;
    success = service.seismicStatistics(result.amplitudes, statistics, error);
    if (!success) { std::cerr << error << '\n'; return 6; }
    std::cout << "min=" << statistics.minimum << " max=" << statistics.maximum
              << " rms=" << statistics.rms << '\n';
)PDS");
    }
    else if(request.kind=="seismic_slice"){
        code+=QString::fromUtf8(R"PDS(    const long long volumeId = %1;
)PDS").arg(request.resourceId);
        if(std::isfinite(request.first))code+=QStringLiteral("    const double timeMs = %1; // 来源时间轴中的毫秒值\n").arg(number(request.first));
        else code+=QString::fromUtf8(R"PDS(    // 时间留空：从文件采样轴选择中间样点，不猜测地质基准。
    SeismicFileInfo info;
    success = service.inspectSeismic(volumeId, info, error);
    if (!success) { std::cerr << error << '\n'; return 2; }
    const double timeMs = info.firstTime + (info.sampleCount / 2) * info.sampleInterval;
)PDS");
        code+=QString::fromUtf8(R"PDS(    TimeSliceReadOptions options;
    options.maxInlineCount = %1;
    options.maxCrosslineCount = %1;
    options.maxValues = %2;
    SeismicTimeSliceResult result;
    success = service.readSeismicTimeSlice(volumeId, timeMs, options, result, error);
    if (!success) { std::cerr << error << '\n'; return 3; }
    // 每个位置仅取一个时间样点，不读取整道振幅，也不加载整个地震体。
    std::cout << "actualTime=" << result.actualTime << " " << result.timeUnit
              << " values=" << result.amplitudes.size() << '\n';
    // 行优先：inlineIndex * result.crosslineNumbers.size() + crosslineIndex。
    for (size_t i = 0; i < result.amplitudes.size() && i < 10; ++i)
        std::cout << result.amplitudes[i] << '\n';
)PDS").arg(std::isfinite(request.last)?int(request.last):128).arg(std::isfinite(request.last)?int(request.last)*int(request.last):16384);
    }
    else if(request.kind=="section")code+=QString::fromUtf8(R"PDS(    // 第 4 步：选择已经保存的剖面路径，它已关联一个地震体。
    const long long sectionId = %1;
    SectionReadOptions options;
    options.firstTime = %2;      // Missing 表示从允许的首样点开始
    options.lastTime = %3;       // Missing 表示到允许的末样点结束
    options.maxColumns = 512;    // 沿路径最多提取 512 列
    options.maxValues = 2000000; // 限制返回数组大小，避免请求过大
    // 时间使用地震体已确认的单位；不是所有体都默认是 ms。

    // 第 5 步：服务现场读取文件的必要部分，返回实际振幅数组。
    // 数据库只保存体路径和元信息；此调用不把整个几十 GB 地震体装入数据库。
    SeismicSectionResult result;
    success = service.extractSection(sectionId, options, result, error);
    if (!success) {
        std::cerr << "提取剖面失败：" << error << '\n';
        return 2;
    }
    std::cout << result.columns << " columns, " << result.samples
              << " samples, " << result.amplitudes.size() << " values\n";

    // 第 6 步：理解二维剖面如何放进一个一维数组。
    // 先放第 0 列的全部样点，再放第 1 列……，称为“列优先”。
    // 数组位置 = 列下标 * 每列样点数 + 样点下标。
    if (result.columns > 0 && result.samples > 0) {
        const size_t column = 0; // 下标从 0 开始，不是 INLINE/XLINE 编号
        const size_t sample = 0;
        const size_t index = column * result.samples + sample;
        const float amplitude = result.amplitudes[index];
        const double time = result.firstTime + sample * result.sampleInterval;
        std::cout << "首点 XY=" << result.x[column] << ',' << result.y[column]
                  << " time=" << time << ' ' << result.timeUnit << '\n';
        if (std::isfinite(amplitude)) std::cout << "振幅=" << amplitude << '\n';
        else std::cout << "此处缺道或缺失，绘图时显示为空。\n";
    }
    // 将整个 result 交给剖面控件即可绘图，不需要再解析一遍 SEG-Y。
    // 0 是合法振幅，NaN 才表示缺失；这里打印首点只是演示数组访问。
)PDS").arg(request.resourceId).arg(number(request.first),number(request.last));
    else code+=QString::fromUtf8(R"PDS(    // 第 4 步：指定地震体和切片时间，时间单位取自该体的元信息。
    const long long volumeId = %1;
    const double requestedTime = %2;
    TimeSliceReadOptions options;
    options.maxInlineCount = 256;    // 最多 256 个纵测线位置
    options.maxCrosslineCount = 256; // 最多 256 个横测线位置
    options.maxValues = 65536;       // 振幅值数量上限

    // 第 5 步：读取水平切片，result 接收真实坐标、测线编号和振幅。
    SeismicTimeSliceResult result;
    success = service.readTimeSlice(volumeId, requestedTime, options, result, error);
    if (!success) {
        std::cerr << "读取时间切片失败：" << error << '\n';
        return 2;
    }
    std::cout << "actualTime=" << result.actualTime << ' ' << result.timeUnit
              << " values=" << result.amplitudes.size() << '\n';
    // actualTime 是最近样点的实际时间，可能与请求时间略有差别；没有隐式插值。

    // 第 6 步：切片是行优先。每一行是一个 INLINE，每一列是一个 XLINE。
    const size_t columnCount = result.crosslineNumbers.size();
    if (!result.inlineNumbers.empty() && columnCount > 0) {
        const size_t row = 0;
        const size_t column = 0;
        const size_t index = row * columnCount + column;
        std::cout << "首点 INLINE=" << result.inlineNumbers[row]
                  << " XLINE=" << result.crosslineNumbers[column] << '\n';
        std::cout << "XY=" << result.x[index] << ',' << result.y[index] << '\n';
        if (std::isfinite(result.amplitudes[index])) {
            std::cout << "振幅=" << result.amplitudes[index] << '\n';
        } else {
            std::cout << "此网格缺失，绘图时保留空白。\n";
        }
    }
    // 实际测线编号存在 inlineNumbers/crosslineNumbers 中，不等于 row/column。
    // 用整个 amplitudes 数组画图；JSON 只是本页查看结果的一种表示方式。
)PDS").arg(request.resourceId).arg(number(request.first));
    code+=QStringLiteral("    return 0;\n}\n");code_->setPlainText(code);
}
void ApiDemoWidget::setBusy(bool busy){example_->setEnabled(!busy);well_->setEnabled(!busy);resource_->setEnabled(!busy);first_->setEnabled(!busy);last_->setEnabled(!busy);processed_->setEnabled(!busy);refresh_->setEnabled(!busy);run_->setEnabled(!busy&&(example_->currentData().toString()=="wells"||resource_->count()>0)&&!projectPath_.isEmpty());cancel_->setVisible(busy);cancel_->setEnabled(busy);setProperty("apiRunState",busy?"running":"idle");emit busyChanged(busy);}
void ApiDemoWidget::runExample(){
    if(isRunning())return;Request request;QString error;if(!requestValues(request,error)){invalidateResult();status_->setText(error);emit runFinished(false);return;}
    invalidateResult();updateCode();cancelRequested_=false;status_->setText(QStringLiteral("正在执行公开 C++ 服务接口，完成后用同一返回对象绘图…"));codeTabs_->setCurrentIndex(1);
    auto result=std::make_shared<Result>();auto* cancel=&cancelRequested_;
    worker_=QThread::create([request,result,cancel]{
        QElapsedTimer timer;timer.start();
        try{
            DataService service;result->ok=service.openProject(utf8(request.project),result->error);
            if(result->ok){
                if(request.kind=="wells")result->ok=service.listWells(result->wells,result->error);
                else if(request.kind=="curve"){
                    result->ok=service.listData("curves",request.wellId,result->entries,result->error);
                    if(result->ok){auto found=std::find_if(result->entries.begin(),result->entries.end(),[&](const DataEntry& e){return e.id==request.resourceId;});if(found==result->entries.end()){result->ok=false;result->error="选中曲线已不属于当前井，请刷新资料。";}}
                    if(result->ok)result->ok=service.readCurveSlice(request.resourceId,request.first,request.last,request.processed,result->curve,result->error);
                }else if(request.kind=="intervals"){
                    result->ok=service.readIntervalTrackInfo(request.resourceId,result->intervalTrack,result->error);
                    if(result->ok)result->ok=service.readIntervals(request.resourceId,request.first,request.last,result->intervals,result->error);
                }else if(request.kind=="time_depth"){
                    result->ok=service.readTimeDepth(request.resourceId,result->timeDepth,result->error);
                }else if(request.kind=="seismic_info"){
                    result->ok=service.inspectSeismic(request.resourceId,result->seismicInfo,result->error,[cancel]{return cancel->load();});
                }else if(request.kind=="seismic_trace"){
                    const auto traceIndex=std::isfinite(request.first)?static_cast<long long>(request.first):0;
                    SeismicTraceReadOptions options;options.sampleCount=std::isfinite(request.last)?int(request.last):0;
                    result->ok=service.readSeismicTrace(request.resourceId,traceIndex,options,result->trace,result->error,[cancel]{return cancel->load();});
                    if(result->ok)result->ok=service.seismicStatistics(result->trace.amplitudes,result->statistics,result->error,[cancel]{return cancel->load();});
                }else if(nativeLine(request.kind)){
                    const std::string direction=request.kind=="crossline"?"crossline":"inline";
                    if(std::isfinite(request.first))result->actualLineNumber=int(request.first);
                    else {
                        result->ok=service.inspectSeismic(request.resourceId,result->seismicInfo,result->error,[cancel]{return cancel->load();});
                        if(result->ok){const auto& info=result->seismicInfo;const int count=direction=="inline"?info.inlineCount:info.crosslineCount;const int first=direction=="inline"?info.inlineMin:info.crosslineMin;const int step=direction=="inline"?info.inlineStep:info.crosslineStep;
                            if(count<1||step<1){result->ok=false;result->error="无法自动选择规则网格的中间线，请查看文件检查结果。";}else result->actualLineNumber=first+(count/2)*step;
                        }
                    }
                    SectionReadOptions options;options.maxColumns=std::isfinite(request.last)?int(request.last):512;options.maxValues=2000000;
                    if(result->ok)result->ok=service.readSeismicLine(request.resourceId,direction,result->actualLineNumber,options,result->section,result->error,[cancel]{return cancel->load();});
                    if(result->ok&&request.kind=="processed_section"){
                        result->ok=service.seismicStatistics(result->section.amplitudes,result->rawStatistics,result->error,[cancel]{return cancel->load();});
                        SeismicProcessingOptions processing;processing.removeMean=false;processing.gain=1;processing.agcWindowSamples=101;processing.clipAmplitude=3;
                        SeismicSectionResult processed;
                        if(result->ok)result->ok=service.processSeismicSection(result->section,processing,processed,result->error,[cancel]{return cancel->load();});
                        if(result->ok)result->section=std::move(processed);
                    }
                    if(result->ok)result->ok=service.seismicStatistics(result->section.amplitudes,result->statistics,result->error,[cancel]{return cancel->load();});
                }else if(request.kind=="seismic_slice"){
                    result->requestedTime=request.first;
                    if(!std::isfinite(request.first)){
                        result->ok=service.inspectSeismic(request.resourceId,result->seismicInfo,result->error,[cancel]{return cancel->load();});
                        if(result->ok)result->requestedTime=result->seismicInfo.firstTime+(result->seismicInfo.sampleCount/2)*result->seismicInfo.sampleInterval;
                    }
                    TimeSliceReadOptions options;options.maxInlineCount=options.maxCrosslineCount=std::isfinite(request.last)?int(request.last):128;options.maxValues=static_cast<long long>(options.maxInlineCount)*options.maxCrosslineCount;
                    if(result->ok)result->ok=service.readSeismicTimeSlice(request.resourceId,result->requestedTime,options,result->slice,result->error,[cancel]{return cancel->load();});
                }else if(request.kind=="section"){
                    SectionReadOptions options;options.firstTime=request.first;options.lastTime=request.last;options.maxColumns=512;options.maxValues=2000000;
                    result->ok=service.extractSection(request.resourceId,options,result->section,result->error,[cancel]{return cancel->load();});
                }else{
                    TimeSliceReadOptions options;options.maxInlineCount=256;options.maxCrosslineCount=256;options.maxValues=65536;
                    result->ok=service.readTimeSlice(request.resourceId,request.first,options,result->slice,result->error,[cancel]{return cancel->load();});
                }
            }
            if(cancel->load()){result->ok=false;result->error="操作已取消，未显示不完整结果。";}
        }catch(const std::exception& e){result->ok=false;result->error=e.what();}catch(...){result->ok=false;result->error="服务调用发生未知异常。";}
        result->milliseconds=timer.elapsed();
    });
    connect(worker_,&QThread::finished,this,[this,request,result]{auto* finished=worker_;worker_=nullptr;finished->deleteLater();showResult(request,*result);QPointer<ApiDemoWidget> guard(this);setBusy(false);if(guard)emit runFinished(result->ok);});setBusy(true);worker_->start();
}
void ApiDemoWidget::showResult(const Request& request,const Result& result){
    succeeded_=result.ok;setProperty("apiLastSucceeded",result.ok);
    QJsonObject object{{"ok",result.ok},{"error",q(result.error)},{"elapsedMs",double(result.milliseconds)},{"project",request.project},{"previewLimit",10},{"nonFiniteNumbers","null (C++ NaN/Infinity)"}};
    const QString method=request.kind=="wells"?"listWells":request.kind=="curve"?"listData + readCurveSlice":request.kind=="intervals"?"readIntervalTrackInfo + readIntervals":request.kind=="time_depth"?"readTimeDepth":request.kind=="section"?"extractSection":request.kind=="seismic_info"?"inspectSeismic":request.kind=="seismic_trace"?"readSeismicTrace":request.kind=="processed_section"?"readSeismicLine + processSeismicSection":nativeLine(request.kind)?"readSeismicLine":request.kind=="seismic_slice"?"readSeismicTimeSlice":"readTimeSlice";object["method"]=method;
    if(!result.ok){json_->setPlainText(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));resultTabs_->setCurrentIndex(1);status_->setText(QStringLiteral("调用失败：")+q(result.error));return;}
    QJsonArray preview;
    auto statisticsJson=[](const SeismicStatistics& statistics){return QJsonObject{{"finiteCount",statistics.finiteCount},{"missingCount",statistics.missingCount},{"minimum",numeric(statistics.minimum)},{"maximum",numeric(statistics.maximum)},{"mean",numeric(statistics.mean)},{"rms",numeric(statistics.rms)}};};
    if(request.kind=="wells"){
        int withCurves=0;wellTable_->setColumnCount(8);wellTable_->setHorizontalHeaderLabels({QStringLiteral("井名"),QStringLiteral("曲线"),QStringLiteral("分层道"),QStringLiteral("样品"),QStringLiteral("时深表"),QStringLiteral("测斜表"),QStringLiteral("井位"),QStringLiteral("编号")});wellTable_->setRowCount(int(result.wells.size()));
        for(size_t i=0;i<result.wells.size();++i){const auto& w=result.wells[i];if(w.curves>0)++withCurves;QStringList row{q(w.name),QString::number(w.curves),QString::number(w.tracks),QString::number(w.samples),QString::number(w.timeDepthTables),QString::number(w.trajectorySurveys),w.hasCoordinates?QStringLiteral("有坐标"):QStringLiteral("无坐标"),QString::number(w.id)};for(int c=0;c<row.size();++c)wellTable_->setItem(int(i),c,new QTableWidgetItem(row[c]));
            if(i<10)preview.append(QJsonObject{{"id",double(w.id)},{"name",q(w.name)},{"logSets",w.logSets},{"curves",w.curves},{"tracks",w.tracks},{"samples",w.samples},{"timeDepthTables",w.timeDepthTables},{"trajectorySurveys",w.trajectorySurveys},{"hasCoordinates",w.hasCoordinates}});
        }
        wellTable_->resizeColumnsToContents();object["returnType"]="std::vector<WellSummary>";object["totalWells"]=int(result.wells.size());object["wellsWithCurves"]=withCurves;object["wellsWithoutCurves"]=int(result.wells.size())-withCurves;object["preview"]=preview;object["previewTruncated"]=result.wells.size()>10;visuals_->setCurrentIndex(0);
    }else if(request.kind=="curve"){
        const auto& c=result.curve;long long finite=0;for(double value:c.values)if(std::isfinite(value))++finite;
        for(size_t i=0;i<std::min<size_t>(10,c.depths.size());++i)preview.append(QJsonObject{{"index",double(i)},{"depth",numeric(c.depths[i])},{"rawValue",numeric(c.rawValues[i])},{"value",numeric(c.values[i])},{"quality",int(c.quality[i])}});
        object["returnType"]="CurveSlice";object["wellId"]=double(request.wellId);object["curveId"]=double(c.curveId);object["name"]=q(c.name);object["unit"]=q(c.unit);object["depthUnit"]=q(c.depthUnit);object["processed"]=request.processed;object["curveDirectoryCount"]=int(result.entries.size());object["totalSamples"]=double(c.depths.size());object["finiteValues"]=double(finite);object["preview"]=preview;object["previewTruncated"]=c.depths.size()>10;curve_->setCurve(c);visuals_->setCurrentIndex(1);
    }else if(request.kind=="intervals"){
        const auto& track=result.intervalTrack;
        const auto& records=result.intervals;
        const auto rows=std::min<size_t>(records.size(),200);
        const QString depthUnit=track.depthInfo.depthUnit.empty()?QStringLiteral("单位未确认"):q(track.depthInfo.depthUnit);
        const QString depthType=track.depthInfo.depthType.empty()?QStringLiteral("类型未确认"):q(track.depthInfo.depthType);
        wellTable_->setColumnCount(6);wellTable_->setRowCount(int(rows));
        wellTable_->setHorizontalHeaderLabels({QStringLiteral("记录编号"),QStringLiteral("层名 / 岩性"),QStringLiteral("顶深 (%1 / %2)").arg(depthType,depthUnit),QStringLiteral("记录底深 (%1 / %2)").arg(depthType,depthUnit),QStringLiteral("相类型"),QStringLiteral("来源行")});
        for(size_t i=0;i<rows;++i){const auto& record=records[i];const auto& value=record.value;
            const QStringList row={QString::number(record.id),q(value.intervalName),std::isfinite(value.topDepth)?number(value.topDepth):QStringLiteral("缺失"),std::isfinite(value.bottomDepth)?number(value.bottomDepth):QStringLiteral("缺失"),q(value.faciesType),QString::number(value.sourceRow)};
            for(int c=0;c<row.size();++c)wellTable_->setItem(int(i),c,new QTableWidgetItem(row[c]));
        }
        for(size_t i=0;i<std::min<size_t>(records.size(),10);++i){const auto& record=records[i];const auto& value=record.value;
            preview.append(QJsonObject{{"id",double(record.id)},{"trackId",double(record.trackId)},{"name",q(value.intervalName)},{"topDepth",numeric(value.topDepth)},{"bottomDepth",numeric(value.bottomDepth)},{"faciesType",q(value.faciesType)},{"sourceRow",value.sourceRow}});
        }
        object["returnType"]="std::vector<IntervalRecord>";object["trackId"]=double(request.resourceId);object["trackName"]=q(track.trackName);object["trackType"]=q(track.trackType);object["depthType"]=q(track.depthInfo.depthType);object["depthUnit"]=q(track.depthInfo.depthUnit);object["mdReference"]=q(track.depthInfo.mdReference);object["tvdReference"]=q(track.depthInfo.tvdReference);object["tvdssReference"]=q(track.depthInfo.tvdssReference);object["tvdssDirection"]=q(track.depthInfo.tvdssDirection);object["totalIntervals"]=double(records.size());object["displayedRows"]=int(rows);object["preview"]=preview;object["previewTruncated"]=records.size()>10;wellTable_->resizeColumnsToContents();visuals_->setCurrentIndex(0);
    }else if(request.kind=="time_depth"){
        const auto& relation=result.timeDepth;const auto& points=relation.depthTimePairs;
        const auto rows=std::min<size_t>(points.size(),200);
        const QString depthUnit=relation.depthInfo.depthUnit.empty()?QStringLiteral("单位未确认"):q(relation.depthInfo.depthUnit);
        const QString depthType=relation.depthInfo.depthType.empty()?QStringLiteral("类型未确认"):q(relation.depthInfo.depthType);
        const QString timeUnit=relation.timeUnit.empty()?QStringLiteral("单位未确认"):q(relation.timeUnit);
        const QString timeType=relation.timeType.empty()?QStringLiteral("类型未确认"):q(relation.timeType);
        wellTable_->setColumnCount(3);wellTable_->setRowCount(int(rows));
        wellTable_->setHorizontalHeaderLabels({QStringLiteral("数组下标"),QStringLiteral("深度 (%1 / %2)").arg(depthType,depthUnit),QStringLiteral("时间 (%1 / %2)").arg(timeType,timeUnit)});
        for(size_t i=0;i<rows;++i){const auto& point=points[i];const QStringList row={QString::number(i),std::isfinite(point.depthValue)?number(point.depthValue):QStringLiteral("缺失"),std::isfinite(point.timeValue)?number(point.timeValue):QStringLiteral("缺失")};for(int c=0;c<row.size();++c)wellTable_->setItem(int(i),c,new QTableWidgetItem(row[c]));}
        for(size_t i=0;i<std::min<size_t>(points.size(),10);++i)preview.append(QJsonObject{{"index",double(i)},{"depth",numeric(points[i].depthValue)},{"time",numeric(points[i].timeValue)}});
        object["returnType"]="TimeDepthTable";object["relationId"]=double(request.resourceId);object["relationName"]=q(relation.relationName);object["depthType"]=q(relation.depthInfo.depthType);object["depthUnit"]=q(relation.depthInfo.depthUnit);object["mdReference"]=q(relation.depthInfo.mdReference);object["tvdReference"]=q(relation.depthInfo.tvdReference);object["tvdssReference"]=q(relation.depthInfo.tvdssReference);object["timeType"]=q(relation.timeType);object["timeUnit"]=q(relation.timeUnit);object["timeReference"]=q(relation.timeReference);object["totalPoints"]=double(points.size());object["displayedRows"]=int(rows);object["preview"]=preview;object["previewTruncated"]=points.size()>10;wellTable_->resizeColumnsToContents();visuals_->setCurrentIndex(0);
    }else if(request.kind=="seismic_info"){
        const auto& info=result.seismicInfo;
        QJsonArray warnings;for(const auto& warning:info.warnings)warnings.append(q(warning));
        object["returnType"]="SeismicFileInfo";object["volumeId"]=request.resourceId;object["engine"]=q(info.engine);object["sourceFile"]=q(info.sourceFile);object["fileBytes"]=info.fileBytes;object["traceCount"]=info.traceCount;object["sampleCount"]=info.sampleCount;object["sampleFormat"]=info.sampleFormat;object["byteOrder"]=q(info.byteOrder);object["firstTraceOffset"]=info.firstTraceOffset;object["extendedTextHeaders"]=info.extendedTextHeaders;object["firstTime"]=numeric(info.firstTime);object["sampleInterval"]=numeric(info.sampleInterval);object["timeUnit"]=q(info.timeUnit);object["geometryStatus"]=q(info.geometryStatus);object["inlineMin"]=info.inlineMin;object["inlineMax"]=info.inlineMax;object["inlineStep"]=info.inlineStep;object["inlineCount"]=info.inlineCount;object["crosslineMin"]=info.crosslineMin;object["crosslineMax"]=info.crosslineMax;object["crosslineStep"]=info.crosslineStep;object["crosslineCount"]=info.crosslineCount;object["checkedHeaders"]=info.checkedHeaders;object["coordinateUnitsCode"]=info.coordinateUnitsCode;object["coordinateScalar"]=info.coordinateScalar;object["textHeader"]=q(info.textHeader);object["warnings"]=warnings;
        const QList<QPair<QString,QString>> rows={
            {QStringLiteral("读取引擎"),q(info.engine)},
            {QStringLiteral("文件大小"),QStringLiteral("%1 GB（%2 字节）").arg(info.fileBytes/1e9,0,'f',3).arg(info.fileBytes)},
            {QStringLiteral("道数 / 每道样点"),QStringLiteral("%1 / %2").arg(info.traceCount).arg(info.sampleCount)},
            {QStringLiteral("样点格式 / 字节序"),QStringLiteral("%1 / %2").arg(info.sampleFormat).arg(q(info.byteOrder))},
            {QStringLiteral("来源采样轴"),QStringLiteral("首时间 %1；间隔 %2 %3").arg(number(info.firstTime),number(info.sampleInterval),q(info.timeUnit))},
            {QStringLiteral("首道字节 / 扩展文本头"),QStringLiteral("%1 / %2").arg(info.firstTraceOffset).arg(info.extendedTextHeaders)},
            {QStringLiteral("网格核验状态"),q(info.geometryStatus)},
            {QStringLiteral("INLINE 范围"),QStringLiteral("%1–%2；步长 %3；%4 条").arg(info.inlineMin).arg(info.inlineMax).arg(info.inlineStep).arg(info.inlineCount)},
            {QStringLiteral("XLINE 范围"),QStringLiteral("%1–%2；步长 %3；%4 条").arg(info.crosslineMin).arg(info.crosslineMax).arg(info.crosslineStep).arg(info.crosslineCount)},
            {QStringLiteral("检查的道头数"),QString::number(info.checkedHeaders)},
            {QStringLiteral("来源坐标单位码 / 缩放码"),QStringLiteral("%1 / %2").arg(info.coordinateUnitsCode).arg(info.coordinateScalar)},
            {QStringLiteral("坐标 / 时间基准"),QStringLiteral("本调用不推断 CRS、OWT/TWT 或地质参考面")},
            {QStringLiteral("文件路径"),q(info.sourceFile)},
            {QStringLiteral("说明"),QStringLiteral("文本头和全部限制见右侧 JSON；抽样规则网格不等于全文件逐道校验")}
        };
        wellTable_->setColumnCount(2);wellTable_->setRowCount(rows.size());wellTable_->setHorizontalHeaderLabels({QStringLiteral("属性"),QStringLiteral("来源事实")});
        for(int i=0;i<rows.size();++i){wellTable_->setItem(i,0,new QTableWidgetItem(rows[i].first));wellTable_->setItem(i,1,new QTableWidgetItem(rows[i].second));}
        wellTable_->resizeColumnsToContents();wellTable_->setColumnWidth(0,180);wellTable_->setWordWrap(true);wellTable_->resizeRowsToContents();visuals_->setCurrentIndex(0);
    }else if(request.kind=="seismic_trace"){
        const auto& trace=result.trace;for(size_t i=0;i<std::min<size_t>(10,trace.amplitudes.size());++i)preview.append(QJsonObject{{"sample",double(i)},{"time",numeric(trace.firstTime+i*trace.sampleInterval)},{"amplitude",numeric(trace.amplitudes[i])}});
        object["returnType"]="SeismicTraceResult";object["volumeId"]=request.resourceId;object["traceIndex"]=trace.traceIndex;object["traceNumber"]=trace.traceNumber;object["inlineNumber"]=trace.inlineNumber;object["crosslineNumber"]=trace.crosslineNumber;object["x"]=numeric(trace.x);object["y"]=numeric(trace.y);object["firstTime"]=numeric(trace.firstTime);object["sampleInterval"]=numeric(trace.sampleInterval);object["timeUnit"]=q(trace.timeUnit);object["amplitudeCount"]=double(trace.amplitudes.size());object["statistics"]=statisticsJson(result.statistics);object["preview"]=preview;object["previewTruncated"]=trace.amplitudes.size()>10;tracePlot_->setTrace(trace);visuals_->setCurrentIndex(3);
    }else if(request.kind=="section"||nativeLine(request.kind)){
        const auto& s=result.section;for(size_t i=0;i<std::min<size_t>(10,s.amplitudes.size());++i){size_t column=s.samples>0?i/size_t(s.samples):0;size_t sample=s.samples>0?i%size_t(s.samples):0;preview.append(QJsonObject{{"column",double(column)},{"sample",double(sample)},{"x",column<s.x.size()?numeric(s.x[column]):QJsonValue(QJsonValue::Null)},{"y",column<s.y.size()?numeric(s.y[column]):QJsonValue(QJsonValue::Null)},{"time",numeric(s.firstTime+double(sample)*s.sampleInterval)},{"amplitude",numeric(s.amplitudes[i])}});}
        object["returnType"]="SeismicSectionResult";object["sectionId"]=double(request.resourceId);object["columns"]=s.columns;object["samples"]=s.samples;object["firstTime"]=numeric(s.firstTime);object["sampleInterval"]=numeric(s.sampleInterval);object["timeUnit"]=q(s.timeUnit);object["amplitudeCount"]=double(s.amplitudes.size());object["layout"]="column-major: column * samples + sample";object["preview"]=preview;object["previewTruncated"]=s.amplitudes.size()>10;image_->setSection(s);visuals_->setCurrentIndex(2);
        if(nativeLine(request.kind)){
            object.remove("sectionId");object["volumeId"]=request.resourceId;object["direction"]=request.kind=="crossline"?"crossline":"inline";object["lineNumber"]=result.actualLineNumber;object["statistics"]=statisticsJson(result.statistics);
            QJsonArray columns;for(size_t i=0;i<std::min<size_t>(10,s.traceIndexes.size());++i)columns.append(QJsonObject{{"column",double(i)},{"traceIndex",s.traceIndexes[i]},{"inline",i<s.inlineNumbers.size()?QJsonValue(s.inlineNumbers[i]):QJsonValue(QJsonValue::Null)},{"crossline",i<s.crosslineNumbers.size()?QJsonValue(s.crosslineNumbers[i]):QJsonValue(QJsonValue::Null)}});object["columnPreview"]=columns;
            if(request.kind=="processed_section"){object["processing"]=QJsonObject{{"removeMean",false},{"gain",1},{"agcWindowSamples",101},{"clipAmplitude",3},{"writesSource",false}};object["rawStatistics"]=statisticsJson(result.rawStatistics);}
        }
    }else{
        const auto& s=result.slice;const size_t width=s.crosslineNumbers.size();
        for(size_t i=0;i<std::min<size_t>(10,s.amplitudes.size());++i){size_t row=width?i/width:0,col=width?i%width:0;preview.append(QJsonObject{{"inline",row<s.inlineNumbers.size()?QJsonValue(s.inlineNumbers[row]):QJsonValue(QJsonValue::Null)},{"crossline",col<s.crosslineNumbers.size()?QJsonValue(s.crosslineNumbers[col]):QJsonValue(QJsonValue::Null)},{"x",i<s.x.size()?numeric(s.x[i]):QJsonValue(QJsonValue::Null)},{"y",i<s.y.size()?numeric(s.y[i]):QJsonValue(QJsonValue::Null)},{"amplitude",numeric(s.amplitudes[i])}});}
        object["returnType"]="SeismicTimeSliceResult";object["volumeId"]=double(request.resourceId);object["inlineCount"]=double(s.inlineNumbers.size());object["crosslineCount"]=double(s.crosslineNumbers.size());object["requestedTime"]=numeric(request.first);object["actualTime"]=numeric(s.actualTime);object["timeUnit"]=q(s.timeUnit);object["amplitudeCount"]=double(s.amplitudes.size());object["layout"]="row-major: inlineIndex * crosslineCount + crosslineIndex";object["preview"]=preview;object["previewTruncated"]=s.amplitudes.size()>10;image_->setTimeSlice(s);visuals_->setCurrentIndex(2);
        if(request.kind=="seismic_slice")object["requestedTime"]=numeric(result.requestedTime);
    }
    json_->setPlainText(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));resultTabs_->setCurrentIndex(0);
    const bool tabular=request.kind=="intervals"||request.kind=="time_depth";
    status_->setText(tabular?QStringLiteral("%1 调用成功 · %2 ms · 完整结果已返回，表格显示前 200 行，JSON 显示前 10 项。").arg(method).arg(result.milliseconds):QStringLiteral("%1 调用成功 · %2 ms · 图形来自同一次返回对象；JSON 页显示字段与前 10 项摘要。").arg(method).arg(result.milliseconds));
}
