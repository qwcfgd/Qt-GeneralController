#include "FrameTableModel.h"
#include <QColor>
#include <QDateTime>
#include <QRegularExpression>
#include <QSaveFile>
namespace host {
QVariant FrameTableModel::data(const QModelIndex &i,int role) const {
    if(!i.isValid() || i.row()<0 || i.row()>=m_rows.size() || i.column()<0 || i.column()>=8)return {};
    const auto &r=m_rows[i.row()];
    if(role==Qt::ForegroundRole)return QColor(r.error?"#C93838":(r.warning?"#9A6700":"#29425D"));
    if(role==Qt::TextAlignmentRole)return int(Qt::AlignCenter);
    if(role==Qt::ToolTipRole || role==Qt::DisplayRole){
        switch(i.column()){case 0:return r.timestamp;case 1:return r.relativeTime;case 2:return r.channel;case 3:return r.direction;
        case 4:return r.identifier;case 5:return r.length;case 6:return r.data;case 7:return r.status;}
    }return {};
}
QVariant FrameTableModel::headerData(int section,Qt::Orientation o,int role) const {
    const QStringList names={"绝对时间","相对时间 / ms","软件通道","方向","ID","长度","数据","状态"};
    return o==Qt::Horizontal && role==Qt::DisplayRole && section>=0 && section<names.size()?names[section]:QVariant();
}
void FrameTableModel::append(const FrameBatch &input) {
    FrameBatch batch=input;if(batch.isEmpty())return;
    for(auto &record:batch){
        bool ok=false;qint64 rawUs=record.relativeTime.toLongLong(&ok);
        if(!ok)rawUs=record.timestamp.toLongLong(&ok);
        if(ok){
            if(!m_hasRelativeOrigin){m_relativeOriginUs=rawUs;m_hasRelativeOrigin=true;}
            record.relativeTime=QString::number((rawUs-m_relativeOriginUs)/1000.0,'f',3);
            if(!QRegularExpression("^\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$").match(record.timestamp).hasMatch())
                record.timestamp=QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        }
    }
    if(batch.size()>Capacity)batch=batch.mid(batch.size()-Capacity);
    const int remove=qMax(0,int(m_rows.size()+batch.size())-Capacity);
    if(remove){beginRemoveRows({},0,remove-1);m_rows.remove(0,remove);endRemoveRows();}
    beginInsertRows({},m_rows.size(),m_rows.size()+batch.size()-1);m_rows+=batch;endInsertRows();
}
void FrameTableModel::clear(){beginResetModel();m_rows.clear();m_relativeOriginUs=0;m_hasRelativeOrigin=false;endResetModel();}
bool FrameTableModel::exportCsv(const QString &path,QString &error) const {
    error.clear();QByteArray bytes("\xEF\xBB\xBF");
    const auto escaped=[](QString s){
        if(!s.isEmpty() && QString("=+-@").contains(s.front()))s.prepend('\'');
        s.replace('"',"\"\"");return "\""+s+"\"";
    };
    for(int row=-1;row<rowCount();++row){
        QStringList cells;for(int c=0;c<8;++c)cells<<escaped(row<0?headerData(c,Qt::Horizontal).toString():data(index(row,c)).toString());
        bytes+=(cells.join(',')+"\r\n").toUtf8();
    }
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()){
        error="报文导出失败。";return false;
    }return true;
}
}
