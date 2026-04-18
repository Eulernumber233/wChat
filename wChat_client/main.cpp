#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QFile>
#ifdef Q_OS_WIN
#include <Windows.h>
#endif

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    QString txt;
    switch (type) {
    case QtDebugMsg:    txt = QString("[DEBUG] %1").arg(msg); break;
    case QtWarningMsg:  txt = QString("[WARN]  %1").arg(msg); break;
    case QtCriticalMsg: txt = QString("[ERROR] %1").arg(msg); break;
    case QtFatalMsg:    txt = QString("[FATAL] %1").arg(msg); break;
    default:            txt = msg; break;
    }
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(txt.utf16()));
    OutputDebugStringW(L"\n");
#endif
    fprintf(stderr, "%s\n", txt.toLocal8Bit().constData());
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qInstallMessageHandler(customMessageHandler);

    // Pink selection palette (QPalette is the only reliable way for
    // QTextEdit / QPlainTextEdit text-selection colors on Windows;
    // QSS `selection-background-color` doesn't always reach those).
    QPalette appPal = QApplication::palette();
    appPal.setColor(QPalette::Highlight,        QColor(0xf6, 0xcf, 0xdd));  // --pink-200
    appPal.setColor(QPalette::HighlightedText,  QColor(0x6b, 0x3a, 0x4a));  // --pink-ink
    QApplication::setPalette(appPal);

    QFile qss(":/style/stylesheet.qss");
    if(qss.open(QFile::ReadOnly)){
        qDebug("Open success");
        QString style =QLatin1String( qss.readAll());
        a.setStyleSheet(style);
        qss.close();
    }else{
        qDebug("Open failed");
    }
    QString fileName="config.ini";
    QString app_path=QCoreApplication::applicationDirPath();
    QString config_path = QDir::toNativeSeparators(app_path+ QDir::separator()+fileName);
    qDebug() << "修正后的读取路径：" << config_path<<Qt::endl;
    QSettings settings(config_path,QSettings::IniFormat);
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();
    QString gate_domain = settings.value("GateServer/domain").toString();
    gate_url_prefix ="http://"+gate_host+":"+gate_port;
    gate_url_prefix_domain = "https://"+gate_domain;
    // mode = "local" uses http://host:port, "domain" uses https://domain
    QString gate_mode = settings.value("GateServer/mode", "local").toString();
    if (gate_mode == "domain") {
        gate_url_prefix = gate_url_prefix_domain;
    }
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "wChat_client_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return a.exec();
}
