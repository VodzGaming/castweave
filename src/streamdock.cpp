#ifndef STREAMDOCK_PREVIEW
#include <obs-module.h>
#include <obs-frontend-api.h>
#endif
#include <QApplication>
#include <cstdio>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QScrollArea>
#include <QDesktopServices>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QDialog>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QAction>
#include <QHash>
#include <QHelpEvent>
#include <QToolTip>
#include "twitch-panel.hpp"
#include "update-installer.hpp"
#include "twitch-chat.hpp"
#include "chat-artwork.hpp"

static constexpr auto kPluginVersion = CASTWEAVE_VERSION;

static QString platformColor(const QString &platform) {
 return platform == "Twitch" ? "#b689ff" : platform == "YouTube" ? "#ff6269" : "#73e344";
}


static QPixmap platformIcon(const QString &name) {
 static QHash<QString,QPixmap> cache;
 if(cache.contains(name)) return cache.value(name);
 QPixmap pix(40,40); pix.fill(Qt::transparent);
 QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); p.scale(2,2); p.setPen(Qt::NoPen);
 if(name=="Twitch") {
  p.setBrush(QColor("#9146ff"));
  p.drawPolygon(QPolygonF{QPointF(3,1),QPointF(19,1),QPointF(19,12),QPointF(13,18),QPointF(9,18),QPointF(6,20),QPointF(6,16),QPointF(1,16),QPointF(1,5)});
  p.setBrush(Qt::white);
  p.drawPolygon(QPolygonF{QPointF(5,3),QPointF(17,3),QPointF(17,11),QPointF(13,15),QPointF(9,15),QPointF(6,17),QPointF(6,15),QPointF(5,15)});
  p.setBrush(QColor("#9146ff")); p.drawRect(QRectF(9,5,2,6)); p.drawRect(QRectF(13,5,2,6));
 } else if(name=="YouTube") {
  p.setBrush(QColor("#ff0033")); p.drawRoundedRect(QRectF(0,3,20,14),4,4); p.setBrush(Qt::white);
  p.drawPolygon(QPolygonF{QPointF(8,6),QPointF(14,10),QPointF(8,14)});
 } else if(name=="Kick") {
  p.setBrush(QColor("#53fc18"));
  p.drawPolygon(QPolygonF{QPointF(2,2),QPointF(7,2),QPointF(7,7),QPointF(10,7),QPointF(10,4),QPointF(13,4),QPointF(13,2),QPointF(18,2),QPointF(18,7),QPointF(15,7),QPointF(15,10),QPointF(18,10),QPointF(18,18),QPointF(13,18),QPointF(13,15),QPointF(10,15),QPointF(10,12),QPointF(7,12),QPointF(7,18),QPointF(2,18)});
 } else {
  p.setBrush(QColor(name=="Moderator" ? "#00a86b" : name=="VIP" ? "#d934c8" : "#8355d8"));
  p.drawRoundedRect(QRectF(1,1,18,18),3,3); p.setBrush(Qt::white);
  if(name=="Moderator") {
   p.drawPolygon(QPolygonF{QPointF(5,13),QPointF(12,4),QPointF(16,3),QPointF(15,7),QPointF(8,14)});
   p.setPen(QPen(Qt::white,2)); p.drawLine(4,11,10,17); p.drawLine(7,14,3,18);
  } else if(name=="VIP") {
   p.drawPolygon(QPolygonF{QPointF(10,4),QPointF(16,9),QPointF(10,16),QPointF(4,9)});
  } else {
   p.drawPolygon(QPolygonF{QPointF(10,3),QPointF(12,7),QPointF(17,8),QPointF(13,11),QPointF(14,16),QPointF(10,13),QPointF(6,16),QPointF(7,11),QPointF(3,8),QPointF(8,7)});
  }
 }
 p.end(); cache.insert(name,pix); return pix;
}
static QString iconHtml(const QString &name) {
 return "<a href='badge:" + name + "'><img width='16' height='16' src='icon:" + name + "' /></a>";
}
class ChatDelegate final : public QStyledItemDelegate {
 ChatArtwork *artwork;
 void prepare(QTextDocument &doc, const QModelIndex &index, int width) const {
  doc.setDefaultFont(QFont("Segoe UI", 9));
  doc.setDocumentMargin(0);
  for (const auto &name : {"Twitch","YouTube","Kick","Moderator","Subscriber","VIP"})
   doc.addResource(QTextDocument::ImageResource,QUrl("icon:" + QString(name)),platformIcon(name).toImage());
  for(const auto &url:index.data(Qt::UserRole+4).toStringList()) artwork->resource(doc,url);
  doc.setHtml(index.data(Qt::DisplayRole).toString());
  doc.setTextWidth(qMax(100, width - 20));
 }
public:
 ChatDelegate(ChatArtwork *assets,QObject *parent):QStyledItemDelegate(parent),artwork(assets) {}

 bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override {
  if (!event || !view || !index.isValid()) return false;
  if(index.data(Qt::UserRole+5).toBool()) { QToolTip::showText(event->globalPos(),index.data(Qt::ToolTipRole).toString(),view); return true; }
  QTextDocument doc; prepare(doc,index,option.rect.width());
  const QPoint point = event->pos() - option.rect.topLeft() - QPoint(10,10);
  const QString anchor = doc.documentLayout()->anchorAt(point);
  if (!anchor.startsWith("badge:")) { QToolTip::hideText(); return true; }
  const QString name = anchor.mid(6);
  QString description = name;
  if(name=="Moderator") description += "\nHelps manage this channel's chat, including messages and timeouts.";
  else if(name=="Subscriber") description += "\nSubscriber badge for this channel. Artwork can vary by channel and subscription duration.";
  else if(name=="VIP") description += "\nA community member recognised by the broadcaster. VIP is not a moderator role.";
  else description += "\nThe platform this message came from.";
  if(name=="Moderator" || name=="Subscriber" || name=="VIP")
   description += "\n\nSample badge: this preview does not reflect a real viewer's roles.";
  QToolTip::showText(event->globalPos(),description,view);
  return true;
 }
 void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
  QTextDocument doc; prepare(doc, index, option.rect.width());
  p->save(); p->setClipRect(option.rect);
  if (index.data(Qt::UserRole + 1).toBool()) {
   const QColor color(platformColor(index.data(Qt::UserRole).toString()));
   QColor fill(color); fill.setAlpha(20);
   p->setBrush(fill); p->setPen(QPen(color, 1));
   p->drawRoundedRect(option.rect.adjusted(1, 3, -1, -3), 5, 5);
  }
  p->translate(option.rect.topLeft() + QPoint(10, 10));
  doc.drawContents(p); p->restore();
 }
 QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
  QTextDocument doc; prepare(doc, index, option.rect.width());
  return QSize(option.rect.width(), qCeil(doc.size().height()) + 20);
 }
};

class DestinationSwitch final : public QCheckBox {
public:
 explicit DestinationSwitch(QWidget *parent = nullptr) : QCheckBox(parent) { setFixedSize(38, 24); setCursor(Qt::PointingHandCursor); }
protected:
 bool hitButton(const QPoint &position) const override { return rect().contains(position); }
 void paintEvent(QPaintEvent *) override {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.setPen(hasFocus() ? QPen(QColor("#ddddff"), 1) : QPen(Qt::NoPen));
  p.setBrush(isChecked() ? QColor("#7359ed") : QColor("#45454a"));
  p.drawRoundedRect(QRectF(1, 4, 36, 16), 8, 8);
  p.setPen(Qt::NoPen); p.setBrush(QColor("#ffffff"));
  p.drawEllipse(QRectF(isChecked() ? 22 : 4, 6, 12, 12));
 }
};

#ifndef STREAMDOCK_PREVIEW
OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void)
{
 return "CastWeave: live Twitch chat and channel management.";
}

#endif

class StreamDock final : public QWidget {
#ifdef STREAMDOCK_PREVIEW
 QSettings settings{"StreamDock", "Preview"};
#else
 QSettings settings{"StreamDock", "OBS"};
#endif
 QNetworkAccessManager network{this};
 QListWidget *chat{};
 QComboBox *filter{};
 TwitchChat liveChat{this};
 ChatArtwork chatArtwork{this};

 QLabel *updateStatus{};
 QPushButton *check{};
 UpdateInstaller *updater{};
 QUrl releaseUrl;
 int historyLimit = 300;

