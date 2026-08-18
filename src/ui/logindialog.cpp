#include "logindialog.h"
#include "ui_logindialog.h"

#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
    , m_guestButton(nullptr)
    , m_authenticated(false)
    , m_guestMode(true)
{
    ui->setupUi(this);

    setMinimumSize(480, 300);
    setWindowTitle(QStringLiteral("星策 StarQuant - 本地研究模式"));

    ui->verticalLayout->setSpacing(18);
    ui->verticalLayout->setContentsMargins(35, 35, 35, 35);
    ui->inputLayout->setSpacing(12);
    ui->horizontalLayout->setSpacing(12);

    ui->titleLabel->setText(QStringLiteral("星策 StarQuant"));
    ui->usernameEdit->hide();
    ui->passwordEdit->hide();
    ui->loginBtn->hide();
    ui->cancelBtn->setText(QStringLiteral("退出"));
    ui->tipLabel->setText(QStringLiteral("当前版本仅提供本地研究与模拟交易，不提供在线账号、支付或真实交易。不会收集或保存密码。"));
    ui->tipLabel->setWordWrap(true);

    m_guestButton = new QPushButton(QStringLiteral("进入本地研究模式"), this);
    m_guestButton->setObjectName(QStringLiteral("guestBtn"));
    ui->horizontalLayout->insertWidget(1, m_guestButton);

    connect(m_guestButton, &QPushButton::clicked, this, &LoginDialog::acceptGuest);

    this->setStyleSheet(R"(
        QDialog {
            background-color: #253b6e;
            border-radius: 8px;
        }
        QLineEdit {
            background-color: #4a5568;
            color: white;
            border: 1px solid #718096;
            border-radius: 6px;
            padding: 10px 12px;
            font-size: 14px;
            selection-background-color: #4299e1;
        }
        QLineEdit:focus {
            border-color: #63b3ed;
        }
        QPushButton {
            border: none;
            border-radius: 6px;
            padding: 10px 18px;
            font-size: 14px;
            min-height: 38px;
            background-color: #718096;
            color: white;
        }
        QPushButton:hover {
            background-color: #5a6678;
        }
        QPushButton#loginBtn {
            background-color: #2196f3;
        }
        QPushButton#loginBtn:hover {
            background-color: #1976d2;
        }
        QPushButton#guestBtn {
            background-color: #2f855a;
        }
        QPushButton#guestBtn:hover {
            background-color: #276749;
        }
        QLabel#titleLabel {
            font-size: 22px;
            font-weight: bold;
            color: #63b3ed;
        }
        QLabel#tipLabel {
            font-size: 12px;
            color: #a0aec0;
        }
        QMessageBox {
            background-color: #1a1a2e;
        }
        QMessageBox QLabel {
            color: #e2e8f0;
            font-size: 13px;
        }
        QMessageBox QPushButton {
            background-color: #718096;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 20px;
            min-width: 78px;
            min-height: 32px;
        }
        QMessageBox QPushButton:hover {
            background-color: #5a6678;
        }
    )");
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

bool LoginDialog::isAuthenticated() const
{
    return m_authenticated;
}

bool LoginDialog::isGuestMode() const
{
    return m_guestMode;
}

QString LoginDialog::accountHint() const
{
    return m_accountHint;
}

void LoginDialog::accept()
{
    QMessageBox::information(this, QStringLiteral("在线账户未接入"),
                             QStringLiteral("正式商业账户、订阅与支付服务尚未接入，请使用本地研究模式。"));
}

void LoginDialog::reject()
{
    QDialog::reject();
}

void LoginDialog::acceptGuest()
{
    m_accountHint.clear();
    m_authenticated = true;
    m_guestMode = true;
    QDialog::accept();
}
