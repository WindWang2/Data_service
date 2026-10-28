#include "plotwidgets.h"
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QImageIOHandler>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace paleo::data;

namespace paleo::demo {
// 独立的缩略图读取入口，便于验证内存/尺寸边界；始终只读原图。
bool loadPreviewImage(const QString& path,QImage& output,QString& error) {
    output={}; error.clear();
    constexpr int MaximumEdge=2048;
    constexpr qint64 MaximumFallbackPixels=16LL*1024*1024;
    QImageReader reader(path);reader.setAutoTransform(true);
    if(!reader.canRead()) {error=reader.errorString();return false;}
    const QSize original=reader.size();
    if(!original.isValid()) {error=QStringLiteral("图片尺寸无效，无法生成预览");return false;}
    const QSize target=original.scaled(MaximumEdge,MaximumEdge,Qt::KeepAspectRatio);
    if(original.width()>MaximumEdge || original.height()>MaximumEdge) {
        // JPEG 等支持缩略解码；不支持的格式可能先完整解码再缩放，过大时应明确停止。
        if(!reader.supportsOption(QImageIOHandler::ScaledSize) && qint64(original.width())*original.height()>MaximumFallbackPixels) {
            error=QStringLiteral("原图为 %1 × %2 像素，当前格式不支持直接缩略解码；请打开原文件查看，预览未加载完整大图").arg(original.width()).arg(original.height());
            return false;
        }
        reader.setScaledSize(target);
    }
    QImage image=reader.read();
    if(image.isNull()) {error=reader.errorString();return false;}
    // 某些插件可能忽略缩略尺寸，最终交给界面的图像仍须满足最长边限制。
    if(image.width()>MaximumEdge || image.height()>MaximumEdge) image=image.scaled(MaximumEdge,MaximumEdge,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    if(image.isNull()) {error=QStringLiteral("无法分配图片预览缓冲");return false;}
    output=std::move(image);return true;
}
}

CurvePlot::CurvePlot(QWidget* parent):QWidget(parent) {setMinimumSize(280,320); setAutoFillBackground(true);}
void CurvePlot::setCurve(const CurveSlice& curve) {curve_=curve; update();}
void CurvePlot::clear() {curve_={}; update();}
void CurvePlot::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const bool dark=palette().base().color().lightness()<128;
    const QColor grid=dark?QColor(72,78,88):palette().mid().color();
    p.fillRect(rect(),palette().base()); p.setPen(palette().text().color());
    if(curve_.depths.empty()){p.drawText(rect(),Qt::AlignCenter,QStringLiteral("选择一条曲线后点击“查询 / 预览”"));return;}
    const auto n=std::min(curve_.depths.size(),curve_.values.size());
    double xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;
    for(size_t i=0;i<n;++i){if(std::isfinite(curve_.depths[i])){ymin=std::min(ymin,curve_.depths[i]);ymax=std::max(ymax,curve_.depths[i]);}
        if(i<curve_.quality.size()&&curve_.quality[i]==1)continue;
        for(const auto* v:{&curve_.values,&curve_.rawValues}) if(i<v->size()&&std::isfinite((*v)[i])){xmin=std::min(xmin,(*v)[i]);xmax=std::max(xmax,(*v)[i]);}}
    if(!std::isfinite(xmin)||!std::isfinite(ymin)){p.drawText(rect(),Qt::AlignCenter,QStringLiteral("此范围没有可绘制的有效数据"));return;}
    if(xmin==xmax){xmin-=1;xmax+=1;} if(ymin==ymax)ymax=ymin+1;
    QRectF area(88,60,std::max(40,width()-118),std::max(40,height()-112));
    p.drawText(QRectF(12,10,width()-24,24),Qt::AlignCenter,QString::fromStdString(curve_.name)+"  "+QString::fromStdString(curve_.unit));
    p.drawText(QRectF(12,34,width()-24,20),Qt::AlignCenter,QStringLiteral("灰虚线：原始有效值　蓝实线：本次查询值　空值断线（原值见数据表）"));
    p.drawRect(area);
    for(int k=0;k<=5;++k){double x=area.left()+area.width()*k/5, y=area.top()+area.height()*k/5;
        p.setPen(grid); p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        p.setPen(palette().text().color());p.drawText(QRectF(x-38,area.bottom()+6,76,22),Qt::AlignCenter,QString::number(xmin+(xmax-xmin)*k/5,'g',5));
        p.drawText(QRectF(2,y-11,78,22),Qt::AlignRight|Qt::AlignVCenter,QString::number(ymin+(ymax-ymin)*k/5,'g',7));}
    auto draw=[&](const std::vector<double>& values,const QColor& color,Qt::PenStyle style){
        QPainterPath path; bool active=false;size_t stride=std::max<size_t>(1,n/12000);
        for(size_t i=0;i<n;++i){if(i>=values.size()||!std::isfinite(values[i])||!std::isfinite(curve_.depths[i])||(i<curve_.quality.size()&&curve_.quality[i]==1)){active=false;continue;}
            if(i%stride!=0&&i+1!=n)continue;
            QPointF pos(area.left()+(values[i]-xmin)/(xmax-xmin)*area.width(),area.top()+(curve_.depths[i]-ymin)/(ymax-ymin)*area.height());
            if(active)path.lineTo(pos);else path.moveTo(pos);active=true;}
        p.save();p.setClipRect(area);p.setPen(QPen(color,1.2,style));p.drawPath(path);p.restore();};
    draw(curve_.rawValues,dark?QColor(164,174,185):QColor(119,129,139),Qt::DashLine);draw(curve_.values,dark?QColor(99,179,255):QColor(36,116,180),Qt::SolidLine);
    p.setPen(palette().text().color());p.drawText(QRectF(4,height()-26,width()-8,22),Qt::AlignCenter,QStringLiteral("深度向下 · %1 · %2 点（绘图抽样不改变存储）").arg(curve_.depthUnit.empty()?QStringLiteral("单位未确认"):QString::fromStdString(curve_.depthUnit)).arg(n));
}

