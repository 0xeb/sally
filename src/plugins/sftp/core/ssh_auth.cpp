// CSshSession authentication: agent, key file, password, keyboard-interactive.
#include <libssh2.h>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "key_file.h"
#include "ppk.h"
#include "ssh_session.h"

namespace
{
std::vector<std::string> SplitMethods(const char* list)
{
    std::vector<std::string> methods;
    std::stringstream ss(list != nullptr ? list : "");
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty())
            methods.push_back(item);
    return methods;
}

bool Contains(const std::vector<std::string>& v, const char* what)
{
    for (const std::string& s : v)
        if (s == what)
            return true;
    return false;
}

constexpr int kBrokenCodeValue = -100000; // CSshSession::kBrokenCode

// The connection itself is gone or stuck (not just a refused credential): stop trying other methods.
bool IsConnectionLost(int code)
{
    return code == LIBSSH2_ERROR_TIMEOUT || code == LIBSSH2_ERROR_SOCKET_DISCONNECT || code == kBrokenCodeValue ||
           code == LIBSSH2_ERROR_SOCKET_SEND || code == LIBSSH2_ERROR_SOCKET_RECV ||
           code == LIBSSH2_ERROR_SOCKET_TIMEOUT || code == LIBSSH2_ERROR_SOCKET_NONE;
}

// Context of one keyboard-interactive exchange; reached from libssh2's callback through the session abstract.
struct CKbdContext
{
    ISshCallbacks* cb = nullptr;
    const CSecret* stored = nullptr; // answer for the first prompt, may be empty
    bool storedUsed = false;
    const std::atomic<bool>* cancelled = nullptr;
    bool declined = false; // the user cancelled a prompt (or there was nobody to ask)
};

char* CopyAnswer(const CSecret& answer)
{
    char* text = static_cast<char*>(malloc(answer.size() + 1)); // libssh2 frees it with free()
    if (text != nullptr)
    {
        memcpy(text, answer.data(), answer.size());
        text[answer.size()] = '\0';
    }
    return text;
}

void KbdCallback(const char* name, int nameLen, const char* instruction, int instructionLen, int numPrompts,
                 const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts, LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
                 void** abstract)
{
    CKbdContext* ctx = static_cast<CKbdContext*>(*abstract);
    for (int i = 0; i < numPrompts; ++i)
    {
        responses[i].text = nullptr;
        responses[i].length = 0;
    }
    if (ctx == nullptr)
        return;
    for (int i = 0; i < numPrompts && !ctx->declined; ++i)
    {
        CSecret answer;
        if (!ctx->storedUsed && ctx->stored != nullptr && !ctx->stored->empty())
        {
            ctx->storedUsed = true;
            answer = ctx->stored->Clone();
        }
        else if (ctx->cb != nullptr && !ctx->cancelled->load())
        {
            const std::string promptText(reinterpret_cast<const char*>(prompts[i].text), prompts[i].length);
            if (!ctx->cb->Prompt(PromptKind::KeyboardInteractive, std::string(name, nameLen > 0 ? nameLen : 0),
                                 std::string(instruction, instructionLen > 0 ? instructionLen : 0), promptText,
                                 prompts[i].echo != 0, answer))
                ctx->declined = true;
        }
        else
        {
            ctx->declined = true;
        }
        if (ctx->declined)
            break;
        responses[i].text = CopyAnswer(answer);
        responses[i].length = responses[i].text != nullptr ? static_cast<unsigned int>(answer.size()) : 0;
    }
}
} // namespace

// What went wrong so far, so the final error can be specific.
struct CSshSession::CAuthState
{
    std::vector<std::string> tried;
    SshErrorKind keyKind = SshErrorKind::None; // problem with the key file itself
    std::string keyMessage;
    SshMsg keyMsgId = SshMsg::None;
    std::vector<std::string> keyArgs;
    int lostCode = 0;      // connection-level failure (timeout, disconnect, cancel)
    bool declined = false; // the user cancelled a prompt
    bool passwordExpired = false;
    bool passphraseAsked = false;

    bool Stop() const { return lostCode != 0 || declined || passwordExpired; }
};

bool CSshSession::TryKey(const CSecret& key, const CSecret& passphrase, const CSshSite& site, int& code)
{
    code = Run(
        [&]
        {
            return libssh2_userauth_publickey_frommemory(session_, site.user.c_str(), site.user.size(), nullptr, 0,
                                                         key.data(), key.size(),
                                                         passphrase.empty() ? nullptr : passphrase.c_str());
        },
        timeoutMs_);
    return code == 0;
}

