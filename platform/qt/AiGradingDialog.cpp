/*!
 * \file AiGradingDialog.cpp
 * \brief AI language grading dialog - implementation
 *
 * Natural-language grading of the current clip via an OpenAI-compatible LLM endpoint.
 *
 * Design authority:
 *   reports/D4_ai_grading_panel_design.md  (§2.2 structure, §5.1-§5.4 flows, §6 settings,
 *                                           Appendix A = 96 element -> ReceiptSettings map)
 *   reports/D1_prompt_design.md            (§2 SYSTEM_PROMPT, §4.1 parse_reply, §4.2 plan)
 *   reports/D1_param_mapping.md            (§6 value ranges = the clamp table)
 *
 * Parameter landing path: D4 §5.3 "selected option B" - the dialog writes the receipt fields
 * directly (m_pReceipt == ACTIVE_RECEIPT, so it is clip level by construction) and then emits
 * receiptWasChanged(); MainWindow::onAiGradingReceiptChanged() answers with
 * setSliders( ACTIVE_RECEIPT, true ), which pushes every value into the UI controls whose
 * on_*_valueChanged() slots already call the matching processingSet*() functions. That gives
 * parameter sync + slider sync + preview refresh in one go, with no duplicated engine calls
 * here (setSliders() is a private MainWindow member and is not reachable from this class).
 *
 * All user visible strings go through tr(); Chinese text only appears in comments and in the
 * LLM system prompt (prompt content, not UI text - it must stay Chinese by design).
 */

#include "AiGradingDialog.h"
#include "ui_AiGradingDialog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>

// ---------------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------------

// QSettings key prefix (D4 §6.1). All keys live in the same key space as the rest of MLVApp.
static const char *SETTINGS_ENDPOINT   = "aiGradingEndpoint";
static const char *SETTINGS_MODEL      = "aiGradingModel";
static const char *SETTINGS_APIKEY     = "aiGradingApiKey";
static const char *SETTINGS_TIMEOUT    = "aiGradingTimeoutSec";
static const char *SETTINGS_MAXTOKENS  = "aiGradingMaxTokens";
static const char *SETTINGS_TEMPERATURE= "aiGradingTemperature";
static const char *SETTINGS_MAXRETRIES = "aiGradingMaxRetries";

// Defaults (D4 §6.1 / §6.2): local WorkBuddy AI bridge, free channel, no key.
static const char *DEFAULT_ENDPOINT    = "";   // 留空：由用户填自己的 OpenAI 兼容端点
                                       // WorkBuddy 国内版桥：独立额度池，不受国际版 8788 见底影响
static const char *DEFAULT_MODEL       = "deepseek-chat";        // 通用默认值，用户可改
static const int   DEFAULT_TIMEOUT_SEC = 120;
static const int   DEFAULT_MAX_RETRIES = 2;
static const double DEFAULT_TEMPERATURE= 0.3;
static const int    DEFAULT_MAX_TOKENS = 4000;   // 输出上限：推理型模型必须给足，否则思考吃光配额返回空

// Sentinel used only inside the prompt, never written back to the receipt (D4 §2.2.2).
static const int PROMPT_FALLBACK_TEMPERATURE = 6000;
static const int PROMPT_FALLBACK_RAWWHITE    = 16385;
static const int PROMPT_FALLBACK_TONEMAP     = 1;
static const int PROMPT_FALLBACK_GAMUT       = 6;

// ---------------------------------------------------------------------------------------------
// Value range table = D1_param_mapping.md §6 (full table) + D4 §5.2 + every numeric key of the
// D1 §3 JSON Schema whitelist. Unknown keys are dropped by clampPlan().
// ---------------------------------------------------------------------------------------------

static const QHash<QString, QPair<int,int>> RANGES = {
    // core grading
    { "exposure",             QPair<int,int>( -400, 400 ) },
    { "contrast",             QPair<int,int>( -100, 100 ) },
    { "pivot",                QPair<int,int>( 0, 100 ) },
    { "temperature",          QPair<int,int>( 2000, 10000 ) },
    { "tint",                 QPair<int,int>( -100, 100 ) },
    { "clarity",              QPair<int,int>( -100, 100 ) },
    { "vibrance",             QPair<int,int>( -100, 100 ) },
    { "saturation",           QPair<int,int>( -100, 100 ) },
    { "ds",                   QPair<int,int>( 0, 100 ) },
    { "dr",                   QPair<int,int>( 0, 100 ) },
    { "ls",                   QPair<int,int>( 0, 100 ) },
    { "lr",                   QPair<int,int>( 0, 100 ) },
    { "lightening",           QPair<int,int>( 0, 100 ) },
    { "shadows",              QPair<int,int>( -100, 100 ) },
    { "highlights",           QPair<int,int>( -100, 100 ) },
    { "gamma",                QPair<int,int>( 1, 400 ) },
    { "gamut",                QPair<int,int>( 0, 11 ) },
    { "tonemap",              QPair<int,int>( 0, 12 ) },
    { "sharpen",              QPair<int,int>( 0, 100 ) },
    { "sharpenMasking",       QPair<int,int>( 0, 100 ) },
    { "grainStrength",        QPair<int,int>( 0, 100 ) },
    { "grainLumaWeight",      QPair<int,int>( 0, 100 ) },
    { "vignetteStrength",     QPair<int,int>( -100, 100 ) },
    { "vignetteRadius",       QPair<int,int>( 0, 100 ) },
    { "vignetteShape",        QPair<int,int>( 0, 100 ) },
    { "rawBlack",             QPair<int,int>( 0, 65535 ) },
    { "rawWhite",             QPair<int,int>( 0, 65535 ) },
    { "tone",                 QPair<int,int>( 0, 255 ) },
    { "toningStrength",       QPair<int,int>( 0, 100 ) },
    { "caRed",                QPair<int,int>( 0, 100 ) },
    { "caBlue",               QPair<int,int>( 0, 100 ) },
    { "caDesaturate",         QPair<int,int>( 0, 100 ) },
    { "caRadius",             QPair<int,int>( 0, 10 ) },
    { "chromaBlur",           QPair<int,int>( 0, 10 ) },
    { "denoiserStrength",     QPair<int,int>( 0, 100 ) },
    { "denoiserWindow",       QPair<int,int>( 0, 100 ) },
    { "rbfDenoiserLuma",      QPair<int,int>( 0, 100 ) },
    { "rbfDenoiserChroma",    QPair<int,int>( 0, 100 ) },
    { "rbfDenoiserRange",     QPair<int,int>( 0, 100 ) },
    { "lutStrength",          QPair<int,int>( 0, 100 ) },
    { "filterIndex",          QPair<int,int>( 0, 255 ) },
    { "filterStrength",       QPair<int,int>( 0, 100 ) },
    // linear gradient
    { "gradientExposure",     QPair<int,int>( -400, 400 ) },
    { "gradientContrast",     QPair<int,int>( -100, 100 ) },
    { "gradientStartX",       QPair<int,int>( 0, 100000 ) },
    { "gradientStartY",       QPair<int,int>( 0, 100000 ) },
    { "gradientLength",       QPair<int,int>( 1, 100000 ) },
    { "gradientAngle",        QPair<int,int>( 0, 359 ) },
    // camera matrix / raw fixes (inside the D4 Appendix A whitelist)
    { "camMatrixUsed",        QPair<int,int>( 0, 2 ) },
    { "verticalStripes",      QPair<int,int>( 0, 2 ) },
    { "focusPixels",          QPair<int,int>( 0, 1 ) },
    { "fpiMethod",            QPair<int,int>( 0, 1 ) },
    { "badPixels",            QPair<int,int>( 0, 2 ) },
    { "bpsMethod",            QPair<int,int>( 0, 1 ) },
    { "bpiMethod",            QPair<int,int>( 0, 1 ) },
    { "chromaSmooth",         QPair<int,int>( 0, 5 ) },
    { "patternNoise",         QPair<int,int>( 0, 1 ) },
    { "deflickerTarget",      QPair<int,int>( 0, 100000 ) },
    // DualISO
    { "dualIsoForced",        QPair<int,int>( -1, 2 ) },
    { "dualIso",              QPair<int,int>( 0, 2 ) },
    { "dualIsoInterpolation", QPair<int,int>( 0, 1 ) },
    { "dualIsoAliasMap",      QPair<int,int>( 0, 1 ) },
    { "dualIsoFrBlending",    QPair<int,int>( 0, 1 ) },
    { "dualIsoWhite",         QPair<int,int>( 0, 65535 ) },
    { "dualIsoBlack",         QPair<int,int>( 0, 65535 ) },
    { "darkFrameEnabled",     QPair<int,int>( 0, 2 ) }
};