 void addMessage(const QString &platform, const QString &user, const QString &message, bool event = false, const QStringList &badges = {}) {
  const QString color = platformColor(platform);
  QString marks = iconHtml(platform);
  for (const auto &badge : badges) marks += " " + iconHtml(badge);
  auto *item = new QListWidgetItem(marks + " <span style='color:" + color + "'><b>" + user.toHtmlEscaped() + "</b></span> <span style='color:#e6e6e8'>" + message.toHtmlEscaped() + "</span>", chat);
  item->setData(Qt::UserRole, platform);
  item->setData(Qt::UserRole + 1, event);
  item->setToolTip(platform + " • Sample message");
  item->setHidden(filter->currentText() != "All platforms" && filter->currentText() != platform);
  while (chat->count() > historyLimit) delete chat->takeItem(0);
 }
 void checkUpdates() {
  const QString repository = "zerithvt-Coder/castweave";
  if (!QRegularExpression("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$").match(repository).hasMatch()) {
   updateStatus->setText("Enter a GitHub repository as owner/repository."); return;
  }
  releaseUrl = QUrl(); updater->offer({}); check->setEnabled(false);
  updateStatus->setText("Checking GitHub releases...");
  QNetworkRequest request{QUrl("https://api.github.com/repos/" + repository + "/releases?per_page=20")};
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", "CastWeave");
  request.setTransferTimeout(15000);
  auto *reply = network.get(request);
  connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
   if (received > 1024 * 1024) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, repository]() {
   check->setEnabled(true);
   const QByteArray body = reply->readAll();
   const auto error = reply->error(); reply->deleteLater();
   if (error != QNetworkReply::NoError) {
    updateStatus->setText("Could not check releases. Check the repository and connection; it may have no public releases yet."); return;
   }
   QJsonObject object;
   QVersionNumber best;
   for(const auto &entry : QJsonDocument::fromJson(body).array()) {
    const auto release = entry.toObject();
    QString candidate=release["tag_name"].toString(); if(candidate.startsWith('v')) candidate.remove(0,1);
    if(release["draft"].toBool() || !QRegularExpression("^[0-9]+[.][0-9]+[.][0-9]+$").match(candidate).hasMatch()) continue;
    const auto version=QVersionNumber::fromString(candidate);
    if(object.isEmpty() || version>best) { best=version; object=release; }
   }
   if(object.isEmpty()) { updateStatus->setText("No published CastWeave releases found."); return; }
   QString tag = object.value("tag_name").toString();
   if (tag.startsWith('v')) tag.remove(0, 1);
   if (!QRegularExpression("^\\d+\\.\\d+\\.\\d+$").match(tag).hasMatch()) {
    updateStatus->setText("The latest release has an unsupported version tag."); return;
   }
   const auto latest = QVersionNumber::fromString(tag);
   if (latest <= QVersionNumber::fromString(kPluginVersion)) {
    updateStatus->setText("You are up to date. Installed: " + QString(kPluginVersion)); return;
   }
   releaseUrl = QUrl("https://github.com/" + repository + "/releases/tag/" + QString::fromUtf8(QUrl::toPercentEncoding(object["tag_name"].toString())));
   QString notes = object.value("body").toString().left(2500);
   updateStatus->setText("Version " + tag + " is available.\n\n" + notes);
   for(const auto &asset:object["assets"].toArray())
    if(asset.toObject()["name"].toString()=="CastWeave-"+tag+"-windows-x64.zip") updater->offer(asset.toObject());
  });
 }
public:
 QWidget *streamsView = nullptr;
 QDialog *settingsView = nullptr;
 void showSettings() {
  if(!settingsView) return;
  settingsView->show();
  settingsView->raise();
  settingsView->activateWindow();
 }
#ifdef STREAMDOCK_PREVIEW
 bool checkChatUi() {
  chat->clear(); chatArtwork.showPreviewState(); liveChat.showPreviewState();
  if(chat->count()!=2 || !chat->item(0)->text().contains("Fixture")) return false;
  auto *input=findChild<QLineEdit*>("chatInput"); auto *send=findChild<QPushButton*>("chatSend");
  if(!input || !input->isEnabled() || !send || !send->isEnabled()) return false;
  input->setText("fixture send"); send->click(); if(!input->text().isEmpty()) return false;
  auto *gear=findChild<QToolButton*>("settingsGear"); if(!gear) return false;
  gear->click(); if(!settingsView->isVisible()) return false; settingsView->hide();
  return true;
 }
