#include "plotwidgets.h"
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QImageIOHandler>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QImageWriter>
#include <QToolButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTransform>
#include <numeric>
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

ImagePreview::ImagePreview(QWidget* parent):QWidget(parent){
    setMinimumSize(280,280);setAutoFillBackground(true);setMouseTracking(true);
    setToolTip(QStringLiteral("滚轮放大/缩小；按住左键拖动；双击适应窗口。1:1 可查看每个原始样点，导出不受窗口大小影响。"));
    toolbar_=new QWidget(this);auto* layout=new QHBoxLayout(toolbar_);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);
    auto button=[&](const QString& name,const QString& text,const QString& tip){auto* b=new QToolButton(toolbar_);b->setObjectName(name);b->setText(text);b->setToolTip(tip);layout->addWidget(b);return b;};
    auto* fit=button("imageFit","适应","完整显示；仅缩放显示，不改变返回数据");connect(fit,&QToolButton::clicked,this,[this]{fitToWindow();});
    auto* actual=button("imageActualPixels","1:1","一个返回的振幅对应一个图像像素；拖动查看其他位置");connect(actual,&QToolButton::clicked,this,[this]{actualPixels();});
    auto* minus=button("imageZoomOut","−","缩小");connect(minus,&QToolButton::clicked,this,[this]{zoomAt(0.8,plotArea().center());});
    auto* plus=button("imageZoomIn","+","放大");connect(plus,&QToolButton::clicked,this,[this]{zoomAt(1.25,plotArea().center());});
    wallButton_=button("imageWallMode","剖面墙","按 XY 路径显示立体剖面墙；时间向下，垂向比例仅用于展示");wallButton_->setCheckable(true);connect(wallButton_,&QToolButton::toggled,this,[this](bool on){setWallMode(on);});
    rotateButton_=button("imageWallRotate","旋转","剖面墙绕时间轴旋转 30°");connect(rotateButton_,&QToolButton::clicked,this,[this]{wallAngle_+=30;update();});
    exportButton_=button("imageExport","导出","导出完整分辨率 PNG；剖面墙还可导出 4096 × 3072 投影图");
    auto* exports=new QMenu(exportButton_);exports->addAction(QStringLiteral("原始分辨率矩阵 PNG"),this,[this]{chooseExport(false);});exports->addAction(QStringLiteral("高清剖面墙 PNG（4096 × 3072）"),this,[this]{chooseExport(true);});exportButton_->setMenu(exports);exportButton_->setPopupMode(QToolButton::InstantPopup);
    layout->addStretch();updateControls();
}
bool ImagePreview::load(const QString& path,QString& error) {
    clear(); // 先清掉上一次资料；失败时不能显示旧图而让人误以为是新图。
    const bool loaded=paleo::demo::loadPreviewImage(path,image_,error);
    caption_=loaded?QFileInfo(path).fileName():QStringLiteral("图片预览失败：%1").arg(error);
    updateControls();update();return loaded;
}
void ImagePreview::clear(){partial_=false;partialScale_=1;image_={};caption_.clear();sectionMode_=false;wallMode_=false;wallPoints_.clear();wallColumns_.clear();wallSamples_.clear();fit_=true;zoom_=1;pan_={};updateControls();update();}
namespace {
constexpr qint64 MaximumSeismicPixels=64000000;
QRgb amplitudePixel(float amplitude,float maximum){
    if(!std::isfinite(amplitude))return qRgb(160,160,160);
    const float value=std::clamp(amplitude/maximum,-1.f,1.f);const int shade=255-int(std::abs(value)*255);
    return value>=0?qRgb(255,shade,shade):qRgb(shade,shade,255);
}
float amplitudeMaximum(const std::vector<float>& values){float maximum=0;for(float value:values)if(std::isfinite(value))maximum=std::max(maximum,std::abs(value));return maximum>0?maximum:1;}
bool savePng(const QImage& image,const QString& path,QString& error){
    error.clear();if(image.isNull()){error=QStringLiteral("没有可导出的图像");return false;}
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
    QImageWriter writer(&file,"png");if(!writer.write(image)){error=writer.errorString();file.cancelWriting();return false;}
    if(!file.commit()){error=file.errorString();return false;}return true;
}
}
void ImagePreview::setTimeSlice(const SeismicTimeSliceResult& s){
    clear();
    const size_t rows=s.inlineNumbers.size(),columns=s.crosslineNumbers.size();
    if(!rows||!columns||rows>MaximumSeismicPixels/columns||rows*columns!=s.amplitudes.size()){caption_=QStringLiteral("时间切片尺寸不一致或超过 6400 万像素预算");return;}
    const int width=static_cast<int>(columns),height=static_cast<int>(rows);
    image_=QImage(width,height,QImage::Format_RGB32);if(image_.isNull()){caption_=QStringLiteral("无法分配切片图像内存，请缩小读取范围");return;}
    const float maximum=amplitudeMaximum(s.amplitudes);
    for(int y=0;y<height;++y){auto* pixels=reinterpret_cast<QRgb*>(image_.scanLine(y));for(int x=0;x<width;++x)pixels[x]=amplitudePixel(s.amplitudes[size_t(y)*columns+x],maximum);}
    caption_=QStringLiteral("时间 %1 %2 · IL %3–%4（纵轴） · XL %5–%6（横轴） · %7 × %8 网格 · 缺失为灰色")
        .arg(s.actualTime).arg(QString::fromStdString(s.timeUnit)).arg(s.inlineNumbers.front()).arg(s.inlineNumbers.back()).arg(s.crosslineNumbers.front()).arg(s.crosslineNumbers.back()).arg(rows).arg(columns);
    if(s.decimated)caption_+=QStringLiteral(" · 当前为抽样预览");updateControls();update();
}
bool ImagePreview::applyTimeSliceTile(const SeismicTimeSliceTile& tile){
    const qint64 cells=qint64(tile.totalInlineCount)*tile.totalCrosslineCount;
    if(tile.totalInlineCount<=0||tile.totalCrosslineCount<=0||cells>MaximumSeismicPixels||
       tile.inlineCount<=0||tile.crosslineCount<=0||tile.inlineOffset<0||tile.crosslineOffset<0||
       qint64(tile.inlineOffset)+tile.inlineCount>tile.totalInlineCount||qint64(tile.crosslineOffset)+tile.crosslineCount>tile.totalCrosslineCount||
       tile.amplitudes.size()!=size_t(tile.inlineCount)*tile.crosslineCount){clear();caption_=QStringLiteral("分块尺寸无效，未发布图像");return false;}
    const QSize size(tile.totalCrosslineCount,tile.totalInlineCount);
    if(!partial_){
        clear();image_=QImage(size,QImage::Format_RGB32);if(image_.isNull()){caption_=QStringLiteral("无法分配切片图像内存");return false;}
        image_.fill(qRgb(225,225,225));partial_=true;partialScale_=amplitudeMaximum(tile.amplitudes);
    }else if(image_.size()!=size){clear();caption_=QStringLiteral("分块所属网格已变化");return false;}
    for(int y=0;y<tile.inlineCount;++y){auto* pixels=reinterpret_cast<QRgb*>(image_.scanLine(tile.inlineOffset+y));
        for(int x=0;x<tile.crosslineCount;++x)pixels[tile.crosslineOffset+x]=amplitudePixel(tile.amplitudes[size_t(y)*tile.crosslineCount+x],partialScale_);}
    caption_=QStringLiteral("时间 %1 %2 · 已读取 %3 / %4 块 · 临时色标，完整返回后统一重绘 · 灰色区域尚未读取 · 完成前不能导出")
        .arg(tile.actualTime).arg(QString::fromStdString(tile.timeUnit)).arg(tile.completedTiles).arg(tile.totalTiles);
    updateControls();update();return true;
}
void ImagePreview::setSection(const SeismicSectionResult& s){
    clear();sectionMode_=true;
    if(s.columns<=0||s.samples<=0||qint64(s.columns)*s.samples>MaximumSeismicPixels||s.amplitudes.size()!=static_cast<size_t>(s.columns)*s.samples){caption_=QStringLiteral("剖面尺寸不一致或超过 6400 万像素预算");return;}
    image_=QImage(s.columns,s.samples,QImage::Format_RGB32);if(image_.isNull()){caption_=QStringLiteral("无法分配剖面图像内存，请缩小读取范围");return;}
    const float maximum=amplitudeMaximum(s.amplitudes);
    for(int y=0;y<s.samples;++y){auto* pixels=reinterpret_cast<QRgb*>(image_.scanLine(y));for(int x=0;x<s.columns;++x)pixels[x]=amplitudePixel(s.amplitudes[size_t(x)*s.samples+y],maximum);}
    firstTime_=s.firstTime;lastTime_=s.firstTime+(s.samples-1)*s.sampleInterval;timeUnit_=QString::fromStdString(s.timeUnit);
    const auto& xs=s.pathX.size()==size_t(s.columns)?s.pathX:s.x;const auto& ys=s.pathY.size()==size_t(s.columns)?s.pathY:s.y;
    if(xs.size()==size_t(s.columns)&&ys.size()==size_t(s.columns)&&s.columns>=2){
        std::vector<int> vertices=s.vertexColumns;if(vertices.empty())vertices={0,s.columns-1};
        bool valid=vertices.size()>=2&&vertices.front()==0&&vertices.back()==s.columns-1;
        for(size_t n=0;n<vertices.size();++n){const int at=vertices[n];if(at<0||at>=s.columns||(n&&at<=vertices[n-1])||!std::isfinite(xs[at])||!std::isfinite(ys[at])){valid=false;break;}}
        for(int at=0;valid&&at<s.columns;++at)if(!std::isfinite(xs[at])||!std::isfinite(ys[at]))valid=false;
        if(valid){for(int at:vertices){wallPoints_.emplace_back(xs[at],ys[at]);wallColumns_.push_back(at);}for(int at=0;at<s.columns;++at)wallSamples_.emplace_back(xs[at],ys[at]);}
    }
    caption_=QStringLiteral("横轴 %1 道 · 纵轴 %3 → %6 %5（向下增加） · %2 样点 / 道 · Δt=%4 %5 · 缺失为灰色").arg(s.columns).arg(s.samples).arg(s.firstTime).arg(s.sampleInterval).arg(timeUnit_).arg(lastTime_);
    if(s.decimated)caption_+=QStringLiteral(" · 当前为抽样预览");updateControls();update();
}
void ImagePreview::updateControls(){
    if(!wallButton_)return;wallButton_->setEnabled(hasWallGeometry());wallButton_->setChecked(wallMode_);
    rotateButton_->setEnabled(wallMode_);exportButton_->setEnabled(!image_.isNull()&&!partial_);
}
QRectF ImagePreview::plotArea() const {return QRectF(12,48,std::max(1,width()-24),std::max(1,height()-116));}
QRectF ImagePreview::imageTarget(const QRectF& area) const {
    QSizeF size=image_.size();if(fit_){if(sectionMode_)size=area.size();else size.scale(area.size(),Qt::KeepAspectRatio);}
    size*=zoom_;return QRectF(area.center()+pan_-QPointF(size.width()/2,size.height()/2),size);
}
void ImagePreview::fitToWindow(){fit_=true;zoom_=1;pan_={};update();}
void ImagePreview::actualPixels(){setWallMode(false);fit_=false;zoom_=1;pan_={};update();}
void ImagePreview::setWallMode(bool enabled){wallMode_=enabled&&hasWallGeometry();fitToWindow();updateControls();}
void ImagePreview::zoomAt(double factor,const QPointF& position){
    if(image_.isNull())return;const double next=std::clamp(zoom_*factor,0.02,64.0);factor=next/zoom_;
    const auto center=plotArea().center();pan_=(position-center)-(position-center-pan_)*factor;zoom_=next;update();
}
void ImagePreview::resizeEvent(QResizeEvent*){toolbar_->setGeometry(8,7,std::max(1,width()-16),32);}
void ImagePreview::wheelEvent(QWheelEvent* event){if(!plotArea().contains(event->position())){event->ignore();return;}zoomAt(std::pow(1.2,event->angleDelta().y()/120.0),event->position());event->accept();}
void ImagePreview::mousePressEvent(QMouseEvent* event){if(event->button()==Qt::LeftButton&&plotArea().contains(event->position())&&!image_.isNull()){dragging_=true;lastMouse_=event->position();setCursor(Qt::ClosedHandCursor);event->accept();}else QWidget::mousePressEvent(event);}
void ImagePreview::mouseMoveEvent(QMouseEvent* event){if(dragging_){pan_+=event->position()-lastMouse_;lastMouse_=event->position();update();event->accept();}else QWidget::mouseMoveEvent(event);}
void ImagePreview::mouseReleaseEvent(QMouseEvent* event){if(event->button()==Qt::LeftButton&&dragging_){dragging_=false;unsetCursor();event->accept();}else QWidget::mouseReleaseEvent(event);}
void ImagePreview::mouseDoubleClickEvent(QMouseEvent* event){if(event->button()==Qt::LeftButton){fitToWindow();event->accept();}else QWidget::mouseDoubleClickEvent(event);}
bool ImagePreview::exportImage(const QString& path,QString& error)const{if(partial_){error=QStringLiteral("切片仍在加载，不能导出不完整图像");return false;}return savePng(image_,path,error);}
bool ImagePreview::exportWallImage(const QString& path,QString& error,QSize size)const{
    error.clear();if(image_.isNull()||!hasWallGeometry()){error=QStringLiteral("没有有效坐标路径，无法导出剖面墙");return false;}
    if(size.width()<320||size.height()<240||qint64(size.width())*size.height()>16000000){error=QStringLiteral("剖面墙输出至少 320 × 240，最多 1600 万像素");return false;}
    QImage output(size,QImage::Format_RGB32);if(output.isNull()){error=QStringLiteral("无法分配导出图像内存");return false;}output.fill(Qt::white);
    QPainter p(&output);QFont font=p.font();font.setPixelSize(std::max(14,size.width()/160));p.setFont(font);p.setPen(QColor(25,35,45));
    drawWall(p,QRectF(0,0,size.width(),size.height()-80));p.drawText(QRect(30,size.height()-75,size.width()-60,65),Qt::AlignCenter|Qt::TextWordWrap,caption_+QStringLiteral(" · XY 路径投影；时间向下，垂向比例仅用于展示"));p.end();return savePng(output,path,error);
}
void ImagePreview::chooseExport(bool wall){
    if(wall&&!hasWallGeometry()){QMessageBox::information(this,QStringLiteral("剖面墙"),QStringLiteral("请先提取具有 XY 路径的剖面"));return;}
    const QString path=QFileDialog::getSaveFileName(this,wall?QStringLiteral("导出高清剖面墙"):QStringLiteral("导出完整分辨率矩阵"),wall?"seismic-wall.png":"seismic-image.png","PNG (*.png)");if(path.isEmpty())return;
    QString error;if(!(wall?exportWallImage(path,error):exportImage(path,error)))QMessageBox::warning(this,QStringLiteral("导出失败"),error);
}
void ImagePreview::contextMenuEvent(QContextMenuEvent* event){
    QMenu menu(this);menu.addAction(QStringLiteral("适应窗口"),this,[this]{fitToWindow();});menu.addAction(QStringLiteral("1:1 原始样点"),this,[this]{actualPixels();});
    auto* wall=menu.addAction(QStringLiteral("展开剖面 / 立体剖面墙"),this,[this]{setWallMode(!wallMode_);});wall->setEnabled(hasWallGeometry());
    auto* save=menu.addAction(QStringLiteral("导出完整分辨率 PNG"),this,[this]{chooseExport(false);});save->setEnabled(!image_.isNull()&&!partial_);
    auto* fence=menu.addAction(QStringLiteral("导出高清剖面墙 PNG"),this,[this]{chooseExport(true);});fence->setEnabled(hasWallGeometry());menu.exec(event->globalPos());
}
void ImagePreview::drawWall(QPainter& p,const QRectF& area,double zoom,QPointF pan)const{
    if(!hasWallGeometry())return;
    double minX=INFINITY,maxX=-INFINITY,minY=INFINITY,maxY=-INFINITY;for(const auto& point:wallSamples_){minX=std::min(minX,point.x());maxX=std::max(maxX,point.x());minY=std::min(minY,point.y());maxY=std::max(maxY,point.y());}
    const double span=std::max({maxX-minX,maxY-minY,1.0}),cx=(minX+maxX)/2,cy=(minY+maxY)/2;
    const double angle=wallAngle_*3.14159265358979323846/180.0,cosine=std::cos(angle),sine=std::sin(angle);
    std::vector<QPointF> top,bottom;std::vector<double> depth;
    for(const auto& point:wallSamples_){const double x=(point.x()-cx)/span,y=(point.y()-cy)/span;const double horizontal=x*cosine-y*sine,behind=x*sine+y*cosine;top.emplace_back(horizontal,behind*0.38);bottom.emplace_back(horizontal,behind*0.38+0.85);depth.push_back(behind);}
    double left=INFINITY,right=-INFINITY,upper=INFINITY,lower=-INFINITY;for(size_t n=0;n<top.size();++n){left=std::min(left,top[n].x());right=std::max(right,top[n].x());upper=std::min(upper,top[n].y());lower=std::max(lower,bottom[n].y());}
    const QRectF safe=area.adjusted(55,30,-55,-42);const double scale=std::min(safe.width()/std::max(right-left,0.15),safe.height()/std::max(lower-upper,0.1))*zoom;
    const QPointF origin=safe.center()+pan-QPointF((left+right)*scale/2,(upper+lower)*scale/2);
    for(size_t n=0;n<top.size();++n){top[n]=origin+top[n]*scale;bottom[n]=origin+bottom[n]*scale;}
    std::vector<size_t> order(top.size()-1);std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return depth[a]+depth[a+1]<depth[b]+depth[b+1];});
    p.save();p.setClipRect(area);p.setRenderHint(QPainter::SmoothPixmapTransform,false);
    for(size_t n:order){
        // 样点中心 n+0.5 对齐真实的请求位置。两点之间显示相邻两列各半个像素，
        // 物理分界在两点中点；折点由相邻段共享，不跨折点拉直，也不按列号等距摆放。
        const double first=n+0.5,end=n+1.5;
        const QPolygonF source{QPointF(first,0),QPointF(end,0),QPointF(end,image_.height()),QPointF(first,image_.height())};
        const QPolygonF destination{top[n],top[n+1],bottom[n+1],bottom[n]};QTransform transform;
        if(QTransform::quadToQuad(source,destination,transform)){p.save();p.setTransform(transform,true);p.drawImage(QRectF(first,0,end-first,image_.height()),image_,QRectF(first,0,end-first,image_.height()));p.restore();}
    }
    // 只画轮廓和输入折点的边界，不能给每一道叠加黑线遮住高清纹理。
    p.setPen(QPen(QColor(55,65,75),std::max(1.0,area.width()/1600)));p.setBrush(Qt::NoBrush);
    for(size_t n=0;n+1<top.size();++n){p.drawLine(top[n],top[n+1]);p.drawLine(bottom[n],bottom[n+1]);}
    p.setPen(QColor(45,65,80));for(size_t n=0;n<wallColumns_.size();++n){const int at=wallColumns_[n];p.drawLine(top[at],bottom[at]);p.setBrush(QColor(30,110,160));p.drawEllipse(top[at],3,3);p.drawText(QRectF(top[at].x()-110,top[at].y()-42,220,38),Qt::AlignCenter,QStringLiteral("P%1  (%2, %3)").arg(n+1).arg(wallPoints_[n].x(),0,'g',9).arg(wallPoints_[n].y(),0,'g',9));}
    p.drawText(QRectF(top.front().x()-130,top.front().y(),122,26),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("%1 %2").arg(firstTime_).arg(timeUnit_));
    p.drawText(QRectF(bottom.front().x()-130,bottom.front().y()-26,122,26),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("%1 %2").arg(lastTime_).arg(timeUnit_));p.restore();
}
void ImagePreview::paintEvent(QPaintEvent*){
    QPainter p(this);p.fillRect(rect(),palette().base());p.setPen(palette().text().color());
    if(image_.isNull()){p.drawText(plotArea(),Qt::AlignCenter|Qt::TextWordWrap,caption_.isEmpty()?QStringLiteral("选择平面图，或提取二维地震剖面"):caption_);return;}
    const auto area=plotArea();if(wallMode_)drawWall(p,area,zoom_,pan_);else {p.save();p.setClipRect(area);p.setRenderHint(QPainter::SmoothPixmapTransform,false);p.drawImage(imageTarget(area),image_);p.restore();}
    p.setPen(palette().text().color());const QString hint=wallMode_?QStringLiteral("XY 路径投影 · 时间向下 · 垂向比例仅用于展示 · 拖动 / 滚轮缩放"):QStringLiteral("图像 %1 × %2 像素 · %3 · 滚轮缩放 / 拖动 / 双击适应 · 导出保留全部像素").arg(image_.width()).arg(image_.height()).arg(fit_?QStringLiteral("适应窗口显示"):QStringLiteral("1:1 基准 × %1").arg(zoom_,0,'f',2));
    p.drawText(QRectF(8,height()-64,width()-16,24),Qt::AlignCenter,hint);p.drawText(QRectF(8,height()-40,width()-16,36),Qt::AlignCenter|Qt::TextWordWrap,caption_);
}