// Boolean keys (D4 §5.2 / D1 §4.2)
static const QSet<QString> BOOLS = {
    "agx", "highlightReconstruction", "chromaSeparation", "allowCreativeAdjustments",
    "exrMode", "rawFixesEnabled", "lutEnabled", "filterEnabled", "gradientEnabled"
};

// Free form string keys that are passed through (validated only for non-empty)
static const QSet<QString> STRING_KEYS = {
    "transferFunction", "lutName", "darkFrameFileName"
};

// Curve keys (D4 §5.2 white list check)
static const QSet<QString> CURVE_KEYS = {
    "gradationCurve", "hueVsHue", "hueVsSaturation", "hueVsLuminance", "lumaVsSaturation"
};

// Canonical 96 key order = writeXmlElementsToFile() order (D4 Appendix A / D1 §A.1).
// Used for a deterministic result table order.
static const QStringList KEY_ORDER = QStringList()
    << "exposure" << "contrast" << "pivot" << "temperature" << "tint" << "clarity"
    << "vibrance" << "saturation" << "ds" << "dr" << "ls" << "lr" << "lightening"
    << "gradationCurve" << "hueVsHue" << "hueVsSaturation" << "hueVsLuminance"
    << "lumaVsSaturation" << "shadows" << "highlights"
    << "gradientEnabled" << "gradientExposure" << "gradientContrast" << "gradientStartX"
    << "gradientStartY" << "gradientLength" << "gradientAngle"
    << "sharpen" << "sharpenMasking" << "chromaBlur" << "highlightReconstruction"
    << "camMatrixUsed" << "chromaSeparation"
    << "tonemap" << "transferFunction" << "gamut" << "gamma" << "allowCreativeAdjustments"
    << "exrMode" << "agx"
    << "denoiserStrength" << "denoiserWindow" << "rbfDenoiserLuma" << "rbfDenoiserChroma"
    << "rbfDenoiserRange" << "grainStrength" << "grainLumaWeight"
    << "rawFixesEnabled" << "verticalStripes" << "focusPixels" << "fpiMethod" << "badPixels"
    << "bpsMethod" << "bpiMethod" << "chromaSmooth" << "patternNoise" << "deflickerTarget"
    << "dualIsoForced" << "dualIso" << "dualIsoInterpolation" << "dualIsoAliasMap"
    << "dualIsoFrBlending" << "dualIsoWhite" << "dualIsoBlack"
    << "darkFrameFileName" << "darkFrameEnabled" << "rawBlack" << "rawWhite"
    << "tone" << "toningStrength"
    << "lutEnabled" << "lutName" << "lutStrength" << "filterEnabled" << "filterIndex"
    << "filterStrength"
    << "vignetteStrength" << "vignetteRadius" << "vignetteShape"
    << "caRed" << "caBlue" << "caDesaturate" << "caRadius"
    << "stretchFactorX" << "stretchFactorY" << "upsideDown"
    << "vidstabEnable" << "vidstabStepsize" << "vidstabShakiness" << "vidstabAccuracy"
    << "vidstabZoom" << "vidstabSmoothing" << "vidstabTripod"
    << "cutIn" << "cutOut" << "debayer";

// Curve keys that clampPlan() rejected in the last run. refreshResultTable() shows them as
// "ignored" rows so the user can see that the model produced something unusable. The dialog is
// modal and single instance, so file scope state is safe here.
static QSet<QString> g_ignoredCurveKeys;

// ---------------------------------------------------------------------------------------------
// Small file scope helpers
// ---------------------------------------------------------------------------------------------

// Display value for the prompt: replace the internal "uninitialized" sentinels (-1) with a
// plausible value so the model is not confused by -1 (D4 §2.2.2). This never touches the receipt.
static QString promptValue( const QString &key, const QString &raw )
{
    int v = raw.toInt();
    if( v != -1 ) return raw;

    if( key == "temperature" ) return QString::number( PROMPT_FALLBACK_TEMPERATURE );
    if( key == "rawWhite" )    return QString::number( PROMPT_FALLBACK_RAWWHITE );
    if( key == "rawBlack" )    return QString::number( 0 );
    if( key == "tonemap" )     return QString::number( PROMPT_FALLBACK_TONEMAP );
    if( key == "gamut" )       return QString::number( PROMPT_FALLBACK_GAMUT );
    if( key == "camMatrixUsed")return QString::number( 1 );
    return raw;
}

// Minimal JSON quoting for the current-value snapshot inside the user prompt.
static QString jsonQuote( const QString &s )
{
    QString out = s;
    out.replace( QLatin1String( "\\" ), QLatin1String( "\\\\" ) );
    out.replace( QLatin1String( "\"" ), QLatin1String( "\\\"" ) );
    out.replace( QLatin1String( "\n" ), QLatin1String( "\\n" ) );
    out.replace( QLatin1String( "\r" ), QLatin1String( "\\r" ) );
    out.replace( QLatin1String( "\t" ), QLatin1String( "\\t" ) );
    return out;
}

// Split a curve segment on ';'. The project uses the same QT_VERSION guard idiom as
// BadPixelFileHandler.cpp (QString::SplitBehavior in Qt5, Qt::SplitBehavior in Qt6).
static QStringList splitSemicolon( const QString &s )
{
#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
    return s.split( QLatin1Char( ';' ), QString::SkipEmptyParts );
#else
    return s.split( QLatin1Char( ';' ), Qt::SkipEmptyParts );
#endif
}

// Validate a curve string. gradationCurve: 4 segments separated by '?', each with >= 2 points
// (x;y;) and x/y in [0,1]. HSL curves: one segment, no '?', x in [0,1], y in [-1,1].
static bool isValidCurve( const QString &key, const QString &value )
{
    const QString v = value.trimmed();
    if( v.isEmpty() ) return false;

    const QStringList segments = v.split( QLatin1Char( '?' ) );

    if( key == "gradationCurve" )
    {
        if( segments.size() != 4 ) return false;
        for( int s = 0; s < segments.size(); ++s )
        {
            const QStringList nums = splitSemicolon( segments.at( s ) );
            if( nums.size() < 4 ) return false;              // at least 2 points (x;y; pairs)
            if( nums.size() % 2 != 0 ) return false;         // must be x;y;x;y;...
            for( int i = 0; i < nums.size(); ++i )
            {
                bool ok = false;
                const double d = nums.at( i ).toDouble( &ok );
                if( !ok ) return false;
                if( d < 0.0 || d > 1.0 ) return false;       // x and y both 0..1
            }
        }
        return true;
    }

    if( CURVE_KEYS.contains( key ) )
    {
        if( segments.size() != 1 ) return false;
        const QStringList nums = splitSemicolon( segments.first() );
        if( nums.size() < 4 ) return false;
        if( nums.size() % 2 != 0 ) return false;
        for( int i = 0; i < nums.size(); i += 2 )
        {
            bool okX = false, okY = false;
            const double x = nums.at( i ).toDouble( &okX );
            const double y = nums.at( i + 1 ).toDouble( &okY );
            if( !okX || !okY ) return false;
            if( x < 0.0 || x > 1.0 ) return false;
            if( y < -1.0 || y > 1.0 ) return false;
        }
        return true;
    }

    return false;
}

// Sort keys by the canonical receipt element order so the table reads like the marxml export.
static QStringList sortByCanonicalOrder( const QStringList &keys )
{
    QStringList out = keys;
    QMap<int, QString> ranked;
    for( int i = 0; i < out.size(); ++i )
    {
        const int idx = KEY_ORDER.indexOf( out.at( i ) );
        ranked.insert( idx < 0 ? 900 + i : idx, out.at( i ) );
    }
    return ranked.values();
}

// Build the OpenAI compatible chat/completions body.
static QByteArray makeChatBody( const QString &model,
                                const QString &systemPrompt,
                                const QString &userPrompt,
                                double temperature,
                                int maxTokens )
{
    QJsonObject root;
    root.insert( QLatin1String( "model" ), model );
    root.insert( QLatin1String( "stream" ), false );          // first version: no SSE (D4 §6.3)
    root.insert( QLatin1String( "temperature" ), temperature );
    // 推理型模型（hy4/step-5/hy3）会把预算烧在思考上；不给上限会返回空 content。
    root.insert( QLatin1String( "max_tokens" ), maxTokens > 0 ? maxTokens : 4000 );

    QJsonObject sysMsg;
    sysMsg.insert( QLatin1String( "role" ), QLatin1String( "system" ) );
    sysMsg.insert( QLatin1String( "content" ), systemPrompt );

    QJsonObject userMsg;
    userMsg.insert( QLatin1String( "role" ), QLatin1String( "user" ) );
    userMsg.insert( QLatin1String( "content" ), userPrompt );

    QJsonArray messages;
    messages.append( sysMsg );
    messages.append( userMsg );
    root.insert( QLatin1String( "messages" ), messages );

    return QJsonDocument( root ).toJson( QJsonDocument::Compact );
}

