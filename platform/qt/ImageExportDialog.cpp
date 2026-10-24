#include "ImageExportDialog.h"
#include "ui_ImageExportDialog.h"

#include <QSettings>
#include <QFileDialog>
#include <QSlider>
#include <QDir>
#include <functional>

//QSettings 键（与图片导出流程共用）
static const char *S_KEY_FORMAT  = "imgExportFormat";
static const char *S_KEY_QUALITY = "imgExportQuality";
static const char *S_KEY_SCALE   = "imgExportScale";
static const char *S_KEY_CWIDTH  = "imgExportCustomWidth";
static const char *S_KEY_DIR     = "imgExportDir";

ImageExportDialog::ImageExportDialog( QWidget *parent )
    : QDialog( parent )
    , ui( new Ui::ImageExportDialog )
{
    ui->setupUi( this );

    connect( ui->comboBoxScale, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( on_comboBoxScale_currentIndexChanged( int ) ) );
    connect( ui->buttonBox, SIGNAL( accepted() ), this, SLOT( on_buttonBox_accepted() ) );
    connect( ui->buttonBox, SIGNAL( rejected() ), this, SLOT( on_buttonBox_rejected() ) );
    connect( ui->toolButtonBrowse, SIGNAL( clicked() ), this, SLOT( on_browse_clicked() ) );
    connect( ui->comboBoxFormat, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( on_comboBoxFormat_currentIndexChanged( int ) ) );
    connect( ui->comboBoxNaming, SIGNAL( currentIndexChanged( int ) ),
             this, SLOT( on_comboBoxNaming_currentIndexChanged( int ) ) );
    connect( ui->groupBoxWatermark, SIGNAL( toggled( bool ) ),
             this, SLOT( on_checkBoxWatermark_toggled( bool ) ) );
    connect( ui->checkBoxUseSourceDir, SIGNAL( toggled( bool ) ),
             this, SLOT( on_checkBoxUseSourceDir_toggled( bool ) ) );

    m_singleImageMode = false;
    m_sourceDir.clear();

    //画质滑块数值实时显示
    connect( ui->horizontalSliderQuality, &QSlider::valueChanged, this, [ this ]( int v )
    {
        ui->labelQualityValue->setText( QString::number( v ) );
    } );

    loadFromSettings();
    updateEnabledState();
}

ImageExportDialog::~ImageExportDialog()
{
    delete ui;
}

int ImageExportDialog::imageFormat() const
{
    return ui->comboBoxFormat->currentIndex();
}

void ImageExportDialog::setImageFormat( int idx )
{
    const int n = ui->comboBoxFormat->count();
    if( n <= 0 ) return;
    if( idx < 0 ) idx = 0;
    if( idx >= n ) idx = n - 1;
    ui->comboBoxFormat->setCurrentIndex( idx );
}

int ImageExportDialog::jpgQuality() const
{
    return ui->horizontalSliderQuality->value();
}

int ImageExportDialog::scalePercent() const
{
    switch( ui->comboBoxScale->currentIndex() )
    {
        case 0: return 100;
        case 1: return 75;
        case 2: return 50;
        case 3: return 25;
        default: return 0;   //自定义
    }
}

int ImageExportDialog::customWidth() const
{
    return ui->spinBoxCustomWidth->value();
}

QString ImageExportDialog::outputDir() const
{
    return ui->lineEditOutputDir->text().trimmed();
}

void ImageExportDialog::setOutputDir( const QString &dir )
{
    ui->lineEditOutputDir->setText( dir );
}

bool ImageExportDialog::useSourceDir() const
{
    return ui->checkBoxUseSourceDir->isChecked();
}

void ImageExportDialog::setUseSourceDir( bool on )
{
    ui->checkBoxUseSourceDir->setChecked( on );
    //setChecked 不触发 toggled 时手动同步一次
    applySourceDirState();
}

void ImageExportDialog::setSourceDir( const QString &dir )
{
    m_sourceDir = dir.trimmed();
    //勾选状态下立刻把源目录显示到输入框
    if( ui->checkBoxUseSourceDir->isChecked() )
        applySourceDirState();
}

void ImageExportDialog::setSourceDirChangedCallback( const std::function<void( bool )> &cb )
{
    m_srcDirChanged = cb;
}

void ImageExportDialog::on_checkBoxUseSourceDir_toggled( bool checked )
{
    applySourceDirState();
    if( m_srcDirChanged ) m_srcDirChanged( checked );
}

