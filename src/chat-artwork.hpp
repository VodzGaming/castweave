#pragma once
#include <QCache>
#include <QImageReader>
#include <QBuffer>
#include <QTextDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <functional>
#include <algorithm>
#include "twitch-chat.hpp"

class ChatArtwork final : public QObject {
 QNetworkAccessManager net{this};
 QCache<QString,QImage> images{512};
 QSet<QString> pending, failed;
 QMap<QString,QString> globalBadges, channelBadges;
 QString accountId;
 int generation=0;
public:
 std::function<void()> changed;
 explicit ChatArtwork(QObject *parent=nullptr):QObject(parent) {}
 void account(const QString &token,const QString &id) {
  if(accountId==id) return;
  accountId=id; ++generation; channelBadges.clear();
  if(id.isEmpty()) return;
  for(const bool global:{true,false}) {
   QNetworkRequest request{QUrl(global ? "https://api.twitch.tv/helix/chat/badges/global" : "https://api.twitch.tv/helix/chat/badges?broadcaster_id="+id)};
   request.setTransferTimeout(10000); request.setRawHeader("Authorization",("Bearer "+token).toUtf8());
   request.setRawHeader("Client-Id","8xfp7vv70jaq2id69sq6kert9mj0ky");
   request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
   auto *reply=net.get(request); const int epoch=generation;
   connect(reply,&QNetworkReply::readyRead,reply,[reply] { if(reply->bytesAvailable()>1024*1024) reply->abort(); });
   connect(reply,&QNetworkReply::finished,this,[this,reply,global,epoch] {
    const auto object=QJsonDocument::fromJson(reply->readAll()).object(); const bool ok=reply->error()==QNetworkReply::NoError; reply->deleteLater();
    if(!ok || epoch!=generation) return;
    auto &badges=global ? globalBadges : channelBadges;
    for(const auto &set:object["data"].toArray()) for(const auto &version:set.toObject()["versions"].toArray())
     badges[set.toObject()["set_id"].toString()+"/"+version.toObject()["id"].toString()]=version.toObject()["image_url_1x"].toString();
    if(changed) changed();
   });
  }
 }
 void resource(QTextDocument &doc,const QString &url) {
  const QString key=url.startsWith("twitch-badge:") ? url.mid(13) : QString{};
  const QString address=key.isEmpty() ? url : channelBadges.value(key,globalBadges.value(key));
  if(const auto *image=images.object(address)) { doc.addResource(QTextDocument::ImageResource,QUrl(url),*image); return; }
  if(pending.contains(address) || failed.contains(address) || pending.size()>=12) return;
  const QUrl source(address);
  if(source.scheme()!="https" || source.host()!="static-cdn.jtvnw.net" || !source.userInfo().isEmpty()) return;
  pending.insert(address);
  QNetworkRequest request(source); request.setTransferTimeout(10000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
  auto *reply=net.get(request);
  connect(reply,&QNetworkReply::readyRead,reply,[reply] { if(reply->bytesAvailable()>512*1024) reply->abort(); });
  connect(reply,&QNetworkReply::finished,this,[this,reply,url=address] {
   pending.remove(url); const auto bytes=reply->readAll(); const bool ok=reply->error()==QNetworkReply::NoError; reply->deleteLater();
   QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
   const QSize size=reader.size();
   if(ok && size.isValid() && size.width()<=1024 && size.height()<=1024) {
    if(size.width()>56 || size.height()>56) reader.setScaledSize(size.scaled(56,56,Qt::KeepAspectRatio));
    const auto image=reader.read(); if(!image.isNull()) { images.insert(url,new QImage(image)); if(changed) changed(); return; }
   }
   if(failed.size()>=512) failed.clear(); failed.insert(url);
  });
 }
 static QString emotes(const TwitchChatMessage &message,QStringList &urls) {
  struct Range { int start,end; QString id; };
  QList<Range> ranges; const auto points=message.text.toStdU32String();
  for(const auto &emote:message.tags.value("emotes").split('/')) {
   const auto pair=emote.split(':'); if(pair.size()!=2 || !QRegularExpression("^[A-Za-z0-9_-]+$").match(pair[0]).hasMatch()) continue;
   for(const auto &location:pair[1].split(',')) {
    const auto ends=location.split('-'); if(ends.size()!=2) continue;
    bool ok1=false,ok2=false; const int start=ends[0].toInt(&ok1),end=ends[1].toInt(&ok2);
    if(ok1 && ok2 && start>=0 && end>=start && end<int(points.size())) ranges.append({start,end,pair[0]});
   }
  }
  std::sort(ranges.begin(),ranges.end(),[](const auto &a,const auto &b) { return a.start<b.start; });
  QString html; int pos=0;
  auto text=[&](int start,int count) { return QString::fromUcs4(points.data()+start,count).toHtmlEscaped(); };
  for(const auto &range:ranges) {
   if(range.start<pos) continue;
   html+=text(pos,range.start-pos);
   const QString url="https://static-cdn.jtvnw.net/emoticons/v2/"+range.id+"/static/dark/1.0";
   urls<<url;
   html+="<img width='28' height='28' src='"+url+"' alt='"+text(range.start,range.end-range.start+1)+"' />";
   pos=range.end+1;
  }
  return html+text(pos,int(points.size())-pos);
 }
#ifdef STREAMDOCK_PREVIEW
 void showPreviewState() {
  globalBadges["moderator/1"]="https://static-cdn.jtvnw.net/preview-moderator";
  QImage badge(18,18,QImage::Format_ARGB32); badge.fill(QColor("#00a86b")); images.insert(globalBadges["moderator/1"],new QImage(badge));
  QImage emote(28,28,QImage::Format_ARGB32); emote.fill(QColor("#999999")); images.insert("https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/1.0",new QImage(emote));
 }
 static bool runOfflineChecks() {
  TwitchChatMessage message; message.text=QString::fromUtf8("😀 Kappa <script>"); message.tags["emotes"]="25:2-6/../../bad!:0-0/26:400-500";
  QStringList urls; const auto html=emotes(message,urls);
  return urls.size()==1 && urls[0].endsWith("/25/static/dark/1.0") && html.contains("&lt;script&gt;") && !html.contains("<script>") && html.startsWith(QString::fromUtf8("😀 "));
 }
#endif
};