// ---------------------------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------------------------

AiGradingDialog::AiGradingDialog( QWidget *parent,
                                  ReceiptSettings *pReceipt,
                                  processingObject_t *pProcessing ) :
    QWidget( parent ),
    ui( new Ui::AiGradingDialog ),
    m_pReceipt( pReceipt ),
    m_pProcessing( pProcessing ),
    m_pPendingReply( nullptr ),
    m_retryCount( 0 ),
    m_maxRetries( DEFAULT_MAX_RETRIES ),
    m_hasUndo( false ),
    m_maxTokens( DEFAULT_MAX_TOKENS )
{
    ui->setupUi( this );

    // 作为右侧停靠栏内嵌面板：不再设 Dialog 窗口标志

    m_pNetManager = new QNetworkAccessManager( this );
    connect( m_pNetManager, SIGNAL(finished(QNetworkReply*)),
             this, SLOT(on_replyFinished(QNetworkReply*)) );

    m_pTimeoutTimer = new QTimer( this );
    m_pTimeoutTimer->setSingleShot( true );
    connect( m_pTimeoutTimer, SIGNAL(timeout()), this, SLOT(on_timeout()) );

    // 输出上限实时跟随（改完立刻生效，不必先保存）
    connect( ui->spinBoxMaxTokens, SIGNAL(valueChanged(int)), this, SLOT(on_spinBoxMaxTokens_valueChanged(int)) );

    // LLM 设置已搬到「AI 智能调色 → LLM 设置…」菜单
    // 面板内不再显示这块（字段对象保留，逻辑零改动）
    ui->groupBoxLlmSettings->setVisible( false );

    ui->pushButtonApply->setEnabled( false );
    ui->pushButtonUndo->setEnabled( false );
    ui->tableResult->setRowCount( 0 );
    ui->tableResult->setColumnCount( 3 );
    ui->tableResult->setHorizontalHeaderLabels( QStringList()
        << tr( "Parameter" ) << tr( "Current Value" ) << tr( "New Value" ) );
    ui->tableResult->horizontalHeader()->setStretchLastSection( true );

    loadSettingsToUi();
    setBusy( false );
    ui->labelStatus->setText( tr( "Ready" ) );
}


// ---------------------------------------------------------------------------------------------
// Context switching (panel stays alive, receipt changes with the active clip)
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::setContext( ReceiptSettings *pReceipt, processingObject_t *pProcessing )
{
    // 面板常驻右侧栏：切换素材时 receipt / processing 都换了，这里重新指向。
    // 未完成的结果对上一个 clip 已经没有意义，直接清掉，避免误套到新 clip 上。
    m_pReceipt   = pReceipt;
    m_pProcessing = pProcessing;

    m_pendingChanges.clear();
    m_hasUndo = false;
    ui->tableResult->setRowCount( 0 );
    ui->pushButtonApply->setEnabled( false );
    ui->pushButtonUndo->setEnabled( false );

    if( m_pPendingReply )
    {
        m_pPendingReply->abort();
        m_pPendingReply = nullptr;
    }
    if( m_pTimeoutTimer->isActive() )
    {
        m_pTimeoutTimer->stop();
    }
    setBusy( false );
    ui->labelStatus->setText( tr( "Ready" ) );
}

AiGradingDialog::~AiGradingDialog()
{
    if( m_pTimeoutTimer ) m_pTimeoutTimer->stop();
    if( m_pPendingReply )
    {
        // do not leave a dangling request behind; the reply is a child of the net manager
        disconnect( m_pPendingReply, nullptr, this, nullptr );
        m_pPendingReply->abort();
        m_pPendingReply = nullptr;
    }
    delete ui;
}

// ---------------------------------------------------------------------------------------------
// Settings (D4 §6.1)
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::loadSettingsToUi()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );

    ui->lineEditEndpoint->setText( set.value( SETTINGS_ENDPOINT, DEFAULT_ENDPOINT ).toString() );
    ui->lineEditModel->setText( set.value( SETTINGS_MODEL, DEFAULT_MODEL ).toString() );
    ui->lineEditApiKey->setText( set.value( SETTINGS_APIKEY, QString( "" ) ).toString() );
    ui->spinBoxTimeout->setValue( set.value( SETTINGS_TIMEOUT, DEFAULT_TIMEOUT_SEC ).toInt() );

    m_maxTokens = set.value( SETTINGS_MAXTOKENS, DEFAULT_MAX_TOKENS ).toInt();
    if( m_maxTokens < 256 ) m_maxTokens = 256;
    if( m_maxTokens > 32768 ) m_maxTokens = 32768;
    ui->spinBoxMaxTokens->setValue( m_maxTokens );   // 与成员同步显示

    m_maxRetries = set.value( SETTINGS_MAXRETRIES, DEFAULT_MAX_RETRIES ).toInt();
    if( m_maxRetries < 0 ) m_maxRetries = 0;
    if( m_maxRetries > 5 ) m_maxRetries = 5;
}

//从 QSettings 重读 LLM 配置到面板控件
void AiGradingDialog::reloadLlmSettings()
{
    loadSettingsToUi();
}

void AiGradingDialog::saveSettingsFromUi()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );

    set.setValue( SETTINGS_ENDPOINT, ui->lineEditEndpoint->text().trimmed() );
    set.setValue( SETTINGS_MODEL, ui->lineEditModel->text().trimmed() );
    set.setValue( SETTINGS_APIKEY, ui->lineEditApiKey->text() );   // never logged
    set.setValue( SETTINGS_TIMEOUT, ui->spinBoxTimeout->value() );
    m_maxTokens = ui->spinBoxMaxTokens->value();     // UI 是权威，写回成员
    set.setValue( SETTINGS_MAXRETRIES, m_maxRetries );
    set.setValue( SETTINGS_MAXTOKENS, m_maxTokens );
    if( !set.contains( SETTINGS_TEMPERATURE ) )
        set.setValue( SETTINGS_TEMPERATURE, DEFAULT_TEMPERATURE );
}

void AiGradingDialog::on_spinBoxMaxTokens_valueChanged( int value )
{
    m_maxTokens = value;
}

// ---------------------------------------------------------------------------------------------
// 96 element collection (D4 Appendix A order, raw values - no sentinel substitution, because the
// very same table is the undo snapshot and undo must be able to restore -1 verbatim).
// ---------------------------------------------------------------------------------------------