#endif
 StreamDock() {
  castweaveLog("plugin_loaded",{{"version",QString(kPluginVersion)}});
  setObjectName("streamdock");
  setFont(QFont("Segoe UI", 9));
  setMinimumSize(300, 380);
  setStyleSheet(
   "#streamdock, #streamdock QWidget { background:#18181b; color:#ededf0; }"
   "#streamdock QLabel { background:transparent; }"
   "#streamdock QListWidget { background:#141416; border:0; padding:4px; }"
   "#streamdock QLineEdit, #streamdock QSpinBox, #streamdock QComboBox { background:#202023; color:#eeeeef; border:1px solid #36363b; border-radius:5px; padding:7px; }"
   "#streamdock QLineEdit:disabled { color:#797980; }"
   "#streamdock QPushButton { padding:6px 10px; border:1px solid #36363b; border-radius:5px; background:#242427; color:#dddddf; }"
   "#streamdock QPushButton:hover { background:#333338; }"
   "#streamdock QPushButton:disabled { color:#74747c; background:#202023; }"
   "#streamdock QToolButton#settingsGear { border:0; border-radius:5px; color:#a8a8b0; padding:3px; font-size:16px; }"
   "#streamdock QToolButton#settingsGear:hover { background:#2a2a2f; color:#ffffff; }"
   "#streamdock QScrollArea { border:0; }");
  auto *root = new QVBoxLayout(this); root->setContentsMargins(12, 8, 12, 10); root->setSpacing(6);
  auto *heading = new QHBoxLayout;
  auto *title = new QLabel("CastWeave"); title->setStyleSheet("font-size:14px; font-weight:600;");
  heading->addWidget(title); heading->addStretch();
  auto *preview = new QLabel("BETA"); preview->setStyleSheet("font-size:10px; color:#a79abd;"); heading->addWidget(preview);
  auto *settingsGear = new QToolButton; settingsGear->setObjectName("settingsGear"); settingsGear->setText(QString::fromUtf8("\xE2\x9A\x99"));
  settingsGear->setToolTip("CastWeave Settings"); settingsGear->setAccessibleName("Open CastWeave Settings"); settingsGear->setFixedSize(28,28);
  heading->addWidget(settingsGear); root->addLayout(heading);
  auto *chatPage = new QWidget; auto *chatLayout = new QVBoxLayout(chatPage); chatLayout->setContentsMargins(0,0,0,0); chatLayout->setSpacing(6);
  filter = new QComboBox; filter->addItems({"All platforms", "Twitch", "YouTube", "Kick"});
  for(int i=1;i<filter->count();++i) filter->setItemIcon(i,QIcon(platformIcon(filter->itemText(i))));
  chat = new QListWidget; chat->setWordWrap(true); chat->setItemDelegate(new ChatDelegate(&chatArtwork,chat)); chat->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); chat->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel); chat->setSelectionMode(QAbstractItemView::NoSelection); chatLayout->addWidget(chat);
  auto *sample = new QPushButton("Sample chat");
  auto *clear = new QPushButton("Clear");
  auto *toolbar = new QHBoxLayout; toolbar->addWidget(filter, 1); toolbar->addWidget(sample); toolbar->addWidget(clear); chatLayout->insertLayout(0, toolbar);
  auto *replyBox = new QLineEdit; replyBox->setObjectName("chatInput"); replyBox->setMaxLength(1000); replyBox->setPlaceholderText("Send a message"); replyBox->setEnabled(false); chatLayout->addWidget(replyBox);
  auto *sendButton=new QPushButton("Send to Twitch"); sendButton->setObjectName("chatSend"); sendButton->setEnabled(false); chatLayout->addWidget(sendButton);
  auto *enableChat=new QPushButton("Enable Twitch chat"); enableChat->hide(); chatLayout->addWidget(enableChat);
  auto *replyHint = new QLabel(liveChat.statusText); replyHint->setTextFormat(Qt::PlainText); replyHint->setStyleSheet("color:#85858e; font-size:11px;"); replyHint->setWordWrap(true); chatLayout->addWidget(replyHint);
  chatArtwork.changed=[this] { chat->doItemsLayout(); chat->viewport()->update(); };
  liveChat.statusChanged=[replyBox,replyHint,sendButton](QString text,bool ready) {
   replyHint->setText(text); replyBox->setEnabled(ready); sendButton->setEnabled(ready);
  };
  auto send=[this,replyBox] { liveChat.send(replyBox->text()); };
  connect(replyBox,&QLineEdit::returnPressed,this,send);
  connect(sendButton,&QPushButton::clicked,this,send);
  liveChat.sendFinished=[replyBox](bool sent,QString) { if(sent) replyBox->clear(); };
  liveChat.messageReceived=[this](const TwitchChatMessage &message) {
   const bool atBottom=chat->verticalScrollBar()->value()>=chat->verticalScrollBar()->maximum()-5;
   QStringList resources; QString marks=iconHtml("Twitch");
   const auto badges=message.tags.value("badges").split(',',Qt::SkipEmptyParts);
   for(const auto &badge:badges) {
    if(!QRegularExpression("^[a-zA-Z0-9_-]+/[a-zA-Z0-9_-]+$").match(badge).hasMatch()) continue;
    const QString url="twitch-badge:"+badge; resources<<url;
    marks+=" <img width='18' height='18' src='"+url+"' alt='"+badge.section('/',0,0).toHtmlEscaped()+"' />";
   }
   const QString color=QRegularExpression("^#[a-fA-F0-9]{6}$").match(message.color).hasMatch() ? message.color : platformColor("Twitch");
   QString body=ChatArtwork::emotes(message,resources);
   auto *item=new QListWidgetItem(marks+" <b style='color:"+color+"'>"+message.name.toHtmlEscaped()+"</b> <span style='color:#e6e6e8'>"+body+"</span>",chat);
   item->setData(Qt::UserRole,"Twitch"); item->setData(Qt::UserRole+2,message.id); item->setData(Qt::UserRole+3,message.userId);
   item->setData(Qt::UserRole+4,resources); item->setData(Qt::UserRole+5,true);
   item->setToolTip("Twitch · "+message.name+"\n"+badges.join(", "));
   item->setHidden(filter->currentText()!="All platforms" && filter->currentText()!="Twitch");
   while(chat->count()>historyLimit) delete chat->takeItem(0);
   if(atBottom) chat->scrollToBottom();
  };
  liveChat.removeMessages=[this](QString id,QString user,bool all) {
   for(int i=chat->count()-1;i>=0;--i) {
    const auto *item=chat->item(i);
    if(item->data(Qt::UserRole+5).toBool() && (all || (!id.isEmpty() && item->data(Qt::UserRole+2).toString()==id) || (!user.isEmpty() && item->data(Qt::UserRole+3).toString()==user))) delete chat->takeItem(i);
   }
  };
  connect(sample, &QPushButton::clicked, this, [this] {
   addMessage("Twitch", "PixelPilot", "subscribed for 5 months", true);
   addMessage("Twitch", "PixelPilot", "Woah, look at that! The stream looks great.", false, {"Moderator","Subscriber"});
   addMessage("Twitch", "StarViewer", "Hello everyone!", false, {"VIP"});
   addMessage("YouTube", "NightOwl", "Hello from YouTube.");
   addMessage("Kick", "GreenRoom", "All three chats in one place.");
   addMessage("Kick", "GreenRoom", "followed!", true);
  });
  connect(clear, &QPushButton::clicked, chat, &QListWidget::clear);
  connect(filter, &QComboBox::currentTextChanged, this, [this](const QString &platform) {
   for (int i = 0; i < chat->count(); ++i) {
    auto *item = chat->item(i); item->setHidden(platform != "All platforms" && item->data(Qt::UserRole).toString() != platform);
   }
  });
  root->addWidget(chatPage);
  auto *streamsPage = new QWidget; auto *streams = new QVBoxLayout(streamsPage);
  streams->addWidget(new QLabel("Destinations & stream info"));
  for(const auto &platform : {"Twitch","YouTube","Kick"}) {
   const QString name(platform);
   auto *row = new QHBoxLayout;
   auto *expand = new QToolButton; expand->setText(name);
   expand->setIcon(QIcon(platformIcon(name))); expand->setIconSize(QSize(20,20));
   expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); expand->setCheckable(true);
   expand->setAccessibleName("Expand " + name + " stream info"); row->addWidget(expand,1);
   auto *state = new QLabel; state->setStyleSheet("color:#92929b; font-size:11px;"); row->addWidget(state);
   auto *toggle = new DestinationSwitch; toggle->setAccessibleName(name + " destination enabled");
   const QString key = "destinations/" + name + "/enabled";
   toggle->setChecked(settings.value(key,false).toBool());
   state->setText(toggle->isChecked() ? "On \u00b7 Not connected" : "Off");
   connect(toggle,&QCheckBox::toggled,this,[this,state,key](bool enabled) {
    settings.setValue(key,enabled); state->setText(enabled ? (state->property("connected").toBool() ? "On - Connected" : "On - Not connected") : (state->property("connected").toBool() ? "Off - Connected" : "Off"));
   });
   row->addWidget(toggle); streams->addLayout(row);
   auto *editor = new QWidget; auto *fields = new QVBoxLayout(editor); fields->setContentsMargins(8,4,8,12);
   if(name=="Twitch") {
    auto *panel=new TwitchPanel(editor);
    panel->chatPaused=[this](QString message) { liveChat.pause(message); };
    panel->chatAccountChanged=[this,enableChat,sample,replyBox](QString token,QString id,QString name,bool permissions) {
     enableChat->setVisible(!id.isEmpty() && !permissions); sample->setEnabled(id.isEmpty());
     if(id.isEmpty()) replyBox->clear();
     if(!id.isEmpty()) { for(int i=chat->count()-1;i>=0;--i) if(!chat->item(i)->data(Qt::UserRole+5).toBool()) delete chat->takeItem(i); }
     chatArtwork.account(token,id); liveChat.account(token,id,name,permissions);
    };
    liveChat.authenticationFailed=[panel] { panel->refreshChatLogin(); };
    connect(enableChat,&QPushButton::clicked,panel,[panel] { panel->enableChat(); });
    panel->connectionChanged=[state,toggle](bool connected) {
     state->setProperty("connected",connected);
     state->setText(toggle->isChecked() ? (connected ? "On - Connected" : "On - Not connected") : (connected ? "Off - Connected" : "Off"));
    };
    fields->addWidget(panel);
   }
   else {
    auto *unavailable = new QLabel(name + " account connection is coming soon.");
    unavailable->setWordWrap(true); fields->addWidget(unavailable);
   }
   streams->addWidget(editor);
   expand->setChecked(name=="Twitch"); editor->setVisible(expand->isChecked());
   connect(expand,&QToolButton::toggled,editor,&QWidget::setVisible);
  }
  auto *info = new QLabel("On selects a destination. It does not start a stream.");
  info->setWordWrap(true); info->setStyleSheet("color:#92929b; font-size:11px;"); streams->addWidget(info); streams->addStretch();
  auto *streamsScroll = new QScrollArea; streamsScroll->setWidgetResizable(true); streamsScroll->setWidget(streamsPage);
  streamsView = new QWidget; streamsView->setObjectName("streamdock"); streamsView->setStyleSheet(styleSheet()); streamsView->setFont(font()); streamsView->setMinimumSize(300,300);
  auto *streamsRoot = new QVBoxLayout(streamsView); streamsRoot->setContentsMargins(10,8,10,8); streamsRoot->addWidget(streamsScroll);
  settingsView = new QDialog(this,Qt::Window); settingsView->setObjectName("streamdock"); settingsView->setWindowTitle("CastWeave Settings");
  settingsView->setModal(false); settingsView->setMinimumSize(390,460); settingsView->resize(430,560); settingsView->setFont(font()); settingsView->setStyleSheet(styleSheet());
  auto *settingsRoot = new QVBoxLayout(settingsView); settingsRoot->setContentsMargins(16,14,16,16);
  auto *settingsPage = new QWidget; auto *options = new QVBoxLayout(settingsPage); options->setContentsMargins(0,0,0,0); options->setSpacing(8);
  auto addSection=[options](const QString &text) {
   auto *label=new QLabel(text); label->setStyleSheet("color:#a79abd; font-size:10px; font-weight:600;"); options->addWidget(label);
  };
  addSection("CHAT");
  options->addWidget(new QLabel("Chat history limit"));
  auto *limit = new QSpinBox; limit->setRange(50, 1000);
  historyLimit = qBound(50, settings.value("historyLimit", 300).toInt(), 1000);
  limit->setValue(historyLimit); options->addWidget(limit);
  connect(limit, &QSpinBox::valueChanged, this, [this](int value) {
   historyLimit = value; settings.setValue("historyLimit", value);
   while(chat->count() > value) delete chat->takeItem(0);
  });
  options->addSpacing(10); addSection("UPDATES");
  options->addWidget(new QLabel("Installed version " + QString(kPluginVersion)));
  auto *automatic = new QCheckBox("Check for updates when OBS starts");
  automatic->setChecked(settings.value("checkAtStartup", true).toBool()); options->addWidget(automatic);
  connect(automatic, &QCheckBox::toggled, this, [this](bool enabled) { settings.setValue("checkAtStartup", enabled); });
  check = new QPushButton("Check for updates"); options->addWidget(check);
  updateStatus = new QLabel("Updates are supplied by zerithvt-Coder/castweave on GitHub, including preview releases.");
  updateStatus->setTextFormat(Qt::PlainText); updateStatus->setWordWrap(true); options->addWidget(updateStatus);
  QString pluginRoot, helper;
