#include "precomp.h"

#include "interaction_ui.h"

#include "encoding.h"
#include "host_key_dialog.h"
#include "prompt_dialog.h"
#include "text_util.h"

namespace
{
HostKeyDecision AskHostKey(HWND parent, const CInteraction& in)
{
    const CNameCodec utf8 = CNameCodec::Utf8();
    CHostKeyDialog dialog(parent, utf8.Decode(FormatHostPort(in.host, in.port)), utf8.Decode(in.keyType),
                          utf8.Decode(in.fingerprint), in.changed);
    return dialog.Run();
}

// True: OK and answer filled in.
bool AskPrompt(HWND parent, const CInteraction& in, CSecret& answer)
{
    const CNameCodec utf8 = CNameCodec::Utf8();
    CPromptItem item;
    if (in.promptKind == PromptKind::KeyPassphrase)
        item.text = SPLFormatStringOwned(LangStr(IDS_PROMPT_PASSPHRASE).c_str(), utf8.Decode(in.promptText).c_str());
    else
        item.text = utf8.Decode(in.promptText); // the server's own text
    item.echo = in.echo;
    CPromptDialog dialog(parent, utf8.Decode(in.name), utf8.Decode(in.instruction), {item});
    std::vector<CSecret> answers;
    if (!dialog.Run(answers) || answers.size() != 1)
        return false;
    answer = std::move(answers[0]);
    return true;
}
} // namespace

void HandleInteraction(HWND parent, CPanelSession& session)
{
    CInteraction in;
    try
    {
        if (!session.TryGetInteraction(in))
            return;
    }
    catch (...)
    {
        return;
    }

    // The answer defaults to reject/cancel; it is delivered outside the dialog's try block.
    HostKeyDecision decision = HostKeyDecision::Reject;
    bool ok = false;
    CSecret answer;
    try
    {
        if (in.kind == CInteraction::Kind::HostKey)
            decision = AskHostKey(parent, in);
        else if (in.kind == CInteraction::Kind::Prompt)
            ok = AskPrompt(parent, in, answer);
    }
    catch (...)
    {
        decision = HostKeyDecision::Reject;
        ok = false;
        answer.clear();
    }

    try
    {
        if (in.kind == CInteraction::Kind::HostKey)
            session.AnswerHostKey(decision);
        else if (in.kind == CInteraction::Kind::Prompt)
            session.AnswerPrompt(ok, ok ? std::move(answer) : CSecret());
    }
    catch (...)
    {
        // last resort: the session thread must not stay blocked
        try
        {
            if (in.kind == CInteraction::Kind::HostKey)
                session.AnswerHostKey(HostKeyDecision::Reject);
            else if (in.kind == CInteraction::Kind::Prompt)
                session.AnswerPrompt(false, CSecret());
        }
        catch (...)
        {
        }
    }
}
