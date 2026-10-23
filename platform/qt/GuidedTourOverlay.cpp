/*!
 * \file GuidedTourOverlay.cpp
 * \brief 功能导览浮层：全屏半透明遮罩 + 目标区域挖洞高亮 + 说明气泡
 */

#include "GuidedTourOverlay.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const char *kAccentColor  = "#c084fc";   // 主色：描边 / 圆点 / 强调文字
const char *kAccentStrong = "#a855f7";   // 主按钮底色
const char *kBubbleBg     = "#f5f0fa";   // 气泡浅色底
const char *kBodyColor    = "#2b2140";   // 标题深色文字
const char *kMutedColor   = "#4a3f63";   // 正文文字

const int kBubbleWidth = 400;
const int kBubbleGap   = 16;
}

GuidedTourOverlay::GuidedTourOverlay( QWidget *parent )
    : QWidget( parent )
    , m_index( 0 )
    , m_hasTarget( false )
    , m_bubbleSide( SideCenter )
    , m_pBubble( nullptr )
    , m_pLabelStep( nullptr )
    , m_pLabelTitle( nullptr )
    , m_pLabelBody( nullptr )
    , m_pLabelProgress( nullptr )
    , m_pButtonSkip( nullptr )
    , m_pButtonPrev( nullptr )
    , m_pButtonNext( nullptr )
    , m_pButtonFinish( nullptr )
    , m_pSyncTimer( nullptr )
{
    // 作为宿主窗口的子 widget 覆盖层，而不是独立 Qt::Tool 窗口。
    // 必须避免 Qt::Tool：当主窗口弹出模态对话框（例如启动时的检查更新）时，
    // Qt 会自动隐藏并销毁 Tool 窗口，导览会在显示十几秒后凭空消失（实测 10~16 秒）。
    // 子 widget 不受模态对话框影响，也不需要换算屏幕坐标，并且不会抢占焦点。
    setAttribute( Qt::WA_NoSystemBackground, true );
    setAutoFillBackground( false );
    setFocusPolicy( Qt::StrongFocus );
    setCursor( Qt::ArrowCursor );
    setMouseTracking( false );

    buildSteps();
    buildUi();

    // 主窗口移动/改尺寸/停靠栏变化时同步遮罩与挖洞位置（比逐个 connect 更省事）
    m_pSyncTimer = new QTimer( this );
    m_pSyncTimer->setInterval( 200 );
    connect( m_pSyncTimer, SIGNAL( timeout() ), this, SLOT( syncGeometry() ) );
}

GuidedTourOverlay::~GuidedTourOverlay()
{
}

