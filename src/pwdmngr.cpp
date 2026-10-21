// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <memory>
#include <time.h>
#include <stdexcept>

#include "cfgdlg.h"
#include "ui/IPrompter.h"
#include "pwdmngr.h"
#include "plugins.h"
#include "spl_crypt.h"
#include "common/Win32TextCodec.h"
#include "common/IRegistry.h"
#include "common/ISecureRandom.h"
#include "common/security/ICredentialChangeHost.h"

const wchar_t* SALAMANDER_PWDMNGR_USEMASTERPWD = L"Use Master Password";
const wchar_t* SALAMANDER_PWDMNGR_MASTERPWD_VERIFIER = L"Master Password Verifier";

using namespace sally::security;

struct CPasswordPersistence
{
    struct CachedPlaintext
    {
        CredentialBytes Text;
        std::vector<CipherFingerprint> Aliases;
    };
    ICredentialChangeHost& Host;
    CredentialRewriteJournal Journal;
    bool Active = false;
    bool Failed = false;
    bool BeforeProtected = false;
    std::vector<CachedPlaintext> Cache;
    explicit CPasswordPersistence(ICredentialChangeHost& host) : Host(host), Journal(host) {}
    CachedPlaintext& CacheFor(const wchar_t* text)
    {
        const auto* bytes = reinterpret_cast<const BYTE*>(text);
        const size_t size = wcslen(text) * sizeof(wchar_t);
        for (auto& entry : Cache)
            if (entry.Text.size() == size && (size == 0 || memcmp(entry.Text.data(), bytes, size) == 0))
                return entry;
        Cache.push_back({CredentialBytes(bytes, bytes + size), {}});
        return Cache.back();
    }
};

namespace
{
    class CPasswordProfileLock
    {
        ICredentialChangeHost* Host;
        bool Held = false;

    public:
        explicit CPasswordProfileLock(CPasswordPersistence* persistence)
            : Host(persistence != nullptr ? &persistence->Host : nullptr)
        {
            if (Host == nullptr)
                Held = true;
            else
            {
                const auto result = Host->Acquire();
                Held = result.success;
                if (!Held)
                    SetLastError(result.errorCode);
            }
        }
        ~CPasswordProfileLock()
        {
            if (Held && Host != nullptr)
                Host->Release();
        }
        explicit operator bool() const { return Held; }
    };
    bool PasswordResult(const RegistryResult& result)
    {
        if (!result.success)
            SetLastError(result.errorCode);
        return result.success;
    }
} // namespace

CPasswordManager PasswordManager(nullptr, GetCredentialChangeHost());

CSalamanderCryptAbstract* GetSalamanderCrypt();

/*  AES modes and parameter sizes
    Field lengths (in bytes) versus File Encryption Mode (0 < mode < 4)

    Mode KeyLen SaltLen  MACLen Overhead
       1     16       8      10       18
       2     24      12      10       22
       3     32      16      10       26

   The following macros assume that the mode value is correct.
*/
#define PASSWORD_MANAGER_AES_MODE 3 // DO NOT CHANGE; for example, CMasterPasswordVerifier is declared "hardcoded"

struct CSecureByteArrayDeleter
{
    CSecureByteArrayDeleter(int size)
        : Size(size)
    {
    }

    void operator()(BYTE* buffer) const
    {
        if (buffer != NULL)
        {
            SecureZeroMemory(buffer, Size);
            delete[] buffer;
        }
    }

    int Size;
};

//****************************************************************************
//
// ScramblePassword / UnscramblePassword
//
// Taken from the FTP plugin. Used in case the user does not set the master
// password and strong AES encryption is therefore not used.
//

unsigned char ScrambleTable[256] =
    {
        0, 223, 235, 233, 240, 185, 88, 102, 22, 130, 27, 53, 79, 125, 66, 201,
        90, 71, 51, 60, 134, 104, 172, 244, 139, 84, 91, 12, 123, 155, 237, 151,
        192, 6, 87, 32, 211, 38, 149, 75, 164, 145, 52, 200, 224, 226, 156, 50,
        136, 190, 232, 63, 129, 209, 181, 120, 28, 99, 168, 94, 198, 40, 238, 112,
        55, 217, 124, 62, 227, 30, 36, 242, 208, 138, 174, 231, 26, 54, 214, 148,
        37, 157, 19, 137, 187, 111, 228, 39, 110, 17, 197, 229, 118, 246, 153, 80,
        21, 128, 69, 117, 234, 35, 58, 67, 92, 7, 132, 189, 5, 103, 10, 15,
        252, 195, 70, 147, 241, 202, 107, 49, 20, 251, 133, 76, 204, 73, 203, 135,
        184, 78, 194, 183, 1, 121, 109, 11, 143, 144, 171, 161, 48, 205, 245, 46,
        31, 72, 169, 131, 239, 160, 25, 207, 218, 146, 43, 140, 127, 255, 81, 98,
        42, 115, 173, 142, 114, 13, 2, 219, 57, 56, 24, 126, 3, 230, 47, 215,
        9, 44, 159, 33, 249, 18, 93, 95, 29, 113, 220, 89, 97, 182, 248, 64,
        68, 34, 4, 82, 74, 196, 213, 165, 179, 250, 108, 254, 59, 14, 236, 175,
        85, 199, 83, 106, 77, 178, 167, 225, 45, 247, 163, 158, 8, 221, 61, 191,
        119, 16, 253, 105, 186, 23, 170, 100, 216, 65, 162, 122, 150, 176, 154, 193,
        206, 222, 188, 152, 210, 243, 96, 41, 86, 180, 101, 177, 166, 141, 212, 116};

BOOL InitUnscrambleTable = TRUE;
BOOL InitSRand = TRUE;
unsigned char UnscrambleTable[256];

#define SCRAMBLE_LENGTH_EXTENSION 50 // number of characters by which we must extend the buffer to fit the scramble

void ScramblePassword(char* password)
{
    // padding + length ones digit + length tens digit + length hundreds digit + password
    int len = (int)strlen(password);
    char* buf = (char*)malloc(len + SCRAMBLE_LENGTH_EXTENSION);
    if (InitSRand)
    {
        srand((unsigned)time(NULL));
        InitSRand = FALSE;
    }
    int padding = (((len + 3) / 17) * 17 + 17) - 3 - len;
    int i;
    for (i = 0; i < padding; i++)
    {
        int p = 0;
        while (p <= 0 || p > 255 || p >= '0' && p <= '9')
            p = (int)((double)rand() / ((double)RAND_MAX / 256.0));
        buf[i] = (unsigned char)p;
    }
    buf[padding] = '0' + (len % 10);
    buf[padding + 1] = '0' + ((len / 10) % 10);
    buf[padding + 2] = '0' + ((len / 100) % 10);
    strcpy(buf + padding + 3, password);
    char* s = buf;
    int last = 31;
    while (*s != 0)
    {
        last = (last + (unsigned char)*s) % 255 + 1;
        *s = ScrambleTable[last];
        s++;
    }
    strcpy(password, buf);
    memset(buf, 0, len + SCRAMBLE_LENGTH_EXTENSION); // wipe the memory that contained the password
    free(buf);
}

BOOL UnscramblePassword(char* password)
{
    if (InitUnscrambleTable)
    {
        int i;
        for (i = 0; i < 256; i++)
        {
            UnscrambleTable[ScrambleTable[i]] = i;
        }
        InitUnscrambleTable = FALSE;
    }

    std::string backup = password; // backup for TRACE_E

    char* s = password;
    int last = 31;
    while (*s != 0)
    {
        int x = (int)UnscrambleTable[(unsigned char)*s] - 1 - (last % 255);
        if (x <= 0)
            x += 255;
        *s = (char)x;
        last = (last + x) % 255 + 1;
        s++;
    }

    s = password;
    while (*s != 0 && (*s < '0' || *s > '9'))
        s++; // find the length of the password
    BOOL ok = FALSE;
    if (strlen(s) >= 3)
    {
        int len = (s[0] - '0') + 10 * (s[1] - '0') + 100 * (s[2] - '0');
        int total = (((len + 3) / 17) * 17 + 17);
        int passwordLen = (int)strlen(password);
        if (len >= 0 && total == passwordLen && total - (s - password) - 3 == len)
        {
            memmove(password, password + passwordLen - len, len + 1);
            ok = TRUE;
        }
    }
    if (!ok)
    {
        password[0] = 0; // some error occured; clear the password
        TRACE_E("Unable to unscramble password! scrambled=" << backup.c_str());
    }
    memset(&backup[0], 0, backup.size()); // wipe the memory that contained the password
    return ok;
}

