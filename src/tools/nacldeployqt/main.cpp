#include <qdebug.h>
#include <qglobal.h>
#include <qcoreapplication.h>
#include <qcommandlineparser.h>
#include <qlibraryinfo.h>
#include <qdir.h>
#include <qprocess.h>

int main(int argc, char **argv)
{
    // Command line handling
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("nacldeployqt");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("nexe", "Application nexe file.");

    parser.addOption(QCommandLineOption(QStringList() << "t" << "template",
                        "Selects a html template. Can be either 'fullscreen' or 'debug'. "
                        "Omit this option to use the default nacl_sdk template, as created "
                        "by create_html.py", "template", ""));
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    const QString tmplate = parser.value("template");

    // Get target nexe file name from command line, or find one
    // in the cuerrent directory
    QStringList nexes = QDir().entryList(QStringList() << "*.nexe", QDir::Files);
    QString nexe;
    if (args.count() == 0) {
        if (nexes.count() == 0) {
            parser.showHelp(0);
            return 0;
        }
        nexe = nexes.at(0);
    } else {
        nexe = args.at(0);
    }

    QString appName = nexe;
    appName.chop(QStringLiteral(".nexe").length());
    QString nmf = appName + ".nmf";
    
    if (!QFile(nexe).exists()) {
        qDebug() << "File not found" << nexe;
        return 0;
    }
    
    // Get the NaCl SDK root from environment
    QString naclSdkRoot = qgetenv("NACL_SDK_ROOT");
    if (naclSdkRoot.isEmpty()) {
        qDebug() << "Plese set NACL_SDK_ROOT";
        qDebug() << "For example: /Users/foo/nacl_sdk/pepper_35";
        return 0;
    }

    // Get deployment scripts:
    QString createNmf = naclSdkRoot + "/tools/create_nmf.py";
    if (!QFile(createNmf).exists()) {
        qDebug() << "create_nmf.py not found at" << createNmf;
        return 0;
    }
    QString createHtml = naclSdkRoot + "/tools/create_html.py";
    if (!QFile(createHtml).exists()) {
        qDebug() << "create_html.py not found at" << createHtml;
        return 0;
    }
    
    // Get the lib dir for this Qt install
    // Use argv[0]: path/to/qtbase/bin/nacldeployqt -> path/to/qtbase/lib
    QString nacldeployqtPath = argv[0];
    nacldeployqtPath.chop(QStringLiteral("nacldeployqt").length());
    QString qtBaseDir = QDir(nacldeployqtPath + QStringLiteral("../")).canonicalPath(); 
    QString qtLibDir = qtBaseDir + "/lib";
    QString nmfFileName = appName + ".nmf";

    qDebug() << " ";
    qDebug() << "Deploying" << nexe;
    qDebug() << "Using SDK" << naclSdkRoot;
    qDebug() << "Qt libs in" << qtLibDir;
    qDebug() << " ";
    
    // create the .nmf manifest file
    QString nmfCommand = QStringLiteral("python ") + createNmf
                + " -o " + nmfFileName
                + " -L " + qtLibDir           // Add Qt libs search payh
                + " -s  . "                   // copy dependencies 
//                + " --debug-libs"    
                + " " + nexe;
    
    system(nmfCommand.toLatin1().constData());

    // create the inxed.html file. Use a built-in template if specifed,
    // else use create_html.py
    if (tmplate.isEmpty()) {
        QString hmtlCommand = QStringLiteral("python ") + createHtml + " " + nmf
                    + " -o index.html";
        system(hmtlCommand.toLatin1().constData());
    } else {
        // select template. See the template_*.cpp files.
        QByteArray html;
        if (tmplate== "fullscreen") {
            extern const char * templateFullscreen;
            html = QByteArray(templateFullscreen);
        } else if (tmplate== "debug") {
            extern const char * templateDebug;
            html = QByteArray(templateDebug);
        }

        // replace %APPNAME%
        html.replace("%APPNAME%", appName.toUtf8());

        // write html contents to index.html
        QFile indexHtml("index.html");
        indexHtml.open(QIODevice::WriteOnly|QIODevice::Truncate);
        indexHtml.write(html);
    }
    
    // NOTE: At this point deployment is done. The following are
    // development aides and should be switched off / placed behind
    // a flag at some point.

    // Find the debugger, print startup instructions
    QString gdb = naclSdkRoot + "/toolchain/mac_x86_glibc/bin/i686-nacl-gdb";
    qDebug() << "debugger (glibc):";
    qDebug() << qPrintable("  " + gdb);
    qDebug() << qPrintable("    target remote localhost:4014");
    qDebug() << qPrintable("    nacl-manifest " + nmfFileName);
    qDebug() << "";

    // Find chrome, print startup instructions
    QString chromeApp = "/Applications/Google\\ Chrome.app/Contents/MacOS/Google\\ Chrome";
    QString chromeNormalOptions = " --incognito --disable-cache --no-default-browser-check --new-window \"http://localhost:8000\"";
    QString chromeDebugOptions = chromeNormalOptions + " --enable-nacl-debug --no-sandbox";
    qDebug() << "chrome:";
    qDebug() << qPrintable("  " + chromeApp + chromeNormalOptions);
    qDebug() << qPrintable("chrome (wait for debugger)");
    qDebug() << qPrintable("  " + chromeApp + chromeDebugOptions);
    qDebug() << "";

    // Start a HTTP server and serve the app.
    qDebug() << "Serving on localhost:8000";
    system("python -m SimpleHTTPServer");
}