// .ppk: converted to an unencrypted OpenSSH key in memory (the MAC tells a wrong passphrase from a damaged file),
// then sent like any other key. The passphrase is asked once, only when the key is encrypted and none is stored.
bool CSshSession::AuthPuttyKey(const CSecret& file, const CSshSite& site, CAuthState& state)
{
    CSecret passphrase = site.passphrase.Clone();
    if (passphrase.empty() && PpkIsEncrypted(file) && cb_ != nullptr && !state.passphraseAsked)
    {
        state.passphraseAsked = true;
        if (!cb_->Prompt(PromptKind::KeyPassphrase, "", "", site.keyFile, false, passphrase))
        {
            state.declined = true;
            return false;
        }
    }
    CSecret converted; // wiped by the destructor on every path
    CSshError err;
    if (!ConvertPpk(file, passphrase, converted, nullptr, err))
    {
        state.keyKind = err.kind;
        state.keyMessage = "The key file '" + site.keyFile + "' could not be used: " + err.message;
        if (err.msgId != SshMsg::None)
        {
            // the .ppk error has its own text; the key file is its first argument
            state.keyMsgId = err.msgId;
            state.keyArgs = {site.keyFile};
            state.keyArgs.insert(state.keyArgs.end(), err.args.begin(), err.args.end());
        }
        else
        {
            state.keyMsgId = SshMsg::KeyFileUnusable;
            state.keyArgs = {site.keyFile, err.message};
        }
        return false;
    }
    state.tried.push_back("publickey");
    int code = 0;
    if (TryKey(converted, CSecret(), site, code))
        return true;
    if (IsConnectionLost(code))
        state.lostCode = code;
    return false;
}

bool CSshSession::AuthKeyFile(const CSshSite& site, CAuthState& state)
{
    CSecret key; // wiped by the destructor on every path
    CSshError readError;
    if (!ReadKeyFile(site.keyFile, key, readError))
    {
        state.keyKind = readError.kind;
        state.keyMessage = readError.message;
        state.keyMsgId = readError.msgId;
        state.keyArgs = readError.args;
        return false;
    }
    if (IsPuttyKey(key))
        return AuthPuttyKey(key, site, state);

    state.tried.push_back("publickey");
    int code = 0;
    if (TryKey(key, site.passphrase, site, code))
        return true;

    // libssh2 reports an encrypted key that could not be decrypted (missing or wrong passphrase) as a file error
    if (code == LIBSSH2_ERROR_FILE)
    {
        if (site.passphrase.empty() && cb_ != nullptr && !state.passphraseAsked)
        {
            state.passphraseAsked = true;
            CSecret entered;
            if (!cb_->Prompt(PromptKind::KeyPassphrase, "", "", site.keyFile, false, entered))
            {
                state.declined = true;
                return false;
            }
            if (TryKey(key, entered, site, code))
                return true;
        }
        if (code == LIBSSH2_ERROR_FILE)
        {
            state.keyKind = SshErrorKind::KeyFileError;
            state.keyMessage = "The key file '" + site.keyFile +
                               "' could not be used: wrong or missing passphrase, or an unsupported key format";
            state.keyMsgId = SshMsg::KeyFileWrongPassphrase;
            state.keyArgs = {site.keyFile};
            return false;
        }
    }
    if (IsConnectionLost(code))
        state.lostCode = code;
    return false;
}

bool CSshSession::AuthAgent(const CSshSite& site, CAuthState& state)
{
    LIBSSH2_AGENT* agent = libssh2_agent_init(session_);
    if (agent == nullptr)
        return false;
    bool ok = false;
    // no agent running (connect fails) or no identities: silently nothing to try
    if (libssh2_agent_connect(agent) == 0 && libssh2_agent_list_identities(agent) == 0)
    {
        state.tried.push_back("agent");
        libssh2_agent_publickey* identity = nullptr;
        libssh2_agent_publickey* previous = nullptr;
        while (!ok && !IsCancelled() && libssh2_agent_get_identity(agent, &identity, previous) == 0)
        {
            int code = Run([&] { return libssh2_agent_userauth(agent, site.user.c_str(), identity); }, timeoutMs_);
            ok = code == 0;
            if (IsConnectionLost(code))
            {
                state.lostCode = code;
                break;
            }
            previous = identity;
        }
    }
    libssh2_agent_disconnect(agent);
    libssh2_agent_free(agent);
    return ok;
}

bool CSshSession::AuthPassword(const CSshSite& site, CAuthState& state)
{
    state.tried.push_back("password");
    int code = Run(
        [&]
        {
            return libssh2_userauth_password_ex(session_, site.user.c_str(), static_cast<unsigned int>(site.user.size()),
                                                site.password.c_str(), static_cast<unsigned int>(site.password.size()),
                                                nullptr);
        },
        timeoutMs_);
    if (code == LIBSSH2_ERROR_PASSWORD_EXPIRED)
        state.passwordExpired = true;
    else if (IsConnectionLost(code))
        state.lostCode = code;
    return code == 0;
}

