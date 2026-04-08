#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QFile>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
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