#ifndef STREAMDOCK_PREVIEW
  const QString binary=QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
  QDir pluginDir(QFileInfo(binary).absolutePath()); pluginDir.cdUp(); pluginDir.cdUp();
  if(pluginDir.dirName()=="castweave") {
   pluginRoot=pluginDir.absolutePath();
   helper=QString::fromUtf8(obs_get_module_data_path(obs_current_module()))+"/install-update.ps1";
  }
#endif
  updater=new UpdateInstaller(pluginRoot,helper,kPluginVersion); options->addWidget(updater);
#ifndef STREAMDOCK_PREVIEW
  updater->configureRestart([]()->QString {
   if(obs_frontend_streaming_active() || obs_frontend_recording_active() || obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active())
    return "Stop streaming, recording, replay buffer and virtual camera before restarting OBS.";
   if(!obs_frontend_get_main_window()) return "OBS main window is unavailable.";
   return {};
  },[] { return static_cast<QWidget*>(obs_frontend_get_main_window())->close(); });
#endif
  connect(check, &QPushButton::clicked, this, [this] { checkUpdates(); });
  options->addSpacing(10); addSection("DIAGNOSTICS");
  auto *openLog=new QPushButton("Open diagnostic log"); options->addWidget(openLog);
  connect(openLog,&QPushButton::clicked,this,[] {
   castweaveLog("log_opened"); QDesktopServices::openUrl(QUrl::fromLocalFile(castweaveLogPath()));
  });
  auto *logLocation=new QLabel("Log: "+QDir::toNativeSeparators(castweaveLogPath()));
  logLocation->setWordWrap(true); logLocation->setTextInteractionFlags(Qt::TextSelectableByMouse); options->addWidget(logLocation);
  auto *restart = new QLabel("After downloading, click Restart OBS to finish installing."); restart->setWordWrap(true); restart->setStyleSheet("color:#85858e; font-size:11px;"); options->addWidget(restart); options->addStretch();
  auto *settingsScroll = new QScrollArea; settingsScroll->setWidgetResizable(true); settingsScroll->setFrameShape(QFrame::NoFrame); settingsScroll->setWidget(settingsPage); settingsRoot->addWidget(settingsScroll);
  connect(settingsGear,&QToolButton::clicked,this,[this] { showSettings(); });
  if (automatic->isChecked()) QTimer::singleShot(5000, this, [this] { checkUpdates(); });
 }
};

