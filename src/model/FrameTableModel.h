#pragma once
#include <QAbstractTableModel>
#include "domain/HostTypes.h"
namespace host {
class FrameTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    static constexpr int Capacity=10000;
    explicit FrameTableModel(QObject *parent=nullptr):QAbstractTableModel(parent){}
    int rowCount(const QModelIndex &p={}) const override {return p.isValid()?0:m_rows.size();}
    int columnCount(const QModelIndex &p={}) const override {return p.isValid()?0:8;}
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole) const override;
    bool exportCsv(const QString &,QString &error) const;
public slots:
    void append(const host::FrameBatch &);
    void clear();
private:
    FrameBatch m_rows;
    qint64 m_relativeOriginUs=0;
    bool m_hasRelativeOrigin=false;
};
}
