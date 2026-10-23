#ifndef IMAGEEXPORTDIALOG_H
#define IMAGEEXPORTDIALOG_H

#include <QDialog>

namespace Ui {
class ImageExportDialog;
}

//「图片导出设置」：分辨率 / 画质 / 格式 / 位深 / 输出目录
class ImageExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageExportDialog( QWidget *parent = nullptr );
    ~ImageExportDialog();

    //格式：0=JPG 1=PNG 2=TIFF 3=DNG(原始 RAW)
    int  imageFormat() const;
    void setImageFormat( int idx );
    //JPG 质量 1..100
    int  jpgQuality() const;
    //缩放百分比：100 / 75 / 50 / 25 / 0(自定义)
    int  scalePercent() const;
    //自定义宽度（scalePercent==0 时生效），0=不缩放
    int  customWidth() const;
    //输出目录
    QString outputDir() const;
    void    setOutputDir( const QString &dir );
    //输出到源素材文件夹（勾选后目录输入框锁定为源目录）
    bool    useSourceDir() const;
    void    setUseSourceDir( bool on );
    //设置"源目录"：勾选时输入框显示它；未勾选时不改动用户已输入的路径
    void    setSourceDir( const QString &dir );
    //勾选状态变化时回调（MainWindow 用它即时同步输出目录）
    void    setSourceDirChangedCallback( const std::function<void( bool )> &cb );

    //单张图片模式（「导出当前帧」用）
    void    setSingleImageMode( bool single );
    bool    isSingleImageMode() const;

    //EXIF 信息保留
    bool    keepExif() const;
    //色彩空间：0=sRGB 1=AdobeRGB 2=原样(不转换)
    int     colorSpace() const;
    //命名规则：0=原文件名 1=原文件名+序号 2=前缀+序号
    int     namingRule() const;
    QString namePrefix() const;
    //数字水印
    bool    watermarkEnabled() const;
    QString watermarkText() const;
    int     watermarkSize() const;
    int     watermarkOpacity() const;
    int     watermarkCorner() const;   //0=左上 1=右上 2=左下 3=右下 4=居中

    //从 QSettings 读 / 写
    void loadFromSettings();
    void saveToSettings();

private slots:
    void on_comboBoxScale_currentIndexChanged( int index );
    void on_checkBoxCustomWidth_toggled( bool checked );
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_browse_clicked();
    void on_comboBoxFormat_currentIndexChanged( int index );
    void on_comboBoxNaming_currentIndexChanged( int index );
    void on_checkBoxWatermark_toggled( bool checked );
    void on_checkBoxUseSourceDir_toggled( bool checked );

protected:
    void changeEvent( QEvent *event );      // 语言切换：重刷对话框文字

private:
    void updateEnabledState();
    void applySourceDirState();

    Ui::ImageExportDialog *ui;
    bool m_singleImageMode;
    QString m_sourceDir;                          //源素材所在目录
    std::function<void( bool )> m_srcDirChanged;  //勾选状态变化回调
};

#endif // IMAGEEXPORTDIALOG_H