#ifndef STREAMDOCK_PREVIEW
static QPointer<StreamDock> dock;
static QPointer<QWidget> streamsDock;
static QPointer<QAction> settingsAction;
bool obs_module_load(void) { return true; }
void obs_module_post_load(void) {
 dock = new StreamDock;
 streamsDock = dock->streamsView;
 if(!obs_frontend_add_dock_by_id("castweave.streams","CastWeave Streams",streamsDock)) { delete streamsDock.data(); streamsDock = nullptr; }
 if (!obs_frontend_add_dock_by_id("castweave.chat", "CastWeave Chat", dock)) {
  delete dock.data(); dock = nullptr;
 }
 if(dock) {
  settingsAction=static_cast<QAction*>(obs_frontend_add_tools_menu_qaction("CastWeave Settings"));
  QObject::connect(settingsAction,&QAction::triggered,dock.data(),[] { if(dock) dock->showSettings(); });
 }
}
void obs_module_unload(void) {
 if(settingsAction) { delete settingsAction.data(); settingsAction=nullptr; }
 if(streamsDock) obs_frontend_remove_dock("castweave.streams");
 if(dock) obs_frontend_remove_dock("castweave.chat");
}



#else
int main(int argc, char **argv) {
 QApplication app(argc, argv);
 if(argc==2 && QString::fromLocal8Bit(argv[1])=="--test-chat-tls") {
  QSslSocket socket; QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); bool encrypted=false;
  QObject::connect(&socket,&QSslSocket::encrypted,&loop,[&] { encrypted=true; loop.quit(); });
  QObject::connect(&socket,&QSslSocket::errorOccurred,&loop,[&](auto) { loop.quit(); });
  QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
  socket.connectToHostEncrypted("irc.chat.twitch.tv",6697); timeout.start(15000); loop.exec(); socket.abort();
  std::fprintf(stdout,"Twitch TLS handshake: %s (no account login or message sent)\n",encrypted ? "PASS" : "FAILED"); return encrypted ? 0 : 1;
 }
 if(argc==3 && QString::fromLocal8Bit(argv[1])=="--test-download") {
  QFile metadata(QString::fromLocal8Bit(argv[2])); if(!metadata.open(QIODevice::ReadOnly)) return 2;
  auto asset=QJsonDocument::fromJson(metadata.readAll()).object();
  UpdateInstaller success({},{}); if(!success.checkDownload(asset,true)) return 3;
  asset["digest"]="sha256:"+QString(64,'0');
  UpdateInstaller failure({},{}); if(!failure.checkDownload(asset,false)) return 4;
  qInfo("PASS: release download and checksum rejection."); return 0;
 }
 UpdateInstaller restartTest({},{}); if(!restartTest.checkRestartCancellation()) { std::fprintf(stderr,"Restart checks failed\n"); return 1; };
 TwitchChat chatTest; if(!chatTest.runOfflineChecks()) { std::fprintf(stderr,"Chat checks failed\n"); return 1; };
 if(!ChatArtwork::runOfflineChecks()) { std::fprintf(stderr,"Chat artwork checks failed\n"); return 1; };
 TwitchPanel testPanel; if(!testPanel.runOfflineChecks()) { std::fprintf(stderr,"Twitch panel checks failed\n"); return 1; };
 testPanel.saveCategoryPreview();
 StreamDock widget; widget.resize(420, 540); widget.ensurePolished();
 for (auto *button : widget.findChildren<QPushButton *>())
  if (button->text() == "Sample chat") button->click();
 app.processEvents();
 widget.grab().save("preview-chat.png");
 if(!widget.checkChatUi()) { std::fprintf(stderr,"Chat UI checks failed\n"); return 1; }
 app.processEvents(); widget.grab().save("preview-live-chat.png");
 for(auto *panel:widget.streamsView->findChildren<QWidget*>()) if(auto *twitch=dynamic_cast<TwitchPanel*>(panel)) twitch->showPreviewState();
 widget.streamsView->resize(420,760); widget.streamsView->ensurePolished(); app.processEvents();
 widget.streamsView->grab().save("preview-streams.png");
 widget.showSettings(); app.processEvents(); widget.settingsView->grab().save("preview-settings.png"); widget.settingsView->hide();
 delete widget.streamsView;
 std::fprintf(stdout,"PASS: chat parsing, permissions, send results, reconnects, emote rendering, editor and restart checks.\n");
 return 0;
}
#endif
