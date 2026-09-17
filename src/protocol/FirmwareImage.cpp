#include "FirmwareImage.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QMap>
namespace boot {
QByteArray be32(quint32 n){QByteArray b;for(int i=3;i>=0;--i)b.append(char(n>>(8*i)));return b;}
quint32 readBe(const QByteArray &b){quint32 n=0;for(unsigned char c:b)n=(n<<8)|c;return n;}
quint32 crc32(const QByteArray &b){quint32 c=0xffffffff;for(unsigned char v:b){c^=v;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return c^0xffffffff;}
bool FirmwareImage::load(const QString &path,quint32 base,FirmwareImage &out,QString &error){
    out={};error.clear();FirmwareImage result;QFile file(path);
    auto fail=[&](const QString &why){error=why;return false;};
    if(!file.open(QIODevice::ReadOnly))return fail("Cannot open image: "+file.errorString());
    constexpr qint64 limit=64*1024*1024;
    if(file.size()<=0||file.size()>limit)return fail("Image must contain 1..64 MiB");
    const QByteArray raw=file.readAll();
    if(file.error()!=QFileDevice::NoError)return fail(file.errorString());
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(suffix=="bin"){
        if(quint64(base)+quint64(raw.size())>0x100000000ULL)return fail("BIN address overflow");
        result.segments.append({base,raw});
    }else if(suffix=="hex"){
        QMap<quint32,QByteArray> records;quint64 upper=0;bool eof=false;int lineNo=0;
        for(QByteArray line:raw.split('\n')){
            ++lineNo;while(line.endsWith('\r'))line.chop(1);
            if(line.isEmpty())continue;
            auto bad=[&](const QString &why){return fail(QString("HEX line %1: %2").arg(lineNo).arg(why));};
            if(!line.startsWith(':')||line.size()<11||((line.size()-1)%2))return bad("Invalid record");
            for(int i=1;i<line.size();++i)if(!((line[i]>='0'&&line[i]<='9')||(line[i]>='A'&&line[i]<='F')||(line[i]>='a'&&line[i]<='f')))return bad("Non-hex digit");
            const auto b=QByteArray::fromHex(line.mid(1));const int n=quint8(b[0]);int sum=0;
            if(b.size()!=n+5)return bad("Byte count mismatch");
            for(unsigned char c:b)sum+=c;
            if(sum&255)return bad("Checksum mismatch");
            const quint32 offset=(quint8(b[1])<<8)|quint8(b[2]);const int type=quint8(b[3]);const auto data=b.mid(4,n);
            // Some production linkers append an empty type-08 terminator after the
            // standard EOF record. Accept only that exact, checksum-valid trailer.
            if(eof){if(type==8&&n==0&&!offset)continue;return bad("Data after EOF");}
            if(type==0){
                if(n==0)continue;
                const quint64 start=upper+offset,end=start+n;
                if(offset+quint32(n)>0x10000||end>0x100000000ULL)return bad("Address overflow");
                auto next=records.lowerBound(quint32(start));
                if(next!=records.end()&&quint64(next.key())<end)return bad("Overlapping data");
                if(next!=records.begin()){auto prev=next;--prev;if(quint64(prev.key())+quint64(prev.value().size())>start)return bad("Overlapping data");}
                records.insert(quint32(start),data);
            }else if(type==1){if(n!=0||offset)return bad("Invalid EOF");eof=true;}
            else if(type==2||type==4){if(n!=2||offset)return bad("Invalid extended address");upper=quint64(readBe(data))<<(type==2?4:16);}
            else if(type==3||type==5){if(n!=4||offset)return bad("Invalid entry address");}
            else return bad("Unsupported record type");
        }
        if(!eof)return fail("HEX EOF missing");
        for(auto it=records.cbegin();it!=records.cend();++it){
            if(!result.segments.isEmpty()&&quint64(result.segments.last().address)+quint64(result.segments.last().data.size())==it.key())result.segments.last().data+=it.value();
            else result.segments.append({it.key(),it.value()});
        }
    }else return fail("Only BIN and Intel HEX are supported");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(const auto &s:result.segments){result.size+=s.data.size();hash.addData(be32(s.address));hash.addData(be32(quint32(s.data.size())));hash.addData(s.data);}
    if(!result.size)return fail("Image has no data");
    result.sha256=hash.result();out=std::move(result);return true;
}
}
