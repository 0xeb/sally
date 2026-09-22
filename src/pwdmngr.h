// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

class CPasswordManager;

//****************************************************************************
//
// CChangeMasterPassword
//

class CChangeMasterPassword : public CCommonDialog
{
private:
    CPasswordManager* PwdManager;

public:
    CChangeMasterPassword(HWND hParent, CPasswordManager* pwdManager);

protected:
    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

    void EnableControls();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CEnterMasterPassword
//

class CEnterMasterPassword : public CCommonDialog
{
private:
    CPasswordManager* PwdManager;

public:
    CEnterMasterPassword(HWND hParent, CPasswordManager* pwdManager);

protected:
    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CRemoveMasterPassword
//

class CRemoveMasterPassword : public CCommonDialog
{
private:
    CPasswordManager* PwdManager;

public:
    CRemoveMasterPassword(HWND hParent, CPasswordManager* pwdManager);

protected:
    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CPasswordManager
//
// Password storage. When the user enables the "Use Master Password" option,
// configuration passwords are stored and encrypted with AES; otherwise they are only
// scrambled using Petr's original FTP client method.
//
// Password manager methods may be called only from Salamander's main thread.
// Planned access points are: FTP connect, WinSCP connect, the
// Salamander configuration and saving/loading Salamander’s configuration. Because all of them currently run in the
// main thread, so we don’t need to handle concurrency or locking of the manager.

#pragma pack(push)
#pragma pack(1)
struct CMasterPasswordVerifier
{
    BYTE Salt[16];  // random salt, mode == 3
    BYTE Dummy[16]; // random encrypted data
    BYTE MAC[10];   // verification record used to check the correctness of the master password
};

#define PWDMNGR_MASTER_VERIFIER_UTF8_MAGIC 0x3256504Du /* MPV2 */
#define PWDMNGR_MASTER_VERIFIER_UTF8_VERSION 2u

struct CMasterPasswordVerifierUtf8
{
    DWORD Magic;
    DWORD Version;
    BYTE Salt[16];
    BYTE Dummy[16];
    BYTE MAC[10];
};
#pragma pack(pop)

static_assert(sizeof(CMasterPasswordVerifier) == 42,
              "legacy master-password verifier layout drift");
static_assert(sizeof(CMasterPasswordVerifierUtf8) == 50,
              "UTF-8 master-password verifier layout drift");

class CPasswordManager
{
private:
    BOOL UseMasterPassword;                          // the user has (at some point) provided the master password used for data encryption; the plaintext value may later be NULL and must be requested again
    std::wstring PlainMasterPassword;    // plaintext session owner; never persisted
    std::wstring OldPlainMasterPassword; // previous password during plug-in re-encryption callbacks
    std::vector<BYTE> MasterPasswordVerifier; // frozen legacy or versioned UTF-8 record
    BOOL MasterPasswordVerifierIsLegacy;

    CSalamanderCryptAbstract* SalamanderCrypt; // interface for the work with cryptographic library

public:
    CPasswordManager();
    ~CPasswordManager();

    BOOL IsPasswordSecure(const wchar_t* password); // returns TRUE when the password meets strength requirements, otherwise FALSE

    // sets the master password; if 'password' is NULL or an empty string, master password is turned off
    BOOL SetMasterPassword(HWND hParent, const wchar_t* password);

    // used to provide the master password when it is not currently known in plaintext form
    BOOL EnterMasterPassword(const wchar_t* password);

    BOOL ChangeMasterPassword(HWND hParent);
    BOOL IsUsingMasterPassword() { return UseMasterPassword; }         // are the passwords protected using AES/Master Password?
    BOOL IsMasterPasswordSet() { return !PlainMasterPassword.empty(); } // has the user entered the Master Password in this session?

    // when master password usage is enabled and the password has not been entered
    // in this session, it displays a dialog for entering it
    // returns FALSE if the correct master password cannot be entered in that situation; returns TRUE otherwise
    // this method needs to be called before calling the EncryptPassword/DecryptPassword methods, when it is encrypt/encrypted == TRUE
    // callers may invoke it even when master password usage is turned off (quietly returns TRUE)
    BOOL AskForMasterPassword(HWND hParent);

    void NotifyAboutMasterPasswordChange(HWND hParent);

    BOOL Save(HKEY hKey); // saves stored passwords to the Registry
    BOOL Load(HKEY hKey); // loads data from the registry

    // encrypts the plaintext password into the binary form using strong encryption (AES)
    // before AES encryption, it performs an additional scramble that adds padding (hardening short passwords)
    // if the caller requires AES password encryption ('encrypt' == TRUE), before calling the method he must call AskForMasterPassword() that must return TRUE
    // 'plainPassword' is the pointer to the zero-terminated password in text form
    // 'encryptedPassword' returns a pointer to a binary buffer allocated by Salamander with the encrypted password; this buffer must be deallocated using CSalamanderGeneralAbstract::Free
    // 'encryptedPasswordSize' returns the size of the 'encryptedPassword' buffer in bytes
    // if 'encrypt' is TRUE, the function encrypts the password using AES (safe, protected by the master password); if FALSE, the password is only scrambled
    BOOL EncryptPassword(const wchar_t* plainPassword, BYTE** encryptedPassword, int* encryptedPasswordSize, BOOL encrypt);
    // If 'plainPassword' is NULL, only checks whether the password can be decrypted.
    // The returned value is dynamically owned UTF-16 and changes only on success.
    BOOL DecryptPassword(const BYTE* encryptedPassword, int encryptedPasswordSize, std::wstring* plainPassword);
    // returns TRUE for an AES-encrypted password, otherwise returns FALSE; the signature in the first byte of the password determines it
    BOOL IsPasswordEncrypted(const BYTE* encyptedPassword, int encyptedPasswordSize);

    // adds a new password to the Passwords array; returns TRUE if successful (also fills 'passwordID' with a value greater than zero and less than 0xffffffff), otherwise FALSE
    // 'pluginDLLName' must be NULL if the password belongs to the Salamander's core, otherwise it is filled by CPluginData
    // 'password' is the password in plain form
    //BOOL StorePassword(const char *pluginDLLName, const char *password, DWORD *passwordID); // the call must be preceded by a successful AskForMasterPassword()
    //BOOL SetPassword(const char *pluginDLLName, DWORD passwordID, const char *password); // the call must be preceded by a successful AskForMasterPassword()
    //BOOL GetPassword(const char *pluginDLLName, DWORD passwordID, char *password, int bufferLen); // the call must be preceded by a successful AskForMasterPassword()
    //BOOL DeletePassword(const char *pluginDLLName, DWORD passwordID);

    // verifies that 'password' matches the password stored in MasterPasswordVerifier; returns TRUE on success, otherwise FALSE
    BOOL VerifyMasterPassword(const wchar_t* password);

protected:
    BOOL CreateMasterPasswordVerifier(const wchar_t* password,
                                      std::vector<BYTE>& verifier);
};

extern CPasswordManager PasswordManager;
