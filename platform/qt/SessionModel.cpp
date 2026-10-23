/*!
 * \file SessionModel.cpp
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents the data for the session list and table
 */

#include "SessionModel.h"
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QMouseEvent>

//Constructor
SessionModel::SessionModel(QObject *parent) : QAbstractItemModel(parent), m_activeRow( -1 )
{
    m_headers = QStringList();
    m_headers << "Name" << "Path" << "Camera" << "Lens" << "Resolution" << "Duration" << "Frames" << "Frame Rate" << "Focal Length" << "Focus Distance" << "Shutter" << "Aperture" << "ISO" << "DualISO" << "Bit Depth" << "Date / Time" << "Audio" << "Size" << "Data Rate" << "星级";
}

//Read data for the session table / list
QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if( role == Qt::DisplayRole )
    {
        //「星级」列：用实心/空心星展示评分
        if( index.column() == COL_RATING )
        {
            const int r = m_dataBase.at( index.row() )->getRating();
            return QString( QString( "★" ).repeated( r ) + QString( "☆" ).repeated( 5 - r ) );
        }
        return m_dataBase.at( index.row() )->getElement( index.column() );
    }
    if( role == ROLE_RATING )
    {
        return m_dataBase.at( index.row() )->getRating();
    }
    if( role == Qt::BackgroundRole )
    {
        return m_dataBase.at( index.row() )->getBackgroundColor();
    }
    if( role == Qt::ForegroundRole )
    {
        if( index.row() == m_activeRow ) return QColor( 255, 154, 50, 255 );
        return QVariant();
    }
    if( role == Qt::ToolTipRole )
    {
        if( index.column() == 0 ) return m_dataBase.at( index.row() )->getPath();
        else return m_dataBase.at( index.row() )->getElement( index.column() );
    }
    if( role == Qt::DecorationRole && index.column() == 0 )
    {
        //底部带「图片 / 视频」角标的缩略图，方便区分素材来源
        return m_dataBase.at( index.row() )->getPreviewBadged();
    }
    if( role == ROLE_REALINDEX )
    {
        return index.row();
    }

    return QVariant();
}

//The whole table is read only
Qt::ItemFlags SessionModel::flags(const QModelIndex &index) const
{
    //「星级」列允许编辑（打分）
    if( index.column() == COL_RATING )
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

//Get the header data for the session table
QVariant SessionModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal)
    {
        if( role == Qt::DisplayRole)
        {
            return m_headers.at(section);
        }
    }
    else
    {
        if( role == Qt::DisplayRole)
        {
            return QVariant();
        }
    }

    return QVariant();
}

//Get the index
QModelIndex SessionModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED( parent );
    return createIndex(row, column);
}

//Get parent
QModelIndex SessionModel::parent(const QModelIndex &index) const
{
    Q_UNUSED( index );
    return QModelIndex();
}

//Read row (clip) count
int SessionModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED( parent );
    return m_dataBase.count();
}

//Read column (number of shown metadata) count
int SessionModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED( parent );
    return m_headers.count();
}

//Set data to the table (Icon, Font color, Background color)
bool SessionModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if( role == Qt::DisplayRole )
    {
        m_dataBase.at( index.row() )->setElement( index.column(), value );
        return true;
    }
    if( role == Qt::BackgroundRole )
    {
        m_dataBase.at( index.row() )->setBackgroundColor( index.column() );
        return true;
    }
    if( role == Qt::DecorationRole )
    {
        m_dataBase.at( index.row() )->setPreview( value.value<QIcon>() );
        return true;
    }
    if( role == ROLE_RATING || ( role == Qt::EditRole && index.column() == COL_RATING ) )
    {
        m_dataBase.at( index.row() )->setRating( value.toInt() );
        emit dataChanged( index, index );
        return true;
    }
    return false;
}

//Show the active clip (clip in editor is marked with orange font)
void SessionModel::setActiveRow(int row)
{
    m_activeRow = row;
}

//Read the number of the active clip
int SessionModel::activeRow()
{
    return m_activeRow;
}

//Get the data for the active clip
ClipInformation *SessionModel::activeClip()
{
    return m_dataBase.at( activeRow() );
}

//Get the data for any clip
ClipInformation *SessionModel::clip(int row)
{
    return m_dataBase.at( row );
}

//Append a new clip to the session
void SessionModel::append(ClipInformation *clip)
{
    beginInsertRows( QModelIndex(), m_dataBase.count(), m_dataBase.count() );
    m_dataBase.push_back( clip );
    endInsertRows();
}

//Delete a clip from the session
void SessionModel::removeRow(int row, const QModelIndex &parent)
{
    beginRemoveRows( parent, row, row );
    m_dataBase.removeAt( row );
    endRemoveRows();
}

//Delete the whole session
void SessionModel::clear()
{
    if( m_dataBase.count() <= 0 ) return;
    beginRemoveRows( QModelIndex(), 0, m_dataBase.count() - 1 );
    m_dataBase.clear();
    endRemoveRows();
}

//Get the receipt for any clip
ReceiptSettings *SessionModel::receipt(int row)
{
    return m_dataBase.at( row )->getReceipt();
}

