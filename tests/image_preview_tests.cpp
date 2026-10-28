// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QImageIOHandler>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace paleo::demo { bool loadPreviewImage(const QString&,QImage&,QString&); }
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QTemporaryDir temp;
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
        std::cout<<"Image preview tests passed\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
