// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <vector>

static char* DupUploadDirectoryText(const char* text)
{
    if (text == NULL)
        return NULL;
    const int length = (int)strlen(text) + 1;
    char* copy = (char*)SalamanderGeneral->Alloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

// Temporary projection for the still-narrow operation dialog. Local filesystem work uses the
// UTF-16 owner stored beside it and never consumes this value.
//
// ****************************************************************************
// CFTPWorker
//

void CFTPWorker::HandleEventInWorkingState4(CFTPWorkerEvent event, BOOL& sendQuitCmd, BOOL& postActivate,
                                            BOOL& reportWorkerChange, std::string& buf, std::string& errBuf,
                                            int& cmdLen, BOOL& sendCmd, char* reply, int replySize,
                                            int replyCode, std::string& ftpPath, std::string& errText,
                                            BOOL& conClosedRetryItem, BOOL& lookForNewWork,
                                            BOOL& handleShouldStop, BOOL& quitCmdWasSent)
{
    const wchar_t* hostBuf = NULL;
    const wchar_t* userBuf = NULL;
    unsigned short portBuf;
    CFTPQueueItemCopyMoveUploadExplore* curItem = (CFTPQueueItemCopyMoveUploadExplore*)CurItem;
    CUploadListingItem* existingItem = NULL; // for passing listing item data between different SubStates
    while (1)
    {
        BOOL nextLoop = FALSE;
        switch (SubState)
        {
        case fwssWorkStartWork: // determine the state of the target directory
        {
            if (ShouldStop)
                handleShouldStop = TRUE; // check whether the worker should stop
            else
            {
                if (curItem->TgtDirState == UPLOADTGTDIRSTATE_UNKNOWN)
                {
                    Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                    CFTPServerPathType pathType = Oper->GetFTPServerPathType(curItem->TgtPath);
                    BOOL notAccessible, getListing, listingInProgress;
                    if (existingItem != NULL)
                        TRACE_E("CFTPWorker::HandleEventInWorkingState4(): unexpected situation: existingItem != NULL!");
                    if (UploadListingCache.GetListing(userBuf, hostBuf, portBuf, curItem->TgtPath,
                                                      pathType, Msg, UID, &listingInProgress,
                                                      &notAccessible, &getListing, curItem->TgtName,
                                                      &existingItem, NULL, TextPolicy.GetCodec()))
                    {
                        if (listingInProgress) // the listing is currently running or is about to start
                        {
                            if (getListing) // we should obtain the listing and then notify any other waiting workers
                            {
                                UploadDirGetTgtPathListing = TRUE;
                                postActivate = TRUE; // post an event to start downloading the listing
                            }
                            else
                            {
                                SubState = fwssWorkUploadWaitForListing; // we should wait until another worker finishes the listing
                                reportWorkerChange = TRUE;               // the worker displays the fwssWorkUploadWaitForListing state in the window, so it needs to be redrawn
                            }
                        }
                        else // the listing in the cache is ready or marked as "unobtainable"
                        {
                            if (notAccessible) // the listing is cached, but only as "unobtainable"
                            {
                                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UPLOADCANNOTLISTTGTPATH, 0, NULL, Oper);
                                lookForNewWork = TRUE;
                            }
                            else // the listing is available; check for a target directory name collision
                            {
                                BOOL nameValid = FTPMayBeValidNameComponent(curItem->TgtName, curItem->TgtPath, TRUE, pathType);
                                nextLoop = TRUE;
                                if (existingItem == NULL && nameValid) // no collision and the name is valid -> try to create the directory
                                    SubState = fwssWorkUploadCreateDir;
                                else
                                {                                                         // if existingItem == NULL then (!nameValid==TRUE), so there is no need to test for existingItem != NULL
                                    if (!nameValid || existingItem->ItemType == ulitFile) // invalid name or a collision with a file -> "dir cannot be created"
                                        SubState = !nameValid ? fwssWorkUploadCantCreateDirInvName : fwssWorkUploadCantCreateDirFileEx;
                                    else
                                    {
                                        if (existingItem->ItemType == ulitDirectory) // collision with a directory -> "dir already exists"
                                            SubState = fwssWorkUploadDirExists;
                                        else // (existingItem->ItemType == ulitLink): collision with a link -> find out whether the link targets a file or directory
                                            SubState = fwssWorkUploadResolveLink;
                                    }
                                }
                            }
                        }
                    }
                    else // out of memory
                    {
                        Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, 0, NULL, Oper);
                        lookForNewWork = TRUE;
                    }
                }
                else // the target directory is already prepared, proceed to list the uploaded directory on disk
                {
                    SubState = fwssWorkUploadGetTgtPath;
                    nextLoop = TRUE;
                }
            }
            break;
        }

        case fwssWorkUploadWaitForListing: // upload copy/move file: wait until another worker finishes listing the target path on the server (to detect collisions)
        {
            if (ShouldStop)
                handleShouldStop = TRUE; // check whether the worker should stop
            else
            {
                if (event == fweTgtPathListingFinished) // the designated worker has finished, try to use the new listing
                {
                    SubState = fwssWorkStartWork;
                    reportWorkerChange = TRUE; // the worker displays the fwssWorkUploadWaitForListing state in the window, so it needs to be redrawn
                    nextLoop = TRUE;
                }
            }
            break;
        }

        case fwssWorkUploadResolveLink: // upload copy/move file: determine what the link is (file/directory) whose name collides with the target directory on the server
        {
            if (!FTPFormatString(ftpPath, "%s", curItem->TgtPath))
            {
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
                break;
            }
            CFTPServerPathType type = Oper->GetFTPServerPathType(ftpPath.c_str());
            if (FTPPathAppend(type, ftpPath, curItem->TgtName, TRUE))
            { // we have the path, send CWD to the examined directory on the server
                if (FTPFormatString(errText, LoadStr(IDS_LOGMSGRESOLVINGLINK), ftpPath.c_str()))
                    Logs.LogMessage(LogUID, errText.c_str(), -1, TRUE);

                if (PrepareFTPCommand(buf, &errBuf, ftpcmdChangeWorkingPath, &cmdLen, ftpPath.c_str()))
                {
                    sendCmd = TRUE;
                    SubState = fwssWorkUploadResLnkWaitForCWDRes;
                }
                else
                {
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                    lookForNewWork = TRUE;
                }

                HaveWorkingPath = FALSE; // change the current working directory on the server
            }
            else // path syntax error or the path would become too long
            {
                // error on the item; record this state
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_INVALIDPATHTOLINK, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
            }
            break;
        }

        case fwssWorkUploadResLnkWaitForCWDRes: // upload copy/move file: wait for the "CWD" result (changing into the examined link; if it succeeds, the link is a directory)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS ||
                    FTP_DIGIT_1(replyCode) == FTP_D1_ERROR)
                {
                    if (ShouldStop)
                        handleShouldStop = TRUE; // check whether the worker should stop
                    else
                    {
                        nextLoop = TRUE;
                        if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS)  // success, the link points to a directory
                            SubState = fwssWorkUploadDirExistsDirLink; // optimization of fwssWorkUploadDirExists: CWD is already done, perform PWD immediately
                        else                                           // permanent error, the link most likely points to a file (but it could also be "550 Permission denied"; unfortunately 550 is also "550 Not a directory", so it is indistinguishable...)
                            SubState = fwssWorkUploadCantCreateDirFileEx;
                    }
                }
                else // an error occurred; show it to the user and move on to the next queue item
                {
                    CopyStr(errText, reply, replySize);
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UNABLETORESOLVELNK, NO_ERROR,
                                           DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                           Oper);
                    lookForNewWork = TRUE;
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadCreateDir: // upload copy/move file: create the target directory on the server - start by setting the target path
        {
            if (PrepareFTPCommand(buf, &errBuf, ftpcmdChangeWorkingPath, &cmdLen, curItem->TgtPath))
            {
                sendCmd = TRUE;
                SubState = fwssWorkUploadCrDirWaitForCWDRes;
            }
            else
            {
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
            }

            HaveWorkingPath = FALSE; // change the current working directory on the server
            break;
        }

        case fwssWorkUploadCrDirWaitForCWDRes: // upload copy/move file: wait for the "CWD" result (setting the target path)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS) // the target path is set, create the target directory
                {
                    if (ShouldStop)
                        handleShouldStop = TRUE; // check whether the worker should stop
                    else
                    {
                        if (FTPFormatString(errText, LoadStr(IDS_LOGMSGCREATEDIR), curItem->TgtName))
                            Logs.LogMessage(LogUID, errText.c_str(), -1, TRUE);

                        if (PrepareFTPCommand(buf, &errBuf, ftpcmdCreateDir, &cmdLen, curItem->TgtName))
                        {
                            sendCmd = TRUE;
                            SubState = fwssWorkUploadCrDirWaitForMKDRes;
                        }
                        else
                        {
                            Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                            lookForNewWork = TRUE;
                        }
                    }
                }
                else // an error occurred; show it to the user and move on to the next queue item
                {
                    CopyStr(errText, reply, replySize);
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UNABLETOCWDONLYPATH, NO_ERROR,
                                           DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                           Oper);
                    lookForNewWork = TRUE;
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadCrDirWaitForMKDRes: // upload copy/move file: wait for the "MKD" result (creating the target directory)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS) // the target directory was created (should be 257)
                {
                    Queue->UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_READY);

                    // if the target directory was created, update the listing cache
                    Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                    UploadListingCache.ReportCreateDirs(userBuf, hostBuf, portBuf, curItem->TgtPath,
                                                        Oper->GetFTPServerPathType(curItem->TgtPath),
                                                        curItem->TgtName, FALSE);

                    if (ShouldStop)
                        handleShouldStop = TRUE; // check whether the worker should stop
                    else
                    {
                        SubState = fwssWorkUploadGetTgtPath;
                        nextLoop = TRUE;
                    }
                }
                else // an error occurred; show it to the user and move on to the next queue item
                {
                    Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                    CFTPServerPathType pathType = Oper->GetFTPServerPathType(curItem->TgtPath);
                    if (UploadListingCache.IsListingFromPanel(userBuf, hostBuf, portBuf, curItem->TgtPath, pathType))
                    {
                        Queue->UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_UNKNOWN);
                        UploadListingCache.InvalidatePathListing(userBuf, hostBuf, portBuf, curItem->TgtPath, pathType);
                        Queue->UpdateItemState(CurItem, sqisWaiting, ITEMPR_OK, NO_ERROR, NULL, Oper); // at least this worker will look for new work, so some worker will certainly handle this item (no need to post "new work available")
                        lookForNewWork = TRUE;
                    }
                    else
                    {
                        CopyStr(errText, reply, replySize);
                        if (CurItem->ForceAction == fqiaUseAutorename) // forced autorename
                        {
                            if (ShouldStop)
                                handleShouldStop = TRUE; // check whether the worker should stop
                            else
                            {
                                SubState = fwssWorkUploadAutorenameDir;
                                nextLoop = TRUE;
                            }
                        }
                        else
                        {
                            switch (Oper->GetUploadCannotCreateDir())
                            {
                            case CANNOTCREATENAME_USERPROMPT:
                            {
                                Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_UPLOADCANNOTCREATETGTDIR, NO_ERROR,
                                                       DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                                       Oper);
                                lookForNewWork = TRUE;
                                break;
                            }

                            case CANNOTCREATENAME_SKIP:
                            {
                                Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_UPLOADCANNOTCREATETGTDIR, NO_ERROR,
                                                       DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                                       Oper);
                                lookForNewWork = TRUE;
                                break;
                            }

                            default: // case CANNOTCREATENAME_AUTORENAME:
                            {
                                if (ShouldStop)
                                    handleShouldStop = TRUE; // check whether the worker should stop
                                else
                                {
                                    SubState = fwssWorkUploadAutorenameDir;
                                    nextLoop = TRUE;
                                }
                                break;
                            }
                            }
                        }
                    }
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                // if we do not know whether the directory creation succeeded, invalidate the listing cache
                Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                UploadListingCache.ReportCreateDirs(userBuf, hostBuf, portBuf, curItem->TgtPath,
                                                    Oper->GetFTPServerPathType(curItem->TgtPath),
                                                    curItem->TgtName, TRUE);
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadCantCreateDirInvName: // upload copy/move file: handle the "target directory cannot be created" error (invalid name)
        case fwssWorkUploadCantCreateDirFileEx:  // upload copy/move file: handle the "target directory cannot be created" error (name already used for a file or link)
        {
            if (CurItem->ForceAction == fqiaUseAutorename) // forced autorename
            {
                SubState = fwssWorkUploadAutorenameDir;
                nextLoop = TRUE;
            }
            else
            {
                switch (Oper->GetUploadCannotCreateDir())
                {
                case CANNOTCREATENAME_USERPROMPT:
                {
                    Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_UPLOADCANNOTCREATETGTDIR,
                                           SubState == fwssWorkUploadCantCreateDirFileEx ? ERROR_ALREADY_EXISTS : NO_ERROR,
                                           NULL, Oper);
                    lookForNewWork = TRUE;
                    break;
                }

                case CANNOTCREATENAME_SKIP:
                {
                    Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_UPLOADCANNOTCREATETGTDIR,
                                           SubState == fwssWorkUploadCantCreateDirFileEx ? ERROR_ALREADY_EXISTS : NO_ERROR,
                                           NULL, Oper);
                    lookForNewWork = TRUE;
                    break;
                }

                default: // case CANNOTCREATENAME_AUTORENAME:
                {
                    SubState = fwssWorkUploadAutorenameDir;
                    nextLoop = TRUE;
                    break;
                }
                }
            }
            break;
        }

        case fwssWorkUploadDirExistsDirLink: // same state as fwssWorkUploadDirExists; additionally, CWD to the target directory has just been performed (testing whether the link is a directory or a file)
        case fwssWorkUploadDirExists:        // upload copy/move file: handle the "target directory already exists" error
        {
            if (CurItem->ForceAction == fqiaUseAutorename) // forced autorename
            {
                SubState = fwssWorkUploadAutorenameDir;
                nextLoop = TRUE;
            }
            else
            {
                if (CurItem->ForceAction == fqiaUseExistingDir) // forced use-existing-dir
                {
                    Queue->UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_READY);
                    Queue->UpdateForceAction(CurItem, fqiaNone); // the forced action no longer applies

                    SubState = SubState == fwssWorkUploadDirExistsDirLink ? fwssWorkUploadGetTgtPathSendPWD : fwssWorkUploadGetTgtPath;
                    nextLoop = TRUE;
                }
                else
                {
                    switch (Oper->GetUploadDirAlreadyExists())
                    {
                    case DIRALREADYEXISTS_USERPROMPT:
                    {
                        Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_UPLOADTGTDIRALREADYEXISTS,
                                               NO_ERROR, NULL, Oper);
                        lookForNewWork = TRUE;
                        break;
                    }

                    case DIRALREADYEXISTS_SKIP:
                    {
                        Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_UPLOADTGTDIRALREADYEXISTS,
                                               NO_ERROR, NULL, Oper);
                        lookForNewWork = TRUE;
                        break;
                    }

                    case DIRALREADYEXISTS_JOIN:
                    {
                        Queue->UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_READY);
                        SubState = SubState == fwssWorkUploadDirExistsDirLink ? fwssWorkUploadGetTgtPathSendPWD : fwssWorkUploadGetTgtPath;
                        nextLoop = TRUE;
                        break;
                    }

                    default: // case DIRALREADYEXISTS_AUTORENAME:
                    {
                        SubState = fwssWorkUploadAutorenameDir;
                        nextLoop = TRUE;
                        break;
                    }
                    }
                }
            }
            break;
        }

        case fwssWorkUploadAutorenameDir: // upload copy/move file: handle the target directory creation error - autorename - start by setting the target path
        {
            if (PrepareFTPCommand(buf, &errBuf, ftpcmdChangeWorkingPath, &cmdLen, curItem->TgtPath))
            {
                sendCmd = TRUE;
                SubState = fwssWorkUploadAutorenDirWaitForCWDRes;
            }
            else
            {
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
            }

            HaveWorkingPath = FALSE; // change the current working directory on the server
            break;
        }

        case fwssWorkUploadAutorenDirWaitForCWDRes: // upload copy/move file: autorename - wait for the "CWD" result (setting the target path)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS) // the target path is set, begin generating target directory names
                {
                    if (ShouldStop)
                        handleShouldStop = TRUE; // check whether the worker should stop
                    else
                    {
                        UploadAutorenamePhase = 0; // start of name generation
                        UploadAutorenameNewName.clear();
                        SubState = fwssWorkUploadAutorenDirSendMKD;
                        nextLoop = TRUE;
                    }
                }
                else // an error occurred; show it to the user and move on to the next queue item
                {
                    CopyStr(errText, reply, replySize);
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UNABLETOCWDONLYPATH, NO_ERROR,
                                           DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                           Oper);
                    lookForNewWork = TRUE;
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadAutorenDirSendMKD: // upload copy/move file: autorename - try to generate another new name for the target directory and attempt to create it
        {
            Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
            CFTPServerPathType pathType = Oper->GetFTPServerPathType(curItem->TgtPath);
            BOOL notAccessible, getListing, listingInProgress, nameExists;
            int index = 0;
            int usedUploadAutorenamePhase = UploadAutorenamePhase; // in case of a name collision - the phase in which we should try generating another name
            while (1)
            {
                if (!FTPGenerateNewName(TextPolicy.GetCodec(), &UploadAutorenamePhase, UploadAutorenameNewName, &index,
                                        curItem->TgtName, pathType, TRUE, FALSE))
                {
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, 0, NULL, Oper);
                    lookForNewWork = TRUE;
                    break;
                }
                // we have a new name; verify that it does not collide with any name from the target path listing
                if (UploadListingCache.GetListing(userBuf, hostBuf, portBuf, curItem->TgtPath,
                                                  pathType, Msg, UID, &listingInProgress,
                                                  &notAccessible, &getListing,
                                                  UploadAutorenameNewName.c_str(), NULL, &nameExists,
                                                  TextPolicy.GetCodec()))
                {
                    if (listingInProgress) // the listing is currently running or is about to start
                    {
                        if (getListing) // we should obtain the listing and then notify any other waiting workers
                        {
                            UploadDirGetTgtPathListing = TRUE;
                            SubState = fwssWorkStartWork;
                            postActivate = TRUE; // post an event to start downloading the listing
                        }
                        else
                        {
                            SubState = fwssWorkUploadWaitForListing; // we should wait until another worker finishes the listing
                            reportWorkerChange = TRUE;               // the worker displays the fwssWorkUploadWaitForListing state in the window, so it needs to be redrawn
                        }
                        break;
                    }
                    else // the listing in the cache is ready or marked as "unobtainable"
                    {
                        if (notAccessible) // the listing is cached, but only as "unobtainable" (highly unlikely, it was "ready" just moments ago)
                        {
                            Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UPLOADCANNOTLISTTGTPATH, 0, NULL, Oper);
                            lookForNewWork = TRUE;
                            break;
                        }
                        else // the listing is available; check for a target directory name collision
                        {
                            if (!nameExists) // no collision -> try to create the target directory
                            {
                                if (FTPFormatString(errText, LoadStr(IDS_LOGMSGCREATEDIR), UploadAutorenameNewName.c_str()))
                                    Logs.LogMessage(LogUID, errText.c_str(), -1, TRUE);

                                if (PrepareFTPCommand(buf, &errBuf, ftpcmdCreateDir, &cmdLen, UploadAutorenameNewName.c_str()))
                                {
                                    sendCmd = TRUE;
                                    SubState = fwssWorkUploadAutorenDirWaitForMKDRes;
                                }
                                else
                                {
                                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                                    lookForNewWork = TRUE;
                                }
                                break;
                            }
                            else // name collision (with a file/link/directory) - try another name in the same autorename phase
                                UploadAutorenamePhase = usedUploadAutorenamePhase;
                        }
                    }
                }
                else // out of memory
                {
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, 0, NULL, Oper);
                    lookForNewWork = TRUE;
                    break;
                }
            }
            break;
        }

        case fwssWorkUploadAutorenDirWaitForMKDRes: // upload copy/move file: autorename - wait for the "MKD" result (creating the target directory under the new name)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS) // the target directory was created (should be 257)
                {
                    // if the target directory was created, update the listing cache
                    Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                    UploadListingCache.ReportCreateDirs(userBuf, hostBuf, portBuf, curItem->TgtPath,
                                                        Oper->GetFTPServerPathType(curItem->TgtPath),
                                                        UploadAutorenameNewName.c_str(), FALSE);

                    char* newName = DupUploadDirectoryText(UploadAutorenameNewName.c_str());
                    if (newName != NULL)
                    {
                        if (CurItem->ForceAction != fqiaNone) // the forced action no longer applies
                            Queue->UpdateForceAction(CurItem, fqiaNone);

                        Queue->UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_READY);
                        Queue->UpdateTgtName(curItem, newName);
                        Oper->ReportItemChange(CurItem->UID); // request a redraw of the item

                        if (ShouldStop)
                            handleShouldStop = TRUE; // check whether the worker should stop
                        else
                        {
                            SubState = fwssWorkUploadGetTgtPath;
                            nextLoop = TRUE;
                        }
                    }
                    else
                    {
                        TRACE_E(LOW_MEMORY);
                        Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, 0, NULL, Oper);
                        lookForNewWork = TRUE;
                    }
                }
                else // an error occurred while creating the directory
                {
                    if (UploadAutorenamePhase != -1) // try to generate another name
                    {
                        if (ShouldStop)
                            handleShouldStop = TRUE; // check whether the worker should stop
                        else
                        {
                            SubState = fwssWorkUploadAutorenDirSendMKD;
                            nextLoop = TRUE;
                        }
                    }
                    else // no other name can be generated, so report an error
                    {
                        CopyStr(errText, reply, replySize);
                        Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UPLOADCRDIRAUTORENFAILED, NO_ERROR,
                                               DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                               Oper);
                        lookForNewWork = TRUE;
                    }
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                // if we do not know whether the directory creation succeeded, invalidate the listing cache
                Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                UploadListingCache.ReportCreateDirs(userBuf, hostBuf, portBuf, curItem->TgtPath,
                                                    Oper->GetFTPServerPathType(curItem->TgtPath),
                                                    UploadAutorenameNewName.c_str(), TRUE);
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadGetTgtPath: // upload copy/move file: determine the path to the target directory on the server - start by changing into it
        {
            if (!FTPFormatString(ftpPath, "%s", curItem->TgtPath))
            {
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
                break;
            }
            CFTPServerPathType type = Oper->GetFTPServerPathType(ftpPath.c_str());
            if (FTPPathAppend(type, ftpPath, curItem->TgtName, TRUE))
            { // we have the path, send CWD to the examined directory on the server
                if (PrepareFTPCommand(buf, &errBuf, ftpcmdChangeWorkingPath, &cmdLen, ftpPath.c_str()))
                {
                    sendCmd = TRUE;
                    SubState = fwssWorkUploadGetTgtPathWaitForCWDRes;
                }
                else
                {
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                    lookForNewWork = TRUE;
                }

                HaveWorkingPath = FALSE; // change the current working directory on the server
            }
            else // path syntax error or the path would become too long
            {
                // error on the item; record this state
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_INVALIDPATHTODIR, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
            }
            break;
        }

        case fwssWorkUploadGetTgtPathWaitForCWDRes: // upload copy/move file: wait for the "CWD" result (setting the path to the target directory)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS) // the path to the target directory is set; send PWD
                {
                    if (ShouldStop)
                        handleShouldStop = TRUE; // check whether the worker should stop
                    else
                    {
                        nextLoop = TRUE;
                        SubState = fwssWorkUploadGetTgtPathSendPWD;
                    }
                }
                else // an error occurred; show it to the user and move on to the next queue item
                {
                    Oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                    CFTPServerPathType pathType = Oper->GetFTPServerPathType(curItem->TgtPath);
                    if (UploadListingCache.IsListingFromPanel(userBuf, hostBuf, portBuf, curItem->TgtPath, pathType))
                    {
                        Queue->UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_UNKNOWN);
                        UploadListingCache.InvalidatePathListing(userBuf, hostBuf, portBuf, curItem->TgtPath, pathType);
                        Queue->UpdateItemState(CurItem, sqisWaiting, ITEMPR_OK, NO_ERROR, NULL, Oper); // at least this worker will look for new work, so some worker will certainly handle this item (no need to post "new work available")
                    }
                    else
                    {
                        CopyStr(errText, reply, replySize);
                        Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UNABLETOCWD, NO_ERROR,
                                               DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                               Oper);
                    }
                    lookForNewWork = TRUE;
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadGetTgtPathSendPWD: // upload copy/move file: send "PWD" (determine the path to the target directory)
        {
            if (PrepareFTPCommand(buf, &errBuf, ftpcmdPrintWorkingPath, &cmdLen))
            {
                sendCmd = TRUE;
                SubState = fwssWorkUploadGetTgtPathWaitForPWDRes;
            }
            else
            {
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
            }
            break;
        }

        case fwssWorkUploadGetTgtPathWaitForPWDRes: // upload copy/move file: wait for the "PWD" result (determining the path to the target directory)
        {
            switch (event)
            {
            // case fweCmdInfoReceived:  // we ignore "1xx" responses (they are only written to the log)
            case fweCmdReplyReceived:
            {
                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS &&
                    FTPGetDirectoryFromReply(reply, replySize, WorkingPath))
                { // success, we have the working path
                    HaveWorkingPath = TRUE;

                    if (ShouldStop)
                        handleShouldStop = TRUE; // check whether the worker should stop
                    else
                    {
                        nextLoop = TRUE;
                        SubState = fwssWorkUploadListDiskDir;
                    }
                }
                else // an error occurred; show it to the user and move on to the next queue item
                {
                    CopyStr(errText, reply, replySize);
                    Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_UNABLETOPWD, NO_ERROR,
                                           DupUploadDirectoryText(errText.c_str()) /* low memory = the error will be without details */,
                                           Oper);
                    lookForNewWork = TRUE;
                }
                break;
            }

            case fweCmdConClosed: // the connection was closed/timed out (see ErrorDescr) -> try to restore it
            {
                conClosedRetryItem = TRUE;
                break;
            }
            }
            break;
        }

        case fwssWorkUploadListDiskDir: // upload copy/move file: list the uploaded directory on disk
        {
            // try to list the source directory on disk
            if (DiskWorkIsUsed)
                TRACE_E("Unexpected situation in CFTPWorker::HandleEventInWorkingState4(): DiskWorkIsUsed may not be TRUE here!");
            const BOOL diskWorkReady = InitDiskWork(WORKER_DISKWORKLISTFINISHED, fdwtListDir, CurItem->LocalPath, CurItem->LocalName,
                                                    fqiaNone, FALSE, NULL, NULL, NULL, 0, NULL);
            if (diskWorkReady && FTPDiskThread->AddWork(&DiskWork))
            {
                DiskWorkIsUsed = TRUE;
                SubState = fwssWorkUploadListDiskWaitForDisk; // wait for the result
            }
            else // unable to list the source directory, cannot continue processing the item
            {
                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                lookForNewWork = TRUE;
            }
            break;
        }

        case fwssWorkUploadListDiskWaitForDisk:        // upload copy/move file: wait for the disk operation (directory listing) to finish
        case fwssWorkUploadListDiskWaitForDiskAftQuit: // upload copy/move file: after sending the "QUIT" command, wait for the disk operation (directory listing) to finish
        {
            if (event == fweWorkerShouldStop && ShouldStop) // we should terminate the worker
            {
                if (SubState != fwssWorkUploadListDiskWaitForDiskAftQuit && !SocketClosed)
                {
                    SubState = fwssWorkUploadListDiskWaitForDiskAftQuit; // prevent sending "QUIT" multiple times
                    sendQuitCmd = TRUE;                                  // we are finishing and the connection is open -> send the server the "QUIT" command (ignore the reply; it should lead to closing the connection and nothing else matters now)
                }
            }
            else
            {
                if (event == fweDiskWorkListFinished) // we have the result of the disk operation (directory listing)
                {
                    DiskWorkIsUsed = FALSE;
                    ReportWorkerMayBeClosed(); // report the completion of the worker's job (for other waiting threads)

                    // if QUIT has already been sent, prevent sending another QUIT from the new state
                    quitCmdWasSent = SubState == fwssWorkUploadListDiskWaitForDiskAftQuit;

                    if (DiskWork.State == sqisNone)
                    { // the directory listing succeeded
                        if (!HaveWorkingPath)
                            TRACE_E("Unexpected situation in CFTPWorker::HandleEventInWorkingState4(): HaveWorkingPath is FALSE!");
                        if (DiskWork.DiskListing == NULL)
                            TRACE_E("Unexpected situation in CFTPWorker::HandleEventInWorkingState4(): DiskWork.DiskListing is NULL!");

                        TIndirectArray<CFTPQueueItem>* ftpQueueItems = new TIndirectArray<CFTPQueueItem>(100, 500);
                        int transferMode = Oper->GetTransferMode();
                        BOOL copy = CurItem->Type == fqitUploadCopyExploreDir;
                        CQuadWord totalSize(0, 0); // total size (in bytes)
                        std::wstring sourcePathPrefixW = CurItem->LocalPath;
                        std::wstring sourceNameW = CurItem->LocalName;
                        std::wstring sourcePathW = sourcePathPrefixW;

                        BOOL err = ftpQueueItems == NULL || !HaveWorkingPath || DiskWork.DiskListing == NULL;
                        SPLSalPathAppendOwned(sourcePathW, sourceNameW.c_str());
                        if (!err) // add queue items for files/directories from the listing
                        {
                            BOOL ok = TRUE;
                            CFTPServerPathType workingPathType = Oper->GetFTPServerPathType(WorkingPath.c_str());
                            BOOL is_AS_400_QSYS_LIB_Path = workingPathType == ftpsptAS400 &&
                                                           FTPIsPrefixOfServerPath(ftpsptAS400, "/QSYS.LIB", WorkingPath.c_str());
                            int i;
                            for (i = 0; i < DiskWork.DiskListing->Count; i++)
                            {
                                CDiskListingItem* lstItem = DiskWork.DiskListing->At(i);

                                std::string listedNameBytes;
                                if (!TextPolicy.GetCodec().EncodeUploadName(lstItem->Name, wcslen(lstItem->Name), listedNameBytes))
                                {
                                    ok = FALSE;
                                    break;
                                }
                                if (is_AS_400_QSYS_LIB_Path)
                                    FTPAS400AddFileNamePart(listedNameBytes);

                                CFTPQueueItemType type;
                                CFTPQueueItem* item = CreateItemForCopyOrMoveUploadOperation(lstItem->Name, lstItem->IsDir,
                                                                                             &lstItem->Size, &type,
                                                                                             transferMode, Oper, copy,
                                                                                             WorkingPath.c_str(), listedNameBytes.c_str(), // we are in a subdirectory; names are no longer generated from the operation mask here
                                                                                             &totalSize, workingPathType == ftpsptOpenVMS);
                                if (item != NULL)
                                {
                                    if (ok)
                                    {
                                        item->SetLocalItem(-1, type, sqisWaiting, ITEMPR_OK, sourcePathW.data(), lstItem->Name);
                                        ftpQueueItems->Add(item); // add the operation to the queue
                                        if (!ftpQueueItems->IsGood())
                                        {
                                            ftpQueueItems->ResetState();
                                            ok = FALSE;
                                        }
                                    }
                                    if (!ok)
                                        delete item;
                                }
                                else
                                {
                                    TRACE_E(LOW_MEMORY);
                                    ok = FALSE;
                                }
                                // determine whether it makes sense to continue (assuming no error)
                                if (!ok)
                                {
                                    err = TRUE;
                                    break;
                                }
                            }
                        }
                        BOOL parentItemAdded = FALSE;       // TRUE = the last item in ftpQueueItems is the "parent" item (deleting the source directory)
                        int parentUID = CurItem->ParentUID; // parent UID for items created by expanding the directory
                        if (!err && !copy)                  // if needed, add an item to delete the source directory (runs after the entire directory contents are moved)
                        {
                            CFTPQueueItem* item = new CFTPQueueItemDir;
                            if (item != NULL && ((CFTPQueueItemDir*)item)->SetItemDir(0, 0, 0, 0))
                            {
                                item->SetLocalItem(CurItem->ParentUID, fqitUploadMoveDeleteDir, sqisWaiting,
                                                   ITEMPR_OK, CurItem->LocalPath, CurItem->LocalName);
                                ftpQueueItems->Add(item); // add the operation to the queue
                                if (!ftpQueueItems->IsGood())
                                {
                                    ftpQueueItems->ResetState();
                                    delete item;
                                    err = TRUE;
                                }
                                else
                                {
                                    parentItemAdded = TRUE;
                                    parentUID = item->UID;
                                }
                            }
                            else
                            {
                                if (item == NULL)
                                    TRACE_E(LOW_MEMORY);
                                else
                                    delete item;
                                err = TRUE;
                            }
                        }
                        if (!err)
                        {
                            int count = ftpQueueItems->Count - (parentItemAdded ? 1 : 0);
                            int childItemsNotDone = count;
                            int i;
                            for (i = 0; i < count; i++) // set the parent for items created by the explore
                            {
                                CFTPQueueItem* actItem = ftpQueueItems->At(i);
                                actItem->ParentUID = parentUID;
                            }

                            // if we add the "parent" item, set its Skipped+Failed+NotDone counters
                            if (parentItemAdded)
                            {
                                CFTPQueueItemDir* parentItem = (CFTPQueueItemDir*)(ftpQueueItems->At(ftpQueueItems->Count - 1)); // must be a descendant of CFTPQueueItemDir (every "parent" item has Skipped+Failed+NotDone counts)
                                parentItem->SetStateAndNotDoneSkippedFailed(childItemsNotDone, 0, 0, 0);
                                // at this point all new items are represented only by the "parent" item -> store the new
                                // NotDone count only for this item
                                childItemsNotDone = 1;
                            }

                            int curItemParent = CurItem->ParentUID;

                            // multiple operations on the data are in progress; others must wait until all of them finish,
                            // otherwise they would work with inconsistent data
                            Queue->LockForMoreOperations();

                            if (Queue->ReplaceItemWithListOfItems(CurItem->UID, ftpQueueItems->GetData(),
                                                                  ftpQueueItems->Count))
                            { // CurItem has already been deallocated; it was replaced with the ftpQueueItems list
                                CurItem = NULL;
                                ftpQueueItems->DetachMembers(); // the items are now in the queue; remove them from the array so they are not deallocated

                                // for the item/operation CurItem->ParentUID subtract one NotDone (for CurItem in the
                                // sqisProcessing state) and increase NotDone according to childItemsNotDone
                                childItemsNotDone--; // subtract one for CurItem
                                Oper->AddToItemOrOperationCounters(curItemParent, childItemsNotDone, 0, 0, 0, FALSE);

                                Queue->UnlockForMoreOperations();

                                // increase the total transferred data size by the size of the new items
                                Oper->AddToTotalSize(totalSize, TRUE);

                                Oper->ReportItemChange(-1); // request a redraw of all items

                                // this worker will need to look for additional work
                                State = fwsLookingForWork;                                 // no need to call Oper->OperationStatusMaybeChanged(); it does not change the operation state (it is not paused and will not be after this change)
                                SubState = quitCmdWasSent ? fwssLookFWQuitSent : fwssNone; // optionally pass to fwsLookingForWork that QUIT has already been sent
                                postActivate = TRUE;                                       // post an activation for the next worker state
                                reportWorkerChange = TRUE;

                                // notify all potentially sleeping workers that new work has appeared
                                HANDLES(LeaveCriticalSection(&WorkerCritSect));
                                // since we are already in CSocketsThread::CritSect, this call is also possible
                                // from within CSocket::SocketCritSect (no deadlock threat)
                                Oper->PostNewWorkAvailable(FALSE);
                                HANDLES(EnterCriticalSection(&WorkerCritSect));
                            }
                            else
                            {
                                err = TRUE; // out of memory -> write the error into the item
                                Queue->UnlockForMoreOperations();
                            }
                        }
                        if (err)
                        { // error on the item; record this state
                            Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                            lookForNewWork = TRUE; // quitCmdWasSent is already set
                        }
                        if (ftpQueueItems != NULL)
                            delete ftpQueueItems;
                    }
                    else // an error occurred while creating the directory or the item was skipped
                    {
                        Queue->UpdateItemState(CurItem, DiskWork.State, DiskWork.ProblemID, DiskWork.WinError, NULL, Oper);
                        lookForNewWork = TRUE; // quitCmdWasSent is already set
                    }
                    if (DiskWork.DiskListing != NULL) // if a listing exists, deallocate it here (it should exist only after a successful listing)
                    {
                        delete DiskWork.DiskListing;
                        DiskWork.DiskListing = NULL;
                    }
                }
            }
            break;
        }
        }
        if (!nextLoop)
            break;
    }
    if (existingItem != NULL)
        delete existingItem;
}
