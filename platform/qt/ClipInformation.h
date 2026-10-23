/*!
 * \file ClipInformation.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents one clip in the session: metadata + receipt
 */

#ifndef CLIPINFORMATION_H
#define CLIPINFORMATION_H

#include <QObject>
#include <QColor>
#include <QIcon>
#include "ReceiptSettings.h"

class ClipInformation
{
public:
    ClipInformation(QString name, QString path, QString camera, QString lens, QString resolution, QString duration, QString frames, QString frameRate, QString focalLength, QString focusDistance, QString shutter, QString aperture, QString iso, QString dualIso, QString bitDepth, QString dateTime, QString audio, QColor backgroundColor);
    ClipInformation(QString name, QString path);

    ~ClipInformation();

    void setPathName( QString name, QString path );

    QVariant getElement( int element ) const;

    void setElement( int element, QVariant value );

    QString getName() const {
        return m_name;
    }

    QString getPath() const {
        return m_path;
    }

    QColor getBackgroundColor() const {
        return m_backgroundColor;
    }

    void setBackgroundColor( QColor color ) {
        m_backgroundColor = color;
    }

    QIcon getPreview() const {
        return m_preview;
    }

    void setPreview( QIcon preview ) {
        m_preview = preview;
        rebuildPreviewBadge();
    }

    //素材来源类型：image = 由 CR2/DNG 照片转码而来，video = 原生 MLV 视频
    QString getSourceType() const {
        return m_sourceType;
    }

    void setSourceType( QString t ) {
        if( m_sourceType == t ) return;
        m_sourceType = t;
        rebuildPreviewBadge();
    }

    bool isImageClip() const {
        return m_sourceType == QString( "image" );
    }

    //0~5 星评分，0 = 未评分
    int getRating() const {
        return m_rating;
    }

    void setRating( int r ) {
        m_rating = qBound( 0, r, 5 );
    }

    //缩略图（底部带「图片 / 视频」角标），供素材池显示
    QIcon getPreviewBadged() const {
        return m_previewBadged.isNull() ? m_preview : m_previewBadged;
    }

    ReceiptSettings* getReceipt() const {
        return m_pReceipt;
    }

    void updateMetadata(QString camera, QString lens, QString resolution, QString duration, QString frames, QString frameRate, QString focalLength, QString focusDistance, QString shutter, QString aperture, QString iso, QString dualIso, QString bitDepth, QString dateTime, QString audio);

private:
    quint64 clipSize() const;

    QString m_name;
    QString m_path;
    QString m_camera;
    QString m_lens;
    QString m_resolution;
    QString m_duration;
    QString m_frames;
    QString m_frameRate;
    QString m_focalLength;
    QString m_focusDistance;
    QString m_shutter;
    QString m_aperture;
    QString m_iso;
    QString m_dualIso;
    QString m_bitDepth;
    QString m_dateTime;
    QString m_audio;
    void rebuildPreviewBadge( void );

    QColor m_backgroundColor;
    QIcon m_preview;
    QIcon m_previewBadged;
    QString m_sourceType;
    int m_rating;
    ReceiptSettings *m_pReceipt;
};

#endif // CLIPINFORMATION_H
