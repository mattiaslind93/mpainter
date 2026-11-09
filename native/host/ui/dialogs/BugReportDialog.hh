// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A plain report box: the user writes what happened and presses Send. The
// message goes to a small endpoint on the product site, which is what holds the
// destination address and the fixed subject. The application never carries the
// address, so it cannot be read out of the binary or seen in a mail client.
#include "Branding.hh"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHttpMultiPart>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QUrl>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
namespace bts {
inline constexpr auto bug_report_endpoint="https://bluedino.se/mpainter/report.php";
class BugReportDialog final : public QDialog {
 public:
  explicit BugReportDialog(QWidget *parent=nullptr):QDialog(parent)
  {
    setObjectName("bugReportDialog");
    setWindowTitle("Report a Bug");
    auto *layout=new QVBoxLayout(this);
    auto *prompt=new QLabel("Describe what happened, or what you would like mPainter to do.",this);
    prompt->setWordWrap(true);
    layout->addWidget(prompt);
    text_=new QPlainTextEdit(this);
    text_->setObjectName("bugReportText");
    text_->setMinimumSize(420,180);
    layout->addWidget(text_);
    status_=new QLabel(this);
    status_->setObjectName("bugReportStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    buttons_=new QDialogButtonBox(this);
    send_=buttons_->addButton("Send",QDialogButtonBox::AcceptRole);
    send_->setObjectName("bugReportSend");
    buttons_->addButton(QDialogButtonBox::Cancel)->setObjectName("bugReportCancel");
    layout->addWidget(buttons_);
    connect(buttons_,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(send_,&QPushButton::clicked,this,[this]{send();});
    connect(text_,&QPlainTextEdit::textChanged,this,[this]{send_->setEnabled(!text_->toPlainText().trimmed().isEmpty());});
    send_->setEnabled(false);
  }

 private:
  void send()
  {
    const auto message=text_->toPlainText().trimmed();
    if(message.isEmpty())return;
    send_->setEnabled(false);
    text_->setReadOnly(true);
    status_->setText("Sending…");
    // The version and system are added here so a report is useful without
    // asking the user for them. Nothing else about the machine is collected.
    QUrlQuery form;
    form.addQueryItem("message",message);
    form.addQueryItem("version",product_version());
    form.addQueryItem("system",QSysInfo::prettyProductName()+" "+QSysInfo::currentCpuArchitecture());
    QNetworkRequest request{QUrl(QString::fromLatin1(bug_report_endpoint))};
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::UserAgentHeader,QString("mPainter/%1").arg(product_version()));
    auto *reply=network_.post(request,form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
      reply->deleteLater();
      if(reply->error()==QNetworkReply::NoError){
        status_->setText("Thank you — your report was sent.");
        QTimer::singleShot(1200,this,&QDialog::accept);
        return;
      }
      // Leave the text in place: a failed send must never lose what was written.
      status_->setText("Could not send the report. Check your connection and try again.");
      text_->setReadOnly(false);
      send_->setEnabled(true);
    });
  }
  QPlainTextEdit *text_=nullptr;QLabel *status_=nullptr;
  QDialogButtonBox *buttons_=nullptr;QPushButton *send_=nullptr;
  QNetworkAccessManager network_;
};
}