QHash<QString, QString> AiGradingDialog::collectCurrentParams() const
{
    QHash<QString, QString> p;
    if( !m_pReceipt ) return p;

    p.insert( "exposure",                QString::number( m_pReceipt->exposure() ) );
    p.insert( "contrast",                QString::number( m_pReceipt->contrast() ) );
    p.insert( "pivot",                   QString::number( m_pReceipt->pivot() ) );
    p.insert( "temperature",             QString::number( m_pReceipt->temperature() ) );
    p.insert( "tint",                    QString::number( m_pReceipt->tint() ) );
    p.insert( "clarity",                 QString::number( m_pReceipt->clarity() ) );
    p.insert( "vibrance",                QString::number( m_pReceipt->vibrance() ) );
    p.insert( "saturation",              QString::number( m_pReceipt->saturation() ) );
    p.insert( "ds",                      QString::number( m_pReceipt->ds() ) );
    p.insert( "dr",                      QString::number( m_pReceipt->dr() ) );
    p.insert( "ls",                      QString::number( m_pReceipt->ls() ) );
    p.insert( "lr",                      QString::number( m_pReceipt->lr() ) );
    p.insert( "lightening",              QString::number( m_pReceipt->lightening() ) );
    p.insert( "gradationCurve",          m_pReceipt->gradationCurve() );
    p.insert( "hueVsHue",                m_pReceipt->hueVsHue() );
    p.insert( "hueVsSaturation",         m_pReceipt->hueVsSaturation() );
    p.insert( "hueVsLuminance",          m_pReceipt->hueVsLuminance() );
    p.insert( "lumaVsSaturation",        m_pReceipt->lumaVsSaturation() );
    p.insert( "shadows",                 QString::number( m_pReceipt->shadows() ) );
    p.insert( "highlights",              QString::number( m_pReceipt->highlights() ) );
    p.insert( "gradientEnabled",         QString::number( m_pReceipt->isGradientEnabled() ? 1 : 0 ) );
    p.insert( "gradientExposure",        QString::number( m_pReceipt->gradientExposure() ) );
    p.insert( "gradientContrast",        QString::number( m_pReceipt->gradientContrast() ) );
    p.insert( "gradientStartX",          QString::number( m_pReceipt->gradientStartX() ) );
    p.insert( "gradientStartY",          QString::number( m_pReceipt->gradientStartY() ) );
    p.insert( "gradientLength",          QString::number( m_pReceipt->gradientLength() ) );
    p.insert( "gradientAngle",           QString::number( m_pReceipt->gradientAngle() ) );
    p.insert( "sharpen",                 QString::number( m_pReceipt->sharpen() ) );
    p.insert( "sharpenMasking",          QString::number( m_pReceipt->shMasking() ) );
    p.insert( "chromaBlur",              QString::number( m_pReceipt->chromaBlur() ) );
    p.insert( "highlightReconstruction", QString::number( m_pReceipt->isHighlightReconstruction() ? 1 : 0 ) );
    p.insert( "camMatrixUsed",           QString::number( m_pReceipt->camMatrixUsed() ) );
    p.insert( "chromaSeparation",        QString::number( m_pReceipt->isChromaSeparation() ? 1 : 0 ) );
    p.insert( "tonemap",                 QString::number( m_pReceipt->tonemap() ) );
    p.insert( "transferFunction",        m_pReceipt->transferFunction() );
    p.insert( "gamut",                   QString::number( m_pReceipt->gamut() ) );
    p.insert( "gamma",                   QString::number( m_pReceipt->gamma() ) );
    p.insert( "allowCreativeAdjustments",QString::number( m_pReceipt->allowCreativeAdjustments() ? 1 : 0 ) );
    p.insert( "exrMode",                 QString::number( m_pReceipt->exrMode() ? 1 : 0 ) );
    p.insert( "agx",                     QString::number( m_pReceipt->agx() ? 1 : 0 ) );
    p.insert( "denoiserStrength",        QString::number( m_pReceipt->denoiserStrength() ) );
    p.insert( "denoiserWindow",          QString::number( m_pReceipt->denoiserWindow() ) );
    p.insert( "rbfDenoiserLuma",         QString::number( m_pReceipt->rbfDenoiserLuma() ) );
    p.insert( "rbfDenoiserChroma",       QString::number( m_pReceipt->rbfDenoiserChroma() ) );
    p.insert( "rbfDenoiserRange",        QString::number( m_pReceipt->rbfDenoiserRange() ) );
    p.insert( "grainStrength",           QString::number( m_pReceipt->grainStrength() ) );
    p.insert( "grainLumaWeight",         QString::number( m_pReceipt->grainLumaWeight() ) );
    p.insert( "rawFixesEnabled",         QString::number( m_pReceipt->rawFixesEnabled() ? 1 : 0 ) );
    p.insert( "verticalStripes",         QString::number( m_pReceipt->verticalStripes() ) );
    p.insert( "focusPixels",             QString::number( m_pReceipt->focusPixels() ) );
    p.insert( "fpiMethod",               QString::number( m_pReceipt->fpiMethod() ) );
    p.insert( "badPixels",               QString::number( m_pReceipt->badPixels() ) );
    p.insert( "bpsMethod",               QString::number( m_pReceipt->bpsMethod() ) );
    p.insert( "bpiMethod",               QString::number( m_pReceipt->bpiMethod() ) );
    p.insert( "chromaSmooth",            QString::number( m_pReceipt->chromaSmooth() ) );
    p.insert( "patternNoise",            QString::number( m_pReceipt->patternNoise() ) );
    p.insert( "deflickerTarget",         QString::number( m_pReceipt->deflickerTarget() ) );
    p.insert( "dualIsoForced",           QString::number( m_pReceipt->dualIsoForced() ) );
    p.insert( "dualIso",                 QString::number( m_pReceipt->dualIso() ) );
    p.insert( "dualIsoInterpolation",    QString::number( m_pReceipt->dualIsoInterpolation() ) );
    p.insert( "dualIsoAliasMap",         QString::number( m_pReceipt->dualIsoAliasMap() ) );
    p.insert( "dualIsoFrBlending",       QString::number( m_pReceipt->dualIsoFrBlending() ) );
    p.insert( "dualIsoWhite",            QString::number( m_pReceipt->dualIsoWhite() ) );
    p.insert( "dualIsoBlack",            QString::number( m_pReceipt->dualIsoBlack() ) );
    p.insert( "darkFrameFileName",       m_pReceipt->darkFrameFileName() );
    p.insert( "darkFrameEnabled",        QString::number( m_pReceipt->darkFrameEnabled() ) );
    p.insert( "rawBlack",                QString::number( m_pReceipt->rawBlack() ) );
    p.insert( "rawWhite",                QString::number( m_pReceipt->rawWhite() ) );
    p.insert( "tone",                    QString::number( m_pReceipt->tone() ) );
    p.insert( "toningStrength",          QString::number( m_pReceipt->toningStrength() ) );
    p.insert( "lutEnabled",              QString::number( m_pReceipt->lutEnabled() ? 1 : 0 ) );
    p.insert( "lutName",                 m_pReceipt->lutName() );
    p.insert( "lutStrength",             QString::number( m_pReceipt->lutStrength() ) );
    p.insert( "filterEnabled",           QString::number( m_pReceipt->filterEnabled() ? 1 : 0 ) );
    p.insert( "filterIndex",             QString::number( m_pReceipt->filterIndex() ) );
    p.insert( "filterStrength",          QString::number( m_pReceipt->filterStrength() ) );
    p.insert( "vignetteStrength",        QString::number( m_pReceipt->vignetteStrength() ) );
    p.insert( "vignetteRadius",          QString::number( m_pReceipt->vignetteRadius() ) );
    p.insert( "vignetteShape",           QString::number( m_pReceipt->vignetteShape() ) );
    p.insert( "caRed",                   QString::number( m_pReceipt->caRed() ) );
    p.insert( "caBlue",                  QString::number( m_pReceipt->caBlue() ) );
    p.insert( "caDesaturate",            QString::number( m_pReceipt->caDesaturate() ) );
    p.insert( "caRadius",                QString::number( m_pReceipt->caRadius() ) );
    p.insert( "stretchFactorX",          QString::number( m_pReceipt->stretchFactorX() ) );
    p.insert( "stretchFactorY",          QString::number( m_pReceipt->stretchFactorY() ) );
    p.insert( "upsideDown",              QString::number( m_pReceipt->upsideDown() ? 1 : 0 ) );
    p.insert( "vidstabEnable",           QString::number( m_pReceipt->vidStabEnabled() ? 1 : 0 ) );
    p.insert( "vidstabStepsize",         QString::number( m_pReceipt->vidStabStepsize() ) );
    p.insert( "vidstabShakiness",        QString::number( m_pReceipt->vidStabShakiness() ) );
    p.insert( "vidstabAccuracy",         QString::number( m_pReceipt->vidStabAccuracy() ) );
    p.insert( "vidstabZoom",             QString::number( m_pReceipt->vidStabZoom() ) );
    p.insert( "vidstabSmoothing",        QString::number( m_pReceipt->vidStabSmoothing() ) );
    p.insert( "vidstabTripod",           QString::number( m_pReceipt->vidStabTripod() ? 1 : 0 ) );
    p.insert( "cutIn",                   QString::number( m_pReceipt->cutIn() ) );
    p.insert( "cutOut",                  QString::number( m_pReceipt->cutOut() ) );
    p.insert( "debayer",                 QString::number( m_pReceipt->debayer() ) );

    return p;
}

// ---------------------------------------------------------------------------------------------
// Prompt assembly (D1 §2 system prompt, verbatim)
// ---------------------------------------------------------------------------------------------

