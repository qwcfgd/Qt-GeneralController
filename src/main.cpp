#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include "views/MainWindow.h"
int main(int argc,char **argv){
    QApplication app(argc,argv);QApplication::setStyle("Fusion");
    app.setApplicationName(host::MainWindowInitialValues::applicationName);app.setApplicationVersion(host::MainWindowInitialValues::version);
    app.setOrganizationName(host::MainWindowInitialValues::organizationName);app.setFont(QFont(host::MainWindowInitialValues::fontFamily,host::MainWindowInitialValues::fontSize));
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"demo","Start with simulated adapters; no physical bus operations."});
    parser.addOption({"config","Save configuration to this file; use Load Configuration to restore channels.","file",QCoreApplication::applicationDirPath()+host::MainWindowInitialValues::configPath});
    parser.process(app);
    host::MainWindow window(parser.value("config"),parser.isSet("demo")||host::MainWindowInitialValues::simulation);window.show();return app.exec();
}