ImagePreview::ImagePreview(QWidget* parent):QWidget(parent){setMinimumSize(280,280);setAutoFillBackground(true);}
bool ImagePreview::load(const QString& path,QString& error) {
    clear(); // 先清掉上一次资料；失败时不能显示旧图而让人误以为是新图。
    const bool loaded=paleo::demo::loadPreviewImage(path,image_,error);
    caption_=loaded?QFileInfo(path).fileName():QStringLiteral("图片预览失败：%1").arg(error);
    update();return loaded;
}
void ImagePreview::clear(){image_={};caption_.clear();sectionMode_=false;update();}
void ImagePreview::setTimeSlice(const SeismicTimeSliceResult& s){
    sectionMode_=false;
    const size_t rows=s.inlineNumbers.size(),columns=s.crosslineNumbers.size();
    if(!rows||!columns||rows*columns!=s.amplitudes.size()){clear();caption_=QStringLiteral("时间切片尺寸不一致");return;}
    const int width=static_cast<int>(std::min<size_t>(columns,1800)),height=static_cast<int>(std::min<size_t>(rows,1800));
    image_=QImage(width,height,QImage::Format_RGB32);float maximum=0;
    for(float a:s.amplitudes)if(std::isfinite(a))maximum=std::max(maximum,std::abs(a));if(maximum==0)maximum=1;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const size_t row=static_cast<size_t>(y)*rows/height,column=static_cast<size_t>(x)*columns/width;const float amplitude=s.amplitudes[row*columns+column];
        if(!std::isfinite(amplitude)){image_.setPixelColor(x,y,QColor(160,160,160));continue;}const float value=std::clamp(amplitude/maximum,-1.f,1.f);const int shade=255-int(std::abs(value)*255);image_.setPixelColor(x,y,value>=0?QColor(255,shade,shade):QColor(shade,shade,255));}
    caption_=QStringLiteral("时间 %1 %2 · IL %3–%4（纵轴） · XL %5–%6（横轴） · %7 × %8 网格 · 缺失为灰色")
        .arg(s.actualTime).arg(QString::fromStdString(s.timeUnit)).arg(s.inlineNumbers.front()).arg(s.inlineNumbers.back()).arg(s.crosslineNumbers.front()).arg(s.crosslineNumbers.back()).arg(rows).arg(columns);update();
}
void ImagePreview::setSection(const SeismicSectionResult& s){
    sectionMode_=true;
    if(s.columns<=0||s.samples<=0||s.amplitudes.size()!=static_cast<size_t>(s.columns)*s.samples){clear();caption_=QStringLiteral("剖面结果尺寸不一致");return;}
    const int w=std::min(s.columns,1800),h=std::min(s.samples,1800); image_=QImage(w,h,QImage::Format_RGB32);
    float maximum=0;for(float a:s.amplitudes)if(std::isfinite(a))maximum=std::max(maximum,std::abs(a));if(maximum==0)maximum=1;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto ix=static_cast<size_t>(x)*s.columns/w,iy=static_cast<size_t>(y)*s.samples/h;float a=s.amplitudes[ix*s.samples+iy];
        if(!std::isfinite(a)){image_.setPixelColor(x,y,QColor(160,160,160));continue;}float value=std::clamp(a/maximum,-1.f,1.f);int shade=255-int(std::abs(value)*255);image_.setPixelColor(x,y,value>=0?QColor(255,shade,shade):QColor(shade,shade,255));}
    caption_=QStringLiteral("横轴 %1 道 · 纵轴 %3 → %6 %5（向下增加） · %2 样点 / 道 · Δt=%4 %5 · 缺失为灰色").arg(s.columns).arg(s.samples).arg(s.firstTime).arg(s.sampleInterval).arg(QString::fromStdString(s.timeUnit)).arg(s.firstTime+(s.samples-1)*s.sampleInterval);update();
}
void ImagePreview::paintEvent(QPaintEvent*){QPainter p(this);p.fillRect(rect(),palette().base());p.setPen(palette().text().color());
    if(image_.isNull()){p.drawText(rect(),Qt::AlignCenter,caption_.isEmpty()?QStringLiteral("选择平面图，或提取二维地震剖面"):caption_);return;}
    QRect available=rect().adjusted(12,12,-12,-46);QSize fitted=image_.size().scaled(available.size(),Qt::KeepAspectRatio);QRect target=sectionMode_?available:QRect(QPoint(available.center().x()-fitted.width()/2,available.center().y()-fitted.height()/2),fitted);p.drawImage(target,image_);p.drawText(rect().adjusted(8,height()-38,-8,-4),Qt::AlignCenter|Qt::TextWordWrap,caption_);}
