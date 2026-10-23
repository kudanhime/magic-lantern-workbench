/*!
 * \file SessionModel.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents the data for the session list and table
 */

#ifndef SESSIONMODEL_H
#define SESSIONMODEL_H

#define ROLE_REALINDEX  Qt::UserRole + 1
#define ROLE_RATING     Qt::UserRole + 2   //星级（0~5）
#define COL_RATING      19                 //素材池表格里的「星级」列

#include <QObject>
#include <QAbstractItemModel>
#include <QStyledItemDelegate>
#include <QSortFilterProxyModel>
#include "ClipInformation.h"

class SessionModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    SessionModel( QObject *parent );
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
    void setActiveRow( int row );
    int activeRow( void );
    ClipInformation *activeClip( void );
    ClipInformation *clip( int row );
    void append(ClipInformation *clip );
    void removeRow(int row, const QModelIndex &parent);
    void clear( void );
    ReceiptSettings *receipt(int row);
    void writeMetadataToCsv( QString fileName );

private:
    QStringList m_headers;
    QList<ClipInformation*> m_dataBase;
    int m_activeRow;
};

/*! 素材池列表项的星级交互：缩略图右上角画 5 颗星，点击第 n 颗即设为 n 星（再点一次当前同分 = 清零）。 */
class StarRatingDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit StarRatingDelegate( QObject *parent = nullptr );

    void paint( QPainter *painter, const QStyleOptionViewItem &option,
                const QModelIndex &index ) const override;
    bool editorEvent( QEvent *event, QAbstractItemModel *model,
                      const QStyleOptionViewItem &option, const QModelIndex &index ) override;

    static QRect previewRect( const QRect &itemRect );   //缩略图区域（扣除底部约20px文件名行）
    static QRect starRect( const QRect &itemRect );      //缩略图右上角的星级条
    static int   starAt( const QRect &itemRect, const QPoint &pos );   //返回 1~5，0 = 未命中
};

/*! 素材池类型筛选代理：0 = 全部，1 = 只看图片，2 = 只看视频。 */
class SessionTypeFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit SessionTypeFilterProxyModel( QObject *parent = nullptr );

    void setFilterType( int t );
    int  filterType() const;

protected:
    bool filterAcceptsRow( int sourceRow, const QModelIndex &sourceParent ) const override;

private:
    int m_filterType;   //0=全部 1=只看图片 2=只看视频
};

#endif // SESSIONMODEL_H