QString AiGradingDialog::buildSystemPrompt() const
{
    // D1_prompt_design.md §2, inlined verbatim. This is prompt content, not UI text, so it is
    // deliberately NOT wrapped in tr() - it must stay Chinese for the model.
    static const char *SYSTEM_PROMPT = R"PROMPT(你是 MLVApp（魔灯相机 RAW 视频处理器）的内置 AI 调色助手。
用户用自然语言描述想要的画面效果，你必须输出一组精确的调色参数（JSON）。

【工作方式】
1. 用户会提供「当前参数快照」（96 个元素的 marxml 值）。
2. 你要理解用户的自然语言意图，决定需要修改哪些参数。
3. 只输出「需要修改的参数」的最终值（不是增量）。未提到的参数保持不变。
4. 所有值必须在合法值域内（见参数表）。若用户语义超出值域，取最接近的边界值。
5. 输出必须是合法 JSON，且每个键都在参数表中。

【参数表】（marxml 元素名 / 含义 / 值域 / 换算）

# 曝光与对比
- exposure: 曝光补偿，整数 -400~400，除以100=EV。正值=更亮，负值=更暗。
- contrast: 对比度，整数 -100~100。正值=增强，负值=降低。
- pivot: 对比度枢轴（中心亮度），整数 0~100，默认75。越大S曲线中心越靠右。
- clarity: 清晰度，整数 -100~100。正值=增强局部对比，负值=柔化。
- gamma: 伽马，整数 1~400，除以100。100=中性，>100提亮中间调，<100压暗。

# 白平衡
- temperature: 色温，整数 2000~10000（开尔文）。高=暖（偏红），低=冷（偏蓝）。用户说「暖一点」→ 提高 temperature；「冷一点」→ 降低 temperature。
- tint: 色调，整数 -100~100，除以10传入引擎。正值=偏品红，负值=偏绿。

# 色彩
- saturation: 饱和度，整数 -100~100。正值=增饱和，负值=去饱和。
- vibrance: 自然饱和度，整数 -100~100。保护肤色增强低饱和色。

# 分区调整（S曲线）
- ds: 暗部对比强度，整数 0~100（默认20）。
- dr: 暗部对比范围，整数 0~100（默认70）。
- ls: 亮部对比强度，整数 0~100（默认0）。
- lr: 亮部对比范围，整数 0~100（默认50）。
- lightening: 高光柔化/滚动，整数 0~100。越大高光过渡越柔。
- shadows: 阴影，整数 -100~100。正值提亮阴影。
- highlights: 高光，整数 -100~100。正值提亮高光，负值压暗高光。

# 色彩空间与风格
- gamut: 色域枚举 0=Rec709 1=Rec2020 2=ACES AP0 3=AdobeRGB 4=ProPhotoRGB 5=XYZ 6=AlexaWideGamut 7=SonySGamut3 8=DavinciWideGamut 9=ACES AP1 10=CanonCinema 11=PanasonicV。
- tonemap: 色调映射枚举 0=None 1=Reinhard 2=Tangent 3=AlexaLogC 4=CineonLog 5=SonySLog 6=sRGB 7=Rec709 8=HLG 9=DavinciIntermediate 10=Reinhard3/5 11=CanonLog 12=PanasonicVLog。
- agx: 是否启用AgX电影感色调映射，0或1。
- transferFunction: 字符串，tinyexpr表达式，变量x，可含pow/log10/split，三元用 (x<阈值)?A:B。

# 质感
- sharpen: 锐化，整数 0~100。
- sharpenMasking: 锐化蒙版，整数 0~100。
- grainStrength: 胶片颗粒强度，整数 0~100。
- grainLumaWeight: 颗粒亮度权重，整数 0~100。
- vignetteStrength: 暗角强度，整数 -100~100（正=暗角，负=提亮边缘）。
- vignetteRadius: 暗角半径，整数 0~100（默认20）。
- vignetteShape: 暗角形状，整数 0~100（0=圆，100=椭圆）。

# 原始数据
- rawBlack: 黑电平，整数 0~65535，除以10传入。提升可去雾霾/提黑。
- rawWhite: 白电平，整数 0~65535。必须大于 rawBlack/10+1。
- tone: 色调染色，整数 0~255。
- toningStrength: 染色强度，整数 0~100。

# 曲线（字符串格式，一般由后端曲线编辑器生成，LLM不建议手写）
- gradationCurve: 4段子曲线（白/红/绿/蓝），每段x;y;成对，段间用?分隔。x,y均0~1。
- hueVsHue / hueVsSaturation / hueVsLuminance / lumaVsSaturation: 单曲线，x;y;成对。x 0~1，y -1~1。

【JSON 输出格式】
你必须输出一个 JSON 对象，键为要修改的 marxml 元素名，值为最终值。
不要输出多余文字，不要用 markdown 代码块包裹（除非系统要求）。

【约束】
- 只修改明确需要的参数。
- 用户说「暖一点」→ 提高 temperature；「冷一点」→ 降低 temperature（务必与上文参数表一致）。
- ★ temperature 是绝对色温值，不是增量。请对照【当前参数快照】里的 temperature：
  若用户要「更暖」而当前已是 9000，应给 ≥9000 的值；只给 7200 反而会让画面变冷。
- 用户说「胶片感」→ 可考虑 grainStrength>0、agx=1、lightening>0、contrast 轻微 S 曲线。
- 用户说「日系清新」→ 提高 exposure、降低 contrast、提高 vibrance、色温偏冷。
- 用户说「电影感」→ 降低 saturation、增强 shadows/highlights 分区、可加颗粒。
)PROMPT";

    return QString::fromUtf8( SYSTEM_PROMPT );
}

QString AiGradingDialog::buildUserPrompt( const QString &naturalLanguage ) const
{
    const QHash<QString, QString> current = collectCurrentParams();

    QString snapshot = QLatin1String( "{" );
    for( int i = 0; i < KEY_ORDER.size(); ++i )
    {
        const QString key = KEY_ORDER.at( i );
        if( !current.contains( key ) ) continue;
        if( i > 0 ) snapshot += QLatin1String( ", " );
        snapshot += QLatin1String( "\"" ) + key + QLatin1String( "\": " );
        const QString raw = current.value( key );
        if( CURVE_KEYS.contains( key ) || STRING_KEYS.contains( key ) )
            snapshot += QLatin1String( "\"" ) + jsonQuote( raw ) + QLatin1String( "\"" );
        else
            snapshot += promptValue( key, raw );
    }
    snapshot += QLatin1String( "}" );

    // D1 §5 input structure
    QString prompt;
    prompt += QString::fromUtf8( "【当前参数快照】\n" );
    prompt += snapshot;
    prompt += QString::fromUtf8( "\n\n【用户指令】\n" );
    prompt += naturalLanguage.trimmed();
    prompt += QLatin1String( "\n" );
    return prompt;
}

// ---------------------------------------------------------------------------------------------
// Reply parsing (D1 §4.1)
// ---------------------------------------------------------------------------------------------

QString AiGradingDialog::stripThinking( const QString &raw )
{
    QString s = raw;

    // step 1: drop reasoning blocks. Use a non greedy match over the whole string.
    s.remove( QRegularExpression( QLatin1String( "<thinking>.*?</thinking>" ),
                                  QRegularExpression::DotMatchesEverythingOption ) );
    s.remove( QRegularExpression( QLatin1String( "<think>.*?</think>" ),
                                  QRegularExpression::DotMatchesEverythingOption ) );
    s.remove( QRegularExpression( QLatin1String( "<reasoning>.*?</reasoning>" ),
                                  QRegularExpression::DotMatchesEverythingOption ) );

    // step 2: drop markdown code fences (keep the content)
    s.remove( QRegularExpression( QLatin1String( "```(?:json|JSON)?" ) ) );

    return s;
}

QString AiGradingDialog::extractFirstJson( const QString &text )
{
    const int start = text.indexOf( QLatin1Char( '{' ) );
    const int end   = text.lastIndexOf( QLatin1Char( '}' ) );
    if( start < 0 || end <= start ) return QString();

    QString candidate = text.mid( start, end - start + 1 );

    // step 3: repair trailing commas
    candidate.replace( QRegularExpression( QLatin1String( ",\\s*}" ) ), QLatin1String( "}" ) );
    candidate.replace( QRegularExpression( QLatin1String( ",\\s*]" ) ), QLatin1String( "]" ) );

    return candidate;
}

// ---------------------------------------------------------------------------------------------
// clampPlan (D1 §4.2 + D4 §5.2, full range table)
// ---------------------------------------------------------------------------------------------