/*! 11 步导览文案。中文文案直接写死在这里，tr() 包裹以便后续 lupdate 提取。 */
void GuidedTourOverlay::buildSteps( void )
{
    m_steps.clear();

    auto add = [ this ]( const QString &title, const QString &body,
                         const QStringList &targets, bool needAiPanel )
    {
        Step s;
        s.title       = title;
        s.body        = body;
        s.targets     = targets;
        s.needAiPanel = needAiPanel;
        m_steps.append( s );
    };

    add( tr( "Welcome to Magic Lantern Workbench" ),
         tr( "Welcome to Magic Lantern Workbench! This short tour covers the core features." ),
         QStringList(), false );

    add( tr( "Import buttons" ),
         tr( "Two import buttons sit above the media pool: \"Video\" opens MLV clips, \"Photo\" opens CR2/DNG and other RAW files. You can also drag files straight in." ),
         QStringList() << QStringLiteral( "toolButtonBubbleVideo" )
                       << QStringLiteral( "toolButtonBubblePhoto" ), false );

    add( tr( "Thumbnail size and filter" ),
         tr( "The slider above the media pool changes thumbnail size (the value shows on its right); the dropdown below filters by type (all / photos only / videos only)." ),
         QStringList() << QStringLiteral( "horizontalSliderPreviewSize" )
                       << QStringLiteral( "comboBoxTypeFilter" ), false );

    add( tr( "Preview area" ),
         tr( "The centre is the preview: scroll to zoom, drag to pan. Ctrl+0 fits the window, Ctrl+1 shows 1:1 detail." ),
         QStringList() << QStringLiteral( "graphicsView" ), false );

    add( tr( "Edit panel" ),
         tr( "The right side is the edit panel with RAW correction, basic grading and AI grading sections; each one collapses." ),
         QStringList() << QStringLiteral( "dockWidgetEdit" ), false );

    add( tr( "Dual ISO panel" ),
         tr( "The Dual ISO panel in this column merges dual-ISO footage. Important: turn \"Force\" on and set exposure matching to \"Histogram\" - a wrong choice silently produces a bad image." ),
         QStringList() << QStringLiteral( "dockWidgetEdit" ), false );

    add( tr( "AI grading entry" ),
         tr( "Press Ctrl+Alt+G, or use the top-level \"AI grading\" menu. The panel is opening on the right - type your request into the box there." ),
         QStringList() << QStringLiteral( "frameAiGrading" ) << QStringLiteral( "menuBar" ), false );

    add( tr( "AI instruction box" ),
         tr( "Describe the look in plain language, e.g. \"a bit dark overall, lift it slightly but keep the highlights, warmer colour, soft daylight feel\"." ),
         QStringList() << QStringLiteral( "textEditInput" ) << QStringLiteral( "dockWidgetEdit" ), true );

    add( tr( "Generate grading" ),
         tr( "Click \"Generate\": the AI returns parameters within a second or two and fills the result table." ),
         QStringList() << QStringLiteral( "pushButtonGenerate" ) << QStringLiteral( "dockWidgetEdit" ), true );

    add( tr( "Apply" ),
         tr( "Check the values, then click \"Apply\" to apply them to the image in a single step." ),
         QStringList() << QStringLiteral( "pushButtonApply" ) << QStringLiteral( "dockWidgetEdit" ), true );

    add( tr( "Export photos and videos" ),
         tr( "Click \"Export\" in the toolbar - it routes by itself: photos are exported as a batch (select several first), videos export the whole clip. If you only want the single frame you are looking at, use \"Export current frame\" right next to it." ),
         QStringList() << QStringLiteral( "toolButtonExport" )
                       << QStringLiteral( "toolButtonExportFrame" )
                       << QStringLiteral( "actionExport" ), false );

    add( tr( "Tour complete" ),
         tr( "That is the tour! Open it again any time from \"Help -> Guided tour\". Happy shooting!" ),
         QStringList(), false );
}