namespace
{
constexpr size_t UTF8_SCRAMBLE_LENGTH_DIGITS = 10;
// Password-manager signatures 1 and 2 persist both the payload and AES key material in the
// process ACP. This is a frozen cryptographic format choice, not a semantic text owner.
constexpr UINT PASSWORD_MANAGER_LEGACY_CODE_PAGE = CP_ACP;

void SecureClear(std::string& value)
{
    if (!value.empty())
        SecureZeroMemory(value.data(), value.size());
    value.clear();
}

void SecureClear(std::wstring& value)
{
    if (!value.empty())
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

void SecureClear(std::vector<BYTE>& value)
{
    if (!value.empty())
        SecureZeroMemory(value.data(), value.size());
    value.clear();
}

bool EncodeMasterPassword(const wchar_t* password, size_t passwordLength,
                          UINT codePage, std::string& encoded)
{
    std::string staged;
    const Win32TextConversionResult conversion = Win32EncodeText(
        codePage, password, passwordLength, staged);
    if (!conversion || staged.size() > SAL_AES_MAX_PWD_LENGTH)
    {
        if (!conversion)
            SetLastError(conversion.Win32Error);
        else
            SetLastError(ERROR_BUFFER_OVERFLOW);
        SecureClear(staged);
        return false;
    }
    encoded.swap(staged);
    SecureClear(staged);
    return true;
}

bool EncodeMasterPassword(const std::wstring& password, UINT codePage,
                          std::string& encoded)
{
    return EncodeMasterPassword(password.data(), password.size(), codePage,
                                encoded);
}

bool ScrambleUtf8Password(std::string& password)
{
    const size_t length = password.size();
    if (length > static_cast<size_t>((std::numeric_limits<int>::max)()) -
                     UTF8_SCRAMBLE_LENGTH_DIGITS - 17)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return false;
    }

    const size_t contentLength = length + UTF8_SCRAMBLE_LENGTH_DIGITS;
    const size_t totalLength = (contentLength / 17) * 17 + 17;
    const size_t padding = totalLength - contentLength;
    std::string scrambled(totalLength, '\0');

    if (InitSRand)
    {
        srand((unsigned)time(NULL));
        InitSRand = FALSE;
    }
    for (size_t i = 0; i < padding; ++i)
    {
        int value = 0;
        while (value <= 0 || value > 255 || value >= '0' && value <= '9')
            value = (int)((double)rand() / ((double)RAND_MAX / 256.0));
        scrambled[i] = static_cast<char>(value);
    }

    size_t remaining = length;
    for (size_t i = 0; i < UTF8_SCRAMBLE_LENGTH_DIGITS; ++i)
    {
        scrambled[padding + i] = static_cast<char>('0' + remaining % 10);
        remaining /= 10;
    }
    memcpy(scrambled.data() + padding + UTF8_SCRAMBLE_LENGTH_DIGITS,
           password.data(), length);

    int last = 31;
    for (char& value : scrambled)
    {
        last = (last + static_cast<unsigned char>(value)) % 255 + 1;
        value = static_cast<char>(ScrambleTable[last]);
    }

    password.swap(scrambled);
    SecureClear(scrambled);
    return true;
}

bool UnscrambleUtf8Password(const BYTE* scrambledBytes, size_t scrambledLength,
                            std::string& password)
{
    if (InitUnscrambleTable)
    {
        for (int i = 0; i < 256; ++i)
            UnscrambleTable[ScrambleTable[i]] = static_cast<unsigned char>(i);
        InitUnscrambleTable = FALSE;
    }

    std::string decoded(reinterpret_cast<const char*>(scrambledBytes), scrambledLength);
    int last = 31;
    for (char& value : decoded)
    {
        int original = static_cast<int>(UnscrambleTable[static_cast<unsigned char>(value)]) -
                       1 - (last % 255);
        if (original <= 0)
            original += 255;
        value = static_cast<char>(original);
        last = (last + original) % 255 + 1;
    }

    size_t lengthOffset = 0;
    while (lengthOffset < decoded.size() &&
           (decoded[lengthOffset] < '0' || decoded[lengthOffset] > '9'))
        ++lengthOffset;

    bool valid = decoded.size() - lengthOffset >= UTF8_SCRAMBLE_LENGTH_DIGITS;
    size_t length = 0;
    size_t multiplier = 1;
    for (size_t i = 0; valid && i < UTF8_SCRAMBLE_LENGTH_DIGITS; ++i)
    {
        const char digit = decoded[lengthOffset + i];
        if (digit < '0' || digit > '9')
        {
            valid = false;
            break;
        }
        const size_t component = static_cast<size_t>(digit - '0') * multiplier;
        if (length > (std::numeric_limits<size_t>::max)() - component)
        {
            valid = false;
            break;
        }
        length += component;
        if (i + 1 < UTF8_SCRAMBLE_LENGTH_DIGITS)
            multiplier *= 10;
    }

    const size_t payloadOffset = lengthOffset + UTF8_SCRAMBLE_LENGTH_DIGITS;
    if (!valid || payloadOffset > decoded.size() || length != decoded.size() - payloadOffset)
    {
        SecureClear(decoded);
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }

    std::string staged(decoded.data() + payloadOffset, length);
    password.swap(staged);
    SecureClear(staged);
    SecureClear(decoded);
    return true;
}
} // namespace

static BOOL GetDlgItemPasswordW(HWND hWindow, int ctrlID,
                                std::wstring& password)
{
    SecureClear(password);
    const HWND edit = GetDlgItem(hWindow, ctrlID);
    if (edit == NULL)
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    const int length = GetWindowTextLengthW(edit);
    if ((length == 0 && GetLastError() != ERROR_SUCCESS) ||
        length < 0 || length > SAL_AES_MAX_PWD_LENGTH)
        return FALSE;

    try
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
        SetLastError(ERROR_SUCCESS);
        const int copied = GetWindowTextW(edit, buffer.data(), static_cast<int>(buffer.size()));
        if ((copied == 0 && GetLastError() != ERROR_SUCCESS) || copied != length)
        {
            SecureZeroMemory(buffer.data(), buffer.size() * sizeof(wchar_t));
            return FALSE;
        }
        std::wstring staged(buffer.data(), static_cast<size_t>(copied));
        SecureZeroMemory(buffer.data(), buffer.size() * sizeof(wchar_t));
        password.swap(staged);
        SecureClear(staged);
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

//****************************************************************************
//
// CChangeMasterPassword
//

CChangeMasterPassword::CChangeMasterPassword(HWND hParent, CPasswordManager* pwdManager)
    : CCommonDialog(HLanguage, IDD_CHANGE_MASTERPWD, IDD_CHANGE_MASTERPWD, hParent)
{
    PwdManager = pwdManager;
}

void CChangeMasterPassword::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CChangeMasterPassword::Validate()");
    HWND hWnd;

    // if master password usage is enabled, we must verify that the user entered it correctly
    if (PwdManager->IsUsingMasterPassword() && ti.GetControl(hWnd, IDC_CHMP_CURRENTPWD))
    {
        std::wstring curPwd;
        const BOOL valid = GetDlgItemPasswordW(
                               HWindow, IDC_CHMP_CURRENTPWD, curPwd) &&
                           PwdManager->VerifyMasterPassword(curPwd.c_str());
        SecureClear(curPwd);
        if (!valid)
        {
            gPrompter->ShowError(LoadStrW(IDS_WARNINGTITLE), LoadStrW(IDS_WRONG_MASTERPASSWORD));
            SetDlgItemTextW(HWindow, IDC_CHMP_CURRENTPWD, L"");
            ti.ErrorOn(IDC_CHMP_CURRENTPWD);
            return;
        }
    }

    if (ti.GetControl(hWnd, IDC_CHMP_NEWPWD))
    {
        std::wstring newPwd;
        if (!GetDlgItemPasswordW(HWindow, IDC_CHMP_NEWPWD, newPwd))
        {
            ti.ErrorOn(IDC_CHMP_NEWPWD);
            return;
        }
        std::wstring confirmation;
        const BOOL confirmed = GetDlgItemPasswordW(HWindow, IDC_CHMP_RETYPEPWD, confirmation) && newPwd == confirmation;
        SecureClear(confirmation);
        if (!confirmed)
        {
            SecureClear(newPwd);
            ti.ErrorOn(IDC_CHMP_RETYPEPWD);
            return;
        }
        if (newPwd.empty() && !PwdManager->IsUsingMasterPassword())
        {
            ti.ErrorOn(IDC_CHMP_NEWPWD);
            return;
        }
        if (!newPwd.empty() && !PwdManager->IsPasswordSecure(newPwd.c_str()))
        {
            if (gPrompter->AskYesNo(LoadStrW(IDS_WARNINGTITLE), LoadStrW(IDS_INSECUREPASSWORD)).type == PromptResult::kNo)
            {
                SecureClear(newPwd);
                ti.ErrorOn(IDC_CHMP_NEWPWD);
                return;
            }
        }
        std::string encodedPassword;
        if (!newPwd.empty() &&
            !EncodeMasterPassword(newPwd, CP_UTF8, encodedPassword))
        {
            SecureClear(encodedPassword);
            SecureClear(newPwd);
            gPrompter->ShowError(LoadStrW(IDS_WARNINGTITLE),
                                 LoadStrW(IDS_MASTERPASSWORD_TOO_LONG));
            ti.ErrorOn(IDC_CHMP_NEWPWD);
            return;
        }
        SecureClear(encodedPassword);
        SecureClear(newPwd);
    }
}

