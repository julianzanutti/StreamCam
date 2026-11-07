#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

inline bool verifyDeviceProof(const QByteArray &publicKeyDer, const QByteArray &message, const QByteArray &signature) {
    if (publicKeyDer.size()<256 || publicKeyDer.size()>1024 || signature.size()!=256 || message.size()>2048) return false;
    CERT_PUBLIC_KEY_INFO *info=nullptr; DWORD size=0;
    if(!CryptDecodeObjectEx(X509_ASN_ENCODING,X509_PUBLIC_KEY_INFO,
        reinterpret_cast<const BYTE *>(publicKeyDer.constData()),static_cast<DWORD>(publicKeyDer.size()),
        CRYPT_DECODE_ALLOC_FLAG,nullptr,&info,&size)) return false;
    BCRYPT_KEY_HANDLE key=nullptr;
    const bool imported=CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,info,0,nullptr,&key)!=FALSE;
    LocalFree(info);
    if(!imported)return false;
    auto hash=QCryptographicHash::hash(message,QCryptographicHash::Sha256);
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    const auto status=BCryptVerifySignature(key,&padding,reinterpret_cast<PUCHAR>(hash.data()),static_cast<ULONG>(hash.size()),
        reinterpret_cast<PUCHAR>(const_cast<char *>(signature.constData())),static_cast<ULONG>(signature.size()),BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(key);
    return status==0;
}
