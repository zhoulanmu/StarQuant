#pragma once

#include <QDialog>

class QCheckBox;

class LegalConsentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LegalConsentDialog(bool requireAcceptance, QWidget* parent = nullptr);

    static bool ensureAccepted(QWidget* parent = nullptr);
    static void showInformation(QWidget* parent = nullptr);

private slots:
    void acceptTerms();

private:
    void addDocumentTab(const QString& title, const QString& resourcePath);

    bool m_requireAcceptance = false;
    QCheckBox* m_acceptCheck = nullptr;
};

