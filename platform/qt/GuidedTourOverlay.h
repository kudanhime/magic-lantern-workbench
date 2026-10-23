/*!
 * \file GuidedTourOverlay.h
 * \brief 功能导览（coach mark）：半透明遮罩 + 目标控件挖洞高亮 + 说明气泡
 *
 * 覆盖在主窗口之上，用 11 个步骤介绍 MLVApp 中文版的核心流程。
 * 点击遮罩空白处不前进（防误触），Esc = 跳过导览。
 */

#ifndef GUIDEDTOUROVERLAY_H
#define GUIDEDTOUROVERLAY_H

#include <QWidget>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QList>

class QFrame;
class QLabel;
class QPushButton;
class QTimer;
class QPainter;

class GuidedTourOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit GuidedTourOverlay( QWidget *parent = nullptr );
    ~GuidedTourOverlay();

    void start( void );                     // 显示遮罩并从第 1 步开始
    void startAt( int index );              // 直接跳到第 index+1 步（调试/自检用）

signals:
    /* 进入某一步之前发出。宿主可趁此准备好目标控件（例如按需创建 AI 调色面板），
       浮层随后会重新查找目标并排布气泡。 */
    void stepAboutToShow( int index );
    void tourFinished( void );

protected:
    void paintEvent( QPaintEvent *event ) override;
    void keyPressEvent( QKeyEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;
    void mouseReleaseEvent( QMouseEvent *event ) override;
    void showEvent( QShowEvent *event ) override;

private slots:
    void nextStep( void );
    void previousStep( void );
    void skipTour( void );
    void finishTour( void );
    void syncGeometry( void );

private:
    struct Step
    {
        QString     title;
        QString     body;
        QStringList targets;      // 候选目标控件 objectName；全空 = 居中气泡、不挖洞
        bool        needAiPanel;  // 该步之前需要 AI 调色面板已创建并可见
    };

    // 气泡相对目标的位置
    enum BubbleSide { SideCenter, SideRight, SideLeft, SideAbove, SideBelow };

    void     buildSteps( void );
    void     buildUi( void );
    void     applyStep( int index );
    QWidget *resolveTarget( const QStringList &names ) const;
    QRect    computeTargetRect( bool *ok ) const;
    void     layoutBubble( void );
    void     updateProgressDots( void );
    void     drawArrow( QPainter &p ) const;
    bool     currentStepNeedsAiPanel( void ) const;

    QList<Step>      m_steps;
    int              m_index;
    QPointer<QWidget> m_pCurrentTarget;   // 当前步骤高亮的目标（可能为空）
    QRect            m_targetRect;        // 挖洞矩形（浮层局部坐标）
    bool             m_hasTarget;
    BubbleSide       m_bubbleSide;

    QFrame      *m_pBubble;
    QLabel      *m_pLabelStep;
    QLabel      *m_pLabelTitle;
    QLabel      *m_pLabelBody;
    QLabel      *m_pLabelProgress;
    QPushButton *m_pButtonSkip;
    QPushButton *m_pButtonPrev;
    QPushButton *m_pButtonNext;
    QPushButton *m_pButtonFinish;
    QTimer      *m_pSyncTimer;
};

#endif // GUIDEDTOUROVERLAY_H