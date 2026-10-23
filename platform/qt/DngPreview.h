/*!
 * \file DngPreview.h
 * \brief Shared helper: build the baseline JPEG preview (thumbnail) that is
 *        embedded into MLVApp's DNG exports by dngSetPreview()/saveDngFrame().
 *
 * Zero new dependencies: the pixels come from the same fully processed 8 bit
 * RGB frame the single frame JPG export writes (getMlvProcessedFrame8()), they
 * are downscaled with the same avir resizer the JPG export uses and encoded by
 * Qt's bundled JPG plugin (imageformats/qjpeg.dll).
 *
 * Used by: platform/qt/SingleFrameExportDialog.cpp (single frame DNG export)
 *          and by the standalone regression test build/cli/e2_dng_export.cpp.
 */

#ifndef MLVAPP_DNG_PREVIEW_H
#define MLVAPP_DNG_PREVIEW_H

#include <QByteArray>
#include <QBuffer>
#include <QImage>
#include <QThread>

#include "../../src/mlv_include.h"
#include "avir/avirthreadpool.h"

/*! long edge of the embedded preview in pixels (D2 prototype used 1024) */
#define DNG_PREVIEW_LONG_EDGE 1024
/*! JPEG quality of the embedded preview */
#define DNG_PREVIEW_JPEG_QUALITY 85

/*!
 * Compute the pixel dimensions the embedded preview will have for \a mlvObject:
 * the long edge is scaled down to DNG_PREVIEW_LONG_EDGE (never upscaled). These
 * dimensions are known without rendering anything, which is what lets the DNG
 * export reserve the preview SubIFD *before* the preview is rendered (dng.h).
 */
static inline void dngPreviewDimensions( mlvObject_t *mlvObject, int &outWidth, int &outHeight )
{
    outWidth = 0;
    outHeight = 0;
    if( mlvObject == NULL ) return;

    const int srcW = getMlvWidth( mlvObject );
    const int srcH = getMlvHeight( mlvObject );
    if( srcW <= 0 || srcH <= 0 ) return;

    const int longEdge = qMax( srcW, srcH );
    double scale = 1.0;
    if( longEdge > DNG_PREVIEW_LONG_EDGE ) scale = ( double )DNG_PREVIEW_LONG_EDGE / ( double )longEdge;

    outWidth  = qMax( 1, ( int )( ( double )srcW * scale + 0.5 ) );
    outHeight = qMax( 1, ( int )( ( double )srcH * scale + 0.5 ) );
}

/*!
 * Render frame \a frameNr of \a mlvObject into a baseline JPEG preview.
 * \param outJpeg     receives the JPEG bytes (cleared on failure)
 * \param outWidth    receives the preview width in pixels
 * \param outHeight   receives the preview height in pixels
 * \return true on success
 */
static inline bool dngMakeJpegPreview( mlvObject_t *mlvObject, int frameNr,
                                       QByteArray &outJpeg, int &outWidth, int &outHeight )
{
    outJpeg.clear();
    outWidth = 0;
    outHeight = 0;

    if( mlvObject == NULL ) return false;

    const int srcW = getMlvWidth( mlvObject );
    const int srcH = getMlvHeight( mlvObject );
    if( srcW <= 0 || srcH <= 0 ) return false;

    //Scale so that the long edge becomes DNG_PREVIEW_LONG_EDGE (never upscale);
    //the very same helper the DNG export uses to reserve the preview IFD up front
    int dstW = 0;
    int dstH = 0;
    dngPreviewDimensions( mlvObject, dstW, dstH );
    if( dstW <= 0 || dstH <= 0 ) return false;
    const double scale = ( double )dstW / ( double )srcW;

    //Same source image as the JPG export channel (grading / WB / dual ISO applied)
    uint8_t *srcBuffer = ( uint8_t* )malloc( ( size_t )srcW * ( size_t )srcH * 3 );
    if( srcBuffer == NULL ) return false;
    getMlvProcessedFrame8( mlvObject, frameNr, srcBuffer, QThread::idealThreadCount() );

    uint8_t *dstBuffer = ( uint8_t* )malloc( ( size_t )dstW * ( size_t )dstH * 3 );
    if( dstBuffer == NULL )
    {
        free( srcBuffer );
        return false;
    }

    if( scale == 1.0 )
    {
        memcpy( dstBuffer, srcBuffer, ( size_t )srcW * ( size_t )srcH * 3 );
    }
    else
    {
        avir_scale_thread_pool scaling_pool;
        avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
        avir::CImageResizerParamsUltra roptions;
        avir::CImageResizer<> image_resizer( 8, 0, roptions );
        image_resizer.resizeImage( srcBuffer, srcW, srcH, 0,
                                   dstBuffer, dstW, dstH, 3, 0, &vars );
    }

    QImage preview( ( const uchar* )dstBuffer, dstW, dstH, dstW * 3, QImage::Format_RGB888 );
    QBuffer buffer( &outJpeg );
    buffer.open( QIODevice::WriteOnly );
    const bool ok = preview.save( &buffer, "JPG", DNG_PREVIEW_JPEG_QUALITY );
    buffer.close();

    free( dstBuffer );
    free( srcBuffer );

    if( !ok || outJpeg.isEmpty() ) return false;

    outWidth = dstW;
    outHeight = dstH;
    return true;
}

#endif // MLVAPP_DNG_PREVIEW_H