void GuidedTourOverlay::buildUi( void )
{
    m_pBubble = new QFrame( this );
    m_pBubble->setObjectName( QStringLiteral( "tourBubble" ) );
    m_pBubble->setAttribute( Qt::WA_StyledBackground, true );
    m_pBubble->setStyleSheet( QStringLiteral(
        "QFrame#tourBubble {"
        "  background-color: %1;"
        "  border: 2px solid %2;"
        "  border-radius: 14px;"
        "}"
        "QFrame#tourBubble QLabel { color: %3; background-color: transparent; border: none; }"
        "QLabel#tourStep { color: %2; font-size: 12px; font-weight: bold; }"
        "QLabel#tourTitle { color: %3; font-size: 16px; font-weight: bold; }"
        "QLabel#tourBody { color: %4; font-size: 13px; }"
        "QLabel#tourDots { color: %2; font-size: 12px; }"
        "QFrame#tourBubble QPushButton {"
        "  border-radius: 8px; padding: 6px 14px; font-size: 13px; min-width: 56px;"
        "}"
        "QPushButton#tourNext, QPushButton#tourFinish {"
        "  background-color: %5; color: #ffffff; border: 1px solid %5; font-weight: bold;"
        "}"
        "QPushButton#tourNext:hover, QPushButton#tourFinish:hover {"
        "  background-color: #9333ea; border-color: #9333ea;"
        "}"
        "QPushButton#tourPrev, QPushButton#tourSkip {"
        "  background-color: transparent; color: #7c5ba6; border: 1px solid #d8c7ef;"
        "}"
        "QPushButton#tourPrev:hover, QPushButton#tourSkip:hover { background-color: #ece2f8; }"
        "QPushButton#tourPrev:disabled { color: #b9aecb; border-color: #e4dcf0; }"
    ).arg( QString::fromLatin1( kBubbleBg ),
           QString::fromLatin1( kAccentColor ),
           QString::fromLatin1( kBodyColor ),
           QString::fromLatin1( kMutedColor ),
           QString::fromLatin1( kAccentStrong ) ) );

    QVBoxLayout *lay = new QVBoxLayout( m_pBubble );
    lay->setContentsMargins( 20, 16, 20, 14 );
    lay->setSpacing( 8 );

    m_pLabelStep = new QLabel( m_pBubble );
    m_pLabelStep->setObjectName( QStringLiteral( "tourStep" ) );

    m_pLabelTitle = new QLabel( m_pBubble );
    m_pLabelTitle->setObjectName( QStringLiteral( "tourTitle" ) );
    m_pLabelTitle->setWordWrap( true );

    m_pLabelBody = new QLabel( m_pBubble );
    m_pLabelBody->setObjectName( QStringLiteral( "tourBody" ) );
    m_pLabelBody->setWordWrap( true );
    m_pLabelBody->setTextFormat( Qt::PlainText );
    m_pLabelBody->setTextInteractionFlags( Qt::NoTextInteraction );

    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->setSpacing( 8 );
    btnLay->setContentsMargins( 0, 6, 0, 0 );

    m_pButtonSkip = new QPushButton( tr( "Skip tour" ), m_pBubble );
    m_pButtonSkip->setObjectName( QStringLiteral( "tourSkip" ) );
    m_pButtonSkip->setCursor( Qt::PointingHandCursor );

    m_pButtonPrev = new QPushButton( tr( "Back" ), m_pBubble );
    m_pButtonPrev->setObjectName( QStringLiteral( "tourPrev" ) );
    m_pButtonPrev->setCursor( Qt::PointingHandCursor );

    m_pButtonNext = new QPushButton( tr( "Next" ), m_pBubble );
    m_pButtonNext->setObjectName( QStringLiteral( "tourNext" ) );
    m_pButtonNext->setCursor( Qt::PointingHandCursor );
    m_pButtonNext->setDefault( true );

    m_pButtonFinish = new QPushButton( tr( "Done" ), m_pBubble );
    m_pButtonFinish->setObjectName( QStringLiteral( "tourFinish" ) );
    m_pButtonFinish->setCursor( Qt::PointingHandCursor );

    btnLay->addWidget( m_pButtonSkip );
    btnLay->addStretch( 1 );
    btnLay->addWidget( m_pButtonPrev );
    btnLay->addWidget( m_pButtonNext );
    btnLay->addWidget( m_pButtonFinish );

    m_pLabelProgress = new QLabel( m_pBubble );
    m_pLabelProgress->setObjectName( QStringLiteral( "tourDots" ) );
    m_pLabelProgress->setTextFormat( Qt::RichText );
    m_pLabelProgress->setAlignment( Qt::AlignHCenter );

    lay->addWidget( m_pLabelStep );
    lay->addWidget( m_pLabelTitle );
    lay->addWidget( m_pLabelBody );
    lay->addLayout( btnLay );
    lay->addWidget( m_pLabelProgress );

    connect( m_pButtonSkip,   SIGNAL( clicked() ), this, SLOT( skipTour() ) );
    connect( m_pButtonPrev,   SIGNAL( clicked() ), this, SLOT( previousStep() ) );
    connect( m_pButtonNext,   SIGNAL( clicked() ), this, SLOT( nextStep() ) );
    connect( m_pButtonFinish, SIGNAL( clicked() ), this, SLOT( finishTour() ) );
}

void GuidedTourOverlay::start( void )
{
    if( m_steps.isEmpty() ) return;

    syncGeometry();
    show();
    raise();
    // 不调用 activateWindow()：子 widget 不主动激活宿主窗口（避免抢焦点）
    setFocus( Qt::OtherFocusReason );
    applyStep( 0 );
    m_pSyncTimer->start();
}

bool GuidedTourOverlay::currentStepNeedsAiPanel( void ) const
{
    if( m_index < 0 || m_index >= m_steps.count() ) return false;
    return m_steps.at( m_index ).needAiPanel;
}

