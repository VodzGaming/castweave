#pragma once
#include <QSslSocket>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QQueue>
#include <QDateTime>
#include <functional>
#include "diagnostics.hpp"

struct TwitchChatMessage {
 QString id, userId, name, text, color;
 QMap<QString,QString> tags;
};

// TLS IRC receives chat; the Helix send API provides an explicit delivery result.
// No socket waits, worker processes, or credential/message bodies in diagnostics.
class TwitchChat final : public QObject {
 QSslSocket socket{this};
 QNetworkAccessManager net{this};
 QTimer retry{this}, watchdog{this};
 QString token, userId, login;
 QByteArray buffer;
 QSet<QString> seen;
 QQueue<QString> seenOrder;
 int epoch=0, retrySeconds=1;
 bool joined=false, sending=false, allowed=false;
 qint64 sendAfter=0;
 using SendDone=std::function<void(int,QJsonObject)>;
#ifdef STREAMDOCK_PREVIEW
 bool offline=false;
 std::function<void(QJsonObject,SendDone)> testSend;
#endif
 void state(const QString &text) {
  statusText=text;
  if(statusChanged) statusChanged(text,joined && !sending && allowed);
 }
 void stopSocket() {
  retry.stop(); watchdog.stop(); joined=false; buffer.clear();
  socket.abort();
 }
 void reconnect() {
  if(!allowed || retry.isActive()) return;
  watchdog.stop(); joined=false;
  retry.start(retrySeconds*1000);
  state(QString("Twitch chat disconnected. Retrying in %1 seconds...").arg(retrySeconds));
  retrySeconds=qMin(30,retrySeconds*2);
  socket.abort(); buffer.clear();
  castweaveLog("chat_reconnecting");
 }
 void open() {
  if(!allowed) return;
  state("Connecting to Twitch chat..."); watchdog.start(20000);
#ifdef STREAMDOCK_PREVIEW
  if(offline) return;
#endif
  socket.connectToHostEncrypted("irc.chat.twitch.tv",6697);
 }
 static QString unescape(const QString &value) {
  QString out;
  for(int i=0;i<value.size();++i) {
   if(value[i]!='\\') { out+=value[i]; continue; }
   if(++i==value.size()) break;
   const auto c=value[i];
   out+=c==':' ? QChar(';') : c=='s' ? QChar(' ') : c=='r' ? QChar('\r') : c=='n' ? QChar('\n') : c;
  }
  return out;
 }
 void received(const QByteArray &bytes) {
  buffer+=bytes;
  if(buffer.size()>65536) { reconnect(); return; }
  int end;
  while((end=buffer.indexOf("\r\n"))>=0) {
   const auto line=QString::fromUtf8(buffer.left(end)); buffer.remove(0,end+2); parse(line);
  }
 }
 void parse(QString line) {
  QMap<QString,QString> tags;
  if(line.startsWith('@')) {
   const int space=line.indexOf(' '); if(space<0) return;
   for(const auto &tag:line.mid(1,space-1).split(';')) {
    const int equals=tag.indexOf('=');
    if(equals>0) tags.insert(tag.left(equals),unescape(tag.mid(equals+1)));
   }
   line.remove(0,space+1);
  }
  QString prefix;
  if(line.startsWith(':')) { const int space=line.indexOf(' '); if(space<0) return; prefix=line.mid(1,space-1); line.remove(0,space+1); }
  const int trailing=line.indexOf(" :");
  const QString text=trailing<0 ? QString{} : line.mid(trailing+2);
  const auto parts=(trailing<0 ? line : line.left(trailing)).split(' ',Qt::SkipEmptyParts);
  if(parts.isEmpty()) return;
  const auto command=parts[0];
  if(command=="PING") { socket.write(("PONG :"+text+"\r\n").toUtf8()); if(joined) watchdog.start(360000); return; }
  if(command=="RECONNECT") { reconnect(); return; }
  if(command=="001") { socket.write(("JOIN #"+login+"\r\n").toUtf8()); return; }
  const QString channel=parts.value(1);
  if(command=="ROOMSTATE" && channel=="#"+login) {
   joined=true; retrySeconds=1; watchdog.start(360000); state("Connected to #"+login+" · Twitch live chat"); castweaveLog("chat_connected"); return;
  }
  if(command=="NOTICE") {
   if(text.contains("authentication",Qt::CaseInsensitive) || text.contains("Login unsuccessful",Qt::CaseInsensitive)) {
    allowed=false; stopSocket(); state("Renewing Twitch chat login...");
    if(authenticationFailed) authenticationFailed();
   } else { state("Twitch: "+text); }
   return;
  }
  if(channel!="#"+login) return;
  if(command=="CLEARMSG") { if(removeMessages) removeMessages(tags.value("target-msg-id"),{},false); return; }
  if(command=="CLEARCHAT") { if(removeMessages) removeMessages({},tags.value("target-user-id"),text.isEmpty()); return; }
  if(command!="PRIVMSG") return;
  watchdog.start(360000);
  TwitchChatMessage message{tags.value("id"),tags.value("user-id"),tags.value("display-name"),text,tags.value("color"),tags};
  if(message.name.isEmpty()) message.name=prefix.section('!',0,0);
  deliver(message);
 }
 void deliver(const TwitchChatMessage &message) {
  if(!message.id.isEmpty()) {
   if(seen.contains(message.id)) return;
   seen.insert(message.id); seenOrder.enqueue(message.id);
   while(seenOrder.size()>2000) seen.remove(seenOrder.dequeue());
  }
  castweaveLog("chat_message_received",{{"id",message.id}});
  if(messageReceived) messageReceived(message);
 }
public:
 QString statusText="Connect Twitch in Streams to use live chat.";
 std::function<void(QString,bool)> statusChanged;
 std::function<void(const TwitchChatMessage&)> messageReceived;
 std::function<void(QString,QString,bool)> removeMessages;
 std::function<void()> authenticationFailed;
 std::function<void(bool,QString)> sendFinished;
 explicit TwitchChat(QObject *parent=nullptr):QObject(parent) {
  retry.setSingleShot(true); watchdog.setSingleShot(true);
  connect(&retry,&QTimer::timeout,this,[this] { open(); });
  connect(&watchdog,&QTimer::timeout,this,[this] { reconnect(); });
  connect(&socket,&QSslSocket::encrypted,this,[this] {
   socket.write(("CAP REQ :twitch.tv/tags twitch.tv/commands\r\nPASS oauth:"+token+"\r\nNICK "+login+"\r\n").toUtf8());
  });
  connect(&socket,&QSslSocket::readyRead,this,[this] { received(socket.readAll()); });
  connect(&socket,&QSslSocket::disconnected,this,[this] { reconnect(); });
  connect(&socket,&QSslSocket::errorOccurred,this,[this](auto) { reconnect(); });
 }
 ~TwitchChat() override { allowed=false; socket.disconnect(this); socket.abort(); }
 void pause(const QString &message) {
  const bool uncertain=sending;
  allowed=false; ++epoch; stopSocket(); sending=false;
  state(message+(uncertain ? " Check Twitch before retrying your pending message." : ""));
 }
 void account(const QString &access,const QString &id,const QString &name,bool permissions) {
  if(!access.isEmpty() && access==token && id==userId && permissions==allowed) return;
  allowed=false; ++epoch; stopSocket(); sending=false;
  if(id!=userId) { seen.clear(); seenOrder.clear(); if(removeMessages) removeMessages({},{},true); }
  token=access; userId=id; login=name.toLower(); retrySeconds=1; sendAfter=0;
  if(token.isEmpty() || userId.isEmpty()) { state("Connect Twitch in Streams to use live chat."); return; }
  if(!permissions) { state("Twitch account connected · Enable Twitch chat to approve chat permissions."); return; }
  if(!QRegularExpression("^[a-z0-9_]{1,25}$").match(login).hasMatch()) { state("Twitch returned an invalid chat account. Reconnect Twitch."); return; }
  allowed=true; open();
 }
 bool send(const QString &input) {
  const QString text=input.trimmed();
  if(!joined || !allowed || sending) return false;
  if(text.isEmpty() || text.toUcs4().size()>500 || text.contains('\r') || text.contains('\n')) { state("Enter a message of 1–500 characters."); return false; }
  if(QDateTime::currentMSecsSinceEpoch()<sendAfter) { state("Please wait before sending another message."); return false; }
  sending=true; state("Sending to Twitch...");
  const int attempt=epoch;
  const QJsonObject body{{"broadcaster_id",userId},{"sender_id",userId},{"message",text}};
  auto done=[this,attempt](int code,QJsonObject object) {
   if(attempt!=epoch) return;
   sending=false;
   const auto data=object["data"].toArray(); const auto result=data.isEmpty() ? QJsonObject{} : data.first().toObject();
   const bool sent=code==200 && result["is_sent"].toBool();
   sendAfter=QDateTime::currentMSecsSinceEpoch()+(code==429 ? 30000 : 1000);
   QString reason=sent ? "Message sent to Twitch." : code==0 ? "Delivery could not be confirmed. Check Twitch before retrying." : code==429 ? "Twitch rate limit reached. Wait 30 seconds, then retry." : "Message not sent: "+result["drop_reason"].toObject()["message"].toString("Twitch rejected the request (HTTP "+QString::number(code)+").");
   state(reason); castweaveLog("chat_send_result",{{"http",code},{"sent",sent}});
   if(sendFinished) sendFinished(sent,reason);
   if(code==401) { allowed=false; stopSocket(); state("Renewing Twitch chat login..."); if(authenticationFailed) authenticationFailed(); }
  };
#ifdef STREAMDOCK_PREVIEW
  if(testSend) { testSend(body,done); return true; }
#endif
  QNetworkRequest request{QUrl("https://api.twitch.tv/helix/chat/messages")};
  request.setTransferTimeout(15000); request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
  request.setRawHeader("Authorization",("Bearer "+token).toUtf8()); request.setRawHeader("Client-Id","8xfp7vv70jaq2id69sq6kert9mj0ky");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
  auto *reply=net.post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(reply,&QNetworkReply::readyRead,reply,[reply] { if(reply->bytesAvailable()>65536) reply->abort(); });
  connect(reply,&QNetworkReply::finished,this,[reply,done] { const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto object=QJsonDocument::fromJson(reply->readAll()).object(); reply->deleteLater(); done(code,object); });
  return true;
 }
#ifdef STREAMDOCK_PREVIEW
 void showPreviewState() {
  offline=true; account("fixture","42","fixture",true); parse(":tmi ROOMSTATE #fixture");
  testSend=[](QJsonObject,SendDone done) { done(200,{{"data",QJsonArray{QJsonObject{{"is_sent",true}}}}}); };
  parse("@id=preview1;user-id=42;display-name=Fixture;color=#b689ff;badges=moderator/1;emotes=25:6-10 :fixture PRIVMSG #fixture :Hello Kappa");
  parse("@id=preview2;user-id=77;display-name=Viewer;color=#73e344 :viewer PRIVMSG #fixture :Your live messages appear here.");
 }
 bool runOfflineChecks() {
  offline=true; int receivedCount=0,removed=0,completed=0; TwitchChatMessage last;
  messageReceived=[&](const auto &message) { ++receivedCount; last=message; };
  removeMessages=[&](auto,auto,bool) { ++removed; };
  account("fake","42","fixture",false); if(allowed || retry.isActive()) return false;
  account("fake","42","fixture",true);
  received(":tmi.twitch.tv ROOMSTATE #fixture\r\n@id=one;user-id=7;display-name=Test\\sUser;color=#123456;badges=moderator/1 :test!test@test PRIVMSG #fixture :Hello ");
  if(!joined || receivedCount) return false;
  received("world\r\n"); if(receivedCount!=1 || last.name!="Test User" || last.text!="Hello world") return false;
  parse("@id=one :test PRIVMSG #fixture :duplicate"); parse("@id=two :test PRIVMSG #other :wrong channel"); if(receivedCount!=1) return false;
  account("fake","42","fixture",true); if(!joined) return false;
  parse("@target-msg-id=one :tmi CLEARMSG #fixture :Hello world"); if(removed!=2) return false;
  SendDone pending;
  testSend=[&](QJsonObject body,SendDone done) { if(body["sender_id"]=="42" && body["message"]=="test") pending=done; };
  sendFinished=[&](bool ok,QString) { completed+=ok ? 1 : 10; };
  if(send(QString(501,'x')) || !send("test") || !pending || send("test")) return false;
  pending(200,{{"data",QJsonArray{QJsonObject{{"is_sent",false},{"drop_reason",QJsonObject{{"message","Blocked"}}}}}}});
  if(completed!=10 || sending || !statusText.contains("Blocked")) return false;
  sendAfter=0; if(!send("test")) return false;
  pending(200,{{"data",QJsonArray{QJsonObject{{"is_sent",true},{"message_id","sent"}}}}}); if(completed!=11) return false;
  sendAfter=0; if(!send("test")) return false;
  account({}, {}, {}, false); pending(200,{{"data",QJsonArray{QJsonObject{{"is_sent",true}}}}}); if(completed!=11 || retry.isActive()) return false;
  account("fake","42","fixture",true); parse("RECONNECT"); if(!retry.isActive() || joined) return false;
  retry.stop(); open(); parse(":tmi ROOMSTATE #fixture"); if(!joined || retrySeconds!=1) return false;
  int authFailures=0; authenticationFailed=[&] { ++authFailures; };
  parse(":tmi NOTICE * :Login authentication failed"); if(authFailures!=1 || joined || retry.isActive()) return false;
  account("rotated","42","fixture",true); if(!allowed) return false;
  received(QByteArray(65537,'x')); if(!retry.isActive() || !buffer.isEmpty()) return false;
  account({}, {}, {}, false); if(retry.isActive()) return false;
  messageReceived={}; removeMessages={}; sendFinished={}; authenticationFailed={}; testSend={}; return true;
 }
#endif
};
