// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include "paleo_data/parser.h"
#include "../vendor/paleo_io/ziparchive.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace paleo::data;
static std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }
static void require(bool value,const std::string& why) {if(!value) throw std::runtime_error(why);}
static const SourceCell* cell(const SourceTable& t,int r,int c) {for(const auto& x:t.cells)if(x.row==r&&x.column==c)return &x;return nullptr;}
static int sampleCount(const ParsedFile& p) {int n=0;for(const auto&w:p.wells)for(const auto&t:w.wellbore.sampleTables)n+=int(t.samples.size());return n;}
static void workbook(const QString& path,const QString& name,const QList<QStringList>& rows) {
 QByteArray xml="<worksheet><sheetData>";int r=0;
 for(const auto& row:rows) {xml+="<row r=\""+QByteArray::number(++r)+"\">";for(const auto& text:row)xml+="<c t=\"inlineStr\"><is><t>"+text.toHtmlEscaped().toUtf8()+"</t></is></c>";xml+="</row>";}
 xml+="</sheetData></worksheet>";QString error;
 require(paleo::data::zip::zipWriteArchive(path,{
  {"xl/workbook.xml","<workbook xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\""+name.toHtmlEscaped().toUtf8()+"\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"},
  {"xl/_rels/workbook.xml.rels","<Relationships><Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\"/></Relationships>"},
  {"xl/worksheets/sheet1.xml",xml}},{},&error),utf8(error));
}
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv);
 try {
  QTemporaryDir temp;
  require(temp.isValid(),"temporary directory");
  const auto malformed=temp.filePath("broken.xlsx");
  {QFile f(malformed);require(f.open(QIODevice::WriteOnly),"write fixture");f.write("not an office archive");}
  auto broken=parseAuxiliaryFile(utf8(malformed),{},{});
  require(broken.status=="failed"&&broken.sourceTables.empty(),"corrupt workbook must fail without partial table");
  require(parseAuxiliaryFile(utf8(malformed),{},[]{return true;}).message=="已取消","cancel before parse");
  QString error;
  const auto doc=temp.filePath("正文.docx");
  const QByteArray xml=QStringLiteral("<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t>HZ84-4-1 100～101m 粒面球藻</w:t></w:r></w:p></w:body></w:document>").toUtf8();
  require(paleo::data::zip::zipWriteArchive(doc,{{"word/document.xml",xml}},{},&error),utf8(error));
  const auto text=parseAuxiliaryFile(utf8(doc),{},{});
  require(text.status=="parsed"&&text.documentText.find("粒面球藻")!=std::string::npos,"DOCX body extraction");
  require(text.wells.empty(),"document captions must not fabricate samples");
  // File-backed Office text extraction must work past the former 512 MiB
  // whole-archive limit, without reading the large sparse prefix into memory.
  auto officeBytes=paleo::data::zip::zipBuildArchive({{"word/document.xml",xml}},{},&error);
  require(!officeBytes.isEmpty(),utf8(error));
  const int eocd=int(officeBytes.size())-22;
  const auto directory=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(officeBytes.constData()+eocd+16));
  constexpr quint32 prefix=513u*1024*1024;
  qToLittleEndian<quint32>(directory+prefix,reinterpret_cast<uchar*>(officeBytes.data()+eocd+16));
  qToLittleEndian<quint32>(prefix,reinterpret_cast<uchar*>(officeBytes.data()+directory+42));
  const auto largeDoc=temp.filePath("large-photo-report.docx");
  {QFile f(largeDoc);require(f.open(QIODevice::WriteOnly)&&f.seek(prefix)&&f.write(officeBytes)==officeBytes.size(),"large sparse Office fixture");}
  const auto largeText=parseAuxiliaryFile(utf8(largeDoc),{},{});
  require(largeText.status=="parsed"&&largeText.documentText==text.documentText,"bounded Office reads beyond 512MiB");
  auto damaged=paleo::data::zip::zipBuildArchive({{"word/document.xml",xml}},{},&error);
  const int damageAt=damaged.indexOf("HZ84-4-1");require(damageAt>=0,"CRC fixture text");damaged[damageAt]='X';
  const auto badDoc=temp.filePath("bad-crc.docx");{QFile f(badDoc);require(f.open(QIODevice::WriteOnly),"CRC fixture file");f.write(damaged);}
  require(parseAuxiliaryFile(utf8(badDoc),{},{}).status=="failed","Office CRC mismatch must remain visible");
  const auto sheet=temp.filePath("HZ99-1-1样品分析.xlsx");
  const QByteArray sheetXml=QStringLiteral("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><dimension ref=\"A1:F30\"/><sheetData><row r=\"2\"><c r=\"B2\" t=\"inlineStr\"><is><t>井深m</t></is></c><c r=\"E2\" t=\"inlineStr\"><is><t>Li ppm</t></is></c></row><row r=\"3\"><c r=\"B3\"><v>100</v></c><c r=\"C3\" t=\"inlineStr\"><is><t>～</t></is></c><c r=\"D3\"><v>101</v></c><c r=\"E3\" t=\"inlineStr\"><is><t>&lt;0.01</t></is></c></row><row r=\"4\"><c r=\"B4\"><v>102</v></c><c r=\"C4\" t=\"inlineStr\"><is><t>～</t></is></c><c r=\"D4\"><v>103</v></c><c r=\"E4\"><v>1.25</v></c></row><row r=\"27\"><c r=\"F27\"><f>1+1</f></c></row></sheetData></worksheet>").toUtf8();
  require(paleo::data::zip::zipWriteArchive(sheet,{
   {"xl/workbook.xml","<workbook xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"样品表\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"},
   {"xl/_rels/workbook.xml.rels","<Relationships><Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\"/></Relationships>"},
   {"xl/worksheets/sheet1.xml",sheetXml}},{},&error),utf8(error));
  const auto synthetic=parseAuxiliaryFile(utf8(sheet),{},{});
  require(synthetic.status=="review"&&sampleCount(synthetic)==2,"structured range workbook");
  const auto& st=synthetic.sourceTables.front();const auto* formula=cell(st,27,6);
  require(st.rowCount==30&&st.columnCount==6&&formula&&formula->text=="=1+1","sparse coordinates and uncached formula preserved");
  const auto& sample=synthetic.wells.front().wellbore.sampleTables.front().samples.front();
  require(sample.topDepth==100&&sample.bottomDepth==101&&sample.measurements.front().originalText=="<0.01"&&std::isnan(sample.measurements.front().numericValue),"range/qualified numeric preservation");
  const auto unknown=temp.filePath("未知资料.xlsx");require(QFile::copy(sheet,unknown),"copy ambiguous fixture");
  const auto ambiguous=parseAuxiliaryFile(utf8(unknown),{},{});
  require(ambiguous.status=="review"&&ambiguous.wells.empty()&&!ambiguous.sourceTables.empty(),"ambiguous workbook preserves raw cells without fabricated sample ownership");
  const auto dev=temp.filePath("HZ83-3-A5P1孔渗分析.xlsx");
  workbook(dev,"孔渗",{{"样品号","井深 ( m )","孔隙度(%)","空气渗透率(mD)"},{"1-01A","100.25","12.5","<0.01"},{"1-02A","100.50","15.25","3.75"}});
  const auto devSamples=parseAuxiliaryFile(utf8(dev),{},{});
  require(sampleCount(devSamples)==2&&devSamples.wells.front().wellInfo.wellName=="HZ83-3-A5P1","complete development-well ID and spaced depth header");
  require(devSamples.wells.front().wellbore.sampleTables.front().samples.front().measurements.back().measurementUnit=="mD","explicit permeability units");
  const auto card=temp.filePath("HZ83-3-A5P1粒度分析.xlsx");
  workbook(card,"粒度",{{"样品号：","1","井深（m）：","100.0"},{"序号","面积频率","序号","面积频率"},{"1","0.1","16","0.2"},{"2","0.3","17","0.4"}});
  require(sampleCount(parseAuxiliaryFile(utf8(card),{},{}))==0,"form-card labels must not turn grain-bin numbers into depths");
  const auto alternating=temp.filePath("HZ83-3-A5P1薄片分析.xlsx");
  workbook(alternating,"薄片",{{"样品号","井深(m)","粒径(mm)","分选性"},{"1","100.0","0.2","好"},{"2","101.0","0.3","中"},{"样品号","井深(m)","定名","特征描述"},{"1","100.0","砂岩","颗粒特征"},{"2","101.0","泥岩","泥质特征"},{"样品号","井深(m)","粒径(mm)","分选性"},{"3","102.0","0.1","好"}});
  require(sampleCount(parseAuxiliaryFile(utf8(alternating),{},{}))==3,"alternating description block must not inherit grain-size headers");
  const auto gamma=temp.filePath("XJ88-8-B1岩心伽马.xlsx");
  workbook(gamma,"数据",{{"Depth","Kfraction","Uppm","THppm","TotalGamAPI","UranFreeGamAPI"},{"100","0","0","0","35.2","0"},{"99.99","0","0","0","-2.0","0"}});
  const auto gammaData=parseAuxiliaryFile(utf8(gamma),{},{});
  require(gammaData.wells.size()==1&&gammaData.wells.front().wellInfo.wellName=="XJ88-8-B1","B1 gamma ownership");
  const auto& gammaSet=gammaData.wells.front().wellbore.logSets.front();
  require(gammaSet.logCurves.size()==5&&gammaSet.samplingAxis.depthValues.size()==2&&gammaSet.logCurves[3].sampleValues[1]==-2.0,"gamma arrays preserve values and descending depth order");
  require(gammaSet.depthInfo.depthUnit.empty()&&gammaSet.logCurves[3].curveUnit=="API","gamma explicit units only");
  const auto survey=temp.filePath("XJ88-8-B1-Survey.xlsx");
  workbook(survey,"测斜",{{"MD\n(m)","Incl\n(°)","Azim Grid\n(°)"},{"0","0","300"},{"1","0.1","200"},{"2","181","1"}});
  const auto surveyData=parseAuxiliaryFile(utf8(survey),{},{});
  require(surveyData.wells.size()==1&&surveyData.wells.front().wellbore.trajectories.size()==1,"survey mapping");
  require(surveyData.wells.front().wellbore.trajectories.front().surveyPoints.size()==2&&surveyData.wells.front().wellbore.trajectories.front().azimuthReference=="grid north","invalid survey angle excluded with original retained");
  require(surveyData.sourceTables.front().cells.size()==12&&surveyData.status=="review","invalid survey retains every original cell and reports review");
  const auto conflictCurve=temp.filePath("XJ88-8-B1岩心伽马冲突.xlsx");
  workbook(conflictCurve,"数据",{{"井号：HZ83-3-A5P1",""},{"Depth","TotalGamAPI"},{"100","20"},{"101","21"}});
  const auto conflictCurveData=parseAuxiliaryFile(utf8(conflictCurve),{},{});
  require(conflictCurveData.status=="review"&&conflictCurveData.wells.empty()&&conflictCurveData.sourceTables.front().cells.size()==7,"conflicting curve identity must retain raw cells without attaching any well object");
  const auto conflictSamples=temp.filePath("HZ83-3-A5P1孔渗冲突.xlsx");
  workbook(conflictSamples,"分析",{{"井号","井深(m)","孔隙度(%)"},{"XJ88-8-B1","100","10"},{"XJ88-8-B1","101","11"}});
  const auto conflictSampleData=parseAuxiliaryFile(utf8(conflictSamples),{},{});
  require(conflictSampleData.status=="review"&&conflictSampleData.wells.empty()&&conflictSampleData.sourceTables.front().cells.size()==9,"conflicting sample identity must retain all original cells without typed ownership");
  ParseOptions confirmedWell;confirmedWell.wellNameOverride="XJ88-8-B1";
  const auto confirmedSamples=parseAuxiliaryFile(utf8(conflictSamples),confirmedWell,{});
  require(sampleCount(confirmedSamples)==2&&confirmedSamples.wells.front().wellInfo.wellName=="XJ88-8-B1","explicitly confirmed well restores typed samples on reparse");
  if(argc<2) {std::cout<<"aux fixture checks passed (real directory not supplied)\n";return 0;}
  const auto root=app.arguments()[1];
  require(QFileInfo(root).isDir(),"real data directory missing");
  QDirIterator files(root,QDir::Files,QDirIterator::Subdirectories);
  int workbooks=0,sheets=0,emptyWorkbooks=0,nonemptySheets=0,samples=0,failed=0,officeFailed=0,totalSourceCells=0;
  QJsonArray report;
  while(files.hasNext()) {
   const auto path=files.next();const auto info=files.fileInfo();
   if(info.fileName().startsWith("._")||info.fileName().startsWith("~$")||info.fileName().startsWith(".~"))continue;
   const auto ext=info.suffix().toLower();
   if(ext!="xls"&&ext!="xlsx"&&ext!="docx"&&ext!="pptx") continue;
   const auto p=parseAuxiliaryFile(utf8(path),{},{});
   int nonempty=0,cellCount=0;
   for(const auto&t:p.sourceTables) {
    if(!t.cells.empty()) ++nonempty;
    cellCount+=int(t.cells.size());
    for(const auto&c:t.cells) require(c.row>0&&c.column>0&&c.row<=t.rowCount&&c.column<=t.columnCount,"source cell physical index invalid");
   }
   QJsonArray issues;for(const auto&i:p.issues)issues.append(QString::fromStdString(i.code));
   if(issues.contains("well_conflict")) require(p.wells.empty()&&!p.sourceTables.empty(),"every workbook well conflict must withhold typed well objects while preserving source tables");
   QJsonObject record{{"path",path},{"status",QString::fromStdString(p.status)},{"message",QString::fromStdString(p.message)},{"sheets",int(p.sourceTables.size())},{"cells",cellCount},{"samples",sampleCount(p)},{"issues",issues}};
   QJsonArray wells;int curveCount=0,curvePoints=0,surveyPoints=0,intervals=0;
   for(const auto&w:p.wells) {
    wells.append(QString::fromStdString(w.wellInfo.wellName));
    for(const auto&l:w.wellbore.logSets){curveCount+=int(l.logCurves.size());curvePoints+=int(l.samplingAxis.depthValues.size()*l.logCurves.size());}
    for(const auto&s:w.wellbore.trajectories)surveyPoints+=int(s.surveyPoints.size());
    for(const auto&t:w.wellbore.intervalTracks)intervals+=int(t.intervals.size());
   }
   record["wellNames"]=wells;record["curves"]=curveCount;record["curvePoints"]=curvePoints;record["surveyPoints"]=surveyPoints;record["intervals"]=intervals;
   if(argc>=4) {QJsonArray tables;for(const auto&t:p.sourceTables) {QJsonArray cells;for(const auto&c:t.cells) cells.append(QJsonArray{c.row,c.column,QString::fromStdString(c.text)});tables.append(QJsonObject{{"name",QString::fromStdString(t.sheetName)},{"rows",t.rowCount},{"columns",t.columnCount},{"cells",cells}});}record["sourceTables"]=tables;}
   report.append(record);
   if(ext=="xls"||ext=="xlsx") {
    ++workbooks;sheets+=int(p.sourceTables.size());nonemptySheets+=nonempty;samples+=sampleCount(p);totalSourceCells+=cellCount;
    if(!nonempty)++emptyWorkbooks;
    if(p.status=="failed") {++failed;std::cerr<<utf8(path)<<": "<<p.message<<"\n";continue;}
    require(!p.sourceTables.empty(),"workbook has no sheet metadata");
   } else {
    if(info.size()==0) {require(p.status=="failed","empty Office file must fail explicitly");continue;}
    if(p.status=="failed") {++officeFailed;std::cerr<<utf8(path)<<": "<<p.message<<"\n";continue;}
   }
  }
  if(argc>=3) {QFile f(app.arguments()[2]);require(f.open(QIODevice::WriteOnly),"write report");f.write(QJsonDocument(report).toJson());}
  std::cout<<"workbooks="<<workbooks<<" sheets="<<sheets<<" nonemptySheets="<<nonemptySheets<<" noCellWorkbooks="<<emptyWorkbooks<<" sourceCells="<<totalSourceCells<<" samples="<<samples<<" failures="<<failed<<"\n";
  require(failed==0,"one or more real workbooks failed");
  require(officeFailed==0,"one or more real Office text files failed");
  std::cout<<"aux parser checks passed\n";
  return 0;
 } catch(const std::exception&e) {std::cerr<<e.what()<<"\n";return 1;}
}