/*! 宿主（MainWindow）在接到 stepAboutToShow 后准备好目标，这里再查找一次。 */
void GuidedTourOverlay::applyStep( int index )
{
    if( m_steps.isEmpty() ) return;


    m_index = qBound( 0, index, m_steps.count() - 1 );
    const Step &s = m_steps.at( m_index );

    emit stepAboutToShow( m_index );
    // 让宿主刚创建/显示的控件完成布局，之后 mapToGlobal 才有正确坐标
    QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );

    m_pCurrentTarget = resolveTarget( s.targets );

    // 目标可能被滚出视野（右侧 Edit 栏内容很长，AI 分组在最底部）：
    // 光 isVisible()==true 不代表用户看得见，必须先把所有祖先滚动区滚到它那里，
    // 再重新取坐标，否则高亮会画在屏幕外、气泡也贴不到目标旁边。
    if( m_pCurrentTarget )
    {
        QWidget *p = m_pCurrentTarget->parentWidget();
        while( p )
        {
            if( QScrollArea *sa = qobject_cast<QScrollArea*>( p ) )
                sa->ensureWidgetVisible( m_pCurrentTarget, 0, 40 );
            if( p == m_pCurrentTarget->window() ) break;
            p = p->parentWidget();
        }
        QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
    }

    bool ok = false;
    m_targetRect = computeTargetRect( &ok );
    m_hasTarget  = ok;

    m_pLabelStep->setText( tr( "Step %1 of %2" ).arg( m_index + 1 ).arg( m_steps.count() ) );
    m_pLabelTitle->setText( s.title );
    m_pLabelBody->setText( s.body );
    updateProgressDots();

    const bool isLast = ( m_index == m_steps.count() - 1 );
    m_pButtonPrev->setEnabled( m_index > 0 );
    m_pButtonSkip->setVisible( !isLast );
    m_pButtonNext->setVisible( !isLast );
    m_pButtonFinish->setVisible( isLast );

    layoutBubble();
    update();

    // 排障用：MLVAPP_TOUR_DEBUG=1 时打印每步的定位结果（正常使用零输出）
    if( qEnvironmentVariableIsSet( "MLVAPP_TOUR_DEBUG" ) )
    {
        qDebug() << "[TOUR]" << ( m_index + 1 ) << "/" << m_steps.count()
                 << s.title.left( 30 )
                 << "hasTarget=" << m_hasTarget
                 << "rect=" << m_targetRect
                 << "target=" << ( m_pCurrentTarget ? m_pCurrentTarget->objectName() : QString( "<none>" ) );
    }
}

QWidget *GuidedTourOverlay::resolveTarget( const QStringList &names ) const
{
    QWidget *host = parentWidget();
    if( !host ) return nullptr;

    for( const QString &name : names )
    {
        if( name.isEmpty() ) continue;
        QWidget *w = host->findChild<QWidget*>( name );
        if( w && w->isVisible() && w->width() > 4 && w->height() > 4 )
            return w;
    }
    return nullptr;
}

/*! 目标 -> 浮层局部坐标矩形；被滚动区/边界裁掉太多则视为不可用。 */
QRect GuidedTourOverlay::computeTargetRect( bool *ok ) const
{
    if( ok ) *ok = false;
    QWidget *w = m_pCurrentTarget.data();
    if( !w || !w->isVisible() ) return QRect();

    const QPoint origin = mapToGlobal( QPoint( 0, 0 ) );
    QRect r( w->mapToGlobal( QPoint( 0, 0 ) ) - origin, w->size() );

    // 裁剪到所有可见祖先（QScrollArea 的 viewport 也在链上），避免高亮到面板外
    QWidget *p = w->parentWidget();
    while( p )
    {
        if( p->isVisible() )
        {
            const QRect pr( p->mapToGlobal( QPoint( 0, 0 ) ) - origin, p->size() );
            r = r.intersected( pr );
        }
        if( p == w->window() ) break;
        p = p->parentWidget();
    }

    r = r.intersected( rect() );
    // 阈值要足够低：水平滑块这类细控件实测高度只有 15px，卡 16px 会把它判成不可用
    if( r.width() < 24 || r.height() < 10 ) return QRect();

    // 大面积被裁掉时挖洞会误导用户，不如居中讲文字
    const qint64 visibleArea = qint64( r.width() ) * qint64( r.height() );
    const qint64 targetArea  = qint64( w->width() ) * qint64( w->height() );
    if( targetArea > 0 && visibleArea * 4 < targetArea ) return QRect();

    r.adjust( -4, -4, 4, 4 );
    if( ok ) *ok = true;
    return r;
}

