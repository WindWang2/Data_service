#include "xls_formula_cache.h"
#include <QFile>
#include <QMap>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Format references (cached values only, no formula execution):
// https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-cfb/05060311-bfce-4b12-874d-71fd4ce63aea
// https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-xls/8e3c6978-6c9f-4915-a826-07613204b244
// https://learn.microsoft.com/en-us/openspecs/office_file_formats/ms-xls/39a0757a-c7bb-4e85-b144-3e7837b059d7

namespace paleo::data {
namespace {
quint16 u16(const QByteArray& b,qsizetype p) {return qFromLittleEndian<quint16>(b.constData()+p);}
quint32 u32(const QByteArray& b,qsizetype p) {return qFromLittleEndian<quint32>(b.constData()+p);}
quint64 u64(const QByteArray& b,qsizetype p) {return qFromLittleEndian<quint64>(b.constData()+p);}
constexpr quint32 End=0xfffffffeU,Free=0xffffffffU;
constexpr qsizetype Limit=64*1024*1024;
bool compoundStream(const QByteArray& file,QByteArray& workbook,QString& error) {
 const auto fail=[&](const char* e){error=QString::fromUtf8(e);return false;};
 if(file.size()<512||file.left(8)!=QByteArray::fromHex("d0cf11e0a1b11ae1"))return fail("XLS 缓存补读不支持非复合文档容器");
 const int shift=u16(file,30),miniShift=u16(file,32);
 if((shift!=9&&shift!=12)||miniShift!=6||u16(file,28)!=0xfffe)return fail("XLS 复合文档扇区格式无效");
 const int sectorSize=1<<shift,miniSize=1<<miniShift;
 const quint32 sectorCount=quint32(file.size()/sectorSize-1);
 auto sector=[&](quint32 id)->QByteArray {if(id>=sectorCount)return {};return file.mid(qsizetype(id+1)*sectorSize,sectorSize);};
 QVector<quint32> fatSectors;
 for(int i=0;i<109;++i) {auto id=u32(file,76+4*i);if(id!=Free)fatSectors.push_back(id);}
 quint32 difat=u32(file,68);QSet<quint32> visited;
 const auto difatCount=u32(file,72);
 if(difatCount>sectorCount)return fail("XLS DIFAT 数量无效");
 for(quint32 i=0;i<difatCount;++i) {
  if(visited.contains(difat))return fail("XLS DIFAT 存在循环");visited.insert(difat);
  const auto b=sector(difat);if(b.size()!=sectorSize)return fail("XLS DIFAT 越界");
  for(int n=0;n<sectorSize/4-1;++n){auto id=u32(b,n*4);if(id!=Free)fatSectors.push_back(id);}
  difat=u32(b,sectorSize-4);
 }
 if(fatSectors.size()!=u32(file,44))return fail("XLS FAT 数量不一致");
 QVector<quint32> fat;
 for(auto id:fatSectors) {const auto b=sector(id);if(b.size()!=sectorSize)return fail("XLS FAT 越界");for(int n=0;n<sectorSize;n+=4)fat.push_back(u32(b,n));}
 auto chain=[&](quint32 first,const QVector<quint32>& table,const QByteArray* mini,QByteArray& result)->bool {
  QSet<quint32> seen;auto id=first;result.clear();
  while(id!=End&&id!=Free) {
   if(id>=quint32(table.size())||seen.contains(id))return fail("XLS 数据链越界或循环");seen.insert(id);
   QByteArray part;
   if(mini){const quint64 off=quint64(id)*miniSize;if(off+miniSize>quint64(mini->size()))return fail("XLS 小扇区越界");part=mini->mid(qsizetype(off),miniSize);}
   else part=sector(id);
   if(part.isEmpty()||result.size()+part.size()>Limit)return fail("XLS 数据链超出读取预算");
   result+=part;id=table[id];
  }
  return true;
 };
 QByteArray directory;if(!chain(u32(file,48),fat,nullptr,directory))return false;
 quint32 first=End,rootFirst=End;quint64 size=0,rootSize=0;
 for(qsizetype at=0;at+128<=directory.size();at+=128) {
  const auto nameBytes=u16(directory,at+64);const auto type=quint8(directory[at+66]);
  if(nameBytes<2||nameBytes>64||nameBytes%2)continue;
  QString name;for(int j=0;j<int(nameBytes)-2;j+=2)name+=QChar(u16(directory,at+j));
  quint64 bytes=u64(directory,at+120);if(shift==9)bytes&=0xffffffffULL;
  if(type==5){rootFirst=u32(directory,at+116);rootSize=bytes;}
  if(type==2&&(name.compare("Workbook",Qt::CaseInsensitive)==0||name.compare("Book",Qt::CaseInsensitive)==0)){first=u32(directory,at+116);size=bytes;}
 }
 if(first==End||size>quint64(Limit))return fail("XLS Workbook 流不存在或超出预算");
 if(size<u32(file,56)) {
  QByteArray miniFatBytes,root;
  if(rootSize>quint64(Limit)||!chain(u32(file,60),fat,nullptr,miniFatBytes)||!chain(rootFirst,fat,nullptr,root))return false;
  root.truncate(qsizetype(rootSize));QVector<quint32> miniFat;
  for(qsizetype i=0;i+4<=miniFatBytes.size();i+=4)miniFat.push_back(u32(miniFatBytes,i));
  if(!chain(first,miniFat,&root,workbook))return false;
 } else if(!chain(first,fat,nullptr,workbook))return false;
 if(quint64(workbook.size())<size)return fail("XLS Workbook 流被截断");
 workbook.truncate(qsizetype(size));return true;
}
QString errorValue(unsigned char c) {
 switch(c){case 0:return "#NULL!";case 7:return "#DIV/0!";case 15:return "#VALUE!";case 23:return "#REF!";case 29:return "#NAME?";case 36:return "#NUM!";case 42:return "#N/A";default:return "#ERROR!";}
}
}
bool supplementXlsFormulaCells(const QString& path,ParsedFile& out,const CancelCheck& cancel,QString& error) {
 QFile input(path);if(!input.open(QIODevice::ReadOnly)){error=input.errorString();return false;}
 if(input.size()>Limit){error="XLS 超过缓存补读预算";return false;}
 const auto source=input.readAll();QByteArray workbook;
 if(!compoundStream(source,workbook,error))return false;
 QVector<quint32> starts;
 for(qsizetype at=0;at+4<=workbook.size();) {
  const auto type=u16(workbook,at),len=u16(workbook,at+2);if(at+4+len>workbook.size()){error="XLS BIFF 记录截断";return false;}
  if(type==0x0085&&len>=8)starts.push_back(u32(workbook,at+4));
  at+=4+len;if(type==0x000a)break;
 }
 if(starts.size()!=qsizetype(out.sourceTables.size())){error="XLS 工作表缓存与主解析器数量不一致";return false;}
 for(qsizetype s=0;s<starts.size();++s) {
  if(cancel&&cancel()){error="已取消";return false;}
  auto& table=out.sourceTables[size_t(s)];QMap<quint64,QString> cached;int pendingRow=0,pendingCol=0;
  for(qsizetype at=starts[s];at+4<=workbook.size();) {
   const auto type=u16(workbook,at),len=u16(workbook,at+2);const auto p=at+4;
   if(p+len>workbook.size()){error="XLS 工作表缓存被截断";return false;}
   if(type==0x0006&&len>=20) {
    const int row=int(u16(workbook,p))+1,col=int(u16(workbook,p+2))+1;QString value;
    if(u16(workbook,p+12)==0xffff) {
     const auto kind=quint8(workbook[p+6]),v=quint8(workbook[p+8]);
     if(kind==0){pendingRow=row;pendingCol=col;}
     else if(kind==1)value=v?"TRUE":"FALSE";
     else if(kind==2)value=errorValue(v);
    } else {const auto bits=u64(workbook,p+6);double d;std::memcpy(&d,&bits,sizeof d);if(std::isfinite(d))value=QString::number(d,'g',17);}
    if(!value.isEmpty())cached[(quint64(row)<<32)|quint32(col)]=value;
   } else if(type==0x0207&&pendingRow&&len>=3) {
    const int chars=u16(workbook,p);const auto flags=quint8(workbook[p+2]);int offset=3;
    if(flags&8)offset+=2;if(flags&4)offset+=4;const int width=(flags&1)?2:1;
    if(offset+chars*width>len){error="XLS 跨记录公式文本缓存尚不支持";return false;}
    QString value;for(int n=0;n<chars;++n)value+=QChar(width==2?u16(workbook,p+offset+n*2):quint8(workbook[p+offset+n]));
    if(!value.isEmpty())cached[(quint64(pendingRow)<<32)|quint32(pendingCol)]=value;pendingRow=0;pendingCol=0;
   }
   at=p+len;if(type==0x000a)break;
  }
  QSet<quint64> present;for(const auto& c:table.cells)present.insert((quint64(c.row)<<32)|quint32(c.column));
  for(auto it=cached.cbegin();it!=cached.cend();++it)if(!present.contains(it.key())) {
   const int row=int(it.key()>>32),col=int(quint32(it.key()));
   table.cells.push_back({row,col,it.value().toUtf8().toStdString()});table.rowCount=std::max(table.rowCount,row);table.columnCount=std::max(table.columnCount,col);
  }
  std::sort(table.cells.begin(),table.cells.end(),[](const SourceCell&a,const SourceCell&b){return a.row==b.row?a.column<b.column:a.row<b.row;});
 }
 return true;
}
}