//Export a csv table of session clips metadata
void SessionModel::writeMetadataToCsv(QString fileName)
{
    //Write file
    QFile data( fileName );
    if( data.open( QFile::WriteOnly | QFile::Truncate ) )
    {
        QTextStream out(&data);

        //Header
        for( int j = 0; j < columnCount( QModelIndex() ); j++ )
        {
            if( j ) out << "\t";
            out << headerData( j, Qt::Horizontal, Qt::DisplayRole ).toString();
        }
        out << '\n';
        //Data
        for( int i = 0; i < rowCount( QModelIndex() ); i++ )
        {
            for( int j = 0; j < columnCount( QModelIndex() ); j++ )
            {
                if( j ) out << "\t";
                out << clip( i )->getElement( j ).toString();
            }
            out << '\n';
        }
        data.close();
    }
}


/* ================= 星级 delegate ================= */

StarRatingDelegate::StarRatingDelegate( QObject *parent ) : QStyledItemDelegate( parent )
{
}

//星级条在列表项里的位置（缩略图右上角，不压文件名）
//注意：IconMode 下 Qt 把文件名渲染在图标下方约 20px，星条必须限制在缩略图区域内，不压文件名。
QRect StarRatingDelegate::previewRect( const QRect &itemRect )
{
    //前 20px 留给文件名行，其余为缩略图区域
    const int textH = 20;
    return itemRect.adjusted( 1, 1, -1, -textH );
}

QRect StarRatingDelegate::starRect( const QRect &itemRect )
{
    const QRect pr = previewRect( itemRect );
    if( pr.height() < 10 || pr.width() < 40 ) return QRect();
    const int h = qBound( 12, pr.height() / 7, 18 );
    const int w = qMin( pr.width() - 4, h * 5 + 8 );
    return QRect( pr.right() - w - 2, pr.top() + 2, w, h );
}

//鼠标落在第几颗星上（1~5），未命中返回 0。清零由 editorEvent 负责（再点一次当前已有星级）。
int StarRatingDelegate::starAt( const QRect &itemRect, const QPoint &pos )
{
    const QRect sr = starRect( itemRect );
    if( !sr.contains( pos ) ) return 0;
    const int starW = sr.width() / 5;
    if( starW <= 0 ) return 0;
    int idx = ( pos.x() - sr.left() ) / starW + 1;
    return qBound( 1, idx, 5 );
}

void StarRatingDelegate::paint( QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index ) const
{
    QStyledItemDelegate::paint( painter, option, index );

    const int rating = index.data( ROLE_RATING ).toInt();
    const QRect sr = starRect( option.rect );
    if( sr.isNull() ) return;

    painter->save();
    //半透明底，保证星星在任何缩略图上都看得清
    painter->setPen( Qt::NoPen );
    painter->setBrush( QColor( 0, 0, 0, 140 ) );
    painter->drawRoundedRect( sr.adjusted( -2, 0, 2, 0 ), 3, 3 );

    QFont f = painter->font();
    f.setPointSizeF( qMax( 7.0, sr.height() * 0.62 ) );
    painter->setFont( f );
    const int starW = sr.width() / 5;
    for( int i = 1; i <= 5; i++ )
    {
        painter->setPen( i <= rating ? QColor( 255, 200, 60 ) : QColor( 200, 200, 200, 170 ) );
        const QRect cell( sr.left() + ( i - 1 ) * starW, sr.top(), starW, sr.height() );
        painter->drawText( cell, Qt::AlignCenter, i <= rating ? QString( "★" ) : QString( "☆" ) );
    }
    painter->restore();
}

bool StarRatingDelegate::editorEvent( QEvent *event, QAbstractItemModel *model,
                                      const QStyleOptionViewItem &option, const QModelIndex &index )
{
    if( event->type() == QEvent::MouseButtonRelease )
    {
        QMouseEvent *me = static_cast<QMouseEvent*>( event );
        if( me->button() == Qt::LeftButton )
        {
            const int hit = starAt( option.rect, me->pos() );
            if( hit > 0 )
            {
                const int cur = index.data( ROLE_RATING ).toInt();
                //点当前已有星级的同一位置 -> 清零（包括5星）；否则设为该星级
                const int next = ( hit == cur ) ? 0 : hit;
                model->setData( index, next, ROLE_RATING );
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent( event, model, option, index );
}

/* ================= 类型筛选代理 ================= */

SessionTypeFilterProxyModel::SessionTypeFilterProxyModel( QObject *parent )
    : QSortFilterProxyModel( parent ), m_filterType( 0 )
{
}

void SessionTypeFilterProxyModel::setFilterType( int t )
{
    if( m_filterType == t ) return;
    m_filterType = t;
    invalidateFilter();
}

int SessionTypeFilterProxyModel::filterType() const
{
    return m_filterType;
}

bool SessionTypeFilterProxyModel::filterAcceptsRow( int sourceRow, const QModelIndex &sourceParent ) const
{
    if( m_filterType == 0 ) return true;

    SessionModel *sm = qobject_cast<SessionModel*>( sourceModel() );
    if( sm == nullptr ) return true;

    ClipInformation *c = sm->clip( sourceRow );
    if( c == nullptr ) return true;

    const bool isImage = c->isImageClip();
    return ( m_filterType == 1 ) ? isImage : !isImage;
}
