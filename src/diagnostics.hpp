#pragma once
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
inline QString castweaveData() {
 const QString path=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/CastWeave";
 QDir().mkpath(path); return path;
}
inline QString castweaveLogPath() {
 const QString dir=castweaveData()+"/logs"; QDir().mkpath(dir);
#ifdef STREAMDOCK_PREVIEW
 return dir+"/preview.log";
#else
 return dir+"/castweave.log";
#endif
}
// Callers provide only bounded diagnostics, never tokens, auth URLs or response bodies.
inline void castweaveLog(const QString &event,const QJsonObject &details={}) {
 const QString path=castweaveLogPath();
 if(QFileInfo(path).size()>1024*1024) { QFile::remove(path+".1"); QFile::rename(path,path+".1"); }
 QFile out(path); if(!out.open(QIODevice::WriteOnly|QIODevice::Append)) return;
 QJsonObject row=details; row["time"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); row["event"]=event;
 out.write(QJsonDocument(row).toJson(QJsonDocument::Compact)); out.write("\n");
}