bool CSshSession::AuthKeyboardInteractive(const CSshSite& site, CAuthState& state)
{
    state.tried.push_back("keyboard-interactive");
    CKbdContext ctx;
    ctx.cb = cb_;
    ctx.stored = &site.password;
    ctx.cancelled = &cancelled_;
    void** abstract = libssh2_session_abstract(session_);
    void* previous = *abstract;
    *abstract = &ctx;
    int code = Run(
        [&]
        {
            return libssh2_userauth_keyboard_interactive_ex(session_, site.user.c_str(),
                                                            static_cast<unsigned int>(site.user.size()), KbdCallback);
        },
        timeoutMs_);
    *abstract = previous;
    if (ctx.declined && code != 0)
        state.declined = true;
    else if (IsConnectionLost(code))
        state.lostCode = code;
    return code == 0;
}

bool CSshSession::FailAuth(const CSshSite& site, const std::string& offered, const CAuthState& state)
{
    if (state.lostCode != 0)
        return FailFromSession("Authentication interrupted", state.lostCode, SshMsg::AuthInterrupted);
    if (state.declined)
        return Fail(SshErrorKind::AuthFailed, SshMsg::AuthCancelled, {}, "Authentication was cancelled");
    if (state.passwordExpired)
    {
        Fail(SshErrorKind::AuthFailed, SshMsg::PasswordExpired, {},
             "The server requires a password change; change the password outside Sally first");
        error_.libssh2Code = LIBSSH2_ERROR_PASSWORD_EXPIRED;
        return false;
    }
    if (state.keyKind != SshErrorKind::None)
        return Fail(state.keyKind, state.keyMsgId, state.keyArgs, state.keyMessage);

    std::string tried;
    for (const std::string& t : state.tried)
        tried += (tried.empty() ? "" : ", ") + t;
    const std::string offeredText = offered.empty() ? std::string("-") : offered; // shown when the server offers nothing
    return Fail(SshErrorKind::AuthFailed, tried.empty() ? SshMsg::AuthFailedNoCredentials : SshMsg::AuthFailed,
                tried.empty() ? std::vector<std::string>{site.user, offeredText}
                              : std::vector<std::string>{site.user, offeredText, tried},
                "Authentication failed for user '" + site.user + "'. The server offers: " +
                    (offered.empty() ? std::string("(nothing)") : offered) + ". Tried: " +
                    (tried.empty() ? std::string("nothing (no matching credentials in the site settings)") : tried));
}

void CSshSession::LogAuthResult(const char* method, bool ok) const
{
    Log(SshLogLevel::Normal, std::string(method) + (ok ? ": accepted" : ": not accepted"));
}

bool CSshSession::Authenticate(const CSshSite& site)
{
    error_ = CSshError();
    if (broken_)
        return FailBroken();
    if (!connected_ || session_ == nullptr)
        return Fail(SshErrorKind::Other, SshMsg::AuthNeedsConnection, {},
                    "Authentication needs an open connection with an accepted host key");
    if (authenticated_)
        return true;

    int code = 0;
    const char* list = RunPtr(
        [&] { return libssh2_userauth_list(session_, site.user.c_str(), static_cast<unsigned int>(site.user.size())); },
        timeoutMs_, code);
    if (list == nullptr)
    {
        if (code == kNullNoError && libssh2_userauth_authenticated(session_)) // the server accepted "none"
        {
            authenticated_ = true;
            Log(SshLogLevel::Normal, "Logged in without credentials (the server accepted 'none')");
            return true;
        }
        return FailFromSession("Cannot query the authentication methods", code == kNullNoError ? 0 : code,
                               SshMsg::AuthMethodsQueryFailed);
    }
    const std::string offered = list;
    const std::vector<std::string> methods = SplitMethods(list);
    Log(SshLogLevel::Normal, "Logging in as '" + site.user + "'; the server offers: " + offered);

    CAuthState state;
    const bool publicKeyOffered = Contains(methods, "publickey");
    auto done = [&] { return libssh2_userauth_authenticated(session_) != 0; };
    auto next = [&] { return !done() && !state.Stop() && !IsCancelled(); };

    if (site.useAgent && publicKeyOffered)
    {
        Log(SshLogLevel::Normal, "Trying the SSH agent");
        AuthAgent(site, state);
        LogAuthResult("SSH agent", done());
    }
    if (next() && !site.keyFile.empty() && publicKeyOffered)
    {
        Log(SshLogLevel::Normal, "Trying the key file '" + site.keyFile + "'");
        AuthKeyFile(site, state);
        LogAuthResult("Key file", done());
    }
    if (next() && !site.password.empty() && Contains(methods, "password"))
    {
        Log(SshLogLevel::Normal, "Trying a password");
        AuthPassword(site, state);
        LogAuthResult("Password", done());
    }
    if (next() && Contains(methods, "keyboard-interactive") && (!site.password.empty() || cb_ != nullptr))
    {
        Log(SshLogLevel::Normal, "Trying keyboard-interactive");
        AuthKeyboardInteractive(site, state);
        LogAuthResult("Keyboard-interactive", done());
    }

    if (done())
    {
        authenticated_ = true;
        Log(SshLogLevel::Normal, "Logged in");
        return true;
    }
    return FailAuth(site, offered, state);
}