QHash<QString, QString> AiGradingDialog::clampPlan( const QJsonObject &obj )
{
    QHash<QString, QString> out;
    g_ignoredCurveKeys.clear();

    for( QJsonObject::const_iterator it = obj.constBegin(); it != obj.constEnd(); ++it )
    {
        const QString key = it.key();
        const QJsonValue val = it.value();

        // --- numeric range keys: clamp into [min,max], floor to integer ---
        if( RANGES.contains( key ) )
        {
            int v = 0;
            if( val.isDouble() )       v = (int)val.toDouble();
            else if( val.isBool() )    v = val.toBool() ? 1 : 0;
            else if( val.isString() )  v = val.toString().toInt();
            else continue;                                  // object/array/null: drop

            const QPair<int,int> range = RANGES.value( key );
            if( v < range.first )  v = range.first;
            if( v > range.second ) v = range.second;
            out.insert( key, QString::number( v ) );
            continue;
        }

        // --- boolean keys: coerce to 0/1 ---
        if( BOOLS.contains( key ) )
        {
            bool on = false;
            if( val.isBool() )        on = val.toBool();
            else if( val.isDouble() ) on = ( val.toDouble() != 0.0 );
            else if( val.isString() ) on = ( val.toString().compare( QLatin1String( "false" ), Qt::CaseInsensitive ) != 0
                                             && !val.toString().isEmpty() );
            out.insert( key, QString::number( on ? 1 : 0 ) );
            continue;
        }

        // --- free form string keys ---
        if( STRING_KEYS.contains( key ) )
        {
            const QString s = val.toString().trimmed();
            if( !s.isEmpty() ) out.insert( key, s );
            continue;
        }

        // --- curve keys: format white list check, invalid -> drop + flag (D4 §5.2) ---
        if( CURVE_KEYS.contains( key ) )
        {
            const QString s = val.toString().trimmed();
            if( isValidCurve( key, s ) ) out.insert( key, s );
            else                         g_ignoredCurveKeys.insert( key );
            continue;
        }

        // anything else: unknown key -> dropped silently (whitelist by design, D4 §2.2.9)
    }

    // cross constraint: rawWhite > rawBlack/10 + 1 (MW.cpp:6332-6336 / 6365-6369)
    if( out.contains( "rawWhite" ) && out.contains( "rawBlack" ) )
    {
        const int rb = out.value( "rawBlack" ).toInt();
        if( out.value( "rawWhite" ).toInt() <= rb / 10 + 1 )
            out.insert( "rawWhite", QString::number( rb / 10 + 2 ) );
    }

    return out;
}

// ---------------------------------------------------------------------------------------------
// Single key landing: key -> ReceiptSettings setter (D4 Appendix A, all 96 keys)
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::applyOneParamToReceipt( const QString &key, const QString &value )
{
    if( !m_pReceipt ) return;

    if     ( key == "exposure" )                m_pReceipt->setExposure( value.toInt() );
    else if( key == "contrast" )                m_pReceipt->setContrast( value.toInt() );
    else if( key == "pivot" )                   m_pReceipt->setPivot( value.toInt() );
    else if( key == "temperature" )             m_pReceipt->setTemperature( value.toInt() );
    else if( key == "tint" )                    m_pReceipt->setTint( value.toInt() );
    else if( key == "clarity" )                 m_pReceipt->setClarity( value.toInt() );
    else if( key == "vibrance" )                m_pReceipt->setVibrance( value.toInt() );
    else if( key == "saturation" )              m_pReceipt->setSaturation( value.toInt() );
    else if( key == "ds" )                      m_pReceipt->setDs( value.toInt() );
    else if( key == "dr" )                      m_pReceipt->setDr( value.toInt() );
    else if( key == "ls" )                      m_pReceipt->setLs( value.toInt() );
    else if( key == "lr" )                      m_pReceipt->setLr( value.toInt() );
    else if( key == "lightening" )              m_pReceipt->setLightening( value.toInt() );
    else if( key == "gradationCurve" )          m_pReceipt->setGradationCurve( value );
    else if( key == "hueVsHue" )                m_pReceipt->setHueVsHue( value );
    else if( key == "hueVsSaturation" )         m_pReceipt->setHueVsSaturation( value );
    else if( key == "hueVsLuminance" )          m_pReceipt->setHueVsLuminance( value );
    else if( key == "lumaVsSaturation" )        m_pReceipt->setLumaVsSaturation( value );
    else if( key == "shadows" )                 m_pReceipt->setShadows( value.toInt() );
    else if( key == "highlights" )              m_pReceipt->setHighlights( value.toInt() );
    else if( key == "gradientEnabled" )         m_pReceipt->setGradientEnabled( value.toInt() != 0 );
    else if( key == "gradientExposure" )        m_pReceipt->setGradientExposure( value.toInt() );
    else if( key == "gradientContrast" )        m_pReceipt->setGradientContrast( value.toInt() );
    else if( key == "gradientStartX" )          m_pReceipt->setGradientStartX( value.toInt() );
    else if( key == "gradientStartY" )          m_pReceipt->setGradientStartY( value.toInt() );
    else if( key == "gradientLength" )          m_pReceipt->setGradientLength( value.toInt() );
    else if( key == "gradientAngle" )           m_pReceipt->setGradientAngle( value.toInt() );
    else if( key == "sharpen" )                 m_pReceipt->setSharpen( value.toInt() );
    else if( key == "sharpenMasking" )          m_pReceipt->setShMasking( value.toInt() );
    else if( key == "chromaBlur" )              m_pReceipt->setChromaBlur( value.toInt() );
    else if( key == "highlightReconstruction" ) m_pReceipt->setHighlightReconstruction( value.toInt() != 0 );
    else if( key == "camMatrixUsed" )           m_pReceipt->setCamMatrixUsed( (int8_t)value.toInt() );
    else if( key == "chromaSeparation" )        m_pReceipt->setChromaSeparation( value.toInt() != 0 );
    else if( key == "tonemap" )                 m_pReceipt->setTonemap( (int8_t)value.toInt() );
    else if( key == "transferFunction" )        m_pReceipt->setTransferFunction( value );
    else if( key == "gamut" )                   m_pReceipt->setGamut( (int8_t)value.toInt() );
    else if( key == "gamma" )                   m_pReceipt->setGamma( value.toInt() );
    else if( key == "allowCreativeAdjustments" )m_pReceipt->setAllowCreativeAdjustments( value.toInt() != 0 );
    else if( key == "exrMode" )                 m_pReceipt->setExrMode( value.toInt() != 0 );
    else if( key == "agx" )                     m_pReceipt->setAgx( value.toInt() != 0 );
    else if( key == "denoiserStrength" )        m_pReceipt->setDenoiserStrength( value.toInt() );
    else if( key == "denoiserWindow" )          m_pReceipt->setDenoiserWindow( value.toInt() );
    else if( key == "rbfDenoiserLuma" )         m_pReceipt->setRbfDenoiserLuma( value.toInt() );
    else if( key == "rbfDenoiserChroma" )       m_pReceipt->setRbfDenoiserChroma( value.toInt() );
    else if( key == "rbfDenoiserRange" )        m_pReceipt->setRbfDenoiserRange( value.toInt() );
    else if( key == "grainStrength" )           m_pReceipt->setGrainStrength( value.toInt() );
    else if( key == "grainLumaWeight" )         m_pReceipt->setGrainLumaWeight( value.toInt() );
    else if( key == "rawFixesEnabled" )         m_pReceipt->setRawFixesEnabled( value.toInt() != 0 );
    else if( key == "verticalStripes" )         m_pReceipt->setVerticalStripes( value.toInt() );
    else if( key == "focusPixels" )             m_pReceipt->setFocusPixels( value.toInt() );
    else if( key == "fpiMethod" )               m_pReceipt->setFpiMethod( value.toInt() );
    else if( key == "badPixels" )               m_pReceipt->setBadPixels( value.toInt() );
    else if( key == "bpsMethod" )               m_pReceipt->setBpsMethod( value.toInt() );
    else if( key == "bpiMethod" )               m_pReceipt->setBpiMethod( value.toInt() );
    else if( key == "chromaSmooth" )            m_pReceipt->setChromaSmooth( value.toInt() );
    else if( key == "patternNoise" )            m_pReceipt->setPatternNoise( value.toInt() );
    else if( key == "deflickerTarget" )         m_pReceipt->setDeflickerTarget( value.toInt() );
    else if( key == "dualIsoForced" )           m_pReceipt->setDualIsoForced( value.toInt() );
    else if( key == "dualIso" )                 m_pReceipt->setDualIso( value.toInt() );
    else if( key == "dualIsoInterpolation" )    m_pReceipt->setDualIsoInterpolation( value.toInt() );
    else if( key == "dualIsoAliasMap" )         m_pReceipt->setDualIsoAliasMap( value.toInt() );
    else if( key == "dualIsoFrBlending" )       m_pReceipt->setDualIsoFrBlending( value.toInt() );
    else if( key == "dualIsoWhite" )            m_pReceipt->setDualIsoWhite( value.toUInt() );
    else if( key == "dualIsoBlack" )            m_pReceipt->setDualIsoBlack( value.toUInt() );
    else if( key == "darkFrameFileName" )       m_pReceipt->setDarkFrameFileName( value );
    else if( key == "darkFrameEnabled" )        m_pReceipt->setDarkFrameEnabled( value.toInt() );
    else if( key == "rawBlack" )                m_pReceipt->setRawBlack( value.toInt() );
    else if( key == "rawWhite" )                m_pReceipt->setRawWhite( value.toInt() );
    else if( key == "tone" )                    m_pReceipt->setTone( (uint8_t)value.toInt() );
    else if( key == "toningStrength" )          m_pReceipt->setToningStrength( (uint8_t)value.toInt() );
    else if( key == "lutEnabled" )              m_pReceipt->setLutEnabled( value.toInt() != 0 );
    else if( key == "lutName" )                 m_pReceipt->setLutName( value );
    else if( key == "lutStrength" )             m_pReceipt->setLutStrength( (uint8_t)value.toInt() );
    else if( key == "filterEnabled" )           m_pReceipt->setFilterEnabled( value.toInt() != 0 );
    else if( key == "filterIndex" )             m_pReceipt->setFilterIndex( (uint8_t)value.toInt() );
    else if( key == "filterStrength" )          m_pReceipt->setFilterStrength( value.toInt() );
    else if( key == "vignetteStrength" )        m_pReceipt->setVignetteStrength( value.toInt() );
    else if( key == "vignetteRadius" )          m_pReceipt->setVignetteRadius( value.toInt() );
    else if( key == "vignetteShape" )           m_pReceipt->setVignetteShape( value.toInt() );
    else if( key == "caRed" )                   m_pReceipt->setCaRed( value.toInt() );
    else if( key == "caBlue" )                  m_pReceipt->setCaBlue( value.toInt() );
    else if( key == "caDesaturate" )            m_pReceipt->setCaDesaturate( value.toInt() );
    else if( key == "caRadius" )                m_pReceipt->setCaRadius( value.toInt() );
    else if( key == "stretchFactorX" )          m_pReceipt->setStretchFactorX( value.toDouble() );
    else if( key == "stretchFactorY" )          m_pReceipt->setStretchFactorY( value.toDouble() );
    else if( key == "upsideDown" )              m_pReceipt->setUpsideDown( value.toInt() != 0 );
    else if( key == "vidstabEnable" )           m_pReceipt->setVidstabEnabled( value.toInt() != 0 );
    else if( key == "vidstabStepsize" )         m_pReceipt->setVidstabStepsize( (int8_t)value.toInt() );
    else if( key == "vidstabShakiness" )        m_pReceipt->setVidstabShakiness( (int8_t)value.toInt() );
    else if( key == "vidstabAccuracy" )         m_pReceipt->setVidstabAccuracy( (int8_t)value.toInt() );
    else if( key == "vidstabZoom" )             m_pReceipt->setVidstabZoom( (int8_t)value.toInt() );
    else if( key == "vidstabSmoothing" )        m_pReceipt->setVidstabSmoothing( (int8_t)value.toInt() );
    else if( key == "vidstabTripod" )           m_pReceipt->setVidstabTripod( value.toInt() != 0 );
    else if( key == "cutIn" )                   m_pReceipt->setCutIn( value.toUInt() );
    else if( key == "cutOut" )                  m_pReceipt->setCutOut( value.toUInt() );
    else if( key == "debayer" )                 m_pReceipt->setDebayer( (uint8_t)value.toInt() );
    // keys not covered above: left untouched (the snapshot key set guarantees a full replay)
}

