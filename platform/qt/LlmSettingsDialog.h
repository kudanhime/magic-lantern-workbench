#ifndef LLMSETTINGSDIALOG_H
#define LLMSETTINGSDIALOG_H

#include <QDialog>

namespace Ui {
class LlmSettingsDialog;
}

//「AI 智能调色 → LLM 设置…」弹窗：
//独立编辑端点/模型/API 密钥/超时/输出上限，读写与 AI 面板同一份 QSettings。
class LlmSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LlmSettingsDialog( QWidget *parent = nullptr );
    ~LlmSettingsDialog();

    //读/写 QSettings（与 AiGradingDialog 共用同一组键）
    void loadFromSettings();
    void saveToSettings();

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

protected:
    void changeEvent( QEvent *event );      // 语言切换：重刷对话框文字

private:
    Ui::LlmSettingsDialog *ui;
};

#endif // LLMSETTINGSDIALOG_H
