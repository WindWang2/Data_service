// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include <QApplication>
#include <QImage>
#include <QImageReader>
#include <QImageIOHandler>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <limits>
#include "plotwidgets.h"

namespace paleo::demo { bool loadPreviewImage(const QString&,QImage&,QString&); }
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);QTemporaryDir temp;
    try {
        require(temp.isValid(),"cannot create temporary directory");
        QImage panorama(8192,256,QImage::Format_RGB32);panorama.fill(Qt::blue);
        const QString wide=temp.path()+"/wide.bmp";require(panorama.save(wide),"cannot write BMP fixture");
        QImage output;QString error="old error";
        require(paleo::demo::loadPreviewImage(wide,output,error),"bounded panorama load failed");
        require(output.width()==2048 && output.height()==64,"preview dimensions/aspect ratio incorrect");
        require(error.isEmpty(),"successful preview retained old error");
        require(QImageReader(wide).size()==QSize(8192,256),"original image was changed");

        require(!paleo::demo::loadPreviewImage(temp.path()+"/missing.png",output,error),"missing image reported success");
        require(output.isNull() && !error.isEmpty(),"failed load retained previous image");
        QImage small(100,200,QImage::Format_RGB32);small.fill(Qt::white);
        const QString smallPath=temp.path()+"/small.png";require(small.save(smallPath),"cannot write small image");
        require(paleo::demo::loadPreviewImage(smallPath,output,error),"small image failed");
        require(output.size()==small.size(),"small preview was needlessly enlarged");

        QImage large(5000,4000,QImage::Format_Grayscale8);large.fill(0);
        const QString largePath=temp.path()+"/large.png";require(large.save(largePath),"cannot write large image");large={};
        QImageReader reader(largePath);
        const bool nativeScale=reader.supportsOption(QImageIOHandler::ScaledSize);
        const bool loaded=paleo::demo::loadPreviewImage(largePath,output,error);
        if(nativeScale) require(loaded && output.width()<=2048 && output.height()<=2048,"native scaled load not bounded");
        else require(!loaded && output.isNull() && !error.isEmpty(),"large fallback decoded entire image");

        // A detailed section must retain every row and column beyond the old 1800 cap.
        // Alternating values make any discarded row/column visible in the assertion.
        paleo::data::SeismicSectionResult section;section.columns=1907;section.samples=1901;
        section.firstTime=100;section.sampleInterval=2;section.timeUnit="ms";
        section.amplitudes.resize(size_t(section.columns)*section.samples);
        for(int x=0;x<section.columns;++x)for(int y=0;y<section.samples;++y)
            section.amplitudes[size_t(x)*section.samples+y]=(x+y)%2?1.f:-1.f;
        section.amplitudes.back()=std::numeric_limits<float>::quiet_NaN();
        section.pathX.resize(section.columns);section.pathY.resize(section.columns);
        const int bend=953;section.vertexColumns={0,bend,section.columns-1};
        for(int x=0;x<section.columns;++x){section.pathX[x]=x<=bend?x:bend;section.pathY[x]=x<=bend?0:x-bend;}
        ImagePreview widget;widget.resize(900,650);widget.setSection(section);
        require(widget.nativeImageSize()==QSize(section.columns,section.samples),"section was irreversibly decimated");
        require(widget.nativeImage().pixel(1801,1801)==qRgb(0,0,255),"native sample color not retained");
        require(widget.nativeImage().pixel(1801,1802)==qRgb(255,0,0),"adjacent native sample missing");
        require(widget.nativeImage().pixel(section.columns-1,section.samples-1)==qRgb(160,160,160),"missing value was not gray");
        require(widget.hasWallGeometry(),"valid three-point wall was not available");widget.setWallMode(true);require(widget.wallMode(),"wall toggle failed");
        const QString sectionPath=temp.path()+"/section.png";require(widget.exportImage(sectionPath,error),"section PNG export failed");
        QImage exported(sectionPath);require(exported.size()==widget.nativeImageSize(),"export used viewport size");
        require(exported.pixel(1801,1802)==widget.nativeImage().pixel(1801,1802),"export changed native sample color");
        const QString wallPath=temp.path()+"/wall.png";require(widget.exportWallImage(wallPath,error,QSize(1200,900)),"wall PNG export failed");
        QImage wall(wallPath);require(wall.size()==QSize(1200,900),"wall export dimensions incorrect");
        int colored=0;for(int y=0;y<wall.height();y+=3)for(int x=0;x<wall.width();x+=3){const auto pixel=wall.pixel(x,y);if(pixel==qRgb(255,0,0)||pixel==qRgb(0,0,255))++colored;}
        require(colored>1000,"wall exported no seismic texture");
        require(!widget.exportWallImage(wallPath,error,QSize(100000,100000)),"wall export memory guard missing");
        widget.actualPixels();require(!widget.wallMode(),"1:1 did not restore exact sample view");

        // Unequal XY spacing must change texture widths on the wall. Merely stretching
        // all columns equally over an endpoint-to-endpoint quad would fail this ratio.
        paleo::data::SeismicSectionResult uneven;uneven.columns=4;uneven.samples=8;uneven.firstTime=0;uneven.sampleInterval=2;uneven.timeUnit="ms";
        uneven.pathX={0,1,2,100};uneven.pathY={0,0,0,0};uneven.vertexColumns={0,3};
        for(float value:{1.f,-1.f,0.5f,-0.5f})for(int n=0;n<8;++n)uneven.amplitudes.push_back(value);
        widget.setSection(uneven);require(widget.exportWallImage(wallPath,error,QSize(1200,900)),"unequal-spacing wall export failed");
        wall.load(wallPath);int narrowBlue=0,widePink=0;
        for(int y=0;y<wall.height();++y)for(int x=0;x<wall.width();++x){const auto pixel=wall.pixel(x,y);if(pixel==qRgb(0,0,255))++narrowBlue;if(pixel==qRgb(255,128,128))++widePink;}
        require(narrowBlue>100 && widePink>20*narrowBlue,"wall ignores nonuniform XY sample spacing");

        // Tiles occupy their final grid positions; partial output is not exportable.
        paleo::data::SeismicTimeSliceTile tile;tile.totalInlineCount=4;tile.totalCrosslineCount=4;
        tile.inlineOffset=1;tile.crosslineOffset=2;tile.inlineCount=2;tile.crosslineCount=2;
        tile.completedTiles=1;tile.totalTiles=4;tile.actualTime=228;tile.timeUnit="ms";
        tile.amplitudes={1.f,-1.f,0.f,std::numeric_limits<float>::quiet_NaN()};
        widget.clear();require(widget.applyTimeSliceTile(tile)&&widget.isPartial(),"valid progress tile rejected");
        require(widget.nativeImageSize()==QSize(4,4),"tile preview uses tile size instead of full image");
        require(widget.nativeImage().pixel(2,1)==qRgb(255,0,0)&&widget.nativeImage().pixel(3,1)==qRgb(0,0,255),"tile offsets or axes incorrect");
        require(widget.nativeImage().pixel(0,0)==qRgb(225,225,225),"unread tile area not marked");
        require(!widget.exportImage(sectionPath,error)&&!error.isEmpty(),"incomplete slice was exported");
        tile.inlineOffset=0;tile.crosslineOffset=0;tile.inlineCount=1;tile.crosslineCount=1;tile.amplitudes={2.f};tile.completedTiles=2;
        require(widget.applyTimeSliceTile(tile),"second progress tile rejected");
        require(widget.nativeImage().pixel(2,1)==qRgb(255,0,0),"later tile recolored prior tile inconsistently");
        tile.inlineOffset=4;require(!widget.applyTimeSliceTile(tile)&&widget.nativeImage().isNull()&&!widget.isPartial(),"invalid tile left a partial image");
        tile.inlineOffset=0;require(widget.applyTimeSliceTile(tile),"partial preview did not recover");

        // Time slices keep both axes, with explicit row-major orientation.
        paleo::data::SeismicTimeSliceResult slice;slice.inlineNumbers.resize(1803);slice.crosslineNumbers.resize(1805);slice.timeUnit="ms";
        slice.amplitudes.assign(size_t(1803)*1805,0.f);slice.amplitudes[size_t(1802)*1805+1804]=-1.f;slice.amplitudes[1]=1.f;
        widget.setTimeSlice(slice);require(!widget.isPartial(),"complete result left export blocked");require(widget.exportImage(sectionPath,error),"completed slice not exportable");require(widget.nativeImageSize()==QSize(1805,1803),"time slice was irreversibly decimated");
        require(widget.nativeImage().pixel(1804,1802)==qRgb(0,0,255),"last native time-slice cell lost");
        require(widget.nativeImage().pixel(1,0)==qRgb(255,0,0),"time-slice row-major axes swapped");
        require(!widget.hasWallGeometry(),"slice retained stale section geometry");
        widget.setSection({});require(widget.nativeImage().isNull(),"invalid result left stale image");
        require(!widget.exportImage(sectionPath,error),"empty view exported stale image");
        std::cout<<"Image preview tests passed\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
