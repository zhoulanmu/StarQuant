#include "mainwindow.h"
#include "ui/logindialog.h"
#include "ui/legalconsentdialog.h"
#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("星策 StarQuant"));
    QApplication::setOrganizationName(QStringLiteral("StarQuant"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/starquant_stars.png")));

    if (!LegalConsentDialog::ensureAccepted()) {
        return 0;
    }

    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "StarQuant_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    LoginDialog loginDialog;
    if (loginDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow w(loginDialog.isGuestMode(), loginDialog.accountHint());
    w.show();
    return a.exec();
}
