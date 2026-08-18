#include "legalconsentdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
constexpr auto LegalNoticeVersion = "2026-07-28.1";
constexpr auto AcceptedNoticeKey = "compliance/acceptedNoticeVersion";

QString loadResourceText(const QString& resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("文档加载失败：%1").arg(resourcePath);
    }
    return QString::fromUtf8(file.readAll());
}
}

LegalConsentDialog::LegalConsentDialog(bool requireAcceptance, QWidget* parent)
    : QDialog(parent)
    , m_requireAcceptance(requireAcceptance)
{
    setWindowTitle(QStringLiteral("StarQuant 法律、隐私与风险说明"));
    setModal(true);
    resize(780, 620);

    auto* layout = new QVBoxLayout(this);
    auto* headline = new QLabel(requireAcceptance
                                    ? QStringLiteral("请阅读并确认后进入本地研究模式。")
                                    : QStringLiteral("以下信息适用于 StarQuant 的本地研究模式。"), this);
    headline->setWordWrap(true);
    layout->addWidget(headline);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("legalTabs"));
    addDocumentTab(QStringLiteral("风险揭示"), QStringLiteral(":/legal/risk-disclosure.md"));
    addDocumentTab(QStringLiteral("隐私说明"), QStringLiteral(":/legal/privacy-notice.md"));
    addDocumentTab(QStringLiteral("数据与算法"), QStringLiteral(":/legal/data-and-algorithm.md"));
    layout->addWidget(tabs, 1);

    if (requireAcceptance) {
        m_acceptCheck = new QCheckBox(QStringLiteral("我已阅读并理解以上风险、隐私、数据与算法说明"), this);
        layout->addWidget(m_acceptCheck);
    }

    auto* buttons = new QDialogButtonBox(this);
    auto* closeButton = buttons->addButton(requireAcceptance ? QStringLiteral("退出") : QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    QPushButton* continueButton = nullptr;
    if (requireAcceptance) {
        continueButton = buttons->addButton(QStringLiteral("同意并进入"), QDialogButtonBox::AcceptRole);
        continueButton->setEnabled(false);
        connect(m_acceptCheck, &QCheckBox::toggled, continueButton, &QPushButton::setEnabled);
        connect(continueButton, &QPushButton::clicked, this, &LegalConsentDialog::acceptTerms);
    }
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool LegalConsentDialog::ensureAccepted(QWidget* parent)
{
    QSettings settings;
    if (settings.value(QString::fromLatin1(AcceptedNoticeKey)).toString()
        == QString::fromLatin1(LegalNoticeVersion)) {
        return true;
    }
    LegalConsentDialog dialog(true, parent);
    return dialog.exec() == QDialog::Accepted;
}

void LegalConsentDialog::showInformation(QWidget* parent)
{
    LegalConsentDialog dialog(false, parent);
    dialog.exec();
}

void LegalConsentDialog::acceptTerms()
{
    if (!m_acceptCheck || !m_acceptCheck->isChecked()) {
        return;
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(AcceptedNoticeKey), QString::fromLatin1(LegalNoticeVersion));
    accept();
}

void LegalConsentDialog::addDocumentTab(const QString& title, const QString& resourcePath)
{
    auto* tabs = findChild<QTabWidget*>(QStringLiteral("legalTabs"));
    if (!tabs) {
        return;
    }
    auto* browser = new QTextBrowser(tabs);
    browser->setOpenExternalLinks(false);
    browser->setMarkdown(loadResourceText(resourcePath));
    tabs->addTab(browser, title);
}
