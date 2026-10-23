#include "LlmSettingsDialog.h"
#include "ui_LlmSettingsDialog.h"

#include <QSettings>

//与 AiGradingDialog 共用同一组 QSettings 键
static const char *SETTINGS_ENDPOINT  = "aiGradingEndpoint";
static const char *SETTINGS_MODEL     = "aiGradingModel";
static const char *SETTINGS_APIKEY    = "aiGradingApiKey";
static const char *SETTINGS_TIMEOUT   = "aiGradingTimeoutSec";
static const char *SETTINGS_MAXTOKENS = "aiGradingMaxTokens";

static const char *DEFAULT_ENDPOINT    = "";   // 留空：由用户填自己的 OpenAI 兼容端点
static const char *DEFAULT_MODEL       = "deepseek-v4-flash";
static const int   DEFAULT_TIMEOUT_SEC = 120;
static const int   DEFAULT_MAX_TOKENS  = 4000;

LlmSettingsDialog::LlmSettingsDialog( QWidget *parent )
    : QDialog( parent )
    , ui( new Ui::LlmSettingsDialog )
{
    ui->setupUi( this );

    connect( ui->buttonBox, SIGNAL( accepted() ), this, SLOT( on_buttonBox_accepted() ) );
    connect( ui->buttonBox, SIGNAL( rejected() ), this, SLOT( on_buttonBox_rejected() ) );

    loadFromSettings();
}

LlmSettingsDialog::~LlmSettingsDialog()
{
    delete ui;
}

void LlmSettingsDialog::loadFromSettings()
{
    QSettings set;
    ui->lineEditEndpoint->setText( set.value( SETTINGS_ENDPOINT, DEFAULT_ENDPOINT ).toString() );
    ui->lineEditModel->setText( set.value( SETTINGS_MODEL, DEFAULT_MODEL ).toString() );
    ui->lineEditApiKey->setText( set.value( SETTINGS_APIKEY, QString( "" ) ).toString() );
    ui->spinBoxTimeout->setValue( set.value( SETTINGS_TIMEOUT, DEFAULT_TIMEOUT_SEC ).toInt() );
    ui->spinBoxMaxTokens->setValue( set.value( SETTINGS_MAXTOKENS, DEFAULT_MAX_TOKENS ).toInt() );
}

void LlmSettingsDialog::saveToSettings()
{
    QSettings set;
    set.setValue( SETTINGS_ENDPOINT, ui->lineEditEndpoint->text().trimmed() );
    set.setValue( SETTINGS_MODEL, ui->lineEditModel->text().trimmed() );
    set.setValue( SETTINGS_APIKEY, ui->lineEditApiKey->text() );   //never logged
    set.setValue( SETTINGS_TIMEOUT, ui->spinBoxTimeout->value() );
    set.setValue( SETTINGS_MAXTOKENS, ui->spinBoxMaxTokens->value() );
    set.sync();
}

void LlmSettingsDialog::on_buttonBox_accepted()
{
    saveToSettings();
    accept();
}

void LlmSettingsDialog::on_buttonBox_rejected()
{
    reject();
}

void LlmSettingsDialog::changeEvent( QEvent *event )
{
    if( event->type() == QEvent::LanguageChange )
        ui->retranslateUi( this );
    QDialog::changeEvent( event );
}