void ImageExportDialog::applySourceDirState()
{
    const bool useSrc = ui->checkBoxUseSourceDir->isChecked();

    //勾选：目录输入框与浏览按钮都禁用，锁定为源目录
    ui->lineEditOutputDir->setEnabled( !useSrc );
    ui->toolButtonBrowse->setEnabled( !useSrc );
    ui->labelUseSourceDirHint->setVisible( useSrc );

    if( useSrc && !m_sourceDir.isEmpty() && QDir( m_sourceDir ).exists() )
    {
        ui->lineEditOutputDir->setText( m_sourceDir );
    }
    else if( useSrc && m_sourceDir.isEmpty() )
    {
        //源目录未知（比如还没加载素材）：提示用户，不能静默留空
        ui->lineEditOutputDir->clear();
        ui->lineEditOutputDir->setPlaceholderText( tr( "Source folder not found" ) );
    }
}

void ImageExportDialog::on_comboBoxScale_currentIndexChanged( int )
{
    updateEnabledState();
}

void ImageExportDialog::on_checkBoxCustomWidth_toggled( bool )
{
    updateEnabledState();
}

void ImageExportDialog::setSingleImageMode( bool single )
{
    m_singleImageMode = single;
}

bool ImageExportDialog::isSingleImageMode() const
{
    return m_singleImageMode;
}

bool ImageExportDialog::keepExif() const
{
    return ui->checkBoxKeepExif->isChecked();
}

int ImageExportDialog::colorSpace() const
{
    return ui->comboBoxColorSpace->currentIndex();
}

int ImageExportDialog::namingRule() const
{
    return ui->comboBoxNaming->currentIndex();
}

QString ImageExportDialog::namePrefix() const
{
    return ui->lineEditPrefix->text().trimmed();
}

bool ImageExportDialog::watermarkEnabled() const
{
    return ui->groupBoxWatermark->isChecked();
}

QString ImageExportDialog::watermarkText() const
{
    return ui->lineEditWmText->text();
}

int ImageExportDialog::watermarkSize() const
{
    return ui->spinBoxWmSize->value();
}

int ImageExportDialog::watermarkOpacity() const
{
    return ui->spinBoxWmOpacity->value();
}

int ImageExportDialog::watermarkCorner() const
{
    return ui->comboBoxWmPos->currentIndex();
}

void ImageExportDialog::on_comboBoxFormat_currentIndexChanged( int )
{
    updateEnabledState();
}

void ImageExportDialog::on_comboBoxNaming_currentIndexChanged( int )
{
    updateEnabledState();
}

void ImageExportDialog::on_checkBoxWatermark_toggled( bool )
{
    updateEnabledState();
}

void ImageExportDialog::updateEnabledState()
{
    const int  fmtIdx = ui->comboBoxFormat->currentIndex();
    const bool isDng  = ( fmtIdx == 3 );   //DNG = 原始 RAW，多数字段不适用

    //DNG 是原始 RAW 全尺寸，不做缩放
    const bool custom = ( ui->comboBoxScale->currentIndex() == 4 ) && !isDng;
    ui->comboBoxScale->setEnabled( !isDng );
    ui->spinBoxCustomWidth->setEnabled( custom );

    //只有 JPG 有画质概念；PNG/TIFF/DNG 无损，禁用滑块
    const bool isJpg = ( fmtIdx == 0 );
    ui->horizontalSliderQuality->setEnabled( isJpg );
    ui->labelQualityValue->setEnabled( isJpg );

    //命名规则 = 前缀+序号 时才启用前缀输入框
    const bool usePrefix = ( ui->comboBoxNaming->currentIndex() == 2 );
    ui->lineEditPrefix->setEnabled( usePrefix );

    //水印：DNG 是 RAW 数据，画不上水印，整组禁用
    const bool wm = ui->groupBoxWatermark->isChecked() && !isDng;
    ui->groupBoxWatermark->setEnabled( !isDng );
    ui->lineEditWmText->setEnabled( wm );
    ui->spinBoxWmSize->setEnabled( wm );
    ui->spinBoxWmOpacity->setEnabled( wm );
    ui->comboBoxWmPos->setEnabled( wm );

    //EXIF 保留只对 JPG/TIFF 有意义（PNG 不带 EXIF；DNG 自带完整原始元数据）
    ui->checkBoxKeepExif->setEnabled( fmtIdx == 0 || fmtIdx == 2 );

    //色彩空间：DNG 写的是传感器原始数据，不谈 sRGB/AdobeRGB
    ui->comboBoxColorSpace->setEnabled( !isDng );
    ui->labelColorSpace->setEnabled( !isDng );
}

void ImageExportDialog::on_buttonBox_accepted()
{
    saveToSettings();
    accept();
}

void ImageExportDialog::on_buttonBox_rejected()
{
    reject();
}

