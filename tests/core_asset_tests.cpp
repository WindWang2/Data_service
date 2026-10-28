// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include "paleo_data/parser.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace paleo::data { void associateCoreAsset(ParsedFile&,const ParseOptions&,const CancelCheck&); }
using namespace paleo::data;

namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
const QByteArray Png=QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jQ4kAAAAASUVORK5CYII=");
std::string create(const QTemporaryDir& dir,const QString& relative,const QByteArray& data=Png) {
    const QString path=dir.path()+"/"+relative;require(QDir().mkpath(QFileInfo(path).absolutePath()),"cannot create fixture directory");
    QFile file(path);require(file.open(QIODevice::WriteOnly),"cannot open fixture");require(file.write(data)==data.size(),"cannot write fixture");return path.toUtf8().toStdString();
}
ParsedFile associate(const std::string& path,const ParseOptions& options={}) {
    ParsedFile result;result.path=path;result.kind="attachment";result.status="registered";
    associateCoreAsset(result,options,{});return result;
}
bool hasIssue(const ParsedFile& file,const char* code) {for(const auto& issue:file.issues)if(issue.code==code)return true;return false;}
const CorePhoto& photo(const ParsedFile& file) {
    require(file.wells.size()==1,"expected one well");require(file.wells[0].wellbore.corePhotoTracks.size()==1,"expected one photo track");
    require(file.wells[0].wellbore.corePhotoTracks[0].photos.size()==1,"expected one photo");return file.wells[0].wellbore.corePhotoTracks[0].photos[0];
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); QTemporaryDir directory;
    try {
        require(directory.isValid(),"temporary directory unavailable");
        auto explicitRange=associate(create(directory,QStringLiteral("岩心/XJ88-8-B2H1/照片/101-102m.png")));
        require(explicitRange.wells[0].wellInfo.wellName=="XJ88-8-B2H1","branch well name was truncated");
        require(photo(explicitRange).topDepth==101 && photo(explicitRange).bottomDepth==102,"explicit depth range missing");
        require(explicitRange.wells[0].defaultDepthInfo.depthUnit=="m","explicit unit missing");
        require(explicitRange.wells[0].defaultDepthInfo.depthType.empty(),"depth type must not be guessed");
        require(explicitRange.status=="review","path-derived metadata must require review");

        auto boxes=associate(create(directory,QStringLiteral("岩心/HZ81-1-1d/岩心照片/1-4 白光.png")),ParseOptions{.depthUnit="m"});
        require(photo(boxes).imagePath==boxes.path,"photo path lost");
        require(std::isnan(photo(boxes).topDepth) && std::isnan(photo(boxes).bottomDepth),"box indexes became depths");
        auto nestedBox=associate(create(directory,QStringLiteral("岩心/HZ85-5-A27P1/岩心照片/（1-1）-（1-4）白光.png")),ParseOptions{.depthUnit="m"});
        require(std::isnan(photo(nestedBox).topDepth),"nested box index became depth");

        const auto rangePath=create(directory,QStringLiteral("岩心/HZ81-1-21d井/扫描照片/zqm/201.00-202.00-1.png"));
        auto unknownUnit=associate(rangePath);
        require(std::isnan(photo(unknownUnit).topDepth),"unit-free depth guessed");
        require(hasIssue(unknownUnit,"CORE_PHOTO_DEPTH_UNIT_UNKNOWN"),"missing-unit issue missing");
        auto confirmedUnit=associate(rangePath,ParseOptions{.depthUnit="m"});
        require(photo(confirmedUnit).topDepth==201 && photo(confirmedUnit).bottomDepth==202,"confirmed unit did not allow range");

        // 重新导入读取的是缓存副本，原始路径仍提供井名、分组、文件名深度。
        const auto cachedPath=create(directory,QStringLiteral("project/sources/hash/blob.png"));
        ParseOptions cachedOptions;cachedOptions.sourceContextPath=(directory.path()+QStringLiteral("/original/岩心/XJ88-8-B2H1/岩心照片/101-102m.png")).toUtf8().toStdString();
        auto cached=associate(cachedPath,cachedOptions);
        require(cached.wells.size()==1 && cached.wells[0].wellInfo.wellName=="XJ88-8-B2H1","cached source lost original well context");
        require(photo(cached).imagePath==cachedPath && photo(cached).topDepth==101 && photo(cached).bottomDepth==102,"cached source read/logical paths mixed");
        require(cached.wells[0].wellbore.corePhotoTracks[0].trackName.find("sources/hash")==std::string::npos,"cache directory leaked into photo grouping");
        const auto cachedAttachmentPath=create(directory,QStringLiteral("project/sources/hash/document.pdf"),"original");
        cachedOptions.sourceContextPath=(directory.path()+QStringLiteral("/original/岩心/XJ88-8-B1/核磁报告.pdf")).toUtf8().toStdString();
        auto cachedAttachment=associate(cachedAttachmentPath,cachedOptions);
        require(cachedAttachment.wells.size()==1 && cachedAttachment.wells[0].attachments[0].attachmentName=="核磁报告.pdf" && cachedAttachment.wells[0].attachments[0].filePath==cachedAttachmentPath,"cached attachment identity or physical path lost");

        auto point=associate(create(directory,QStringLiteral("岩心/HZ86-6-2d/岩心库拍摄/301.25m-1-裂缝.png")));
        require(photo(point).topDepth==301.25 && photo(point).bottomDepth==301.25,"point depth followed by photo number misread");
        auto pointRange=associate(create(directory,QStringLiteral("岩心/HZ86-6-2d/岩心库拍摄/301.50m-301.75m-1-裂缝.png")));
        require(photo(pointRange).topDepth==301.50 && photo(pointRange).bottomDepth==301.75,"two-unit range failed");
        auto badUnit=associate(create(directory,QStringLiteral("岩心/HZ86-6-2d/岩心照片/301m-302ft.png")));
        require(std::isnan(photo(badUnit).topDepth) && hasIssue(badUnit,"CORE_PHOTO_UNIT_CONFLICT"),"mixed units were accepted");

        auto fluorescent=associate(create(directory,QStringLiteral("岩心/HZ81-1-1d/岩心照片/1-4 荧光.png")));
        require(boxes.wells[0].wellbore.corePhotoTracks[0].trackName!=fluorescent.wells[0].wellbore.corePhotoTracks[0].trackName,"illumination groups merged");
        auto otherDirection=associate(create(directory,QStringLiteral("岩心/HZ81-1-21d井/扫描照片/ygtx/201.00-202.00-1.png")));
        require(unknownUnit.wells[0].wellbore.corePhotoTracks[0].trackName!=otherDirection.wells[0].wellbore.corePhotoTracks[0].trackName,"source view folders merged");

        const auto conflictPath=create(directory,QStringLiteral("岩心/HZ81-1-12/HZ82-2-12井岩心照相/HZ81-1-12 1-4 白光.png"));
        auto conflict=associate(conflictPath);
        require(conflict.wells.empty() && hasIssue(conflict,"CORE_WELL_CONFLICT"),"conflicting path was assigned to well");
        auto overrideWell=associate(conflictPath,ParseOptions{.wellNameOverride="HZ81-1-12"});
        require(overrideWell.wells.size()==1 && hasIssue(overrideWell,"CORE_WELL_OVERRIDE"),"explicit well override failed");

        auto chart=associate(create(directory,QStringLiteral("岩心/HZ83-3-A5P1/岩心观察报告/附图1 岩心柱状图.png")));
        require(chart.wells.size()==1 && chart.wells[0].wellbore.corePhotoTracks.empty() && chart.wells[0].attachments.size()==1,"report chart was classified as core photo");
        require(chart.wells[0].wellInfo.wellName=="HZ83-3-A5P1","letter-prefixed sidetrack name lost");
        auto chartNumber=associate(create(directory,QStringLiteral("岩心/HZ87-7-4Sa/HZ87-7-4sa井岩芯分析-1：50-CORE2-1.png")));
        require(chartNumber.wells.size()==1 && chartNumber.wells[0].wellInfo.wellName=="HZ87-7-4SA","CORE2-1 was mistaken for a well");
        auto logSuffix=associate(create(directory,QStringLiteral("岩心/XJ88-8-B1/XJ88-8-B1_NMR220.pdf"),"original"));
        require(logSuffix.wells.size()==1 && logSuffix.wells[0].wellInfo.wellName=="XJ88-8-B1","log suffix became part of well name");
        auto nmrChannels=associate(create(directory,QStringLiteral("岩心/XJ88-8-B1/XJ88_8_B1_nmr_proc(NMR110_220_250_240).dls"),"original"));
        require(nmrChannels.wells.size()==1 && nmrChannels.wells[0].wellInfo.wellName=="XJ88-8-B1","NMR channels became another well");
        auto multiwell=associate(create(directory,QStringLiteral("岩心/XJ88-8-B1/西江88-8连井.scg"),"original"));
        require(multiwell.wells.empty() && hasIssue(multiwell,"CORE_MULTIWELL_ASSET"),"multiwell section assigned to a single well");
        auto spacedWell=associate(create(directory,QStringLiteral("岩心/XJ88-8-B1/XJ88-8 B1井分析报告.pdf"),"original"));
        require(spacedWell.wells.size()==1 && spacedWell.wells[0].wellInfo.wellName=="XJ88-8-B1","space-delimited well name became a conflict");
        auto fieldName=associate(create(directory,QStringLiteral("岩心/HZ81-1-1d/惠州81-1东区储层报告.pdf"),"original"));
        require(fieldName.wells.size()==1 && fieldName.wells[0].wellInfo.wellName=="HZ81-1-1D","field name was mistaken for a different well");
        auto reportFigure=associate(create(directory,QStringLiteral("岩心/HZ83-3-A5P1/岩心观察报告/image001.png")));
        require(reportFigure.wells[0].wellbore.corePhotoTracks.empty(),"generic report figure became core photo");
        auto datePhoto=associate(create(directory,QStringLiteral("岩心/HZ81-1-25dsa/岩心照片/IMG_20200101_120000.png")));
        require(std::isnan(photo(datePhoto).topDepth),"camera date became depth");
        auto genericImage=associate(create(directory,QStringLiteral("岩心/HZ87-7-1基岩/DSC_0001.png")));
        require(genericImage.wells[0].wellbore.corePhotoTracks.empty(),"unconfirmed root image became core photo");

        auto broken=associate(create(directory,QStringLiteral("岩心/HZ81-1-12/岩心照片/坏照片.png"),"not an image"));
        require(broken.status=="failed" && broken.wells.empty(),"invalid image accepted");
        auto unknownWell=associate(create(directory,QStringLiteral("岩心/待归井/岩心照片/1-4 白光.png")));
        require(unknownWell.wells.empty() && hasIssue(unknownWell,"CORE_WELL_UNKNOWN"),"unknown well generated placeholder");
        auto metadata=associate(create(directory,QStringLiteral("岩心/HZ81-1-12/Thumbs.db"),"index"));
        require(metadata.status=="ignored" && metadata.wells.empty(),"system index became an attachment");

        for(const QString& extension:QStringList{"pdf","cdr","mp4","dlis","rar"}) {
            auto file=associate(create(directory,QStringLiteral("岩心/XJ88-8-B1/原始资料.")+extension,"original bytes, not parsed"));
            require(file.status=="registered" && file.wells.size()==1 && file.wells[0].attachments.size()==1,"external asset registration failed");
            require(file.wells[0].attachments[0].filePath==file.path && file.wells[0].sourceFiles[0]==file.path,"asset provenance lost");
        }
        auto doc=associate(create(directory,QStringLiteral("岩心/西江88-8-B1/报告.docx"),"fixture"));
        require(doc.wells[0].wellInfo.wellName=="XJ88-8-B1","known Chinese well prefix not normalized");
        ParsedFile parsed;parsed.path=create(directory,QStringLiteral("岩心/XJ88-8-B1/正文.docx"),"fixture");parsed.kind="document";parsed.status="parsed";parsed.documentText="原报告文字";
        associateCoreAsset(parsed,{},{});
        require(parsed.documentText=="原报告文字" && parsed.wells.size()==1 && parsed.status=="review","document result overwritten");
        associateCoreAsset(parsed,{},{});require(parsed.wells.size()==1,"association must be idempotent");
        ParsedFile existing;existing.path=parsed.path;existing.status="parsed";existing.wells.push_back(WellDataset{});
        associateCoreAsset(existing,{},{});require(existing.wells.size()==1 && existing.wells[0].attachments.empty(),"typed parser result duplicated");
        ParsedFile conflicted;conflicted.path=parsed.path;conflicted.status="review";
        conflicted.issues.push_back({"well_conflict","Workbook belongs to a different well","","",0,true});
        associateCoreAsset(conflicted,{},{});require(conflicted.wells.empty(),"path association overrode workbook well conflict");

        ParsedFile cancelled;cancelled.path=parsed.path;cancelled.status="registered";
        associateCoreAsset(cancelled,{},[]{return true;});require(cancelled.status=="failed" && cancelled.wells.empty(),"cancellation ignored");
        std::cout<<"Core asset parser tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