void CChangeMasterPassword::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        // limit the password length; see the AES library limitations
        SendDlgItemMessage(HWindow, IDC_CHMP_CURRENTPWD, EM_LIMITTEXT, SAL_AES_MAX_PWD_LENGTH, 0);
        SendDlgItemMessage(HWindow, IDC_CHMP_NEWPWD, EM_LIMITTEXT, SAL_AES_MAX_PWD_LENGTH, 0);
        SendDlgItemMessage(HWindow, IDC_CHMP_RETYPEPWD, EM_LIMITTEXT, SAL_AES_MAX_PWD_LENGTH, 0);

        if (!PwdManager->IsUsingMasterPassword())
        {
            // remove the ES_PASSWORD style from the current password field so we can display the "not set" text
            HWND hEdit = GetDlgItem(HWindow, IDC_CHMP_CURRENTPWD);
            SendMessage(hEdit, EM_SETPASSWORDCHAR, 0, 0);
            SetWindowTextW(hEdit, LoadStrW(IDS_MASTERPASSWORD_NOTSET));
            EnableWindow(hEdit, FALSE);
        }

        EnableControls();
    }
    else
    {
        if (PwdManager->IsUsingMasterPassword())
        {
            std::wstring oldPwd;
            const BOOL entered = GetDlgItemPasswordW(HWindow, IDC_CHMP_CURRENTPWD, oldPwd) &&
                                 PwdManager->EnterMasterPassword(oldPwd.c_str());
            SecureClear(oldPwd);
            if (!entered)
            {
                ti.ErrorOn(IDC_CHMP_CURRENTPWD);
                return;
            }
        }

        std::wstring newPwd;
        if (!GetDlgItemPasswordW(HWindow, IDC_CHMP_NEWPWD, newPwd))
        {
            SecureClear(newPwd);
            ti.ErrorOn(IDC_CHMP_NEWPWD);
            return;
        }
        if (!PwdManager->SetMasterPassword(HWindow, newPwd.c_str()))
        {
            const DWORD error = GetLastError();
            ti.ErrorOn(IDC_CHMP_NEWPWD);
            gPrompter->ShowError(LoadStrW(IDS_WARNINGTITLE), GetErrorTextOwned(error).c_str());
        }
        SecureClear(newPwd);
    }
}

void CChangeMasterPassword::EnableControls()
{
    std::wstring newPwd;
    std::wstring retypedPwd;
    const BOOL read = GetDlgItemPasswordW(HWindow, IDC_CHMP_NEWPWD, newPwd) &&
                      GetDlgItemPasswordW(HWindow, IDC_CHMP_RETYPEPWD, retypedPwd);
    BOOL enableOK = read && newPwd == retypedPwd;
    if (enableOK && !PwdManager->IsUsingMasterPassword() && newPwd.empty())
        enableOK = FALSE;
    EnableWindow(GetDlgItem(HWindow, IDOK), enableOK);
    SecureClear(newPwd);
    SecureClear(retypedPwd);
}

INT_PTR
CChangeMasterPassword::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CChangeMasterPassword::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == EN_CHANGE && (LOWORD(wParam) == IDC_CHMP_NEWPWD || LOWORD(wParam) == IDC_CHMP_RETYPEPWD))
        {
            // the new (and confirmation) passwords must match; otherwise, disable the OK button
            EnableControls();
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CEnterMasterPassword
//

CEnterMasterPassword::CEnterMasterPassword(HWND hParent, CPasswordManager* pwdManager)
    : CCommonDialog(HLanguage, IDD_ENTER_MASTERPWD, IDD_ENTER_MASTERPWD, hParent)
{
    PwdManager = pwdManager;
}

void CEnterMasterPassword::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CEnterMasterPassword::Validate()");
    HWND hWnd;

    if (ti.GetControl(hWnd, IDC_MPR_PASSWORD))
    {
        std::wstring curPwd;
        const BOOL valid = GetDlgItemPasswordW(HWindow, IDC_MPR_PASSWORD,
                                               curPwd) &&
                           PwdManager->VerifyMasterPassword(curPwd.c_str());
        SecureClear(curPwd);
        if (!valid)
        {
            gPrompter->ShowError(LoadStrW(IDS_WARNINGTITLE), LoadStrW(IDS_WRONG_MASTERPASSWORD));
            SetDlgItemTextW(HWindow, IDC_MPR_PASSWORD, L"");
            ti.ErrorOn(IDC_MPR_PASSWORD);
            return;
        }
    }
}

void CEnterMasterPassword::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataFromWindow)
    {
        std::wstring plainMasterPassword;
        GetDlgItemPasswordW(HWindow, IDC_MPR_PASSWORD, plainMasterPassword);
        PwdManager->EnterMasterPassword(plainMasterPassword.c_str());
        SecureClear(plainMasterPassword);
    }
}

INT_PTR
CEnterMasterPassword::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CEnterMasterPassword::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CRemoveMasterPassword
//

CRemoveMasterPassword::CRemoveMasterPassword(HWND hParent, CPasswordManager* pwdManager)
    : CCommonDialog(HLanguage, IDD_REMOVE_MASTERPWD, IDD_REMOVE_MASTERPWD, hParent)
{
    PwdManager = pwdManager;
}

void CRemoveMasterPassword::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CRemoveMasterPassword::Validate()");
    HWND hWnd;

    if (ti.GetControl(hWnd, IDC_RMP_CURRENTPWD))
    {
        std::wstring curPwd;
        const BOOL valid = GetDlgItemPasswordW(HWindow, IDC_RMP_CURRENTPWD,
                                               curPwd) &&
                           PwdManager->VerifyMasterPassword(curPwd.c_str());
        SecureClear(curPwd);
        if (!valid)
        {
            gPrompter->ShowError(LoadStrW(IDS_WARNINGTITLE), LoadStrW(IDS_WRONG_MASTERPASSWORD));
            SetDlgItemTextW(HWindow, IDC_RMP_CURRENTPWD, L"");
            ti.ErrorOn(IDC_RMP_CURRENTPWD);
            return;
        }
    }
}

void CRemoveMasterPassword::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataFromWindow)
    {
        std::wstring plainMasterPassword;
        GetDlgItemPasswordW(HWindow, IDC_RMP_CURRENTPWD, plainMasterPassword);
        // pass the password to the password manager; plugins need it for the pending event
        PwdManager->EnterMasterPassword(plainMasterPassword.c_str());
        SecureClear(plainMasterPassword);
    }
}

INT_PTR
CRemoveMasterPassword::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CRemoveMasterPassword::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CCfgPageSecurity
//

CCfgPageSecurity::CCfgPageSecurity()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_SECURITY, IDD_CFGPAGE_SECURITY, PSP_USETITLE, NULL)
{
}

void CCfgPageSecurity::Transfer(CTransferInfo& ti)
{
}

void CCfgPageSecurity::EnableControls()
{
    BOOL useMasterPwd = IsDlgButtonChecked(HWindow, IDC_SEC_ENABLE_MASTERPWD);
    EnableWindow(GetDlgItem(HWindow, IDC_SEC_ENABLE_MASTERPWD), PasswordManager.IsPersistentStateValid());
    EnableWindow(GetDlgItem(HWindow, IDC_SEC_CHANGE_MASTERPWD), useMasterPwd && PasswordManager.IsPersistentStateValid());
    if (!PasswordManager.IsPersistentStateValid())
        SetDlgItemTextW(HWindow, IDC_STATIC_2, LoadStrW(IDS_MASTERPASSWORD_STORE_DAMAGED));
}