void GuidedTourOverlay::layoutBubble( void )
{
    if( !m_pBubble ) return;

    const int bh = m_pBubble->sizeHint().height();
    m_pBubble->resize( kBubbleWidth, bh );

    const QRect area = rect().adjusted( 12, 12, -12, -12 );
    int x = area.left() + ( area.width() - kBubbleWidth ) / 2;
    int y = area.top() + ( area.height() - bh ) / 2;
    m_bubbleSide = SideCenter;

    if( m_hasTarget && !m_targetRect.isEmpty() )
    {
        const int yMax = qMax( area.top(), area.bottom() - bh );
        const int xMax = qMax( area.left(), area.right() - kBubbleWidth );

        if( m_targetRect.right() + kBubbleGap + kBubbleWidth <= area.right() )
        {
            x = m_targetRect.right() + kBubbleGap;
            y = qBound( area.top(), m_targetRect.center().y() - bh / 2, yMax );
            m_bubbleSide = SideRight;
        }
        else if( m_targetRect.left() - kBubbleGap - kBubbleWidth >= area.left() )
        {
            x = m_targetRect.left() - kBubbleGap - kBubbleWidth;
            y = qBound( area.top(), m_targetRect.center().y() - bh / 2, yMax );
            m_bubbleSide = SideLeft;
        }
        else if( m_targetRect.bottom() + kBubbleGap + bh <= area.bottom() )
        {
            y = m_targetRect.bottom() + kBubbleGap;
            x = qBound( area.left(), m_targetRect.center().x() - kBubbleWidth / 2, xMax );
            m_bubbleSide = SideBelow;
        }
        else if( m_targetRect.top() - kBubbleGap - bh >= area.top() )
        {
            y = m_targetRect.top() - kBubbleGap - bh;
            x = qBound( area.left(), m_targetRect.center().x() - kBubbleWidth / 2, xMax );
            m_bubbleSide = SideAbove;
        }
    }

    m_pBubble->move( x, y );
    m_pBubble->raise();
}

void GuidedTourOverlay::updateProgressDots( void )
{
    QString dots;
    for( int i = 0; i < m_steps.count(); ++i )
    {
        const bool active = ( i <= m_index );
        dots += QStringLiteral( "<span style=\"color:%1;\">%2</span>&nbsp;" )
                    .arg( active ? QString::fromLatin1( kAccentStrong )
                                 : QStringLiteral( "#d8cfe8" ) )
                    .arg( active ? QStringLiteral( "&#9679;" ) : QStringLiteral( "&#9675;" ) );
    }
    m_pLabelProgress->setText( dots );
}

void GuidedTourOverlay::drawArrow( QPainter &p ) const
{
    if( !m_pBubble || !m_hasTarget || m_bubbleSide == SideCenter ) return;

    const QRect b = m_pBubble->geometry();
    const int tip  = 10;
    const int half = 8;
    QPolygonF tri;

    switch( m_bubbleSide )
    {
    case SideRight:   // 气泡在目标右侧，箭头朝左
    {
        const int y = qBound( b.top() + 24, m_targetRect.center().y(), b.bottom() - 24 );
        tri << QPointF( b.left() + 2, y - half )
            << QPointF( b.left() - tip, y )
            << QPointF( b.left() + 2, y + half );
        break;
    }
    case SideLeft:    // 气泡在目标左侧，箭头朝右
    {
        const int y = qBound( b.top() + 24, m_targetRect.center().y(), b.bottom() - 24 );
        tri << QPointF( b.right() - 2, y - half )
            << QPointF( b.right() + tip, y )
            << QPointF( b.right() - 2, y + half );
        break;
    }
    case SideBelow:   // 气泡在目标下方，箭头朝上
    {
        const int x = qBound( b.left() + 24, m_targetRect.center().x(), b.right() - 24 );
        tri << QPointF( x - half, b.top() + 2 )
            << QPointF( x, b.top() - tip )
            << QPointF( x + half, b.top() + 2 );
        break;
    }
    case SideAbove:   // 气泡在目标上方，箭头朝下
    {
        const int x = qBound( b.left() + 24, m_targetRect.center().x(), b.right() - 24 );
        tri << QPointF( x - half, b.bottom() - 2 )
            << QPointF( x, b.bottom() + tip )
            << QPointF( x + half, b.bottom() - 2 );
        break;
    }
    default:
        return;
    }

    p.setPen( QPen( QColor( QString::fromLatin1( kAccentColor ) ), 2 ) );
    p.setBrush( QColor( QString::fromLatin1( kAccentColor ) ) );
    p.drawPolygon( tri );
}

