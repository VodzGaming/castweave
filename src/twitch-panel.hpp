#pragma once
#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimer>
#include <QDateTime>
#include <QDesktopServices>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCompleter>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QRegularExpression>
#include <functional>
#include <QMenu>
#include <QToolButton>
#include <QAction>
#include "tag-chips.hpp"
#include "diagnostics.hpp"
#include <QImageReader>
#include <QBuffer>
#include <QCache>
#include <QPainter>
#include <QCheckBox>
#include <QSettings>
#include <QSet>
#include <memory>
#include <QLocale>
#include <QUuid>
#include "twitch-credentials.hpp"
#include "category-result.hpp"
#include <QScrollBar>

// Public desktop application. No client secret is embedded or requested.
class TwitchPanel final : public QWidget {
 const QString clientId = "8xfp7vv70jaq2id69sq6kert9mj0ky";
 QNetworkAccessManager net{this};
 QTimer poll{this}, searchDelay{this}, validation{this}, expiry{this};
 QString token, refreshToken, deviceCode, userId, categoryId, categoryName;
 qint64 deadline = 0;
 int generation = 0, searchRevision = 0, editRevision = 0;
 bool pending = false, loaded = false, busy = false, refreshing = false;
 QCheckBox *remember;
#ifdef STREAMDOCK_PREVIEW
 TwitchCredentials credentials{"CastWeave/Preview/"+QUuid::createUuid().toString(QUuid::WithoutBraces)};
 std::function<QPair<int,QJsonObject>(QString,QByteArray)> testTransport;
#else
 TwitchCredentials credentials;
#endif
 QSettings preferences{"StreamDock","OBS"};
 void persistLogin() {
  if(remember->isChecked() && !token.isEmpty()) {
   if(!credentials.write({{"access_token",token},{"refresh_token",refreshToken}})) {
    castweaveLog("login_store_failed"); remember->setToolTip("Windows could not remember this login. Reconnect may be required after restarting OBS.");
   } else { castweaveLog("login_remembered"); remember->setToolTip("Stored securely for your Windows account. Disconnect removes the saved login."); }
  }
 }
 void restoreLogin() {
  if(!remember->isChecked()) return;
  const auto stored=credentials.read();
  token=stored["access_token"].toString(); refreshToken=stored["refresh_token"].toString();
  if(token.isEmpty()) return;
  pending=true; controls(); setStatus("Restoring your Twitch connection...");
  if(chatPaused) chatPaused("Restoring your remembered Twitch account...");
  castweaveLog("login_restore_started"); validateToken(true);
 }
 void renewToken(bool first) {
  if(refreshing) return;
  if(refreshToken.isEmpty()) { clearSession("Twitch needs a new login. Connect again."); return; }
  refreshing=true; busy=true; validation.stop(); expiry.stop(); controls();
  if(chatPaused) chatPaused("Renewing Twitch login. Chat will reconnect automatically...");
  setStatus("Renewing Twitch login. Your unsaved edits are kept.");
  request("https://id.twitch.tv/oauth2/token","POST",form({{"client_id",clientId},{"grant_type","refresh_token"},{"refresh_token",refreshToken}}),false,
   [this,first](int code,QJsonObject object) {
    if(code==200 && !object["access_token"].toString().isEmpty() && !object["refresh_token"].toString().isEmpty()) {
     token=object["access_token"].toString(); refreshToken=object["refresh_token"].toString();
     persistLogin(); castweaveLog("login_refreshed"); validateToken(first); return;
    }
    refreshing=false;
    if(code==0 || code==429 || code>=500) {
     setStatus("Twitch is temporarily unavailable. Retrying your connection...");
     const int epoch=generation;
     QTimer::singleShot(30000,this,[this,first,epoch] { if(epoch==generation) renewToken(first); }); return;
    }
    clearSession("Your saved Twitch login expired or was revoked. Connect again.");
   });
 }
 QJsonObject baseline;
 QWidget *channelFields;
 QLabel *status, *saveStatus=nullptr;
 QPushButton *keepCategory;
 int countsRevision=0, countsActive=0;
 QTimer countDelay{this};
 struct CachedCount { QString text; qint64 time; };
 QCache<QString,CachedCount> viewerCache{40};
 QList<QString> countQueue;
 QSet<QString> countPending;
 struct ViewerScan { qint64 viewers=0; QSet<QString> seen; int pages=0; };
 void resetCounts() {
  ++countsRevision; countDelay.stop(); countQueue.clear(); countPending.clear(); countsActive=0;
 }
 QString cachedCount(const QString &id) {
  const auto *cached=viewerCache.object(id);
  return cached && QDateTime::currentMSecsSinceEpoch()-cached->time<120000 ? cached->text : QString{};
 }
 void applyCount(const QString &id,const QString &text) {
  for(int row=0;row<results->rowCount();++row) {
   auto *item=results->item(row);
   if(item->data(Qt::UserRole).toString()!=id) continue;
   item->setData(text,CategoryCountRole);
   item->setToolTip(item->text()+"\n"+text+"\nEstimated from live streams; partial means only the first 500 streams were counted.");
  }
 }
 void queueVisibleCounts(bool requireVisible=true) {
  if(userId.isEmpty() || (requireVisible && !completion->popup()->isVisible())) return;
  const int first=qMax(0,completion->popup()->indexAt(QPoint(4,4)).row());
  for(int row=first;row<qMin(first+6,results->rowCount());++row) {
   const QString id=results->item(row)->data(Qt::UserRole).toString();
   if(id.isEmpty()) continue;
   const auto cached=cachedCount(id);
   if(!cached.isEmpty()) { applyCount(id,cached); continue; }
   if(!countPending.contains(id)) { countPending.insert(id); countQueue<<id; }
  }
  pumpCounts();
 }
 void pumpCounts() {
  while(countsActive<6 && !countQueue.isEmpty()) {
   const QString id=countQueue.takeFirst(); ++countsActive;
   viewerPage(id,countsRevision,{},std::make_shared<ViewerScan>());
  }
 }
 void finishCount(const QString &id,int revision,const QString &text,bool cache) {
  if(revision!=countsRevision) return;
  if(cache) viewerCache.insert(id,new CachedCount{text,QDateTime::currentMSecsSinceEpoch()});
  applyCount(id,text); countPending.remove(id); --countsActive; pumpCounts();
 }
 void viewerPage(const QString &id,int revision,const QString &cursor,std::shared_ptr<ViewerScan> scan) {
  QString url="https://api.twitch.tv/helix/streams?first=100&game_id="+QString::fromUtf8(QUrl::toPercentEncoding(id));
  if(!cursor.isEmpty()) url+="&after="+QString::fromUtf8(QUrl::toPercentEncoding(cursor));
  request(url,"GET",{},true,[this,id,revision,scan](int code,QJsonObject object) {
   if(revision!=countsRevision) return;
   if(code!=200 || !object["data"].isArray()) { finishCount(id,revision,"Viewers unavailable",false); return; }
   for(const auto &value:object["data"].toArray()) {
    const auto stream=value.toObject(); const QString streamId=stream["id"].toString();
    if(streamId.isEmpty() || scan->seen.contains(streamId)) continue;
    scan->seen.insert(streamId); scan->viewers+=qMax(qint64(0),stream["viewer_count"].toInteger());
   }
   ++scan->pages;
   const QString next=object["pagination"].toObject()["cursor"].toString();
   if(!next.isEmpty() && scan->pages<5) {
    applyCount(id,QString("≈ %1 live viewers (partial)").arg(QLocale().toString(scan->viewers)));
    viewerPage(id,revision,next,scan); return;
   }
   finishCount(id,revision,QString(next.isEmpty() ? "≈ %1 live viewers" : "≈ %1 live viewers (partial)").arg(QLocale().toString(scan->viewers)),true);
  });
 }
 void setStatus(const QString &text) { status->setText(text); if(saveStatus) saveStatus->setText(text); }
 QPushButton *login, *refresh, *save;
 QLineEdit *title, *category, *tags;
 TagChips *tagChips;
 QToolButton *classifications;
 QList<QAction*> labels;
 QCompleter *completion;
 QStandardItemModel *results;
 QCache<QString,QPixmap> artwork{40};
 using Done = std::function<void(int,QJsonObject)>;
 void controls() {
  const bool connected = !userId.isEmpty();
  channelFields->setVisible(connected);
  status->setVisible(!loaded);
  login->setText(pending ? "Cancel login" : connected ? "Disconnect Twitch" : "Connect Twitch");
  title->setEnabled(connected && loaded && !busy && !refreshing);
  category->setEnabled(connected && loaded && !busy && !refreshing);
  tags->setEnabled(connected && loaded && !busy && !refreshing);
  tagChips->setEnabled(connected && loaded && !busy && !refreshing);
  classifications->setEnabled(connected && loaded && !busy && !refreshing);
  refresh->setEnabled(connected && !busy && !refreshing);
  save->setEnabled(connected && !busy && !refreshing);
  save->setText(loaded ? "Save" : "Retry loading");
  if(connectionChanged) connectionChanged(connected);
 }
 void clearSession(const QString &message) {
  const bool forgotten=credentials.remove(); if(!forgotten) castweaveLog("login_forget_failed");
  ++generation; ++searchRevision; resetCounts(); keepCategory->hide();
  token.clear(); refreshToken.clear(); deviceCode.clear(); userId.clear(); refreshing=false;
  pending = false; loaded = false; busy = false;
  poll.stop(); validation.stop(); expiry.stop(); searchDelay.stop();
  results->clear(); completion->popup()->hide();
  tagChips->resetEntry();
  title->clear(); category->clear(); tags->clear(); categoryId.clear(); baseline = {};
  if(chatAccountChanged) chatAccountChanged({}, {}, {}, false);
  castweaveLog("session_cleared");
  setStatus(forgotten ? message : message+" Windows could not remove the saved credential."); controls();
 }
 void request(const QString &url, const QByteArray &method, const QByteArray &body, bool auth, Done done) {
  QNetworkRequest req{QUrl(url)};
  req.setTransferTimeout(15000);
  req.setRawHeader("User-Agent","CastWeave");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
  req.setHeader(QNetworkRequest::ContentTypeHeader,auth ? "application/json" : "application/x-www-form-urlencoded");
  if(auth) {
   req.setRawHeader("Authorization",("Bearer "+token).toUtf8());
   req.setRawHeader("Client-Id",clientId.toUtf8());
  }
  if(url=="https://id.twitch.tv/oauth2/validate") req.setRawHeader("Authorization",("OAuth "+token).toUtf8());
  const int epoch=generation;
  const QString endpoint=QUrl(url).path(), sentToken=token;
  auto dispatch=[this,epoch,done,auth,endpoint,sentToken](int code,QJsonObject obj) {
   if(epoch!=generation || (endpoint=="/oauth2/validate" && sentToken!=token)) return;
   castweaveLog("twitch_response",{{"endpoint",endpoint},{"http",code}});
   if(auth && code==401) { if(endpoint=="/helix/streams") { done(code,obj); return; } if(sentToken==token) renewToken(!loaded); else done(code,obj); return; }
   done(code,obj);
  };
#ifdef STREAMDOCK_PREVIEW
  if(testTransport) { const auto response=testTransport(url,body); dispatch(response.first,response.second); return; }
#endif
  auto *reply = method=="GET" ? net.get(req) : net.sendCustomRequest(req,method,body);
  connect(reply,&QNetworkReply::readyRead,reply,[reply] { if(reply->bytesAvailable()>1024*1024) reply->abort(); });
  connect(reply,&QNetworkReply::finished,this,[reply,dispatch] {
   const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
   const auto obj=QJsonDocument::fromJson(reply->readAll()).object(); reply->deleteLater();
   dispatch(code,obj);
  });
 }
 QByteArray form(const QList<QPair<QString,QString>> &items) {
  QByteArray result; for(const auto &item:items) { if(!result.isEmpty()) result+="&"; result+=QUrl::toPercentEncoding(item.first)+"="+QUrl::toPercentEncoding(item.second); }
  return result;
 }
 void validateToken(bool first) {
  request("https://id.twitch.tv/oauth2/validate","GET",{},false,[this,first](int code,QJsonObject object) {
   if(code==401) { if(refreshing) clearSession("Twitch rejected the renewed login. Connect again."); else renewToken(first); return; }
   if(code==0 || code==429 || code>=500) {
    setStatus("Twitch validation is temporarily unavailable. Retrying...");
    const int retryEpoch=generation;
    QTimer::singleShot(30000,this,[this,first,retryEpoch] { if(retryEpoch==generation) validateToken(first); }); return;
   }
   if(code!=200 || object["client_id"].toString()!=clientId ||
      object["user_id"].toString().isEmpty() ||
      !object["scopes"].toArray().contains("channel:manage:broadcast")) {
    clearSession("Could not validate Twitch login. Connect again."); return;
   }
   userId=object["user_id"].toString();
   const int remaining=object["expires_in"].toInt();
   if(remaining<=60) { if(refreshing) clearSession("Twitch returned an expired login. Connect again."); else renewToken(first); return; }
   const bool recovered=refreshing;
   pending=false; if(first || recovered) busy=false; refreshing=false; persistLogin(); controls();
   castweaveLog("login_validated");
   const auto scopes=object["scopes"].toArray();
   if(chatAccountChanged) chatAccountChanged(token,userId,object["login"].toString(),scopes.contains("chat:read") && scopes.contains("user:write:chat"));
   expiry.start(qMin(remaining-60,86400)*1000);
   validation.start(60*60*1000);
   if(first) { setStatus("Connected as "+object["login"].toString()); controls(); loadChannel(); }
   else if(recovered) setStatus("Twitch connection ready. Click Save to send any unsaved edits.");
  });
 }
 void pollLogin() {
  if(QDateTime::currentMSecsSinceEpoch()>=deadline) { clearSession("Login timed out. Try again."); return; }
  request("https://id.twitch.tv/oauth2/token","POST",form({
   {"client_id",clientId},{"device_code",deviceCode},
   {"grant_type","urn:ietf:params:oauth:grant-type:device_code"},{"scopes","channel:manage:broadcast chat:read user:write:chat"}
  }),false,[this](int code,QJsonObject object) {
   if(code==200 && !object["access_token"].toString().isEmpty()) {
    token=object["access_token"].toString(); refreshToken=object["refresh_token"].toString(); deviceCode.clear(); pending=true;
    setStatus("Validating Twitch login..."); validateToken(true); return;
   }
   const QString error=object["error"].toString(object["message"].toString()).trimmed().toLower();
   if(error=="authorization_pending") { poll.start(); return; }
   if(error=="slow_down") { poll.setInterval(poll.interval()+5000); poll.start(); return; }
   if(code==0 || code==429 || code>=500) {
    setStatus("Waiting for Twitch. A temporary connection error occurred; retrying...");
    poll.start(qMax(10000,poll.interval())); return;
   }
   if(error=="access_denied") { clearSession("Twitch login was declined. Click Connect Twitch to try again."); return; }
   if(error=="expired_token" || error=="invalid device code") { clearSession("This login code expired or is no longer valid. Start a new login."); return; }
   if(error=="invalid_client" || error=="unauthorized_client") { clearSession("Twitch rejected the app configuration. It must be registered as a Public client."); return; }
   clearSession("Twitch could not finish login (HTTP "+QString::number(code)+"). Start a new login and complete the browser approval.");
  });
 }
 void beginLogin() {
  if(pending || !userId.isEmpty()) { clearSession("Disconnected. The saved Twitch login has been removed."); return; }
  clearSession("Opening Twitch login...");
  pending=true; controls();
  request("https://id.twitch.tv/oauth2/device","POST",form({{"client_id",clientId},{"scopes","channel:manage:broadcast chat:read user:write:chat"}}),false,[this](int code,QJsonObject object) {
   if(code!=200 || object["device_code"].toString().isEmpty()) {
    clearSession("Unable to start login. Check the Twitch app is registered as a Public client."); return;
   }
   const QUrl url(object["verification_uri"].toString());
   if(url.scheme()!="https" || url.host()!="www.twitch.tv" || url.path()!="/activate") {
    clearSession("Twitch returned an unexpected login address."); return;
   }
   deviceCode=object["device_code"].toString();
   deadline=QDateTime::currentMSecsSinceEpoch()+qBound(1,object["expires_in"].toInt(),3600)*1000LL;
   poll.setInterval(qBound(5,object["interval"].toInt(5),60)*1000);
   setStatus("Enter code "+object["user_code"].toString()+" in Twitch, then approve CastWeave.");
   QDesktopServices::openUrl(url); poll.start();
  });
 }
 void loadChannel() {
  busy=true; controls(); setStatus("Loading channel information...");
  request("https://api.twitch.tv/helix/channels?broadcaster_id="+userId,"GET",{},true,[this](int code,QJsonObject object) {
   busy=false;
   const auto data=object["data"].toArray();
   if(code!=200 || data.isEmpty()) { setStatus("Could not load channel information. Click Retry loading."); controls(); return; }
   baseline=data.first().toObject();
   castweaveLog("channel_loaded",{{"tags",baseline["tags"].toArray()},{"category",baseline["game_name"]}});
   title->setText(baseline["title"].toString());
   categoryName=baseline["game_name"].toString(); categoryId=baseline["game_id"].toString(); category->setText(categoryName);
   QStringList current; for(const auto &tag:baseline["tags"].toArray()) current<<tag.toString();
   tags->setText(current.join(", ")); tagChips->resetEntry();
   for(auto *action:labels) action->setChecked(baseline["content_classification_labels"].toArray().contains(action->data().toString()));
   updateClassificationText();
   loaded=true; editRevision=0; keepCategory->hide(); setStatus("Loaded from Twitch. Changes are sent when you click Save."); controls();
  });
 }
 void search() {
  if(userId.isEmpty() || category->text().trimmed().isEmpty()) return;
  const QString query=category->text().trimmed(); const int revision=searchRevision;
  request("https://api.twitch.tv/helix/search/categories?first=20&query="+QString::fromUtf8(QUrl::toPercentEncoding(query)),"GET",{},true,
   [this,revision,query](int code,QJsonObject object) {
    if(revision!=searchRevision || category->text().trimmed()!=query) return;
    results->clear();
    if(code!=200) { setStatus("Category search failed. Try again shortly."); return; }
    for(const auto &entry:object["data"].toArray()) {
     const auto obj=entry.toObject(); auto *item=new QStandardItem(obj["name"].toString());
     item->setData(obj["id"].toString(),Qt::UserRole); item->setSizeHint(QSize(240,72));
     const QString cached=cachedCount(obj["id"].toString());
     item->setData(cached.isEmpty() ? "Loading viewers..." : cached,CategoryCountRole); results->appendRow(item);
     loadArtwork(QUrl(obj["box_art_url"].toString()),obj["id"].toString(),revision);
    }
    countDelay.start();
    completion->setCompletionPrefix("");
    if(category->hasFocus() && results->rowCount()>0) completion->complete();
    setStatus(results->rowCount() ? "Select a category from the results." : "No matching Twitch categories.");
   });
 }
 void sync() {
  if(!loaded || userId.isEmpty() || busy || refreshing) return;
  if(!tagChips->commit()) { castweaveLog("save_blocked",{{"reason","pending_tag_invalid"}}); return; }
  if(title->text().trimmed().isEmpty()) { castweaveLog("save_blocked",{{"reason","empty_title"}}); setStatus("A stream title is required."); return; }
  if(category->text()!=categoryName) { setStatus("Select a category from Twitch's search results first."); return; }
  QJsonArray values; QStringList unique;
  for(const auto &part:tags->text().split(',',Qt::SkipEmptyParts)) {
   const QString tag=part.trimmed();
   if(tag.isEmpty() || tag.size()>25 || !QRegularExpression("^[\\p{L}\\p{N}]+$").match(tag).hasMatch()) {
    setStatus("Tags must contain 1–25 letters or numbers, without spaces."); return;
   }
   if(!unique.contains(tag,Qt::CaseInsensitive)) { unique<<tag; values.append(tag); }
  }
  if(values.size()>10) { setStatus("Use at most 10 Twitch tags."); return; }
  QJsonObject changes;
  if(title->text()!=baseline["title"].toString()) changes["title"]=title->text();
  if(categoryId!=baseline["game_id"].toString()) changes["game_id"]=categoryId;
  if(values!=baseline["tags"].toArray()) changes["tags"]=values;
  QJsonArray classificationChanges;
  for(auto *action:labels) {
   const QString id=action->data().toString();
   if(action->isChecked()!=baseline["content_classification_labels"].toArray().contains(id))
    classificationChanges.append(QJsonObject{{"id",id},{"is_enabled",action->isChecked()}});
  }
  if(!classificationChanges.isEmpty()) changes["content_classification_labels"]=classificationChanges;
  if(changes.isEmpty()) { castweaveLog("save_no_changes"); setStatus("No changes to send."); return; }
  castweaveLog("save_requested",{{"fields",QJsonArray::fromStringList(changes.keys())},{"tags_before",baseline["tags"].toArray()},{"tags_after",values}});
  busy=true; controls(); setStatus("Saving to Twitch...");
  request("https://api.twitch.tv/helix/channels?broadcaster_id="+userId,"PATCH",QJsonDocument(changes).toJson(QJsonDocument::Compact),true,
   [this,changes](int code,QJsonObject) {
    if(code==204) {
     setStatus("Twitch accepted the save. Checking the channel...");
     verifySaved(changes,0); return;
    }
    busy=false;
    castweaveLog("save_rejected",{{"http",code}});
    setStatus("Twitch rejected the update (HTTP "+QString::number(code)+"). Your edits are still here.");
    controls();
   });
 }
 static bool matchesChanges(const QJsonObject &actual,const QJsonObject &changes) {
  for(auto it=changes.begin();it!=changes.end();++it) {
   if(it.key()=="content_classification_labels") {
    for(const auto &item:it.value().toArray()) {
     const auto label=item.toObject();
     if(actual[it.key()].toArray().contains(label["id"])!=label["is_enabled"].toBool()) return false;
    }
   } else if(it.key()=="tags") {
    QStringList got,wanted;
    for(const auto &item:actual["tags"].toArray()) got<<item.toString().toLower();
    for(const auto &item:it.value().toArray()) wanted<<item.toString().toLower();
    got.sort(); wanted.sort(); if(got!=wanted) return false;
   } else if(actual[it.key()]!=it.value()) return false;
  }
  return true;
 }
 void verifySaved(const QJsonObject &changes,int attempt) {
  request("https://api.twitch.tv/helix/channels?broadcaster_id="+userId,"GET",{},true,
   [this,changes,attempt](int code,QJsonObject object) {
    const auto data=object["data"].toArray();
    const auto actual=data.isEmpty() ? QJsonObject{} : data.first().toObject();
    if(code==200 && !actual.isEmpty() && matchesChanges(actual,changes)) {
     baseline=actual; editRevision=0; busy=false; controls();
     setStatus("Saved and verified on Twitch at "+QDateTime::currentDateTime().toString("HH:mm:ss")+".");
     castweaveLog("save_verified",{{"tags",actual["tags"].toArray()},{"category",actual["game_name"]}});
     return;
    }
    if(attempt<2) {
     const int epoch=generation;
     QTimer::singleShot(750,this,[this,changes,attempt,epoch] { if(epoch==generation) verifySaved(changes,attempt+1); });
     return;
    }
    busy=false; controls();
    setStatus("Twitch accepted the save, but read-back could not confirm it. Check your Twitch dashboard or diagnostic log.");
    castweaveLog("save_verification_failed",{{"http",code},{"tags_returned",actual["tags"].toArray()}});
   });
 }
 void loadArtwork(const QUrl &url,const QString &id,int revision) {
  if(url.scheme()!="https" || url.host()!="static-cdn.jtvnw.net") return;
  auto apply=[this,id,revision](const QPixmap &pix) {
   if(revision!=searchRevision) return;
   for(int row=0;row<results->rowCount();++row) if(results->item(row)->data(Qt::UserRole).toString()==id) results->item(row)->setIcon(QIcon(pix));
  };
  if(auto *cached=artwork.object(url.toString())) { apply(*cached); return; }
  QNetworkRequest request(url); request.setTransferTimeout(10000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
  auto *reply=net.get(request);
  connect(reply,&QNetworkReply::readyRead,reply,[reply] { if(reply->bytesAvailable()>512*1024) reply->abort(); });
  connect(reply,&QNetworkReply::finished,this,[this,reply,url,apply] {
   const auto bytes=reply->readAll(); const bool ok=reply->error()==QNetworkReply::NoError; reply->deleteLater();
   if(!ok || bytes.size()>512*1024) return;
   QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
   QImageReader reader(&buffer); const QSize size=reader.size();
   if(!size.isValid() || size.width()>2048 || size.height()>2048) return;
   reader.setScaledSize(QSize(40,54)); const QImage image=reader.read(); if(image.isNull()) return;
   const QPixmap pix=QPixmap::fromImage(image); artwork.insert(url.toString(),new QPixmap(pix)); apply(pix);
  });
 }
 void updateClassificationText() {
  QStringList selected; for(auto *action:labels) if(action->isChecked()) selected<<action->text();
  classifications->setText(selected.isEmpty() ? "Select classifications" : QString::number(selected.size())+" selected");
  classifications->setToolTip(selected.join(", "));
 }
public:
 std::function<void(bool)> connectionChanged;
 std::function<void(QString,QString,QString,bool)> chatAccountChanged;
 std::function<void(QString)> chatPaused;
 void enableChat() { clearSession("Opening Twitch chat approval..."); beginLogin(); }
 void refreshChatLogin() { renewToken(!loaded); }
#ifdef STREAMDOCK_PREVIEW
 void saveCategoryPreview() {
  QStandardItemModel model;
  auto *item=new QStandardItem("Once Human");
  item->setData("≈ 4,595 live viewers (partial)",CategoryCountRole);
  QPixmap art(40,54); art.fill(QColor("#48282b"));
  { QPainter painter(&art); painter.setPen(Qt::white); painter.drawText(art.rect(),Qt::AlignCenter,"ART"); }
  item->setIcon(QIcon(art)); model.appendRow(item);
  QPixmap picture(420,72); picture.fill(QColor("#26262e"));
  QStyleOptionViewItem option; option.rect=picture.rect(); option.font=QFont("Segoe UI",9); option.widget=completion->popup();
  option.state=QStyle::State_Enabled|QStyle::State_Selected;
  { QPainter painter(&picture); CategoryResultDelegate delegate; delegate.paint(&painter,option,model.index(0,0)); }
  picture.save("preview-category.png");
 }
 void showPreviewState() {
  userId="preview"; loaded=true; title->setText("Exploring new worlds | CastWeave");
  categoryName="Project Zomboid"; categoryId="1"; category->setText(categoryName);
  tags->setText("English, VTuber, Collab, Funny, UK, BeginnerFriendly, survival, Zombies");
  setStatus("Connected preview - sample channel information"); controls();
 }
 bool runOfflineChecks() {
  if(!tagChips->runOfflineChecks()) return false;
  TwitchCredentials fixture("CastWeave/Test/"+QString::number(QDateTime::currentMSecsSinceEpoch()));
  const QJsonObject fake{{"access_token","test-only-access"},{"refresh_token","test-only-refresh"}};
  if(!fixture.write(fake)) return false;
  const bool roundTrip=fixture.read()==fake; const bool removed=fixture.remove();
  if(!roundTrip || !removed || !fixture.read().isEmpty()) return false;
  if(form({{"refresh_token","a+b&c="}})!="refresh_token=a%2Bb%26c%3D") return false;
  remember->setChecked(true);
  int validations=0, renewals=0, viewerCalls=0; bool firstPageShown=false;
  int chatLogins=0; bool chatPermission=false,grantChat=false;
  chatAccountChanged=[&](QString access,QString id,QString name,bool permitted) { if(!id.isEmpty()) { if(access.isEmpty() || name!="fixture") chatLogins=-100; else ++chatLogins; chatPermission=permitted; } };
  testTransport=[&](QString url,QByteArray body)->QPair<int,QJsonObject> {
   if(url.endsWith("/validate")) {
    if(++validations==1) return {401,{}};
    return {200,{{"client_id",clientId},{"user_id","fixture"},{"login","fixture"},{"expires_in",14400},{"scopes",grantChat ? QJsonArray{"channel:manage:broadcast","chat:read","user:write:chat"} : QJsonArray{"channel:manage:broadcast"}}}};
   }
   if(url.endsWith("/token")) {
    ++renewals;
    if(!body.contains("refresh_token=test-only-refresh")) return {400,{}};
    return {200,{{"access_token","rotated-access"},{"refresh_token","rotated-refresh"}}};
   }
   if(url.contains("/channels?")) return {200,{{"data",QJsonArray{QJsonObject{{"title","Fixture"},{"game_name","Fixture game"},{"game_id","1"},{"tags",QJsonArray{"English"}}}}}}};
   if(url.contains("/streams?")) {
    ++viewerCalls;
    if(!url.contains("&after=")) return {200,{{"data",QJsonArray{QJsonObject{{"id","a"},{"viewer_count",10}}}},{"pagination",QJsonObject{{"cursor","next"}}}}};
    firstPageShown=results->rowCount()>0 && results->item(0)->data(CategoryCountRole)=="≈ 10 live viewers (partial)";
    return {200,{{"data",QJsonArray{QJsonObject{{"id","a"},{"viewer_count",10}},QJsonObject{{"id","b"},{"viewer_count",20}}}}}};
   }
   return {200,{{"data",QJsonArray{}}}};
  };
  if(!credentials.write(fake)) return false;
  restoreLogin();
  if(!loaded || busy || pending || renewals!=1 || credentials.read()["refresh_token"]!="rotated-refresh") return false;
  if(chatLogins!=1 || chatPermission) return false;
  grantChat=true; validateToken(false); if(chatLogins!=2 || !chatPermission) return false;
  auto *countItem=new QStandardItem("Fixture category"); countItem->setData("1",Qt::UserRole); results->appendRow(countItem);
  queueVisibleCounts(false);
  if(!firstPageShown || countDelay.interval()!=0 || countItem->data(CategoryCountRole)!="≈ 30 live viewers") return false;
  if(countItem->text()!="Fixture category") return false;
  queueVisibleCounts(false); if(viewerCalls!=2) return false;
  const int obsolete=countsRevision; resetCounts();
  finishCount("1",obsolete,"wrong stale count",true);
  if(cachedCount("1")!="≈ 30 live viewers" || countsActive!=0) return false;
  remember->setChecked(false);
  if(!credentials.read().isEmpty() || !loaded) return false;
  remember->setChecked(true);
  if(credentials.read().isEmpty()) return false;
  beginLogin();
  if(!credentials.read().isEmpty() || loaded) return false;
  testTransport=[](QString,QByteArray)->QPair<int,QJsonObject> { return {503,{}}; };
  if(!credentials.write(fake)) return false;
  restoreLogin();
  if(credentials.read()!=fake || loaded) return false;
  clearSession("Test complete");
  testTransport=[](QString url,QByteArray)->QPair<int,QJsonObject> { return {url.endsWith("/validate") ? 401 : 400,{}}; };
  if(!credentials.write(fake)) return false;
  restoreLogin();
  if(loaded || !credentials.read().isEmpty()) return false;
  testTransport={};
  chatAccountChanged={};
  if(!matchesChanges(QJsonObject{{"tags",QJsonArray{"English"}}},QJsonObject{{"tags",QJsonArray{"english"}}})) return false;
  if(matchesChanges(QJsonObject{{"tags",QJsonArray{"English","Zombies"}}},QJsonObject{{"tags",QJsonArray{"English"}}})) return false;
  if(save->isEnabled() || title->isEnabled() || !channelFields->isHidden()) return false;
  userId="test"; loaded=true; controls();
  if(channelFields->isHidden()) return false;
  baseline=QJsonObject{{"title","Test"},{"game_name","Test category"},{"game_id","1"},{"tags",QJsonArray{"English"}}};
  title->setText("Test"); categoryName="Test category"; categoryId="1"; category->setText(categoryName); tags->setText("English");
  sync(); if(status->text()!="No changes to send.") return false;
  category->setText("Unresolved"); sync(); if(!status->text().startsWith("Select a category")) return false;
  testTransport=[](QString,QByteArray)->QPair<int,QJsonObject> { return {200,{{"data",QJsonArray{}}}}; };
  keepCategory->click(); sync();
  if(category->text()!="Test category" || status->text()!="No changes to send.") return false;
  testTransport={};
  category->setText(categoryName); tags->setText("has spaces"); sync(); if(!status->text().startsWith("Tags must")) return false;
  title->clear(); sync(); if(!status->text().startsWith("A stream title")) return false;
  clearSession("Connect to load your Twitch channel.");
  return !save->isEnabled() && !title->isEnabled() && channelFields->isHidden();
 }
#endif
 explicit TwitchPanel(QWidget *parent=nullptr):QWidget(parent) {
  auto *layout=new QVBoxLayout(this);
  status=new QLabel("Connect to load your Twitch channel.");
  status->setWordWrap(true); status->setTextFormat(Qt::PlainText);
  login=new QPushButton("Connect Twitch"); layout->addWidget(login); layout->addWidget(status);
  remember=new QCheckBox("Remember this Twitch account");
  remember->setChecked(preferences.value("rememberTwitch",true).toBool());
  layout->addWidget(remember);
  connect(remember,&QCheckBox::toggled,this,[this](bool enabled) {
#ifndef STREAMDOCK_PREVIEW
   preferences.setValue("rememberTwitch",enabled);
#endif
   if(enabled) persistLogin();
   else if(!credentials.remove()) { castweaveLog("login_forget_failed"); setStatus("Windows could not remove the remembered login. Try Disconnect again."); }
  });
  channelFields=new QWidget;
  auto *fieldsLayout=new QVBoxLayout(channelFields); fieldsLayout->setContentsMargins(0,0,0,0);
  layout->addWidget(channelFields);
  title=new QLineEdit; title->setMaxLength(140);
  category=new QLineEdit; category->setClearButtonEnabled(true); category->setPlaceholderText("Search Twitch categories");
  QPixmap searchIcon(20,20); searchIcon.fill(Qt::transparent);
  { QPainter painter(&searchIcon); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(QColor("#dddde4"),1.6)); painter.drawEllipse(QRectF(3,3,10,10)); painter.drawLine(QPointF(12,12),QPointF(17,17)); }
  category->addAction(QIcon(searchIcon),QLineEdit::LeadingPosition);
  category->setStyleSheet("QLineEdit:focus { border:2px solid #a970ff; }");
  tags=new QLineEdit(this); tags->hide();
  for(const auto &field:QList<QPair<QString,QLineEdit*>>{{"Title",title},{"Category",category}}) {
   fieldsLayout->addWidget(new QLabel(field.first)); fieldsLayout->addWidget(field.second);
   connect(field.second,&QLineEdit::textEdited,this,[this] { ++editRevision; });
  }
  keepCategory=new QPushButton("Keep current category"); keepCategory->hide(); fieldsLayout->addWidget(keepCategory);
  connect(keepCategory,&QPushButton::clicked,this,[this] {
   categoryId=baseline["game_id"].toString(); categoryName=baseline["game_name"].toString();
   category->setText(categoryName); ++searchRevision; searchDelay.stop(); resetCounts(); completion->popup()->hide(); keepCategory->hide();
   setStatus("Current channel category kept. Click Save to send your other edits.");
  });
  fieldsLayout->addWidget(new QLabel("Classifications"));
  classifications=new QToolButton; classifications->setPopupMode(QToolButton::InstantPopup);
  classifications->setToolButtonStyle(Qt::ToolButtonTextOnly); classifications->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
  classifications->setStyleSheet("QToolButton { text-align:left; padding:8px; background:#202023; border:1px solid #36363b; border-radius:5px; }");
  auto *menu=new QMenu(classifications);
  for(const auto &label:QList<QPair<QString,QString>>{
   {"Drugs & Intoxication","DrugsIntoxication"},{"Gambling","Gambling"},{"Politics","DebatedSocialIssuesAndPolitics"},
   {"Profanity","ProfanityVulgarity"},{"Sexual Themes","SexualThemes"},{"Violence","ViolentGraphic"}}) {
   auto *action=menu->addAction(label.first); action->setCheckable(true); action->setData(label.second); labels<<action;
   connect(action,&QAction::toggled,this,[this] { ++editRevision; updateClassificationText(); });
  }
  classifications->setMenu(menu); updateClassificationText(); fieldsLayout->addWidget(classifications);
  fieldsLayout->addWidget(new QLabel("Tags"));
  tagChips=new TagChips(tags); fieldsLayout->addWidget(tagChips);
  connect(tags,&QLineEdit::textChanged,this,[this] { ++editRevision; });
  results=new QStandardItemModel(this);
  completion=new QCompleter(results,this); completion->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
  category->setCompleter(completion);
  completion->setMaxVisibleItems(6);
  completion->popup()->setItemDelegate(new CategoryResultDelegate(completion->popup()));
  countDelay.setSingleShot(true); countDelay.setInterval(0);
  connect(&countDelay,&QTimer::timeout,this,[this] { queueVisibleCounts(); });
  connect(completion->popup()->verticalScrollBar(),&QScrollBar::valueChanged,this,[this] { countDelay.start(); });
  completion->popup()->setIconSize(QSize(40,54));
  completion->popup()->setStyleSheet("QAbstractItemView { background:#26262e; color:#eeeef2; border:1px solid #44444e; outline:0; } QAbstractItemView::item { padding:5px; border-bottom:1px solid #393940; } QAbstractItemView::item:selected { background:#4c3b68; }");
  connect(completion,qOverload<const QModelIndex &>(&QCompleter::activated),this,[this](const QModelIndex &index) {
   categoryName=index.data().toString(); categoryId=index.data(Qt::UserRole).toString();
   category->setText(categoryName); ++searchRevision; searchDelay.stop(); resetCounts(); keepCategory->hide();
   setStatus("Category selected. Click Save to apply your changes.");
  });
  searchDelay.setSingleShot(true); searchDelay.setInterval(350);
  connect(category,&QLineEdit::textEdited,this,[this] {
   ++searchRevision; resetCounts(); keepCategory->setVisible(category->text()!=categoryName);
   results->clear(); completion->popup()->hide(); searchDelay.start();
  });
  connect(&searchDelay,&QTimer::timeout,this,[this] { search(); });
  refresh=new QPushButton("Reload from Twitch",this); refresh->hide(); save=new QPushButton("Save");
  fieldsLayout->addWidget(save);
  saveStatus=new QLabel; saveStatus->setWordWrap(true); saveStatus->setTextFormat(Qt::PlainText); fieldsLayout->addWidget(saveStatus);
  auto *openLog=new QPushButton("Open diagnostic log"); fieldsLayout->addWidget(openLog);
  connect(openLog,&QPushButton::clicked,this,[] { castweaveLog("log_opened"); QDesktopServices::openUrl(QUrl::fromLocalFile(castweaveLogPath())); });
  connect(refresh,&QPushButton::clicked,this,[this] {
   if(editRevision>0 && QMessageBox::question(this,"Reload Twitch info","Discard your unsent edits and reload from Twitch?")!=QMessageBox::Yes) return;
   loadChannel();
  });
  connect(save,&QPushButton::clicked,this,[this] { if(!loaded) loadChannel(); else sync(); });
  connect(login,&QPushButton::clicked,this,[this] { beginLogin(); });
  poll.setSingleShot(true); connect(&poll,&QTimer::timeout,this,[this] { pollLogin(); });
  validation.setSingleShot(true); connect(&validation,&QTimer::timeout,this,[this] { validateToken(false); });
  expiry.setSingleShot(true); connect(&expiry,&QTimer::timeout,this,[this] { renewToken(false); });
  controls();
#ifndef STREAMDOCK_PREVIEW
  QTimer::singleShot(0,this,[this] { restoreLogin(); });
#endif
 }
};
