/*!
 * \file AiGradingDialog.h
 * \brief AI language grading dialog - header
 *        Natural-language grading of the current clip via an LLM endpoint.
 */

#ifndef AIGRADINGDIALOG_H
#define AIGRADINGDIALOG_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QString>
#include <QHash>
#include <QVariant>

#include "../../src/mlv_include.h"
#include "ReceiptSettings.h"

namespace Ui {
class AiGradingDialog;
}

// One pending parameter change: key + old value + new value (for result table / undo)
struct AiParamChange {
    QString key;
    QString oldValue;   // display value (stringified receipt getter)
    QString newValue;   // clamped LLM output value
};

class AiGradingDialog : public QWidget
{
    Q_OBJECT

public:
    // Constructor signature mirrors SingleFrameExportDialog (MW.cpp:7057 call site)
    explicit AiGradingDialog( QWidget *parent,
                              ReceiptSettings *pReceipt,      // ACTIVE_RECEIPT, not owned
                              processingObject_t *pProcessing  // m_pProcessingObject, not owned
                              = nullptr );
    ~AiGradingDialog();

    // 切换素材时重新指向当前 clip 的 receipt + processing（面板常驻必需）
    void setContext( ReceiptSettings *pReceipt, processingObject_t *pProcessing );

signals:
    void receiptWasChanged();      // emitted after changes were applied; MainWindow re-syncs sliders

private slots:
    void on_pushButtonGenerate_clicked();   // "Generate grade"
    void on_pushButtonApply_clicked();      // "Apply"
    void on_pushButtonUndo_clicked();       // "Undo last step"
    void on_pushButtonClose_clicked();      // "Close"
    void on_spinBoxMaxTokens_valueChanged( int value );   // output cap -> m_maxTokens

public:
    //供「AI 智能调色 → LLM 设置…」弹窗保存后重读配置
    void reloadLlmSettings();
    void on_replyFinished( QNetworkReply *reply );  // HTTP callback
    void on_timeout();                      // timeout timer callback
    void retryLastRequest();                // retry after retryable network error

protected:
    void changeEvent( QEvent *event );      // 语言切换：重刷面板文字

private:
    // -- parameter collection / apply / undo --
    QHash<QString, QString> collectCurrentParams() const;  // receipt -> current values
    void applyChanges( const QHash<QString, QString> &changes );
    void snapshotForUndo();                 // full-field snapshot before applying
    void restoreSnapshot();                 // undo: snapshot -> receipt + processing
    // -- prompt / parse (C++ port of D1 §2 §4) --
    QString buildSystemPrompt() const;
    QString buildUserPrompt( const QString &naturalLanguage ) const;
    static QString stripThinking( const QString &raw );          // D1 §4.1 step 1
    static QString extractFirstJson( const QString &text );      // D1 §4.1 steps 2-3
    static QHash<QString, QString> clampPlan( const QJsonObject &obj ); // D1 §4.2
    // -- single key application --
    void applyOneParamToReceipt( const QString &key, const QString &value );
    // -- UI helpers --
    void refreshResultTable();
    void setBusy( bool busy );
    void loadSettingsToUi();
    void saveSettingsFromUi();

    Ui::AiGradingDialog *ui;
    ReceiptSettings *m_pReceipt;            // not owned, not deleted (clip life cycle belongs to SessionModel)
    processingObject_t *m_pProcessing;      // not owned
    QNetworkAccessManager *m_pNetManager;   // owned by this class
    QNetworkReply *m_pPendingReply;
    QTimer *m_pTimeoutTimer;
    int m_retryCount;
    int m_maxRetries;
    int m_maxTokens;                       // 输出上限，防 reasoning 模型把配额烧在思考上
    // pending changes for the result table (key -> new value)
    QHash<QString, QString> m_pendingChanges;
    // full before/after snapshot for one apply (key -> value), used for undo
    QHash<QString, QString> m_undoSnapshot;
    bool m_hasUndo;
};

#endif // AIGRADINGDIALOG_H