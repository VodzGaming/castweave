#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincred.h>
#endif
// Generic credentials belong to the current Windows user, not a plaintext settings file.
class TwitchCredentials {
 QString target;
public:
 explicit TwitchCredentials(QString name="CastWeave/Twitch/v1"):target(std::move(name)) {}
 QJsonObject read() const {
#ifdef Q_OS_WIN
  PCREDENTIALW entry=nullptr;
  if(!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()),CRED_TYPE_GENERIC,0,&entry)) return {};
  const QByteArray bytes(reinterpret_cast<const char*>(entry->CredentialBlob),int(entry->CredentialBlobSize));
  const auto result=QJsonDocument::fromJson(bytes).object(); CredFree(entry); return result;
#else
  return {};
#endif
 }
 bool write(const QJsonObject &value) const {
#ifdef Q_OS_WIN
  const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact);
  if(bytes.size()>CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;
  CREDENTIALW entry{}; entry.Type=CRED_TYPE_GENERIC;
  entry.TargetName=const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
  entry.CredentialBlobSize=DWORD(bytes.size());
  entry.CredentialBlob=reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
  entry.Persist=CRED_PERSIST_LOCAL_MACHINE;
  return CredWriteW(&entry,0);
#else
  return false;
#endif
 }
 bool remove() const {
#ifdef Q_OS_WIN
  return CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()),CRED_TYPE_GENERIC,0) || GetLastError()==ERROR_NOT_FOUND;
#else
  return true;
#endif
 }
};