INT_PTR
CCfgPageSecurity::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageSecurity::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SetDlgItemTextW(HWindow, IDC_STATIC_2, LoadStrW(IDS_CFGSEC_MASTERPWD_INFO));
        // bypass Transfer(); this is a special handling of a checkbox
        CheckDlgButton(HWindow, IDC_SEC_ENABLE_MASTERPWD, PasswordManager.IsUsingMasterPassword() ? BST_CHECKED : BST_UNCHECKED);

        EnableControls();
        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_SEC_ENABLE_MASTERPWD)
        {
            // checkbox was clicked
            EnableControls();

            // if the user checked the "Use Master Password" option, display the change password dialog
            BOOL useMasterPwd = IsDlgButtonChecked(HWindow, IDC_SEC_ENABLE_MASTERPWD);
            if (useMasterPwd)
            {
                // the user enabled the option
                CChangeMasterPassword dlg(HWindow, &PasswordManager);
                if (dlg.Execute() == IDOK)
                {
                    PasswordManager.NotifyAboutMasterPasswordChange(HWindow);
                }
                else
                {
                    // if the user selected Cancel, turn off the option that was just being enabled
                    CheckDlgButton(HWindow, IDC_SEC_ENABLE_MASTERPWD, BST_UNCHECKED);
                }
            }
            else
            {
                // the user disabled the option
                CRemoveMasterPassword dlg(HWindow, &PasswordManager);
                if (dlg.Execute() == IDOK)
                {
                    if (PasswordManager.SetMasterPassword(HWindow, NULL))
                        PasswordManager.NotifyAboutMasterPasswordChange(HWindow);
                    else
                    {
                        const DWORD error = GetLastError();
                        CheckDlgButton(HWindow, IDC_SEC_ENABLE_MASTERPWD, BST_CHECKED);
                        gPrompter->ShowError(LoadStrW(IDS_ERROR), GetErrorTextOwned(error).c_str());
                    }
                }
                else
                {
                    // if the user cancels, restore the option that was being disabled
                    CheckDlgButton(HWindow, IDC_SEC_ENABLE_MASTERPWD, BST_CHECKED);
                }
            }
            EnableControls(); // CheckDlgButton() does not send notifications, so we must call it manually
        }

        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_SEC_CHANGE_MASTERPWD)
        {
            CChangeMasterPassword dlg(HWindow, &PasswordManager);
            // if the user reset the password, uncheck the checkbox
            if (dlg.Execute() == IDOK)
            {
                if (!PasswordManager.IsUsingMasterPassword())
                {
                    CheckDlgButton(HWindow, IDC_SEC_ENABLE_MASTERPWD, BST_UNCHECKED);
                    SetFocus(GetDlgItem(HWindow, IDC_SEC_ENABLE_MASTERPWD)); // focus must move away from the button we are about to disable
                    EnableControls();                                        // CheckDlgButton() does not send notifications, so we must call it manually
                }
                PasswordManager.NotifyAboutMasterPasswordChange(HWindow);
            }
        }
        break;
    }
    }

    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CPasswordManager
//

// signature values for passwords stored in the binary form
#define PWDMNGR_SIGNATURE_SCRAMBLED 1      // legacy ACP payload, scrambled only
#define PWDMNGR_SIGNATURE_ENCRYPTED 2      // legacy ACP payload, scrambled and AES encrypted
#define PWDMNGR_SIGNATURE_UTF8_SCRAMBLED 3 // UTF-8 payload, dynamically scrambled only
#define PWDMNGR_SIGNATURE_UTF8_ENCRYPTED 4 // UTF-8 payload, dynamically scrambled and AES encrypted

CPasswordManager::CPasswordManager(ISecureRandom* random, ICredentialChangeHost* persistence)
    : SecureRandom(random != nullptr ? random : GetWin32SecureRandom())
{
    UseMasterPassword = FALSE;
    MasterPasswordVerifierIsLegacy = FALSE;

    SalamanderCrypt = GetSalamanderCrypt();
    if (persistence != nullptr) Persistence.reset(new CPasswordPersistence(*persistence));
}

CPasswordManager::~CPasswordManager()
{
    SecureClear(PlainMasterPassword);
    SecureClear(OldPlainMasterPassword);
    SecureClear(MasterPasswordVerifier);
}

BOOL CPasswordManager::IsPasswordSecure(const wchar_t* password)
{
    int l = (int)wcslen(password);
    int a = 0, b = 0, c = 0, d = 0;

    while (*password)
    {
        if (*password >= L'a' && *password <= L'z')
            a = 1;
        else if (*password >= L'A' && *password <= L'Z')
            b = 1;
        else if (*password >= L'0' && *password <= L'9')
            c = 1;
        else
            d = 1;

        password++;
    }
    return l >= 6 && (a + b + c + d) >= 2;
}

BOOL CPasswordManager::EnsureCredentialJournal()
{
    try
    {
        if (!Persistence)
            return TRUE;
        CredentialAuthority authority;
        authority.Protected = UseMasterPassword != FALSE;
        authority.Verifier.assign(MasterPasswordVerifier.begin(), MasterPasswordVerifier.end());
        if (!Persistence->Journal.IsLoaded() && !PasswordResult(Persistence->Journal.Load(authority)))
            return FALSE;
        if (!(Persistence->Journal.Authority() == authority))
        {
            SetLastError(ERROR_REVISION_MISMATCH);
            return FALSE;
        }
        return PasswordResult(Persistence->Journal.CheckRevision());
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
}

BOOL CPasswordManager::CheckCredentialRevision()
{
    CPasswordProfileLock lock(Persistence.get());
    return lock && PersistentStateValid && EnsureCredentialJournal();
}

BOOL CPasswordManager::EncryptPassword(const wchar_t* plainPassword, BYTE** encryptedPassword, int* encryptedPasswordSize, BOOL encrypt)
{
    if (encryptedPassword != nullptr)
        *encryptedPassword = nullptr;
    if (encryptedPasswordSize != nullptr)
        *encryptedPasswordSize = 0;
    CPasswordProfileLock lock(Persistence.get());
    auto fail = [&]() -> BOOL
    {
        if (Persistence && Persistence->Active)
            Persistence->Failed = true;
        return FALSE;
    };
    if (!lock || !PersistentStateValid || !EnsureCredentialJournal())
        return fail();
    BYTE* result = nullptr;
    int size = 0;
    if (encryptedPassword == nullptr || encryptedPasswordSize == nullptr ||
        !EncryptRaw(plainPassword, &result, &size, encrypt, PlainMasterPassword))
        return fail();
    bool okay = true;
    if (Persistence && Persistence->Active)
    {
        try
        {
            auto canonical = [&](BOOL protectedValue, const std::wstring& master, CredentialBytes& bytes) -> bool
            {
                BYTE* data = nullptr;
                int count = 0;
                if (!EncryptRaw(plainPassword, &data, &count, protectedValue, master))
                    return false;
                try
                {
                    bytes.assign(data, data + count);
                }
                catch (...)
                {
                    SecureZeroMemory(data, count);
                    free(data);
                    throw;
                }
                SecureZeroMemory(data, count);
                free(data);
                return true;
            };
            CredentialBytes before, after;
            CipherFingerprint alias;
            okay = canonical(Persistence->BeforeProtected, OldPlainMasterPassword, before) &&
                   canonical(UseMasterPassword, PlainMasterPassword, after);
            if (okay)
            {
                const DWORD error = FingerprintCipher(result, size, alias);
                okay = error == ERROR_SUCCESS;
                if (!okay)
                    SetLastError(error);
            }
            if (okay)
            {
                auto& cache = Persistence->CacheFor(plainPassword);
                auto aliases = cache.Aliases;
                aliases.push_back(alias);
                okay = PasswordResult(Persistence->Journal.Record(aliases, before, after));
                if (okay)
                    cache.Aliases.push_back(alias);
            }
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            okay = false;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_FILE_TOO_LARGE);
            okay = false;
        }
    }
    if (!okay)
    {
        SecureZeroMemory(result, size);
        free(result);
        return fail();
    }
    *encryptedPassword = result;
    *encryptedPasswordSize = size;
    return TRUE;
}