void ImageExportDialog::on_browse_clicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr( "Choose export folder" ), ui->lineEditOutputDir->text(), QFileDialog::ShowDirsOnly );
    if( !dir.isEmpty() )
    {
        ui->lineEditOutputDir->setText( dir );
    }
}

void ImageExportDialog::loadFromSettings()
{
    QSettings set;
    ui->comboBoxFormat->setCurrentIndex( set.value( S_KEY_FORMAT, 0 ).toInt() );
    ui->horizontalSliderQuality->setValue( set.value( S_KEY_QUALITY, 95 ).toInt() );
    ui->comboBoxScale->setCurrentIndex( set.value( S_KEY_SCALE, 0 ).toInt() );
    ui->spinBoxCustomWidth->setValue( set.value( S_KEY_CWIDTH, 1920 ).toInt() );
    ui->lineEditOutputDir->setText( set.value( S_KEY_DIR, QString() ).toString() );
    ui->labelQualityValue->setText( QString::number( ui->horizontalSliderQuality->value() ) );

    //「输出到源文件夹」默认勾选（用户高频需求）；只有用户显式取消过才记忆为 false
    ui->checkBoxUseSourceDir->setChecked( set.value( "imgExportUseSourceDir", true ).toBool() );

    ui->checkBoxKeepExif->setChecked( set.value( "imgExportKeepExif", true ).toBool() );
    ui->comboBoxColorSpace->setCurrentIndex( set.value( "imgExportColorSpace", 0 ).toInt() );
    ui->comboBoxNaming->setCurrentIndex( set.value( "imgExportNaming", 0 ).toInt() );
    ui->lineEditPrefix->setText( set.value( "imgExportPrefix", "export" ).toString() );
    ui->groupBoxWatermark->setChecked( set.value( "imgExportWmEnabled", false ).toBool() );
    ui->lineEditWmText->setText( set.value( "imgExportWmText", QString() ).toString() );
    ui->spinBoxWmSize->setValue( set.value( "imgExportWmSize", 36 ).toInt() );
    ui->spinBoxWmOpacity->setValue( set.value( "imgExportWmOpacity", 55 ).toInt() );
    ui->comboBoxWmPos->setCurrentIndex( set.value( "imgExportWmPos", 3 ).toInt() );
    on_checkBoxWatermark_toggled( ui->groupBoxWatermark->isChecked() );
    on_comboBoxNaming_currentIndexChanged( ui->comboBoxNaming->currentIndex() );
    on_checkBoxUseSourceDir_toggled( ui->checkBoxUseSourceDir->isChecked() );
}

void ImageExportDialog::saveToSettings()
{
    QSettings set;
    set.setValue( S_KEY_FORMAT, ui->comboBoxFormat->currentIndex() );
    set.setValue( S_KEY_QUALITY, ui->horizontalSliderQuality->value() );
    set.setValue( S_KEY_SCALE, ui->comboBoxScale->currentIndex() );
    set.setValue( S_KEY_CWIDTH, ui->spinBoxCustomWidth->value() );
    set.setValue( S_KEY_DIR, ui->lineEditOutputDir->text().trimmed() );

    set.setValue( "imgExportKeepExif", ui->checkBoxKeepExif->isChecked() );
    set.setValue( "imgExportColorSpace", ui->comboBoxColorSpace->currentIndex() );
    set.setValue( "imgExportNaming", ui->comboBoxNaming->currentIndex() );
    set.setValue( "imgExportPrefix", ui->lineEditPrefix->text() );
    set.setValue( "imgExportWmEnabled", ui->groupBoxWatermark->isChecked() );
    set.setValue( "imgExportWmText", ui->lineEditWmText->text() );
    set.setValue( "imgExportWmSize", ui->spinBoxWmSize->value() );
    set.setValue( "imgExportWmOpacity", ui->spinBoxWmOpacity->value() );
    set.setValue( "imgExportWmPos", ui->comboBoxWmPos->currentIndex() );

    //勾选「输出到源文件夹」时不记忆目录本身（每次按当前素材动态算），
    //只记忆勾选状态；取消勾选才记忆用户手动选的目录
    set.setValue( "imgExportUseSourceDir", ui->checkBoxUseSourceDir->isChecked() );
    if( !ui->checkBoxUseSourceDir->isChecked() )
        set.setValue( S_KEY_DIR, ui->lineEditOutputDir->text().trimmed() );
}

void ImageExportDialog::changeEvent( QEvent *event )
{
    if( event->type() == QEvent::LanguageChange )
        ui->retranslateUi( this );
    QDialog::changeEvent( event );
}