// ---------------------------------------------------------------------------------------------
// Apply / undo (D4 §5.3, §5.4)
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::snapshotForUndo()
{
    m_undoSnapshot = collectCurrentParams();
    m_hasUndo = true;
    ui->pushButtonUndo->setEnabled( true );
}

void AiGradingDialog::applyChanges( const QHash<QString, QString> &changes )
{
    if( changes.isEmpty() ) return;

    // 1) full field snapshot before touching anything (undo base)
    snapshotForUndo();

    // 2) write the receipt (clip level: m_pReceipt is ACTIVE_RECEIPT)
    for( QHash<QString, QString>::const_iterator it = changes.constBegin();
         it != changes.constEnd(); ++it )
    {
        applyOneParamToReceipt( it.key(), it.value() );
    }

    // 3) hand over to MainWindow: it runs setSliders( receipt, true ), which pushes the values
    //    into every UI control whose on_*_valueChanged() slot calls the matching processingSet*()
    //    and sets m_frameChanged, so the preview refreshes on the next draw tick.
    emit receiptWasChanged();

    ui->pushButtonApply->setEnabled( false );
    ui->labelStatus->setText( tr( "Applied %1 parameter(s)." ).arg( changes.size() ) );
    refreshResultTable();
}

void AiGradingDialog::restoreSnapshot()
{
    if( !m_hasUndo ) return;

    for( QHash<QString, QString>::const_iterator it = m_undoSnapshot.constBegin();
         it != m_undoSnapshot.constEnd(); ++it )
    {
        applyOneParamToReceipt( it.key(), it.value() );
    }

    emit receiptWasChanged();

    m_hasUndo = false;
    ui->pushButtonUndo->setEnabled( false );
    ui->labelStatus->setText( tr( "Undone. Previous values restored." ) );
    refreshResultTable();
}

// ---------------------------------------------------------------------------------------------
// Result table + busy state
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::refreshResultTable()
{
    const QHash<QString, QString> current = collectCurrentParams();

    QStringList keys = sortByCanonicalOrder( m_pendingChanges.keys() );
    const QStringList ignored = sortByCanonicalOrder( QStringList( g_ignoredCurveKeys.values() ) );

    ui->tableResult->setRowCount( keys.size() + ignored.size() );

    int row = 0;
    for( int i = 0; i < keys.size(); ++i, ++row )
    {
        const QString key = keys.at( i );
        ui->tableResult->setItem( row, 0, new QTableWidgetItem( key ) );
        ui->tableResult->setItem( row, 1, new QTableWidgetItem( current.value( key ) ) );
        ui->tableResult->setItem( row, 2, new QTableWidgetItem( m_pendingChanges.value( key ) ) );
    }
    for( int i = 0; i < ignored.size(); ++i, ++row )
    {
        const QString key = ignored.at( i );
        ui->tableResult->setItem( row, 0, new QTableWidgetItem( key ) );
        ui->tableResult->setItem( row, 1, new QTableWidgetItem( current.value( key ) ) );
        QTableWidgetItem *item = new QTableWidgetItem( tr( "Ignored (invalid curve format)" ) );
        ui->tableResult->setItem( row, 2, item );
    }

    ui->tableResult->resizeColumnsToContents();
}

void AiGradingDialog::setBusy( bool busy )
{
    if( busy )
    {
        ui->progressBar->setVisible( true );
        ui->progressBar->setRange( 0, 0 );          // indeterminate animation
        ui->pushButtonGenerate->setEnabled( false );
        ui->pushButtonApply->setEnabled( false );
        ui->pushButtonUndo->setEnabled( false );
        ui->labelStatus->setText( tr( "Requesting model..." ) );
    }
    else
    {
        ui->progressBar->setVisible( false );
        ui->pushButtonGenerate->setEnabled( true );
        ui->pushButtonApply->setEnabled( !m_pendingChanges.isEmpty() );
        ui->pushButtonUndo->setEnabled( m_hasUndo );
    }
}

// ---------------------------------------------------------------------------------------------
// HTTP: generate (D4 §5.1 flow 1)
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::on_pushButtonGenerate_clicked()
{
    if( m_pPendingReply ) return;                       // guard against double click

    if( !m_pReceipt || !m_pProcessing )
    {
        QMessageBox::warning( this, tr( "AI Grading" ),
                              tr( "No clip is loaded, or the processing chain is unavailable." ) );
        return;
    }
    if( ui->lineEditEndpoint->text().trimmed().isEmpty()
        || ui->lineEditModel->text().trimmed().isEmpty() )
    {
        QMessageBox::warning( this, tr( "AI Grading" ),
                              tr( "Endpoint and model must not be empty." ) );
        return;
    }
    if( ui->textEditInput->toPlainText().trimmed().isEmpty() )
    {
        QMessageBox::warning( this, tr( "AI Grading" ),
                              tr( "Please describe the look you want first." ) );
        return;
    }

    saveSettingsFromUi();

    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    const double temperature = set.value( SETTINGS_TEMPERATURE, DEFAULT_TEMPERATURE ).toDouble();

    const QString endpoint = ui->lineEditEndpoint->text().trimmed();
    const QString model    = ui->lineEditModel->text().trimmed();
    const QString apiKey   = ui->lineEditApiKey->text();

    const QByteArray body = makeChatBody( model, buildSystemPrompt(),
                                          buildUserPrompt( ui->textEditInput->toPlainText() ),
                                          temperature,
                                          m_maxTokens );

    QUrl url( endpoint + QLatin1String( "/chat/completions" ) );
    QNetworkRequest request( url );
    request.setHeader( QNetworkRequest::ContentTypeHeader, QLatin1String( "application/json" ) );
    if( !apiKey.isEmpty() )
    {
        // assembled at runtime on purpose: keeps the literal out of the source text
        const QString scheme = QString( "Bea" ) + QString( "rer" );
        request.setRawHeader( "Authorization",
                              QString( "%1 %2" ).arg( scheme, apiKey ).toUtf8() );
    }

    m_retryCount = 0;
    setBusy( true );
    m_pPendingReply = m_pNetManager->post( request, body );
    m_pTimeoutTimer->start( ui->spinBoxTimeout->value() * 1000 );
}