void GuidedTourOverlay::paintEvent( QPaintEvent *event )
{
    Q_UNUSED( event );

    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing, true );

    const QColor shade( 0, 0, 0, 170 );
    const QColor accent( QString::fromLatin1( kAccentColor ) );

    // 目标不可用：整屏压暗，气泡居中
    if( !m_hasTarget || m_targetRect.isEmpty() )
    {
        p.fillRect( rect(), shade );
        return;
    }

    // 用 even-odd 相减的路径挖洞：外部保留 170 的黑，洞内完全透明（看到真实界面）
    QPainterPath outer;
    outer.addRect( QRectF( rect() ) );
    QPainterPath inner;
    inner.addRoundedRect( QRectF( m_targetRect ), 10, 10 );
    p.fillPath( outer.subtracted( inner ), shade );

    // 豁口描边：外侧淡光 + 内侧实线，都是 #c084fc
    QColor glow = accent;
    glow.setAlpha( 80 );
    p.setBrush( Qt::NoBrush );
    p.setPen( QPen( glow, 8 ) );
    p.drawRoundedRect( QRectF( m_targetRect ).adjusted( -3, -3, 3, 3 ), 12, 12 );
    p.setPen( QPen( accent, 2 ) );
    p.drawRoundedRect( QRectF( m_targetRect ), 10, 10 );

    drawArrow( p );
}

void GuidedTourOverlay::syncGeometry( void )
{
    QWidget *host = parentWidget();
    if( !host ) return;

    // 子 widget 坐标以父窗口左上角为原点（不再需要换算屏幕坐标）
    const QRect g( 0, 0, host->width(), host->height() );
    const bool geomChanged = ( geometry() != g );
    if( geomChanged ) setGeometry( g );

    // 主窗口布局变化（例如 AI 面板被展开）会移动目标，这里重算挖洞位置
    const QRect oldRect = m_targetRect;
    bool ok = false;
    const QRect newRect = computeTargetRect( &ok );
    if( newRect != oldRect || ok != m_hasTarget )
    {
        m_targetRect = newRect;
        m_hasTarget  = ok;
        layoutBubble();
        update();
    }
    else if( geomChanged )
    {
        m_pBubble->raise();
        update();
    }

    // 遮罩必须始终位于宿主所有子控件（含停靠栏）之上
    raise();
}

void GuidedTourOverlay::startAt( int index )
{
    if( m_steps.isEmpty() ) return;

    start();                                        // 建立遮罩 + 第 1 步
    const int target = qBound( 0, index, m_steps.count() - 1 );
    for( int i = 0; i < target; ++i )
    {
        if( m_index >= m_steps.count() - 1 ) break;
        nextStep();                                 // 内部逐级触发 stepAboutToShow（会按需展开面板）
        QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
    }
}

void GuidedTourOverlay::nextStep( void )
{
    if( m_index >= m_steps.count() - 1 ) { finishTour(); return; }
    applyStep( m_index + 1 );
}

void GuidedTourOverlay::previousStep( void )
{
    if( m_index <= 0 ) return;
    applyStep( m_index - 1 );
}

void GuidedTourOverlay::skipTour( void )
{
    finishTour();
}

void GuidedTourOverlay::finishTour( void )
{
    if( m_pSyncTimer ) m_pSyncTimer->stop();
    emit tourFinished();
    hide();
    deleteLater();
}

void GuidedTourOverlay::keyPressEvent( QKeyEvent *event )
{
    switch( event->key() )
    {
    case Qt::Key_Escape:
        skipTour();
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        nextStep();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Up:
        previousStep();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent( event );
}

/*! 点击遮罩空白处不前进，只是吞掉事件（防误触）。 */
void GuidedTourOverlay::mousePressEvent( QMouseEvent *event )
{
    event->accept();
}

void GuidedTourOverlay::mouseReleaseEvent( QMouseEvent *event )
{
    event->accept();
}

void GuidedTourOverlay::showEvent( QShowEvent *event )
{
    QWidget::showEvent( event );
    syncGeometry();
}