BOOL CPasswordManager::EncryptRaw(const wchar_t* plainPassword, BYTE** encryptedPassword, int* encryptedPasswordSize, BOOL encrypt, const std::wstring& master)
{
    if (encryptedPassword != NULL)
        *encryptedPassword = NULL;
    if (encryptedPasswordSize != NULL)
        *encryptedPasswordSize = 0;
    if (!PersistentStateValid)
        return FALSE;
    if (encrypt && master.empty())
    {
        TRACE_E("CPasswordManager::EncryptPassword(): Unexpected situation, Master Password was not entered. Call AskForMasterPassword() first.");
        return FALSE;
    }
    if (plainPassword == NULL || encryptedPassword == NULL || encryptedPasswordSize == NULL)
    {
        TRACE_E("CPasswordManager::EncryptPassword(): plainPassword == NULL || encryptedPassword == NULL || encryptedPasswordSize == NULL!");
        return FALSE;
    }

    std::string scrambledPassword;
    const Win32TextConversionResult conversion =
        Win32EncodeText(CP_UTF8, plainPassword, wcslen(plainPassword), scrambledPassword);
    if (!conversion)
    {
        SetLastError(conversion.Win32Error);
        SecureClear(scrambledPassword);
        return FALSE;
    }
    try
    {
        if (!ScrambleUtf8Password(scrambledPassword))
        {
            SecureClear(scrambledPassword);
            return FALSE;
        }
    }
    catch (const std::bad_alloc&)
    {
        SecureClear(scrambledPassword);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    const size_t overhead = encrypt ? 27u : 1u;
    if (scrambledPassword.size() >
        static_cast<size_t>((std::numeric_limits<int>::max)()) - overhead)
    {
        SecureClear(scrambledPassword);
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
    const int scrambledPasswordLen = static_cast<int>(scrambledPassword.size());
    const int resultSize = encrypt ? 1 + 16 + scrambledPasswordLen + 10
                                   : 1 + scrambledPasswordLen;
    BYTE* result = static_cast<BYTE*>(malloc(resultSize));
    if (result == NULL)
    {
        SecureClear(scrambledPassword);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    result[0] = encrypt ? PWDMNGR_SIGNATURE_UTF8_ENCRYPTED
                        : PWDMNGR_SIGNATURE_UTF8_SCRAMBLED;
    if (encrypt)
    {
        std::string masterPasswordBytes;
        if (!EncodeMasterPassword(master, CP_UTF8,
                                  masterPasswordBytes))
        {
            SecureZeroMemory(result, resultSize);
            free(result);
            SecureClear(scrambledPassword);
            return FALSE;
        }

        const DWORD randomError = SecureRandom->Fill(result + 1, 16);
        if (randomError != ERROR_SUCCESS)
        {
            SecureClear(masterPasswordBytes);
            SecureZeroMemory(result, resultSize);
            free(result);
            SecureClear(scrambledPassword);
            SetLastError(randomError);
            return FALSE;
        }
        memcpy(result + 1 + 16, scrambledPassword.data(), scrambledPasswordLen);
        CSalAES aes;
        WORD dummy;
        const int aesResult = SalamanderCrypt->AESInit(
            &aes, PASSWORD_MANAGER_AES_MODE, masterPasswordBytes.data(),
            masterPasswordBytes.size(), result + 1, &dummy);
        SecureClear(masterPasswordBytes);
        if (aesResult != SAL_AES_ERR_GOOD_RETURN)
        {
            SecureZeroMemory(result, resultSize);
            free(result);
            SecureClear(scrambledPassword);
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        SalamanderCrypt->AESEncrypt(&aes, result + 1 + 16,
                                    scrambledPasswordLen);
        SalamanderCrypt->AESEnd(
            &aes, result + 1 + 16 + scrambledPasswordLen, NULL);
        SecureZeroMemory(&aes, sizeof(aes));
    }
    else
    {
        memcpy(result + 1, scrambledPassword.data(), scrambledPasswordLen);
    }

    SecureClear(scrambledPassword);
    *encryptedPassword = result;
    *encryptedPasswordSize = resultSize;
    return TRUE;
}

/*
extern "C"
{
void mytrace(const char *txt)
{
  TRACE_I(txt);
}
}
*/

BOOL CPasswordManager::DecryptPassword(const BYTE* encryptedPassword, int encryptedPasswordSize, std::wstring* plainPassword)
{
    CPasswordProfileLock lock(Persistence.get());
    auto fail = [&]() -> BOOL
    {
        if (Persistence && Persistence->Active)
            Persistence->Failed = true;
        return FALSE;
    };
    if (!lock || !PersistentStateValid || !EnsureCredentialJournal())
        return fail();
    if (!Persistence)
        return DecryptRaw(encryptedPassword, encryptedPasswordSize, plainPassword);
    if (encryptedPassword == nullptr || encryptedPasswordSize <= 1)
        return fail();
    CipherFingerprint alias;
    const DWORD error = FingerprintCipher(encryptedPassword, encryptedPasswordSize, alias);
    if (error != ERROR_SUCCESS)
    {
        SetLastError(error);
        return fail();
    }
    const auto* canonical = Persistence->Journal.Resolve(alias);
    std::wstring decoded;
    if (!DecryptRaw(canonical != nullptr ? canonical->data() : encryptedPassword,
                    canonical != nullptr ? static_cast<int>(canonical->size()) : encryptedPasswordSize,
                    &decoded))
        return fail();
    try
    {
        if (Persistence->Active)
            Persistence->CacheFor(decoded.c_str()).Aliases.push_back(alias);
    }
    catch (const std::bad_alloc&)
    {
        SecureClear(decoded);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return fail();
    }
    if (plainPassword != nullptr)
        plainPassword->swap(decoded);
    SecureClear(decoded);
    return TRUE;
}

BOOL CPasswordManager::DecryptRaw(const BYTE* encryptedPassword, int encryptedPasswordSize, std::wstring* plainPassword)
{
    if (!PersistentStateValid)
        return FALSE;
    if (encryptedPassword == NULL || encryptedPasswordSize <= 1)
    {
        TRACE_E("CPasswordManager::DecryptPassword(): encryptedPassword == NULL || encryptedPasswordSize == 0!");
        return FALSE;
    }
    // if the password is encrypted with AES and we do not know the master password, we fail
    const BYTE signature = *encryptedPassword;
    const BOOL encrypted = signature == PWDMNGR_SIGNATURE_ENCRYPTED ||
                           signature == PWDMNGR_SIGNATURE_UTF8_ENCRYPTED;
    const BOOL utf8 = signature == PWDMNGR_SIGNATURE_UTF8_SCRAMBLED ||
                      signature == PWDMNGR_SIGNATURE_UTF8_ENCRYPTED;
    if (!encrypted && signature != PWDMNGR_SIGNATURE_SCRAMBLED &&
        signature != PWDMNGR_SIGNATURE_UTF8_SCRAMBLED)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (encrypted && (!UseMasterPassword || PlainMasterPassword.empty()) && OldPlainMasterPassword.empty())
    {
        TRACE_I("CPasswordManager::DecryptPassword(): Master Password was not entered. Call AskForMasterPassword() first.");
        return FALSE;
    }
    if (encrypted && (encryptedPasswordSize < 1 + 16 + 1 + 10)) // the password itself must contain at least one character (signature + SALT + password + MAC)
    {
        TRACE_E("CPasswordManager::DecryptPassword(): stored password is too small, probably corrupted!");
        return FALSE;
    }

    std::unique_ptr<BYTE[], CSecureByteArrayDeleter> tmpBuff(
        static_cast<BYTE*>(nullptr),
        CSecureByteArrayDeleter(encryptedPasswordSize + 1));
    try
    {
        tmpBuff.reset(new BYTE[encryptedPasswordSize + 1]);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(tmpBuff.get(), encryptedPassword, encryptedPasswordSize);
    tmpBuff[encryptedPasswordSize] = 0; // terminator required by UnscramblePassword

    int pwdOffset = 1; // signature
    if (encrypted)
    {
        const UINT keyCodePage = utf8 ? CP_UTF8 : PASSWORD_MANAGER_LEGACY_CODE_PAGE;
        auto decryptWith = [&](const std::wstring& masterPassword) -> BOOL
        {
            std::string masterPasswordBytes;
            if (!EncodeMasterPassword(masterPassword, keyCodePage,
                                      masterPasswordBytes))
                return FALSE;
            memcpy(tmpBuff.get(), encryptedPassword, encryptedPasswordSize);
            tmpBuff[encryptedPasswordSize] = 0;
            CSalAES aes;
            WORD dummy;
            const int aesResult = SalamanderCrypt->AESInit(
                &aes, PASSWORD_MANAGER_AES_MODE, masterPasswordBytes.data(),
                masterPasswordBytes.size(), tmpBuff.get() + 1, &dummy);
            SecureClear(masterPasswordBytes);
            if (aesResult != SAL_AES_ERR_GOOD_RETURN)
                return FALSE;
            SalamanderCrypt->AESDecrypt(
                &aes, tmpBuff.get() + 1 + 16,
                encryptedPasswordSize - 1 - 16 - 10);
            BYTE mac[10];
            SalamanderCrypt->AESEnd(&aes, mac, NULL);
            const BOOL matches =
                memcmp(mac, &tmpBuff[encryptedPasswordSize - 10],
                       sizeof(mac)) == 0;
            SecureZeroMemory(mac, sizeof(mac));
            SecureZeroMemory(&aes, sizeof(aes));
            return matches;
        };

        BOOL decrypted = FALSE;
        if (!OldPlainMasterPassword.empty())
            decrypted = decryptWith(OldPlainMasterPassword);
        if (!decrypted && UseMasterPassword && !PlainMasterPassword.empty())
            decrypted = decryptWith(PlainMasterPassword);
        if (!decrypted)
        {
            TRACE_I("CPasswordManager::DecryptPassword(): wrong master password, password cannot be decrypted!");
            return FALSE;
        }
        pwdOffset += 16;                         // skip the AES salt
        tmpBuff[encryptedPasswordSize - 10] = 0; // terminator for unscramble (placed over the first MAC byte)
    }
    std::string passwordBytes;
    if (utf8)
    {
        const size_t scrambledLength = encrypted
                                           ? static_cast<size_t>(encryptedPasswordSize - pwdOffset - 10)
                                           : static_cast<size_t>(encryptedPasswordSize - pwdOffset);
        try
        {
            if (!UnscrambleUtf8Password(tmpBuff.get() + pwdOffset,
                                        scrambledLength, passwordBytes))
                return FALSE;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    else
    {
        try
        {
            if (!UnscramblePassword((char*)tmpBuff.get() + pwdOffset))
                return FALSE;
            passwordBytes.assign((char*)tmpBuff.get() + pwdOffset);
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }

    std::wstring decoded;
    const Win32TextConversionResult conversion =
        Win32DecodeText(utf8 ? CP_UTF8 : PASSWORD_MANAGER_LEGACY_CODE_PAGE,
                        passwordBytes, decoded);
    SecureClear(passwordBytes);
    if (!conversion)
    {
        SetLastError(conversion.Win32Error);
        SecureClear(decoded);
        return FALSE;
    }
    if (plainPassword != NULL)
        plainPassword->swap(decoded);
    SecureClear(decoded);

    return TRUE;
}

BOOL CPasswordManager::IsPasswordEncrypted(const BYTE* encryptedPassword, int encryptedPasswordSize)
{
    if (Persistence)
    {
        CPasswordProfileLock lock(Persistence.get());
        if (!lock || !EnsureCredentialJournal())
            return TRUE;
        if (encryptedPassword != nullptr && encryptedPasswordSize > 0)
        {
            CipherFingerprint alias;
            const DWORD error = FingerprintCipher(encryptedPassword, encryptedPasswordSize, alias);
            if (error != ERROR_SUCCESS)
            {
                SetLastError(error);
                return TRUE;
            }
            if (const auto* canonical = Persistence->Journal.Resolve(alias))
            {
                encryptedPassword = canonical->data();
                encryptedPasswordSize = static_cast<int>(canonical->size());
            }
        }
    }
    return encryptedPassword != nullptr && encryptedPasswordSize > 0 &&
           (*encryptedPassword == PWDMNGR_SIGNATURE_ENCRYPTED || *encryptedPassword == PWDMNGR_SIGNATURE_UTF8_ENCRYPTED);
}

BOOL CPasswordManager::SetMasterPassword(HWND hParent, const wchar_t* password)
{
    CPasswordProfileLock lock(Persistence.get());
    if (!lock || !PersistentStateValid || !EnsureCredentialJournal())
        return FALSE;
    if (!OldPlainMasterPassword.empty() || (Persistence && Persistence->Active) ||
        (UseMasterPassword && PlainMasterPassword.empty()))
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    std::wstring newPassword;
    std::vector<BYTE> newVerifier;
    try
    {
        if (password != nullptr && password[0] != L'\0')
        {
            newPassword = password;
            if (!CreateMasterPasswordVerifier(newPassword.c_str(), newVerifier))
            {
                SecureClear(newPassword);
                return FALSE;
            }
        }
        if (Persistence)
        {
            CredentialAuthority target;
            target.Protected = !newPassword.empty();
            target.Verifier.assign(newVerifier.begin(), newVerifier.end());
            std::vector<CredentialRewrite> prepared;
            for (const auto& record : Persistence->Journal.Records())
            {
                CredentialRewrite next;
                next.Aliases = record.Aliases;
                next.Before = Persistence->Journal.ReadableCipher(record);
                std::wstring plaintext;
                if (!DecryptRaw(next.Before.data(), static_cast<int>(next.Before.size()), &plaintext))
                {
                    SecureClear(newPassword);
                    SecureClear(newVerifier);
                    return FALSE;
                }
                BYTE* data = nullptr;
                int size = 0;
                const BOOL encrypted = EncryptRaw(plaintext.c_str(), &data, &size, target.Protected, newPassword);
                SecureClear(plaintext);
                if (!encrypted)
                {
                    SecureClear(newPassword);
                    SecureClear(newVerifier);
                    return FALSE;
                }
                try
                {
                    next.After.assign(data, data + size);
                }
                catch (...)
                {
                    SecureZeroMemory(data, size);
                    free(data);
                    throw;
                }
                SecureZeroMemory(data, size);
                free(data);
                prepared.push_back(std::move(next));
            }
            if (!PasswordResult(Persistence->Journal.Begin(target, std::move(prepared))))
            {
                SecureClear(newPassword);
                SecureClear(newVerifier);
                return FALSE;
            }
            Persistence->BeforeProtected = UseMasterPassword != FALSE;
            Persistence->Failed = false;
            Persistence->Active = true;
        }
    }
    catch (const std::bad_alloc&)
    {
        SecureClear(newPassword);
        SecureClear(newVerifier);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    const BOOL oldUse = UseMasterPassword;
    const BOOL oldLegacy = MasterPasswordVerifierIsLegacy;
    OldPlainMasterPassword = std::move(PlainMasterPassword);
    MasterPasswordVerifier.swap(newVerifier); // newVerifier now owns the old verifier
    MasterPasswordVerifierIsLegacy = FALSE;
    UseMasterPassword = !newPassword.empty();
    PlainMasterPassword.swap(newPassword);
    const int event = !UseMasterPassword ? PME_MASTERPASSWORDREMOVED
                      : oldUse           ? PME_MASTERPASSWORDCHANGED
                                         : PME_MASTERPASSWORDCREATED;
    bool success = true;
    if (Persistence)
    {
        try
        {
            success = Persistence->Host.Notify(hParent, event) && !Persistence->Failed;
            if (success)
                success = PasswordResult(Persistence->Host.SaveConfiguration(hParent)) && !Persistence->Failed;
            if (success)
            {
                CredentialAuthority authority;
                authority.Protected = UseMasterPassword != FALSE;
                authority.Verifier.assign(MasterPasswordVerifier.begin(), MasterPasswordVerifier.end());
                success = PasswordResult(Persistence->Journal.Commit(authority));
            }
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            success = false;
        }
        if (!success)
        {
            const DWORD failure = GetLastError();
            Persistence->Journal.Abort(); // durable old authority; pending restart also selects the old authority
            SecureClear(PlainMasterPassword);
            PlainMasterPassword.swap(OldPlainMasterPassword);
            MasterPasswordVerifier.swap(newVerifier);
            UseMasterPassword = oldUse;
            MasterPasswordVerifierIsLegacy = oldLegacy;
            SetLastError(failure != ERROR_SUCCESS ? failure : ERROR_WRITE_FAULT);
        }
        Persistence->Active = false;
        Persistence->Cache.clear();
    }
    else
        success = Plugins.PasswordManagerEvent(hParent, event) != FALSE;
    SecureClear(newPassword);
    SecureClear(newVerifier);
    SecureClear(OldPlainMasterPassword);
    return success;
}

BOOL CPasswordManager::EnterMasterPassword(const wchar_t* password)
{
    if (!PersistentStateValid)
        return FALSE;
    if (!UseMasterPassword || password == NULL)
    {
        TRACE_E("CPasswordManager::EnterMasterPassword(): Unexpected situation, Master Password is not used.");
        return FALSE;
    }
    if (!PlainMasterPassword.empty())
    {
        // if an attempt is made to insert the current password again, silently ignore it
        if (PlainMasterPassword == password)
            return TRUE;

        TRACE_E("CPasswordManager::EnterMasterPassword(): Unexpected situation, Master Password is already entered.");
        return FALSE;
    }
    if (!VerifyMasterPassword(password))
    {
        TRACE_E("CPasswordManager::EnterMasterPassword(): Wrong master password.");
        return FALSE;
    }

    try
    {
        PlainMasterPassword = password;
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

BOOL CPasswordManager::CreateMasterPasswordVerifier(
    const wchar_t* password, std::vector<BYTE>& verifier)
{
    if (password == NULL || password[0] == L'\0')
        return FALSE;

    std::string passwordBytes;
    if (!EncodeMasterPassword(password, wcslen(password), CP_UTF8,
                              passwordBytes))
        return FALSE;

    CMasterPasswordVerifierUtf8 candidate = {};
    candidate.Magic = PWDMNGR_MASTER_VERIFIER_UTF8_MAGIC;
    candidate.Version = PWDMNGR_MASTER_VERIFIER_UTF8_VERSION;
    DWORD randomError = SecureRandom->Fill(candidate.Salt, sizeof(candidate.Salt));
    if (randomError == ERROR_SUCCESS)
        randomError = SecureRandom->Fill(candidate.Dummy, sizeof(candidate.Dummy));
    if (randomError != ERROR_SUCCESS)
    {
        SecureClear(passwordBytes);
        SecureZeroMemory(&candidate, sizeof(candidate));
        SetLastError(randomError);
        return FALSE;
    }

    CSalAES aes;
    WORD dummy;
    const int aesResult = SalamanderCrypt->AESInit(
        &aes, PASSWORD_MANAGER_AES_MODE, passwordBytes.data(),
        passwordBytes.size(), candidate.Salt, &dummy);
    SecureClear(passwordBytes);
    if (aesResult != SAL_AES_ERR_GOOD_RETURN)
    {
        SecureZeroMemory(&candidate, sizeof(candidate));
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    SalamanderCrypt->AESEncrypt(&aes, candidate.Dummy,
                                sizeof(candidate.Dummy));
    SalamanderCrypt->AESEnd(&aes, candidate.MAC, NULL);
    SecureZeroMemory(&aes, sizeof(aes));

    try
    {
        std::vector<BYTE> staged(sizeof(candidate));
        memcpy(staged.data(), &candidate, sizeof(candidate));
        SecureZeroMemory(&candidate, sizeof(candidate));
        verifier.swap(staged);
        SecureClear(staged);
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        SecureZeroMemory(&candidate, sizeof(candidate));
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

BOOL CPasswordManager::VerifyMasterPassword(const wchar_t* password)
{
    if (!PersistentStateValid)
        return FALSE;
    if (!UseMasterPassword || password == NULL)
    {
        TRACE_E("CPasswordManager::VerifyMasterPassword() Using of Master Password is turned off in Salamanader configuration.");
        return FALSE;
    }

    // if the plaintext master password is cached, we can perform a simple comparison
    if (!PlainMasterPassword.empty())
    {
        return (PlainMasterPassword == password);
    }

    if (MasterPasswordVerifier.empty())
    {
        TRACE_E("CPasswordManager::VerifyMasterPassword() verifier is missing.");
        return FALSE;
    }

    BYTE salt[16];
    BYTE dummyData[16];
    BYTE storedMac[10];
    UINT keyCodePage = CP_UTF8;
    if (MasterPasswordVerifierIsLegacy)
    {
        if (MasterPasswordVerifier.size() != sizeof(CMasterPasswordVerifier))
            return FALSE;
        const CMasterPasswordVerifier* legacy =
            reinterpret_cast<const CMasterPasswordVerifier*>(
                MasterPasswordVerifier.data());
        memcpy(salt, legacy->Salt, sizeof(salt));
        memcpy(dummyData, legacy->Dummy, sizeof(dummyData));
        memcpy(storedMac, legacy->MAC, sizeof(storedMac));
        keyCodePage = PASSWORD_MANAGER_LEGACY_CODE_PAGE;
    }
    else
    {
        if (MasterPasswordVerifier.size() !=
            sizeof(CMasterPasswordVerifierUtf8))
            return FALSE;
        const CMasterPasswordVerifierUtf8* current =
            reinterpret_cast<const CMasterPasswordVerifierUtf8*>(
                MasterPasswordVerifier.data());
        if (current->Magic != PWDMNGR_MASTER_VERIFIER_UTF8_MAGIC ||
            current->Version != PWDMNGR_MASTER_VERIFIER_UTF8_VERSION)
            return FALSE;
        memcpy(salt, current->Salt, sizeof(salt));
        memcpy(dummyData, current->Dummy, sizeof(dummyData));
        memcpy(storedMac, current->MAC, sizeof(storedMac));
    }

    std::string passwordBytes;
    if (!EncodeMasterPassword(password, wcslen(password), keyCodePage,
                              passwordBytes))
    {
        SecureZeroMemory(salt, sizeof(salt));
        SecureZeroMemory(dummyData, sizeof(dummyData));
        SecureZeroMemory(storedMac, sizeof(storedMac));
        return FALSE;
    }

    CSalAES aes;
    WORD passwordVerifier;
    const int aesResult = SalamanderCrypt->AESInit(
        &aes, PASSWORD_MANAGER_AES_MODE, passwordBytes.data(),
        passwordBytes.size(), salt, &passwordVerifier);
    SecureClear(passwordBytes);
    if (aesResult != SAL_AES_ERR_GOOD_RETURN)
    {
        SecureZeroMemory(salt, sizeof(salt));
        SecureZeroMemory(dummyData, sizeof(dummyData));
        SecureZeroMemory(storedMac, sizeof(storedMac));
        return FALSE;
    }
    SalamanderCrypt->AESDecrypt(&aes, dummyData, sizeof(dummyData));
    BYTE calculatedMac[10];
    SalamanderCrypt->AESEnd(&aes, calculatedMac, NULL);
    const BOOL matches =
        memcmp(calculatedMac, storedMac, sizeof(calculatedMac)) == 0;
    SecureZeroMemory(&aes, sizeof(aes));
    SecureZeroMemory(salt, sizeof(salt));
    SecureZeroMemory(dummyData, sizeof(dummyData));
    SecureZeroMemory(storedMac, sizeof(storedMac));
    SecureZeroMemory(calculatedMac, sizeof(calculatedMac));
    return matches;
}

void CPasswordManager::NotifyAboutMasterPasswordChange(HWND hParent)
{
    BOOL set = IsUsingMasterPassword();
    if (set)
        gPrompter->ShowInfo(LoadStrW(IDS_MASTERPASSWORD_CHANGED_TITLE), LoadStrW(IDS_MASTERPASSWORD_SET));
    else
        gPrompter->ShowError(LoadStrW(IDS_MASTERPASSWORD_CHANGED_TITLE), LoadStrW(IDS_MASTERPASSWORD_REMOVED));
}

BOOL CPasswordManager::Save(HKEY hKey)
{
    try
    {
        CPasswordProfileLock lock(Persistence.get());
        if (!lock || !EnsureCredentialJournal())
            return FALSE;
        if (!PersistentStateValid)
            return FALSE;
        if (UseMasterPassword)
        {
            if (MasterPasswordVerifier.empty())
                return FALSE;

            std::vector<BYTE> upgraded;
            const std::vector<BYTE>* record = &MasterPasswordVerifier;
            if (MasterPasswordVerifierIsLegacy && !PlainMasterPassword.empty())
            {
                if (!CreateMasterPasswordVerifier(PlainMasterPassword.c_str(),
                                                  upgraded))
                    return FALSE;
                record = &upgraded;
            }

            BOOL ret = SetValue(
                hKey, SALAMANDER_PWDMNGR_MASTERPWD_VERIFIER, REG_BINARY,
                record->data(), static_cast<DWORD>(record->size()));
            if (ret)
                ret = SetValue(hKey, SALAMANDER_PWDMNGR_USEMASTERPWD,
                               REG_DWORD, &UseMasterPassword,
                               sizeof(UseMasterPassword));
            if (ret && record == &upgraded && Persistence)
            {
                CredentialAuthority authority;
                authority.Protected = true;
                authority.Verifier.assign(upgraded.begin(), upgraded.end());
                ret = PasswordResult(Persistence->Journal.Commit(authority));
            }
            if (ret && record == &upgraded)
            {
                SecureClear(MasterPasswordVerifier);
                MasterPasswordVerifier.swap(upgraded);
                MasterPasswordVerifierIsLegacy = FALSE;
            }
            SecureClear(upgraded);
            return ret;
        }

        const BOOL ret = SetValue(hKey, SALAMANDER_PWDMNGR_USEMASTERPWD,
                                  REG_DWORD, &UseMasterPassword,
                                  sizeof(UseMasterPassword));
        DeleteValue(hKey, SALAMANDER_PWDMNGR_MASTERPWD_VERIFIER);
        return ret;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
}

BOOL CPasswordManager::Load(HKEY hKey)
{
    try
    {
        PersistentStateValid = FALSE;
        if (Persistence)
        {
            CredentialJournalSnapshot snapshot;
            if (!PasswordResult(Persistence->Host.Read(snapshot)))
                return FALSE;
            if (snapshot.Present)
            {
                if (!PasswordResult(Persistence->Journal.Load({})))
                    return FALSE;
                const auto& authority = Persistence->Journal.Authority();
                const auto& bytes = authority.Verifier;
                const BOOL legacy = bytes.size() == sizeof(CMasterPasswordVerifier);
                if (authority.Protected && !legacy)
                {
                    if (bytes.size() != sizeof(CMasterPasswordVerifierUtf8))
                        return FALSE;
                    CMasterPasswordVerifierUtf8 verifier;
                    memcpy(&verifier, bytes.data(), sizeof(verifier));
                    const bool valid = verifier.Magic == PWDMNGR_MASTER_VERIFIER_UTF8_MAGIC &&
                                       verifier.Version == PWDMNGR_MASTER_VERIFIER_UTF8_VERSION;
                    SecureZeroMemory(&verifier, sizeof(verifier));
                    if (!valid)
                        return FALSE;
                }
                SecureClear(PlainMasterPassword);
                SecureClear(OldPlainMasterPassword);
                SecureClear(MasterPasswordVerifier);
                MasterPasswordVerifier.assign(bytes.begin(), bytes.end());
                MasterPasswordVerifierIsLegacy = authority.Protected && legacy;
                UseMasterPassword = authority.Protected;
                PersistentStateValid = TRUE;
                return TRUE;
            }
        }
        BOOL loadedUseMasterPassword = FALSE;
        IRegistry* registry = gRegistry != NULL ? gRegistry : GetWin32Registry();
        DWORD enabled = FALSE;
        const RegistryResult enabledResult = registry->GetDWord(hKey, SALAMANDER_PWDMNGR_USEMASTERPWD, enabled);
        if (!enabledResult.success && !enabledResult.notFound())
            return FALSE;
        if (enabledResult.notFound())
        {
            RegValueType type = RegValueType::None;
            DWORD size = 0;
            const RegistryResult verifier = registry->ReadValue(hKey, SALAMANDER_PWDMNGR_MASTERPWD_VERIFIER, type, NULL, size);
            // A genuinely old/unconfigured store has neither value. A verifier
            // without its protection flag is inconsistent, not an empty store.
            if (verifier.success || !verifier.notFound())
                return FALSE;
        }
        loadedUseMasterPassword = enabledResult.success && enabled != FALSE;

        std::vector<BYTE> loadedVerifier;
        BOOL loadedLegacy = FALSE;
        if (loadedUseMasterPassword)
        {
            DWORD size = 0;
            RegValueType type = RegValueType::None;
            if (!registry->ReadValue(hKey, SALAMANDER_PWDMNGR_MASTERPWD_VERIFIER, type, NULL, size).success ||
                type != RegValueType::Binary ||
                (size != sizeof(CMasterPasswordVerifier) &&
                 size != sizeof(CMasterPasswordVerifierUtf8)))
                return FALSE;
            try
            {
                loadedVerifier.resize(size);
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            const DWORD expectedSize = size;
            if (!registry->ReadValue(hKey, SALAMANDER_PWDMNGR_MASTERPWD_VERIFIER,
                                     type, loadedVerifier.data(), size)
                     .success ||
                type != RegValueType::Binary || size != expectedSize)
            {
                SecureClear(loadedVerifier);
                return FALSE;
            }
            loadedLegacy = size == sizeof(CMasterPasswordVerifier);
            if (!loadedLegacy)
            {
                const CMasterPasswordVerifierUtf8* current =
                    reinterpret_cast<const CMasterPasswordVerifierUtf8*>(
                        loadedVerifier.data());
                if (current->Magic != PWDMNGR_MASTER_VERIFIER_UTF8_MAGIC ||
                    current->Version != PWDMNGR_MASTER_VERIFIER_UTF8_VERSION)
                {
                    SecureClear(loadedVerifier);
                    return FALSE;
                }
            }
        }

        SecureClear(PlainMasterPassword);
        SecureClear(OldPlainMasterPassword);
        SecureClear(MasterPasswordVerifier);
        MasterPasswordVerifier.swap(loadedVerifier);
        SecureClear(loadedVerifier);
        MasterPasswordVerifierIsLegacy = loadedLegacy;
        UseMasterPassword = loadedUseMasterPassword;
        PersistentStateValid = TRUE;
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
}

BOOL CPasswordManager::AskForMasterPassword(HWND hParent)
{
    if (!PersistentStateValid)
        return FALSE;
    // return FALSE if master password usage is disabled
    if (!UseMasterPassword)
        return FALSE;

    // prompt for the master password (even if cached; the caller might have verified it beforehand using IsMasterPasswordSet())
    CEnterMasterPassword dlg(hParent, this);
    return dlg.Execute() == IDOK; // return TRUE if the user entered it correctly, otherwise FALSE
}

//****************************************************************************
//
// CSalamanderPasswordManager (called by plugins)
//

BOOL CSalamanderPasswordManager::IsUsingMasterPassword()
{
    CALL_STACK_MESSAGE_NONE
#ifdef _DEBUG
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderPasswordManager::IsUsingMasterPassword() only from main thread!");
        return FALSE;
    }
#endif // _DEBUG
    return PasswordManager.IsUsingMasterPassword();
}

BOOL CSalamanderPasswordManager::IsMasterPasswordSet()
{
    CALL_STACK_MESSAGE_NONE
#ifdef _DEBUG
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderPasswordManager::IsMasterPasswordSet() only from main thread!");
        return FALSE;
    }
#endif // _DEBUG
    return PasswordManager.IsMasterPasswordSet();
}

BOOL CSalamanderPasswordManager::AskForMasterPassword(HWND hParent)
{
    CALL_STACK_MESSAGE1("CSalamanderPasswordManager::AskForMasterPassword()");
#ifdef _DEBUG
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderPasswordManager::AskForMasterPassword() only from main thread!");
        return FALSE;
    }
#endif // _DEBUG
    return PasswordManager.AskForMasterPassword(hParent);
}

BOOL CSalamanderPasswordManager::EncryptPassword(const wchar_t* plainPassword, BYTE** encryptedPassword, int* encryptedPasswordSize, BOOL encrypt)
{
    CALL_STACK_MESSAGE1("CSalamanderPasswordManager::EncryptPassword()");
#ifdef _DEBUG
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderPasswordManager::EncryptPassword() only from main thread!");
        if (encryptedPassword != NULL)
            *encryptedPassword = NULL;
        if (encryptedPasswordSize != NULL)
            *encryptedPasswordSize = 0;
        return FALSE;
    }
#endif // _DEBUG
    if (encryptedPassword != NULL)
        *encryptedPassword = NULL;
    if (encryptedPasswordSize != NULL)
        *encryptedPasswordSize = 0;
    try
    {
        return PasswordManager.EncryptPassword(plainPassword, encryptedPassword,
                                               encryptedPasswordSize, encrypt);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (...)
    {
        SetLastError(ERROR_INVALID_DATA);
    }
    return FALSE;
}

BOOL CSalamanderPasswordManager::DecryptPassword(const BYTE* encryptedPassword, int encryptedPasswordSize, CSalamanderStringBuffer* plainPassword)
{
    CALL_STACK_MESSAGE1("CSalamanderPasswordManager::DecryptPassword()");
#ifdef _DEBUG
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderPasswordManager::DecryptPassword() only from main thread!");
        return FALSE;
    }
#endif // _DEBUG
    try
    {
        std::wstring decoded;
        if (!PasswordManager.DecryptPassword(
                encryptedPassword, encryptedPasswordSize,
                plainPassword != NULL ? &decoded : NULL))
            return FALSE;
        return plainPassword == NULL ||
               sally::plugin_abi::WriteStringBuffer(*plainPassword, decoded);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (...)
    {
        SetLastError(ERROR_INVALID_DATA);
    }
    return FALSE;
}

BOOL CSalamanderPasswordManager::IsPasswordEncrypted(const BYTE* encryptedPassword, int encryptedPasswordSize)
{
    CALL_STACK_MESSAGE1("CSalamanderPasswordManager::IsPasswordEncrypted()");
#ifdef _DEBUG
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderPasswordManager::IsPasswordEncrypted() only from main thread!");
        return FALSE;
    }
#endif // _DEBUG
    return PasswordManager.IsPasswordEncrypted(encryptedPassword, encryptedPasswordSize);
}