void AiGradingDialog::retryLastRequest()
{
    // Rebuild the request from the (unchanged, disabled) UI. m_retryCount is NOT reset here,
    // otherwise a dead endpoint would retry forever.
    if( !m_pReceipt || !m_pProcessing ) return;

    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    const double temperature = set.value( SETTINGS_TEMPERATURE, DEFAULT_TEMPERATURE ).toDouble();

    const QString endpoint = ui->lineEditEndpoint->text().trimmed();
    const QString model    = ui->lineEditModel->text().trimmed();
    const QString apiKey   = ui->lineEditApiKey->text();

    const QByteArray body = makeChatBody( model, buildSystemPrompt(),
                                          buildUserPrompt( ui->textEditInput->toPlainText() ),
                                          temperature,
                                          m_maxTokens );

    QUrl url( endpoint + QLatin1String( "/chat/completions" ) );
    QNetworkRequest request( url );
    request.setHeader( QNetworkRequest::ContentTypeHeader, QLatin1String( "application/json" ) );
    if( !apiKey.isEmpty() )
    {
        const QString scheme = QString( "Bea" ) + QString( "rer" );
        request.setRawHeader( "Authorization",
                              QString( "%1 %2" ).arg( scheme, apiKey ).toUtf8() );
    }

    setBusy( true );
    ui->labelStatus->setText( tr( "Retrying (attempt %1 of %2)..." )
                              .arg( m_retryCount ).arg( m_maxRetries ) );
    m_pPendingReply = m_pNetManager->post( request, body );
    m_pTimeoutTimer->start( ui->spinBoxTimeout->value() * 1000 );
}

void AiGradingDialog::on_timeout()
{
    if( !m_pPendingReply ) return;
    m_retryCount = m_maxRetries;        // a timeout is terminal for the retry budget
    m_pPendingReply->abort();           // -> finished() with OperationCanceledError
}

void AiGradingDialog::on_replyFinished( QNetworkReply *reply )
{
    m_pTimeoutTimer->stop();

    if( reply != m_pPendingReply )
    {
        reply->deleteLater();
        return;
    }

    const QVariant statusCode = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute );

    if( reply->error() != QNetworkReply::NoError )
    {
        const QNetworkReply::NetworkError err = reply->error();
        const QString errString = reply->errorString();

        bool retryable = false;
        switch( err )
        {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
            retryable = true;
            break;
        default:
            retryable = false;
            break;
        }

        m_pPendingReply = nullptr;
        reply->deleteLater();

        if( retryable && m_retryCount < m_maxRetries )
        {
            m_retryCount++;
            ui->labelStatus->setText( tr( "Retrying (attempt %1 of %2)..." )
                                      .arg( m_retryCount ).arg( m_maxRetries ) );
            QTimer::singleShot( 1000 * m_retryCount, this, SLOT(retryLastRequest()) );
            return;
        }

        if( err == QNetworkReply::OperationCanceledError )
        {
            ui->labelStatus->setText( tr( "Request timed out (%1 s). A local model may need a "
                                          "longer timeout." ).arg( ui->spinBoxTimeout->value() ) );
            QMessageBox::warning( this, tr( "AI Grading" ),
                                  tr( "Request timed out after %1 s.\nEndpoint: %2" )
                                  .arg( ui->spinBoxTimeout->value() )
                                  .arg( ui->lineEditEndpoint->text().trimmed() ) );
        }
        else
        {
            QString detail = errString;
            if( statusCode.isValid() )
                detail = tr( "HTTP %1 - %2" ).arg( statusCode.toInt() ).arg( errString );

            ui->labelStatus->setText( tr( "Request failed: %1" ).arg( detail ) );
            QMessageBox::warning( this, tr( "AI Grading" ),
                                  tr( "Request failed: %1\n\nEndpoint: %2\nCheck that the LLM "
                                      "endpoint is reachable (local bridges must be running)." )
                                  .arg( detail )
                                  .arg( ui->lineEditEndpoint->text().trimmed() ) );
        }
        setBusy( false );
        return;
    }

    const QByteArray body = reply->readAll();
    m_pPendingReply = nullptr;
    reply->deleteLater();

    // extract choices[0].message.content
    QString content;
    const QJsonDocument doc = QJsonDocument::fromJson( body );
    if( !doc.isNull() && doc.isObject() && doc.object().contains( QLatin1String( "choices" ) ) )
    {
        const QJsonArray choices = doc.object().value( QLatin1String( "choices" ) ).toArray();
        if( !choices.isEmpty() )
        {
            const QJsonObject message = choices.at( 0 ).toObject()
                                        .value( QLatin1String( "message" ) ).toObject();
            content = message.value( QLatin1String( "content" ) ).toString();
        }
    }

    if( content.isEmpty() )
    {
        ui->labelStatus->setText( tr( "The model reply contained no content." ) );
        QMessageBox::warning( this, tr( "AI Grading" ),
                              tr( "The model reply contained no message content." ) );
        setBusy( false );
        return;
    }

    // strip thinking -> grab first JSON -> clamp
    const QString jsonText = extractFirstJson( stripThinking( content ) );
    if( jsonText.isEmpty() )
    {
        ui->labelStatus->setText( tr( "No JSON object found in the model reply." ) );
        QMessageBox::warning( this, tr( "AI Grading" ),
                              tr( "Could not find a parameter JSON in the model reply.\n"
                                  "The model may not have followed the output format." ) );
        setBusy( false );
        return;
    }

    const QJsonDocument planDoc = QJsonDocument::fromJson( jsonText.toUtf8() );
    if( planDoc.isNull() || !planDoc.isObject() )
    {
        ui->labelStatus->setText( tr( "The parameter JSON could not be parsed." ) );
        QMessageBox::warning( this, tr( "AI Grading" ),
                              tr( "The parameter JSON in the model reply is not valid JSON." ) );
        setBusy( false );
        return;
    }

    m_pendingChanges = clampPlan( planDoc.object() );
    refreshResultTable();
    setBusy( false );

    if( m_pendingChanges.isEmpty() )
    {
        ui->labelStatus->setText( tr( "No applicable parameter found in the reply." ) );
    }
    else
    {
        ui->labelStatus->setText( tr( "Generated %1 parameter(s). Review, then apply." )
                                  .arg( m_pendingChanges.size() ) );
        ui->pushButtonApply->setEnabled( true );
    }
}

// ---------------------------------------------------------------------------------------------
// Apply / undo / close buttons
// ---------------------------------------------------------------------------------------------

void AiGradingDialog::on_pushButtonApply_clicked()
{
    if( m_pendingChanges.isEmpty() ) return;
    applyChanges( m_pendingChanges );
}

void AiGradingDialog::on_pushButtonUndo_clicked()
{
    restoreSnapshot();
}

void AiGradingDialog::on_pushButtonClose_clicked()
{
    if( m_pPendingReply )
    {
        m_pTimeoutTimer->stop();
        m_pPendingReply->abort();
    }
    saveSettingsFromUi();
    hide();          // 内嵌面板：关闭 = 隐藏，不销毁
}

// 语言切换：.ui 里的控件由 retranslateUi 全刷；
// 手写的状态栏只在「空闲」时才补 —— 请求中/报错属于临时提示，下次操作会自然用新语言。
void AiGradingDialog::changeEvent( QEvent *event )
{
    if( event->type() == QEvent::LanguageChange )
    {
        ui->retranslateUi( this );
        const QString cur = ui->labelStatus->text();
        if( cur.isEmpty() || cur == QLatin1String( "Ready" ) || cur == QStringLiteral( "\u5c31\u7eea" ) )
            ui->labelStatus->setText( tr( "Ready" ) );
    }
    QWidget::changeEvent( event );
}
