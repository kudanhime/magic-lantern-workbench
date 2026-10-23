/*!
 * \file main.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The main... the start of the horror
 */

#include "MainWindow.h"
#include "MyApplication.h"

#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSettings>

/*! ★ 语言模式判定
 *  中文版 exe 名为「魔灯工作台.exe」，英文版为「MagicLanternWorkbench.exe」。
 *  一份代码、一次编译，复制改名即得两个语言版。
 */
static bool isChineseBuild()
{
    const QString base = QFileInfo( QCoreApplication::applicationFilePath() ).completeBaseName();
    //魔灯 = U+9B54 U+706F
    return base.contains( QStringLiteral( "\u9b54\u706f" ) );
}

// ===== 运行时语言切换支持 ======
// 翻译器必须是常驻对象：菜单里切语言时要把旧的摘掉、装新的，然后重刷界面。
QTranslator *g_appTranslator = nullptr;          // 应用译文（mlvapp_zh_CN.qm），生命周期 = 程序

// 装/摘应用译文。zh=false 时只摘不装（界面回落到源码英文）。
// 返回是否正确加载（false 只表示没找到/没加载，不影响摘除动作）。
bool applyAppTranslator( bool zh )
{
    if( !g_appTranslator ) return false;
    QCoreApplication::removeTranslator( g_appTranslator );

    if( !zh )
    {
        qDebug() << "Language switched to English (no app translator installed)";
        return true;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList dirs;
    // 磁盘优先；内嵌资源（:/）放最后，避免旧 .qm 盖掉磁盘上的新版
    dirs << appDir << appDir + "/translations"
         << QStringLiteral( ":/" ) << QStringLiteral( ":/translations" );

    for( const QString &d : dirs )
    {
        const QString candidate = d.endsWith( '/' ) ? d + "mlvapp_zh_CN.qm"
                                                    : d + "/mlvapp_zh_CN.qm";
        if( d.startsWith( ':' ) || QFileInfo::exists( candidate ) )
        {
            if( g_appTranslator->load( candidate ) )
            {
                QCoreApplication::installTranslator( g_appTranslator );
                qDebug() << "MLVApp translation loaded:" << candidate;
                return true;
            }
        }
    }
    qDebug() << "MLVApp translation NOT found";
    return false;
}

int main(int argc, char *argv[])
{
    MyApplication a(argc, argv);
    a.setAttribute(Qt::AA_UseHighDpiPixmaps);
#ifdef Q_OS_WIN
    a.setAttribute(Qt::AA_Use96Dpi);
#endif

    /*! ---------------------------------------------------------------
     *  Translation loading (zh_CN sample work).
     *  This project had no QTranslator code at all before, so without
     *  this block nothing can ever be translated. Installed BEFORE
     *  MainWindow is constructed, so the uic-generated
     *  QCoreApplication::translate("MainWindow", ...) calls already
     *  resolve to Chinese.
     *  --------------------------------------------------------------- */

    // 1) Qt's own strings (QMessageBox buttons, QFileDialog, ...)
    //语言偏好优先（菜单里切过），否则按 exe 文件名判断
    bool zhMode = isChineseBuild();
    {
        QSettings langSet( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
        const QString langPref = langSet.value( QStringLiteral( "language" ), QString() ).toString();
        if( langPref == QLatin1String( "zh" ) )      zhMode = true;
        else if( langPref == QLatin1String( "en" ) ) zhMode = false;
    }


    QTranslator qtTranslator;
    const QString qtTransDir = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    if (zhMode)
    {
        //中文版：强制 Qt 自带中文（对话框按钮等）
        if (qtTranslator.load(QStringLiteral("qt_zh_CN"), qtTransDir))
            a.installTranslator(&qtTranslator);
    }
    else
    {
        //英文版：跟随系统语言（不强制中文）
        if (qtTranslator.load("qt_" + QLocale::system().name(), qtTransDir))
            a.installTranslator(&qtTranslator);
    }

    // 2) MLVApp's own strings. Preferred locale from the system, fallback zh_CN.
    //    Searched in: app dir, app dir/translations, and the compiled-in resource.
    // 常驻译文对象（parent = app，随程序退出释放）；运行时切语言要复用它
    g_appTranslator = new QTranslator( &a );
    if( zhMode )
    {
        applyAppTranslator( true );
    }
    else
    {
        qDebug() << "English build: skipping mlvapp_zh_CN translation";
    }

    MainWindow w(argc, argv);
    w.show();

    /* E8b v2: a command-line export (--export / --export-jpg) already ran
       synchronously inside the MainWindow constructor. There is nothing left
       to do and no user watching, so bypass the event loop entirely instead of
       relying on a deferred quit that would never be delivered. */
    if( w.m_cliShouldQuit )
    {
        return 0;
    }

    return a.exec();
}
