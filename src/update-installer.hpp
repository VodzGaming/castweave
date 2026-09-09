#pragma once
#include "diagnostics.hpp"
#include <QWidget>
#include <memory>
#include <functional>
#include <QCoreApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QProcess>
#include <QUrl>
#include <QRegularExpression>

class UpdateInstaller final : public QWidget {
 QNetworkAccessManager net{this};
 QJsonObject asset;
 QString target, script, zipPath, expectedHash;
 QPushButton *fetch,*install;
 QLabel *status;
 bool active=false, closing=false;
 QString statePath, cancelPath;
 QTimer monitor{this};
 qint64 started=0;
 std::function<QString()> restartBlocker;
 std::function<bool()> closeObs;
 void cancelRestart(const QString &message) {
  if(!cancelPath.isEmpty()) {
   QFile file(cancelPath);
   if(!file.open(QIODevice::WriteOnly) || file.write("cancel")!=6) {
    status->setText("Could not cancel the queued update. Keep OBS open until the installer times out.");
    castweaveLog("update_cancel_failed"); return;
   }
  }
  monitor.stop(); active=false; closing=false; install->setEnabled(true); fetch->setEnabled(true); status->setText(message);
  castweaveLog("update_restart_cancelled");
 }
 void beginRestart() {
  if(active || zipPath.isEmpty() || target.isEmpty() || !QFile::exists(script)) return;
  if(!restartBlocker || !closeObs) { status->setText("Restart is only available inside OBS."); return; }
  const QString blocked=restartBlocker();
  if(!blocked.isEmpty()) { status->setText(blocked); return; }
  const QString dir=QFileInfo(zipPath).absolutePath();
  const QString id=QString::number(QDateTime::currentMSecsSinceEpoch());
  statePath=dir+"/install-"+id+".json"; cancelPath=statePath+".cancel";
  const QString cachedHelper=dir+"/install-"+id+".ps1";
  if(!QFile::copy(script,cachedHelper)) { status->setText("Cannot prepare the update installer."); return; }
  QProcess process;
  process.setProgram(qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0/powershell.exe");
  QStringList args={"-NoProfile","-NonInteractive","-WindowStyle","Hidden","-ExecutionPolicy","Bypass","-File",cachedHelper,
   "-Zip",zipPath,"-Destination",target,"-ExpectedHash",expectedHash,"-LogPath",castweaveLogPath(),
   "-RestartExecutable",QCoreApplication::applicationFilePath(),"-ParentProcessId",QString::number(QCoreApplication::applicationPid()),
   "-StatePath",statePath,"-ResultPath",dir+"/last-update.json"};
  if(QCoreApplication::arguments().contains("--portable") || QCoreApplication::arguments().contains("-p")) args<<"-Portable";
  process.setArguments(args);
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
  if(!process.startDetached()) { status->setText("Could not start the installer. OBS is still open."); return; }
  active=true; closing=false; started=QDateTime::currentMSecsSinceEpoch();
  install->setEnabled(false); fetch->setEnabled(false);
  status->setText("Preparing to restart OBS..."); monitor.start(200); castweaveLog("update_restart_requested");
 }
 void checkRestart() {
  QFile file(statePath); QJsonObject state;
  if(file.open(QIODevice::ReadOnly)) state=QJsonDocument::fromJson(file.readAll()).object();
  const QString phase=state["phase"].toString();
  if(phase=="failed" || phase=="restart_failed" || phase=="cancelled") {
   monitor.stop(); active=false; install->setEnabled(true); fetch->setEnabled(true);
   status->setText(state["message"].toString()); return;
  }
  if(phase=="ready" && !closing) {
   const QString blocked=restartBlocker();
   if(!blocked.isEmpty()) { cancelRestart(blocked); return; }
   closing=true; status->setText("Restarting OBS to install the update...");
   const auto close=closeObs; const QPointer<UpdateInstaller> self(this);
   const bool accepted=close();
   if(self && !accepted) self->cancelRestart("Restart cancelled. Your downloaded update is ready when you are.");
   return;
  }
  if(!closing && QDateTime::currentMSecsSinceEpoch()-started>60000)
   cancelRestart("The installer did not become ready. OBS stayed open. Try Restart OBS again or check the diagnostic log.");
 }
public:
 UpdateInstaller(const QString &pluginRoot,const QString &helper,const QString &installedVersion={},QWidget *parent=nullptr):QWidget(parent),target(pluginRoot),script(helper) {
  setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
  auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
  fetch=new QPushButton("Download update"); install=new QPushButton("Restart OBS");
  status=new QLabel; status->setWordWrap(true); status->setTextFormat(Qt::PlainText);
  layout->addWidget(fetch); layout->addWidget(install); layout->addWidget(status);
  fetch->setEnabled(false); install->hide();
  connect(fetch,&QPushButton::clicked,this,[this] { download(); });
  connect(install,&QPushButton::clicked,this,[this] { beginRestart(); });
  connect(&monitor,&QTimer::timeout,this,[this] { checkRestart(); });
  QFile last(castweaveData()+"/updates/last-update.json");
  if(last.open(QIODevice::ReadOnly)) {
   const auto result=QJsonDocument::fromJson(last.readAll()).object();
   if(result["phase"]=="installed" && result["version"]==installedVersion)
    status->setText("CastWeave "+installedVersion+" was installed successfully.");
   else if(result["phase"]=="failed" || result["phase"]=="restart_failed") status->setText(result["message"].toString());
  }
 }
 void configureRestart(std::function<QString()> blocker,std::function<bool()> close) { restartBlocker=std::move(blocker); closeObs=std::move(close); }
 void offer(const QJsonObject &value) {
  if(active) return;
  asset=value; install->hide(); fetch->setEnabled(!asset.isEmpty());
 }
#ifdef STREAMDOCK_PREVIEW
 bool checkRestartCancellation() {
  QTemporaryDir temp;
  if(!temp.isValid()) return false;
  statePath=temp.path()+"/state.json"; cancelPath=statePath+".cancel";
  QFile state(statePath); if(!state.open(QIODevice::WriteOnly)) return false;
  state.write(R"({"phase":"ready"})"); state.close();
  int closes=0;
  configureRestart([] { return QString("Stop recording before restarting."); },[&] { ++closes; return false; });
  active=true; checkRestart();
  if(closes!=0 || active || !QFile::exists(cancelPath)) return false;
  QFile::remove(cancelPath);
  configureRestart([] { return QString{}; },[&] { ++closes; return false; });
  active=true; closing=false; checkRestart();
  return closes==1 && !active && QFile::exists(cancelPath) && status->text().startsWith("Restart cancelled.");
 }
 bool checkDownload(const QJsonObject &value,bool shouldSucceed) {
  offer(value); download();
  QEventLoop loop; QTimer timeout, observe;
  timeout.setSingleShot(true); timeout.setInterval(60000); observe.setInterval(50);
  connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
  connect(&observe,&QTimer::timeout,&loop,[&] { if(!active) loop.quit(); });
  timeout.start(); observe.start(); loop.exec();
  const bool verified=status->text().startsWith("Update ready.");
  const bool rejected=status->text().startsWith("Download failed or checksum");
  return shouldSucceed ? verified : rejected;
 }
#endif
private:
 void download() {
  const QString name=asset["name"].toString();
  const QUrl url(asset["browser_download_url"].toString());
  expectedHash=asset["digest"].toString().mid(7).toLower();
  if(!QRegularExpression("^CastWeave-[0-9]+[.][0-9]+[.][0-9]+-windows-x64[.]zip$").match(name).hasMatch() ||
     !asset["digest"].toString().startsWith("sha256:") || !QRegularExpression("^[0-9a-f]{64}$").match(expectedHash).hasMatch() ||
     url.scheme()!="https" || url.host()!="github.com" || !url.path().startsWith("/zerithvt-Coder/castweave/releases/download/")) {
   status->setText("Release download metadata is incomplete. No update was downloaded."); return;
  }
  const qint64 declared=asset["size"].toInteger();
  if(declared<=0 || declared>100*1024*1024) { status->setText("Unexpected update size."); return; }
  const QString dir=castweaveData()+"/updates"; QDir().mkpath(dir); zipPath=dir+"/"+name;
  auto *file=new QSaveFile(zipPath,this);
  if(!file->open(QIODevice::WriteOnly)) { file->deleteLater(); status->setText("Cannot create the update download."); return; }
  auto hash=std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
  QNetworkRequest request(url); request.setTransferTimeout(30000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setRawHeader("User-Agent","CastWeave");
  auto *reply=net.get(request); active=true; fetch->setEnabled(false); install->hide();
  status->setText("Downloading update...");
  auto received=std::make_shared<qint64>(0);
  connect(reply,&QNetworkReply::readyRead,this,[reply,file,hash,received,declared] {
   const auto chunk=reply->readAll(); *received+=chunk.size();
   if(*received>declared || file->write(chunk)!=chunk.size()) { reply->abort(); return; }
   hash->addData(chunk);
  });
  connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64 read,qint64 total) {
   if(total>0) status->setText("Downloading update... "+QString::number(100*read/total)+"%");
  });
  connect(reply,&QNetworkReply::finished,this,[this,reply,file,hash,received,declared] {
   active=false; fetch->setEnabled(true);
   const bool valid=reply->error()==QNetworkReply::NoError && *received==declared &&
    QString::fromLatin1(hash->result().toHex())==expectedHash;
   if(valid && file->commit()) {
    status->setText("Update ready. Restart OBS to finish installing. OBS will reopen automatically.");
    install->show(); install->setEnabled(!target.isEmpty() && QFile::exists(script));
    castweaveLog("update_download_verified",{{"file",QFileInfo(zipPath).fileName()}});
   } else {
    file->cancelWriting(); status->setText("Download failed or checksum did not match. Nothing was installed.");
    castweaveLog("update_download_failed");
   }
   reply->deleteLater(); file->deleteLater();
  });
 }
};
