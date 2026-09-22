// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <vector>

//
// ****************************************************************************
// CFTPWorker
//

void CFTPWorker::HandleEventInPreparingState(CFTPWorkerEvent event, BOOL& sendQuitCmd, BOOL& postActivate,
                                             BOOL& reportWorkerChange)
{
    if ((!DiskWorkIsUsed || event == fweWorkerShouldStop) && ShouldStop) // we should terminate the worker
    {
        if (SubState != fwssPrepQuitSent && SubState != fwssPrepWaitForDiskAfterQuitSent && !SocketClosed)
        {
            SubState = (SubState == fwssPrepWaitForDisk ? fwssPrepWaitForDiskAfterQuitSent : fwssPrepQuitSent); // so that we do not send "QUIT" more than once
            sendQuitCmd = TRUE;                                                                                 // we are supposed to finish and the connection is open -> send the server the "QUIT" command (we ignore the reply, it should lead to closing the connection and nothing else matters now)
        }
    }
    else // normal activity
    {
        // verify whether the item can be processed
        BOOL fail = FALSE;
        BOOL wait = FALSE;
        BOOL quitSent = FALSE;
        if (CurItem != NULL) // "always true"
        {
            switch (CurItem->Type)
            {
            case fqitCopyResolveLink: // copy: detect whether it is a link to a file or directory (object of class CFTPQueueItemCopyOrMove)
            case fqitMoveResolveLink: // move: detect whether it is a link to a file or directory (object of class CFTPQueueItemCopyOrMove)
                break;                // nothing to check

            case fqitCopyExploreDir:     // explore a directory or a link to a directory for copying (object of class CFTPQueueItemCopyMoveExplore)
            case fqitMoveExploreDir:     // explore a directory for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveExplore)
            case fqitMoveExploreDirLink: // explore a link to a directory for moving (deletes the directory link after completion) (object of class CFTPQueueItemCopyMoveExplore)
            {
                switch (SubState)
                {
                case fwssNone:
                {
                    if (((CFTPQueueItemCopyMoveExplore*)CurItem)->TgtDirState == TGTDIRSTATE_UNKNOWN)
                    {
                        // try to create the target directory on disk (also see whether it already exists)
                        if (DiskWorkIsUsed)
                            TRACE_E("Unexpected situation in CFTPWorker::HandleEventInPreparingState(): DiskWorkIsUsed may not be TRUE here!");
                        const BOOL diskWorkReady = InitDiskWork(WORKER_DISKWORKFINISHED, fdwtCreateDir,
                                                               ((CFTPQueueItemCopyMoveExplore*)CurItem)->LocalTgtPath,
                                                               ((CFTPQueueItemCopyMoveExplore*)CurItem)->LocalTgtName,
                                                               CurItem->ForceAction, FALSE, NULL, NULL, NULL, 0, NULL);
                        if (CurItem->ForceAction != fqiaNone) // the forced action stops being valid here
                            Queue->UpdateForceAction(CurItem, fqiaNone);
                        if (diskWorkReady && FTPDiskThread->AddWork(&DiskWork))
                        {
                            DiskWorkIsUsed = TRUE;
                            SubState = fwssPrepWaitForDisk; // wait for the result
                            wait = TRUE;
                        }
                        else // cannot create the directory, cannot continue processing the item
                        {
                            Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                        }
                    }
                    // else ; // nothing to do (the target directory exists)
                    break;
                }

                case fwssPrepWaitForDisk:
                case fwssPrepWaitForDiskAfterQuitSent:
                {
                    if (event == fweDiskWorkFinished) // we have the result of the disk operation (creating the target directory)
                    {
                        DiskWorkIsUsed = FALSE;
                        ReportWorkerMayBeClosed(); // announce the worker has finished (for other waiting threads)

                        // if we have already sent QUIT, prevent sending QUIT again from the new state
                        quitSent = SubState == fwssPrepWaitForDiskAfterQuitSent;

                        BOOL itemChange = FALSE;
                        if (DiskWork.NewTgtName != NULL)
                        {
                            BOOL nameUpdated = Queue->UpdateLocalTgtName((CFTPQueueItemCopyMoveExplore*)CurItem, DiskWork.NewTgtName);
                            free(DiskWork.NewTgtName);
                            DiskWork.NewTgtName = NULL;
                            if (nameUpdated)
                                itemChange = TRUE;
                            else
                            {
                                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                                itemChange = TRUE;
                                fail = TRUE;
                            }
                        }
                        if (DiskWork.State == sqisNone)
                        { // the directory was created (including autorename) or it already existed and we can use it
                            Queue->UpdateTgtDirState((CFTPQueueItemCopyMoveExplore*)CurItem, TGTDIRSTATE_READY);
                        }
                        else // an error occurred while creating the directory or the item was skipped
                        {
                            Queue->UpdateItemState(CurItem, DiskWork.State, DiskWork.ProblemID, DiskWork.WinError, NULL, Oper);
                            itemChange = TRUE;
                            fail = TRUE;
                        }
                        if (itemChange)
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                    }
                    else
                        wait = TRUE;
                    break;
                }
                }
                break;
            }

            case fqitUploadCopyExploreDir: // upload: explore directories for copying (object of class CFTPQueueItemCopyMoveUploadExplore)
            case fqitUploadMoveExploreDir: // upload: explore directories for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveUploadExplore)
            {
                if (CurItem->ForceAction == fqiaNone &&
                    ((CFTPQueueItemCopyMoveUploadExplore*)CurItem)->TgtDirState == UPLOADTGTDIRSTATE_UNKNOWN &&
                    !FTPMayBeValidNameComponent(((CFTPQueueItemCopyMoveUploadExplore*)CurItem)->TgtName,
                                                ((CFTPQueueItemCopyMoveUploadExplore*)CurItem)->TgtPath, TRUE,
                                                Oper->GetFTPServerPathType(((CFTPQueueItemCopyMoveUploadExplore*)CurItem)->TgtPath)))
                { // if the name does not follow the conventions for the given path type, the problem is "cannot create target directory"
                    switch (Oper->GetUploadCannotCreateDir())
                    {
                    case CANNOTCREATENAME_AUTORENAME:
                        break; // autorename (when it happens) must deal even with a bad name

                    case CANNOTCREATENAME_SKIP:
                    {
                        Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_UPLOADCANNOTCREATETGTDIR, 0, NULL, Oper);
                        Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                        fail = TRUE;
                        break;
                    }

                    default: // CANNOTCREATENAME_USERPROMPT
                    {
                        Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_UPLOADCANNOTCREATETGTDIR, 0, NULL, Oper);
                        Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                        fail = TRUE;
                        break;
                    }
                    }
                }
                // else ; // nothing to do (force actions are handled in the fwsWorking state, or the target directory already exists, or the directory name is OK)
                break;
            }

            case fqitDeleteExploreDir: // explore directories for delete (note: links to directories are deleted as a whole, the operation's purpose is fulfilled and nothing "extra" is removed) (object of class CFTPQueueItemDelExplore)
            case fqitDeleteLink:       // delete for a link (object of class CFTPQueueItemDel)
            case fqitDeleteFile:       // delete for a file (object of class CFTPQueueItemDel)
            {
                if (CurItem->Type == fqitDeleteExploreDir && ((CFTPQueueItemDelExplore*)CurItem)->IsHiddenDir || // if the directory/file/link is hidden, check what the user wants to do with it
                    (CurItem->Type == fqitDeleteLink || CurItem->Type == fqitDeleteFile) &&
                        ((CFTPQueueItemDel*)CurItem)->IsHiddenFile)
                {
                    int operationsHiddenFileDel;
                    int operationsHiddenDirDel;
                    Oper->GetParamsForDeleteOper(NULL, &operationsHiddenFileDel, &operationsHiddenDirDel);
                    if (CurItem->Type == fqitDeleteExploreDir)
                    {
                        switch (operationsHiddenDirDel)
                        {
                        case HIDDENDIRDEL_DELETEIT:
                            Queue->UpdateIsHiddenDir((CFTPQueueItemDelExplore*)CurItem, FALSE);
                            break;

                        case HIDDENDIRDEL_SKIP:
                        {
                            Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_DIRISHIDDEN, 0, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                            break;
                        }

                        default: // HIDDENDIRDEL_USERPROMPT
                        {
                            Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_DIRISHIDDEN, 0, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                            break;
                        }
                        }
                    }
                    else
                    {
                        switch (operationsHiddenFileDel)
                        {
                        case HIDDENFILEDEL_DELETEIT:
                            Queue->UpdateIsHiddenFile((CFTPQueueItemDel*)CurItem, FALSE);
                            break;

                        case HIDDENFILEDEL_SKIP:
                        {
                            Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_FILEISHIDDEN, 0, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                            break;
                        }

                        default: // HIDDENFILEDEL_USERPROMPT
                        {
                            Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_FILEISHIDDEN, 0, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                            break;
                        }
                        }
                    }
                }
            }

            case fqitDeleteDir:             // delete for a directory (object of class CFTPQueueItemDir)
            case fqitChAttrsExploreDir:     // explore directories for attribute changes (also adds an item for changing the directory attributes) (object of class CFTPQueueItemChAttrExplore)
            case fqitChAttrsResolveLink:    // attribute change: determine whether it is a link to a directory (object of class CFTPQueueItem)
            case fqitChAttrsExploreDirLink: // explore a link to a directory for attribute changes (object of class CFTPQueueItem)
                break;                      // nothing to verify

            case fqitCopyFileOrFileLink: // copying a file or a link to a file (object of class CFTPQueueItemCopyOrMove)
            case fqitMoveFileOrFileLink: // moving a file or a link to a file (object of class CFTPQueueItemCopyOrMove)
            {
                switch (SubState)
                {
                case fwssNone:
                {
                    if (((CFTPQueueItemCopyOrMove*)CurItem)->TgtFileState != TGTFILESTATE_TRANSFERRED)
                    {
                        // try to create/open the target file on disk
                        if (DiskWorkIsUsed)
                            TRACE_E("Unexpected situation 2 in CFTPWorker::HandleEventInPreparingState(): DiskWorkIsUsed may not be TRUE here!");
                        CFTPDiskWorkType type = fdwtCreateFile; // TGTFILESTATE_UNKNOWN
                        switch (((CFTPQueueItemCopyOrMove*)CurItem)->TgtFileState)
                        {
                        case TGTFILESTATE_CREATED:
                            type = fdwtRetryCreatedFile;
                            break;
                        case TGTFILESTATE_RESUMED:
                            type = fdwtRetryResumedFile;
                            break;
                        }
                        // "Is the target name ALREADY a renamed name?" - pre-unicode computed it as
                        // strcmp(CurItem->Name, TgtName) != 0, and widening replaced it with a
                        // hardcoded TRUE. This is the one call site of the seven that passed a
                        // computed value, and the flag drives StripAutorenameSuffix: a constant TRUE
                        // strips a trailing " (N)" from the FIRST attempt too, so downloading a
                        // server file genuinely named "Report (2).pdf" over a local file of the same
                        // name produced "Report (3).pdf" - which reads as the next revision of an
                        // unrelated "Report.pdf" family - instead of "Report (2) (2).pdf".
                        //
                        // The two names now live in different domains (Name is remote bytes,
                        // LocalTgtName is local UTF-16), so the source name is decoded with the
                        // session codec before the comparison. If either name is missing or will not
                        // decode, fall back to FALSE - the value the other six call sites pass, and
                        // the one that treats this as a first rename.
                        BOOL alreadyRenamedName = FALSE;
                        const wchar_t* localTgtName = ((CFTPQueueItemCopyOrMove*)CurItem)->LocalTgtName;
                        if (CurItem->Name != NULL && localTgtName != NULL)
                        {
                            std::wstring sourceNameW;
                            const CFtpTextCodec textCodec = TextPolicy.GetCodec();
                            if (textCodec.Decode(CurItem->Name, strlen(CurItem->Name), sourceNameW))
                                alreadyRenamedName = sourceNameW != localTgtName;
                        }
                        const BOOL diskWorkReady = InitDiskWork(WORKER_DISKWORKFINISHED, type,
                                                               ((CFTPQueueItemCopyOrMove*)CurItem)->LocalTgtPath,
                                                               ((CFTPQueueItemCopyOrMove*)CurItem)->LocalTgtName,
                                                               CurItem->ForceAction,
                                                               alreadyRenamedName,
                                                               NULL, NULL, NULL, 0, NULL);
                        if (CurItem->ForceAction != fqiaNone) // the forced action stops being valid here
                            Queue->UpdateForceAction(CurItem, fqiaNone);
                        if (diskWorkReady && FTPDiskThread->AddWork(&DiskWork))
                        {
                            DiskWorkIsUsed = TRUE;
                            SubState = fwssPrepWaitForDisk; // wait for the result
                            wait = TRUE;
                        }
                        else // cannot create/open the file, cannot continue processing the item
                        {
                            Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                        }
                    }
                    // else ; // nothing to do (the file is already downloaded)
                    break;
                }

                case fwssPrepWaitForDisk:
                case fwssPrepWaitForDiskAfterQuitSent:
                {
                    if (event == fweDiskWorkFinished) // we have the result of the disk operation (creating the target file)
                    {
                        DiskWorkIsUsed = FALSE;
                        ReportWorkerMayBeClosed(); // announce the worker has finished (for other waiting threads)

                        // if we have already sent QUIT, prevent sending QUIT again from the new state
                        quitSent = SubState == fwssPrepWaitForDiskAfterQuitSent;

                        BOOL itemChange = FALSE;
                        if (DiskWork.NewTgtName != NULL)
                        {
                            BOOL nameUpdated = Queue->UpdateLocalTgtName((CFTPQueueItemCopyOrMove*)CurItem, DiskWork.NewTgtName);
                            free(DiskWork.NewTgtName);
                            DiskWork.NewTgtName = NULL;
                            if (nameUpdated)
                                itemChange = TRUE;
                            else
                            {
                                Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                                itemChange = TRUE;
                                fail = TRUE;
                            }
                        }
                        if (DiskWork.State == sqisNone)
                        { // the file was created or opened successfully
                            if (OpenedFile != NULL)
                                TRACE_E("Unexpected situation in CFTPWorker::HandleEventInPreparingState(): OpenedFile is not NULL!");
                            OpenedFile = DiskWork.OpenedFile;
                            DiskWork.OpenedFile = NULL;
                            OpenedFileSize = DiskWork.FileSize;
                            OpenedFileOriginalSize = DiskWork.FileSize;
                            OpenedFileCurOffset.Set(0, 0);
                            OpenedFileResumedAtOffset.Set(0, 0);
                            ResumingOpenedFile = FALSE;
                            CanDeleteEmptyFile = DiskWork.CanDeleteEmptyFile;
                            Queue->UpdateTgtFileState((CFTPQueueItemCopyOrMove*)CurItem,
                                                      DiskWork.CanOverwrite ? TGTFILESTATE_CREATED : TGTFILESTATE_RESUMED);
                        }
                        else // an error occurred while creating/opening the file or the item was skipped
                        {
                            Queue->UpdateItemState(CurItem, DiskWork.State, DiskWork.ProblemID, DiskWork.WinError, NULL, Oper);
                            itemChange = TRUE;
                            fail = TRUE;
                        }
                        if (itemChange)
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                    }
                    else
                        wait = TRUE;
                    break;
                }
                }
                break;
            }

            case fqitUploadCopyFile: // upload: copying files (object of class CFTPQueueItemCopyOrMoveUpload)
            case fqitUploadMoveFile: // upload: moving files (object of class CFTPQueueItemCopyOrMoveUpload)
            {
                switch (SubState)
                {
                case fwssNone:
                {
                    if (CurItem->ForceAction != fqiaUseAutorename && CurItem->ForceAction != fqiaUploadForceAutorename &&
                        CurItem->ForceAction != fqiaUploadContinueAutorename &&
                        ((CFTPQueueItemCopyOrMoveUpload*)CurItem)->TgtFileState == UPLOADTGTFILESTATE_UNKNOWN &&
                        !FTPMayBeValidNameComponent(((CFTPQueueItemCopyOrMoveUpload*)CurItem)->TgtName,
                                                    ((CFTPQueueItemCopyOrMoveUpload*)CurItem)->TgtPath, FALSE,
                                                    Oper->GetFTPServerPathType(((CFTPQueueItemCopyOrMoveUpload*)CurItem)->TgtPath)))
                    { // if the name does not follow the conventions for the given path type, the problem is "cannot create target file"
                        switch (Oper->GetUploadCannotCreateFile())
                        {
                        case CANNOTCREATENAME_AUTORENAME:
                            break; // autorename (when it happens) must deal even with a bad name

                        case CANNOTCREATENAME_SKIP:
                        {
                            Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_UPLOADCANNOTCREATETGTFILE, 0, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                            break;
                        }

                        default: // CANNOTCREATENAME_USERPROMPT
                        {
                            Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_UPLOADCANNOTCREATETGTFILE, 0, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                            break;
                        }
                        }
                    }
                    if (!fail && ((CFTPQueueItemCopyOrMoveUpload*)CurItem)->TgtFileState != UPLOADTGTFILESTATE_TRANSFERRED)
                    { // open the source file on disk for reading
                        if (DiskWorkIsUsed)
                            TRACE_E("Unexpected situation 4 in CFTPWorker::HandleEventInPreparingState(): DiskWorkIsUsed may not be TRUE here!");
                        const BOOL diskWorkReady = InitDiskWork(WORKER_DISKWORKFINISHED, fdwtOpenFileForReading, CurItem->LocalPath, CurItem->LocalName,
                                                               fqiaNone, FALSE, NULL, NULL, NULL, 0, NULL);
                        if (diskWorkReady && FTPDiskThread->AddWork(&DiskWork))
                        {
                            DiskWorkIsUsed = TRUE;
                            SubState = fwssPrepWaitForDisk; // wait for the result
                            wait = TRUE;
                        }
                        else // cannot create/open the file, cannot continue processing the item
                        {
                            Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                        }
                    }
                    // else ; // nothing to do (the upload has already failed or the file is already uploaded)
                    break;
                }

                case fwssPrepWaitForDisk:
                case fwssPrepWaitForDiskAfterQuitSent:
                {
                    if (event == fweDiskWorkFinished) // we have the result of the disk operation (opening the source file)
                    {
                        DiskWorkIsUsed = FALSE;
                        ReportWorkerMayBeClosed(); // announce the worker has finished (for other waiting threads)

                        // if we have already sent QUIT, prevent sending QUIT again from the new state
                        quitSent = SubState == fwssPrepWaitForDiskAfterQuitSent;

                        if (DiskWork.State == sqisNone)
                        { // the file was opened successfully
                            if (OpenedInFile != NULL)
                                TRACE_E("Unexpected situation in CFTPWorker::HandleEventInPreparingState(): OpenedInFile is not NULL!");
                            OpenedInFile = DiskWork.OpenedFile;
                            DiskWork.OpenedFile = NULL;
                            OpenedInFileSize = DiskWork.FileSize;
                            OpenedInFileCurOffset.Set(0, 0);
                            OpenedInFileNumberOfEOLs.Set(0, 0);
                            OpenedInFileSizeWithCRLF_EOLs.Set(0, 0);
                            FileOnServerResumedAtOffset.Set(0, 0);
                            ResumingFileOnServer = FALSE;
                        }
                        else // an error occurred while opening the file
                        {
                            Queue->UpdateItemState(CurItem, DiskWork.State, DiskWork.ProblemID, DiskWork.WinError, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                        }
                    }
                    else
                        wait = TRUE;
                    break;
                }
                }
                break;
            }

            case fqitUploadMoveDeleteDir: // upload: delete a directory after its contents were moved (object of class CFTPQueueItemDir)
            {
                switch (SubState)
                {
                case fwssNone:
                {
                    // try to delete an empty source directory from disk
                    if (DiskWorkIsUsed)
                        TRACE_E("Unexpected situation 3 in CFTPWorker::HandleEventInPreparingState(): DiskWorkIsUsed may not be TRUE here!");
                    const BOOL diskWorkReady = InitDiskWork(WORKER_DISKWORKFINISHED, fdwtDeleteDir, CurItem->LocalPath, CurItem->LocalName,
                                                           fqiaNone, FALSE, NULL, NULL, NULL, 0, NULL);
                    if (diskWorkReady && FTPDiskThread->AddWork(&DiskWork))
                    {
                        DiskWorkIsUsed = TRUE;
                        SubState = fwssPrepWaitForDisk; // wait for the result
                        wait = TRUE;
                    }
                    else // cannot delete the directory, store the error in the item
                    {
                        Queue->UpdateItemState(CurItem, sqisFailed, ITEMPR_LOWMEM, NO_ERROR, NULL, Oper);
                        Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                        fail = TRUE;
                    }
                    break;
                }

                case fwssPrepWaitForDisk:
                case fwssPrepWaitForDiskAfterQuitSent:
                {
                    if (event == fweDiskWorkFinished) // we have the result of the disk operation (deleting the empty source directory)
                    {
                        DiskWorkIsUsed = FALSE;
                        ReportWorkerMayBeClosed(); // announce the worker has finished (for other waiting threads)

                        // if we have already sent QUIT, prevent sending QUIT again from the new state
                        quitSent = SubState == fwssPrepWaitForDiskAfterQuitSent;

                        if (DiskWork.State == sqisNone)
                        { // the directory was deleted successfully
                            Queue->UpdateItemState(CurItem, sqisDone, ITEMPR_OK, NO_ERROR, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item

                            // go find more work
                            CurItem = NULL;
                            State = fwsLookingForWork; // no need to call Oper->OperationStatusMaybeChanged(), the operation state does not change (it is not paused and will not be after this change)
                            if (quitSent)
                                SubState = fwssLookFWQuitSent; // hand over to fwsLookingForWork that QUIT was already sent
                            else
                                SubState = fwssNone;
                            postActivate = TRUE; // post an activation for the next worker state
                            reportWorkerChange = TRUE;
                            wait = TRUE; // skip state changes below this switch
                        }
                        else // an error occurred while deleting the directory
                        {
                            Queue->UpdateItemState(CurItem, DiskWork.State, DiskWork.ProblemID, DiskWork.WinError, NULL, Oper);
                            Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                            fail = TRUE;
                        }
                    }
                    else
                        wait = TRUE;
                    break;
                }
                }
                break;
            }

            case fqitMoveDeleteDir:     // deleting a directory after its contents were moved (object of class CFTPQueueItemDir)
            case fqitMoveDeleteDirLink: // deleting a link to a directory after its contents were moved (object of class CFTPQueueItemDir)
                break;                  // nothing to verify

            case fqitChAttrsFile: // change file attributes (note: attributes cannot be changed on links) (object of class CFTPQueueItemChAttr)
            case fqitChAttrsDir:  // change directory attributes (object of class CFTPQueueItemChAttrDir)
            {
                if (CurItem->Type == fqitChAttrsFile && ((CFTPQueueItemChAttr*)CurItem)->AttrErr || // respond to the error "an unknown attribute should be preserved, which we cannot do"
                    CurItem->Type == fqitChAttrsDir && ((CFTPQueueItemChAttrDir*)CurItem)->AttrErr)
                {
                    switch (Oper->GetUnknownAttrs())
                    {
                    case UNKNOWNATTRS_IGNORE:
                    {
                        if (CurItem->Type == fqitChAttrsFile)
                            Queue->UpdateAttrErr((CFTPQueueItemChAttr*)CurItem, FALSE);
                        else // fqitChAttrsDir
                            Queue->UpdateAttrErr((CFTPQueueItemChAttrDir*)CurItem, FALSE);
                        break;
                    }

                    case UNKNOWNATTRS_SKIP:
                    {
                        Queue->UpdateItemState(CurItem, sqisSkipped, ITEMPR_UNKNOWNATTRS, 0, NULL, Oper);
                        Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                        fail = TRUE;
                        break;
                    }

                    default: // UNKNOWNATTRS_USERPROMPT
                    {
                        Queue->UpdateItemState(CurItem, sqisUserInputNeeded, ITEMPR_UNKNOWNATTRS, 0, NULL, Oper);
                        Oper->ReportItemChange(CurItem->UID); // request a redraw of the item
                        fail = TRUE;
                        break;
                    }
                    }
                }
                break;
            }

            default:
            {
                TRACE_E("Unexpected situation in CFTPWorker::HandleEventInPreparingState(): unknown active operation item type!");
                break;
            }
            }
        }
        else
        {
            TRACE_E("Unexpected situation in CFTPWorker::HandleEventInPreparingState(): missing active operation item!");
            fail = TRUE;
        }

        if (!wait)
        {
            if (fail) // the item cannot be performed, it already has an error set, go find another item
            {
                CurItem = NULL;
                State = fwsLookingForWork; // no need to call Oper->OperationStatusMaybeChanged(), the operation state does not change (it is not paused and will not be after this change)
                if (quitSent)
                    SubState = fwssLookFWQuitSent; // hand over to fwsLookingForWork that QUIT was already sent
                else
                    SubState = fwssNone;
            }
            else // everything OK so far, continue
            {
                if (SocketClosed)
                {
                    State = fwsConnecting; // no need to call Oper->OperationStatusMaybeChanged(), the operation state does not change (it is not paused and will not be after this change)
                    SubState = fwssNone;   // the connection is not open, therefore we can ignore quitSent (there is nowhere to send QUIT)
                }
                else
                {
                    State = fwsWorking; // no need to call Oper->OperationStatusMaybeChanged(), the operation state does not change (it is not paused and will not be after this change)
                    if (UploadDirGetTgtPathListing)
                        TRACE_E("CFTPWorker::HandleEventInPreparingState(): UploadDirGetTgtPathListing==TRUE!");
                    StatusType = wstNone;
                    if (quitSent)
                        SubState = fwssWorkStopped; // hand over to fwsWorking that QUIT was already sent
                    else
                        SubState = fwssNone;
                }
            }
            postActivate = TRUE; // post an activation for the next worker state
            reportWorkerChange = TRUE;
        }
    }
}

void CFTPWorker::HandleEventInConnectingState(CFTPWorkerEvent event, BOOL& sendQuitCmd, BOOL& postActivate,
                                              BOOL& reportWorkerChange, std::string& buf, std::string& errBuf, std::wstring& host,
                                              int& cmdLen, BOOL& sendCmd, char* reply, int replySize,
                                              int replyCode, BOOL& operStatusMaybeChanged)
{
    BOOL run;
    do
    {
        run = FALSE;              // when changed to TRUE, the loop runs again immediately
        BOOL closeSocket = FALSE; // TRUE = the worker's socket should be closed
        auto failCommandAllocation = [&]()
        {
            SetLocalErrorDescr(LoadStr(IDS_OPERDOPPR_LOWMEM));
            CorrectErrorDescr();
            closeSocket = TRUE;
            State = fwsConnectionError;
            operStatusMaybeChanged = TRUE;
            ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
            SubState = fwssNone;
            postActivate = TRUE;
            reportWorkerChange = TRUE;
        };
        if (ShouldStop)           // we should terminate the worker (everything inside the loop due to 'closeSocket')
        {
            switch (SubState)
            {
                // case fwssNone:  // nothing needs to be done

                // case fwssConConnect:           // cannot occur (only an intermediate state)
                // case fwssConReconnect:         // cannot occur (only an intermediate state)
                // case fwssConSendNextScriptCmd: // cannot occur (only an intermediate state)
                // case fwssConSendInitCmds:      // cannot occur (only an intermediate state)
                // case fwssConSendSyst:          // cannot occur (only an intermediate state)

            case fwssConWaitingForIP: // delete the WORKER_CONTIMEOUTTIMID timer
            {
                // because we are already inside CSocketsThread::CritSect, this call is also possible
                // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                SocketsThread->DeleteTimer(UID, WORKER_CONTIMEOUTTIMID);
                break;
            }

            case fwssConWaitForConRes: // close the connection and delete the WORKER_CONTIMEOUTTIMID timer
            {
                // because we are already inside CSocketsThread::CritSect, this call is also possible
                // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                SocketsThread->DeleteTimer(UID, WORKER_CONTIMEOUTTIMID);
                closeSocket = TRUE;
                break;
            }

            case fwssConWaitForPrompt:       // close the connection and delete the WORKER_TIMEOUTTIMERID timer
            case fwssConWaitForScriptCmdRes: // close the connection and delete the WORKER_TIMEOUTTIMERID timer (it might no longer exist, but that does not matter)
            case fwssConWaitForOptsUtf8Res:  // close the connection and delete the WORKER_TIMEOUTTIMERID timer
            case fwssConWaitForInitCmdRes:   // close the connection and delete the WORKER_TIMEOUTTIMERID timer (it might no longer exist, but that does not matter)
            case fwssConWaitForSystRes:      // close the connection and delete the WORKER_TIMEOUTTIMERID timer (it might no longer exist, but that does not matter)
            {
                // because we are already inside CSocketsThread::CritSect, this call is also possible
                // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                SocketsThread->DeleteTimer(UID, WORKER_TIMEOUTTIMERID);
                closeSocket = TRUE;
                break;
            }
            }
        }
        else // normal activity
        {
            switch (SubState)
            {
            case fwssNone: // determine whether we need to obtain the IP address
            {
                if (ConnectAttemptNumber == 0) // for this worker it is the first attempt to establish a connection
                {
                    ConnectAttemptNumber = 1;
                }
                else
                {
                    if (ConnectAttemptNumber == 1) // first reconnect attempt (additional attempts are handled from fwssConReconnect)
                    {
                        if (ConnectAttemptNumber + 1 > Config.GetConnectRetries() + 1)
                        { // the second attempt to establish a connection (the first attempt = the connection that broke) is
                            // immediate (without waiting after the failure), we only wait between individual attempts
                            // to establish the connection
                            State = fwsConnectionError; // ATTENTION: assumes ErrorDescr is set
                            operStatusMaybeChanged = TRUE;
                            ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                            SubState = fwssNone;
                            postActivate = TRUE; // post an activation for the next worker state
                            reportWorkerChange = TRUE;
                            break; // end of executing the fwsConnecting state
                        }
                        else
                            ConnectAttemptNumber++;
                    }
                }

                // reset caches
                HaveWorkingPath = FALSE;
                CurrentTransferMode = ctrmUnknown;

                DWORD serverIP;
                BOOL hostReady;
                if (Oper->GetServerAddress(&serverIP, host, &hostReady)) // we have the IP
                {
                    SubState = fwssConConnect;
                    run = TRUE;
                }
                else // we only have a host name
                {
                    if (!hostReady)
                    {
                        failCommandAllocation();
                        break;
                    }
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    BOOL getHostByAddressRes = GetHostByAddress(host.c_str(), ++IPRequestUID);
                    RefreshCopiesOfUIDAndMsg(); // refresh the UID+Msg copies (they changed)
                    if (!getHostByAddressRes)
                    { // no chance of success -> report an error
                        SetFormattedLocalErrorDescr(IDS_WORKERGETIPERROR,
                                                    GetWorkerErrorTxt(NO_ERROR, errBuf));
                        CorrectErrorDescr();
                        SubState = fwssConReconnect;
                        run = TRUE;
                    }
                    else // wait until the server IP address arrives as fwseIPReceived, then obtain the IP from the operation again
                    {
                        // set a new timeout timer
                        int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
                        if (serverTimeout < 1000)
                            serverTimeout = 1000; // at least one second
                        // because we are already inside CSocketsThread::CritSect, this call is also possible
                        // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                        SocketsThread->AddTimer(Msg, UID, GetTickCount() + serverTimeout,
                                                WORKER_CONTIMEOUTTIMID, NULL); // ignore the error, at worst the user hits Stop

                        SubState = fwssConWaitingForIP;
                        // run = TRUE;  // pointless (no event has occurred yet)
                    }
                }
                break;
            }

            case fwssConWaitingForIP: // waiting for the IP address (translation from the host name)
            {
                switch (event)
                {
                case fweIPReceived:
                {
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    SocketsThread->DeleteTimer(UID, WORKER_CONTIMEOUTTIMID);
                    SubState = fwssConConnect;
                    run = TRUE;
                    break;
                }

                case fweIPRecFailure:
                {
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    SocketsThread->DeleteTimer(UID, WORKER_CONTIMEOUTTIMID);
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }

                case fweConTimeout:
                {
                    SetLocalErrorDescr(LoadStr(IDS_GETIPTIMEOUT));
                    CorrectErrorDescr();
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConConnect: // perform Connect()
            {
                if (ConnectAttemptNumber == 1) // connect
                {
                    if (Oper->GetConnectLogMsg(FALSE, buf, 0, NULL))
                        Logs.LogMessage(LogUID, buf.c_str(), -1, TRUE);
                }
                else // reconnect
                {
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    std::string dateText;
                    std::string timeText;
                    std::string timestamp;
                    if (!GetLocaleDateTimePart(st, TRUE, dateText) ||
                        !GetLocaleDateTimePart(st, FALSE, timeText) ||
                        !FTPFormatString(timestamp, "%s - %s", dateText.c_str(), timeText.c_str()))
                    {
                        failCommandAllocation();
                        break;
                    }
                    if (Oper->GetConnectLogMsg(TRUE, buf, ConnectAttemptNumber, timestamp.c_str()))
                        Logs.LogMessage(LogUID, buf.c_str(), -1);
                }

                ResetBuffersAndEvents(); // clear the buffers (discard old data) and initialize variables related to the connection
                TextPolicy.ResetForConnection();

                DWORD serverIP;
                unsigned short port;
                CFTPProxyServerType proxyType;
                DWORD hostIP;
                unsigned short hostPort;
                std::wstring proxyUser;
                std::wstring proxyPassword;
                if (!Oper->GetConnectInfo(&serverIP, &port, host, &proxyType, &hostIP, &hostPort,
                                          proxyUser, proxyPassword))
                {
                    SetLocalErrorDescr(LoadStr(IDS_PRXSCRERR_CANNOTENCODE));
                    SubState = fwssConReconnect;
                    break;
                }
                DWORD error;
                // because we are already inside CSocketsThread::CritSect, this call is also possible
                // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                BOOL conRes = ConnectWithProxy(serverIP, port, proxyType, &error, host.c_str(), hostPort,
                                               proxyUser.c_str(), proxyPassword.c_str(), hostIP);
                FTPSecureWipe(proxyPassword);
                RefreshCopiesOfUIDAndMsg(); // refresh the UID+Msg copies (they changed)
                Logs.SetIsConnected(LogUID, IsConnected());
                Logs.RefreshListOfLogsInLogsDlg();
                if (conRes)
                {
                    SocketClosed = FALSE; // the socket is open again

                    // set a new timeout timer
                    int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
                    if (serverTimeout < 1000)
                        serverTimeout = 1000; // at least one second
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    SocketsThread->AddTimer(Msg, UID, GetTickCount() + serverTimeout,
                                            WORKER_CONTIMEOUTTIMID, NULL); // ignore the error, at worst the user hits Stop

                    SubState = fwssConWaitForConRes;
                    // run = TRUE;  // pointless (no event has occurred yet)
                }
                else
                {
                    SetFormattedLocalErrorDescr(IDS_WORKEROPENCONERR,
                                                GetWorkerErrorTxt(error, errBuf));
                    CorrectErrorDescr();
                    SubState = fwssConReconnect;
                    run = TRUE;
                }
                break;
            }

            case fwssConWaitForConRes: // wait for the result of Connect()
            {
                switch (event)
                {
                case fweConnected:
                {
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    SocketsThread->DeleteTimer(UID, WORKER_CONTIMEOUTTIMID);

                    // set the worker state so it starts waiting for a command reply (even if we did not
                    // send any command, we wait for the server response) - set the timeout for receiving a command reply
                    CommandState = fwcsWaitForLoginPrompt;
                    int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
                    if (serverTimeout < 1000)
                        serverTimeout = 1000; // at least one second
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    SocketsThread->AddTimer(Msg, UID, GetTickCount() + serverTimeout,
                                            WORKER_TIMEOUTTIMERID, NULL); // ignore the error, at worst the user hits Stop

                    SubState = fwssConWaitForPrompt;
                    // run = TRUE; // pointless (no event has occurred yet)
                    break;
                }

                case fweConnectFailure:
                {
                    // because we are already inside CSocketsThread::CritSect, this call is also possible
                    // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    SocketsThread->DeleteTimer(UID, WORKER_CONTIMEOUTTIMID);
                    SubState = fwssConReconnect;
                    run = TRUE;
                    closeSocket = TRUE;
                    break;
                }

                case fweConTimeout:
                {
                    // because we are already inside CSocket::SocketCritSect, this call is also possible
                    // from within CFTPWorker::WorkerCritSect (no risk of dead-lock)
                    if (GetProxyTimeoutDescr(errBuf))
                        SetLocalErrorDescr(errBuf);
                    else
                        SetLocalErrorDescr(LoadStr(IDS_OPENCONTIMEOUT));
                    CorrectErrorDescr();
                    SubState = fwssConReconnect;
                    run = TRUE;
                    closeSocket = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConWaitForPrompt: // waiting for the login prompt from the server
            {
                switch (event)
                {
                // case fweCmdInfoReceived:  // ignore "1xx" replies (they are only written to the Log)
                case fweCmdReplyReceived:
                {
                    if (replyCode != -1)
                    {
                        if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS &&
                            FTP_DIGIT_2(replyCode) == FTP_D2_CONNECTION) // e.g. 220 - Service ready for new user
                        {
                            if (!Oper->SetServerFirstReply(reply, replySize))
                            {
                                failCommandAllocation();
                                break;
                            }

                            ProxyScriptExecPoint = NULL; // start sending from the first login script command again
                            ProxyScriptLastCmdReply = -1;
                            SubState = Oper->GetEncryptControlConnection() ? fwssConSendAUTH : fwssConSendNextScriptCmd; // we will send commands from the login script
                            run = TRUE;
                        }
                        else
                        {
                            if (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR ||
                                FTP_DIGIT_1(replyCode) == FTP_D1_ERROR) // e.g. 421 Service not available, closing control connection
                            {
                                CopyStr(errBuf, reply, replySize);
                                SetServerErrorDescr(errBuf);
                                CorrectErrorDescr();
                                closeSocket = TRUE; // close the connection (no point in continuing)

                                SubState = fwssConReconnect;
                                run = TRUE;
                            }
                            else // unexpected response, ignore it
                            {
                                CopyStr(errBuf, reply, replySize);
                                TRACE_E("Unexpected reply: " << errBuf.c_str());
                            }
                        }
                    }
                    else // not an FTP server
                    {
                        CopyStr(errBuf, reply, replySize);
                        SetFormattedServerErrorDescr(IDS_NOTFTPSERVERERROR, errBuf);
                        CorrectErrorDescr();
                        closeSocket = TRUE; // close the connection (no point in continuing)

                        State = fwsConnectionError;
                        operStatusMaybeChanged = TRUE;
                        ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                        SubState = fwssNone;
                        postActivate = TRUE; // post an activation for the next worker state
                        reportWorkerChange = TRUE;
                    }
                    break;
                }

                case fweCmdConClosed:
                {
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConSendAUTH: // Initiate TLS
            {
                if (FTPFormatString(buf, "AUTH TLS\r\n") && FTPFormatString(errBuf, "%s", buf.c_str()))
                {
                    sendCmd = TRUE;
                    cmdLen = static_cast<int>(buf.size());
                    if (pCertificate)
                        pCertificate->Release();
                    pCertificate = Oper->GetCertificate();
                    SubState = fwssConWaitForAUTHCmdRes;
                }
                else
                    failCommandAllocation();
                break;
            }

            case fwssConSendPBSZ: // After AUTH TLS, but only if encrypting also data
            {
                if (FTPFormatString(buf, "PBSZ 0\r\n") && FTPFormatString(errBuf, "%s", buf.c_str()))
                {
                    sendCmd = TRUE;
                    cmdLen = static_cast<int>(buf.size());
                    SubState = fwssConWaitForPBSZCmdRes;
                }
                else
                    failCommandAllocation();
                break;
            }

            case fwssConSendPROT: // After PBSZ
            {
                if (FTPFormatString(buf, "PROT P\r\n") && FTPFormatString(errBuf, "%s", buf.c_str()))
                {
                    sendCmd = TRUE;
                    cmdLen = static_cast<int>(buf.size());
                    SubState = fwssConWaitForPROTCmdRes;
                }
                else
                    failCommandAllocation();
                break;
            }

            case fwssConSendMODEZ: // Init compression
            {
                if (FTPFormatString(buf, "MODE Z\r\n") && FTPFormatString(errBuf, "%s", buf.c_str()))
                {
                    sendCmd = TRUE;
                    cmdLen = static_cast<int>(buf.size());
                    SubState = fwssConWaitForMODEZCmdRes;
                }
                else
                    failCommandAllocation();
                break;
            }

            case fwssConSendNextScriptCmd: // send the next login script command
            {
                BOOL fail = FALSE;
                std::string errorDescription;
                BOOL needUserInput;
                if (Oper->PrepareNextScriptCmd(buf, errBuf, &cmdLen,
                                               &ProxyScriptExecPoint, ProxyScriptLastCmdReply,
                                               errorDescription, &needUserInput) &&
                    !needUserInput)
                {
                    if (buf.empty()) // end of the script
                    {
                        if (ProxyScriptLastCmdReply == -1) // the script does not contain any command that would be sent to the server - e.g. commands were skipped because they contain optional variables
                        {
                            SetLocalErrorDescr(LoadStr(IDS_INCOMPLETEPRXSCR2));
                            CorrectErrorDescr();
                            fail = TRUE;
                        }
                        else
                        {
                            if (FTP_DIGIT_1(ProxyScriptLastCmdReply) == FTP_D1_SUCCESS) // e.g. 230 User logged in, proceed
                            {
                                Logs.LogMessage(LogUID, LangStr(IDS_LOGMSGLOGINSUCCESS).c_str(), -1, TRUE);
                                NextInitCmd = 0; // send the first init-ftp command
                                SubState = fwssConSendOptsUtf8;
                                run = TRUE;
                            }
                            else // FTP_DIGIT_1(ProxyScriptLastCmdReply) == FTP_D1_PARTIALSUCCESS  // e.g. 331 User name okay, need password
                            {    // assumes we got here from fwssConWaitForScriptCmdRes and reply+replySize is still valid
                                CopyStr(errBuf, reply, replySize);
                                SetFormattedServerErrorDescr(IDS_INCOMPLETEPRXSCR3, errBuf);
                                CorrectErrorDescr();
                                fail = TRUE;
                            }
                        }
                    }
                    else // there is another command to send
                    {
                        sendCmd = TRUE;
                        SubState = fwssConWaitForScriptCmdRes;
                        // run = TRUE; // pointless (no event has occurred yet)
                    }
                }
                else // script error or missing variable value
                {
                    if (needUserInput)
                        SetLocalErrorDescr(errorDescription);
                    else
                        SetFormattedLocalErrorDescr(IDS_ERRINPROXYSCRIPT, errorDescription);
                    CorrectErrorDescr();
                    fail = TRUE;
                }
                if (fail)
                {
                    closeSocket = TRUE; // close the connection (no point in continuing)

                    State = fwsConnectionError; // NOTE: ErrorDescr is set above (see "fail = TRUE")
                    operStatusMaybeChanged = TRUE;
                    ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                    SubState = fwssNone;
                    postActivate = TRUE; // post an activation for the next worker state
                    reportWorkerChange = TRUE;
                }
                break;
            }

            case fwssConWaitForAUTHCmdRes:
            case fwssConWaitForPBSZCmdRes:
            case fwssConWaitForPROTCmdRes:
            {
                switch (event)
                {
                case fweCmdReplyReceived:
                {
                    BOOL failed = FALSE;
                    BOOL retryLoginWithoutAsking = FALSE;
                    if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS)
                    {
                        switch (SubState)
                        {
                        case fwssConWaitForAUTHCmdRes:
                        {
                            int errID;
                            if (InitSSL(LogUID, &errID))
                            {
                                int err;
                                std::string sslError;
                                HANDLES(LeaveCriticalSection(&WorkerCritSect));
                                CCertificate* unverifiedCert;
                                BOOL ret = EncryptSocket(LogUID, &err, &unverifiedCert, &errID, &sslError,
                                                         NULL /* for the control connection this is always NULL */);
                                HANDLES(EnterCriticalSection(&WorkerCritSect));
                                if (ret)
                                {
                                    if (unverifiedCert != NULL) // close the connection and retry only after the user learns about the untrusted certificate and accepts it or ensures it becomes trusted (in the Solve Error dialog)
                                    {
                                        if (UnverifiedCertificate != NULL)
                                            UnverifiedCertificate->Release();
                                        UnverifiedCertificate = unverifiedCert;
                                        SetLocalErrorDescr(LoadStr(IDS_SSLNEWUNVERIFIEDCERT));
                                        failed = TRUE;
                                    }
                                    else
                                    {
                                        SubState = Oper->GetEncryptDataConnection() ? fwssConSendPBSZ : fwssConSendNextScriptCmd;
                                        ProxyScriptLastCmdReply = replyCode;
                                        run = TRUE;
                                    }
                                }
                                else
                                {
                                    if (sslError.empty())
                                        SetLocalErrorDescr(LoadStr(errID));
                                    else
                                        SetFormattedLocalErrorDescr(errID, sslError);
                                    failed = TRUE;
                                    retryLoginWithoutAsking = err == SSLCONERR_CANRETRY;
                                }
                            }
                            else
                            {
                                SetLocalErrorDescr(LoadStr(errID));
                                failed = TRUE;
                            }
                            break;
                        }
                        case fwssConWaitForPBSZCmdRes:
                            SubState = fwssConSendPROT;
                            ProxyScriptLastCmdReply = replyCode;
                            run = TRUE;
                            break;
                        case fwssConWaitForPROTCmdRes:
                            SubState = fwssConSendNextScriptCmd;
                            ProxyScriptLastCmdReply = replyCode;
                            run = TRUE;
                            break;
                        }
                    }
                    else
                    {
                        SetLocalErrorDescr(LoadStr(SubState == fwssConWaitForAUTHCmdRes ? IDS_SSL_ERR_CONTRENCUNSUP : IDS_SSL_ERR_DATAENCUNSUP));
                        failed = TRUE;
                    }
                    if (failed)
                    {
                        CorrectErrorDescr();

                        closeSocket = TRUE; // close the connection (no point in continuing)

                        if (retryLoginWithoutAsking) // try again
                        {
                            SubState = fwssConReconnect;
                            run = TRUE;
                        }
                        else // report the error to the user
                        {
                            State = fwsConnectionError;
                            operStatusMaybeChanged = TRUE;
                            ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                            SubState = fwssNone;
                            postActivate = TRUE; // post an activation for the next worker state
                            reportWorkerChange = TRUE;
                        }
                    }
                    break;
                }
                case fweCmdConClosed:
                {
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConSendOptsUtf8:
            {
                if (FTPFormatString(buf, "OPTS UTF8 ON\r\n") && FTPFormatString(errBuf, "%s", buf.c_str()))
                {
                    sendCmd = TRUE;
                    cmdLen = static_cast<int>(buf.size());
                    SubState = fwssConWaitForOptsUtf8Res;
                }
                else
                    failCommandAllocation();
                break;
            }

            case fwssConWaitForOptsUtf8Res:
            {
                switch (event)
                {
                case fweCmdReplyReceived:
                    TextPolicy.ApplyUtf8OptionsReply(replyCode);
                    SubState = Oper->GetCompressData() ? fwssConSendMODEZ : fwssConSendInitCmds;
                    run = TRUE;
                    break;

                case fweCmdConClosed:
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                break;
            }

            case fwssConWaitForMODEZCmdRes:
            {
                switch (event)
                {
                case fweCmdReplyReceived:
                {
                    if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS)
                    {
                        SubState = fwssConSendInitCmds;
                        ProxyScriptLastCmdReply = replyCode;
                        run = TRUE;
                    }
                    else
                    {
                        // Server does not support compression -> swallow the error, disable compression and go on
                        // NOTE: Probably cannot happen because CompresData was set to FALSE in main connection
                        replyCode = 200; // Emulate Full success
                        Oper->SetCompressData(FALSE);
                        Logs.LogMessage(LogUID, LangStr(IDS_MODEZ_LOG_UNSUPBYSERVER).c_str(), -1);
                    }
                    break;
                }
                case fweCmdConClosed:
                {
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConWaitForScriptCmdRes: // waiting for the result of a login script command
            {
                switch (event)
                {
                // case fweCmdInfoReceived:  // ignore "1xx" replies (they are only written to the Log)
                case fweCmdReplyReceived:
                {
                    if (replyCode != -1)
                    {
                        if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS ||      // e.g. 230 User logged in, proceed
                            FTP_DIGIT_1(replyCode) == FTP_D1_PARTIALSUCCESS) // e.g. 331 User name okay, need password
                        {
                            SubState = fwssConSendNextScriptCmd;
                            ProxyScriptLastCmdReply = replyCode;
                            run = TRUE;
                        }
                        else
                        {
                            if (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR || // e.g. 421 Service not available (too many users), closing control connection
                                FTP_DIGIT_1(replyCode) == FTP_D1_ERROR)            // e.g. 530 Not logged in (invalid password)
                            {
                                BOOL retryLoginWithoutAsking;
                                if (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR)
                                { // convenient handling of the "too many users" error - no questions, immediately retry
                                    // this may be a problem: this code is accompanied by a message that requires changing the user/password
                                    retryLoginWithoutAsking = TRUE;
                                }
                                else
                                    retryLoginWithoutAsking = Oper->GetRetryLoginWithoutAsking();

                                CopyStr(errBuf, reply, replySize);
                                SetFormattedServerErrorDescr(IDS_WORKERLOGINERR, errBuf);
                                CorrectErrorDescr();
                                closeSocket = TRUE; // close the connection (no point in continuing)

                                if (retryLoginWithoutAsking) // try again
                                {
                                    SubState = fwssConReconnect;
                                    run = TRUE;
                                }
                                else // report the error to the user and wait until they change the password/account
                                {
                                    State = fwsConnectionError;
                                    operStatusMaybeChanged = TRUE;
                                    ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                                    SubState = fwssNone;
                                    postActivate = TRUE; // post an activation for the next worker state
                                    reportWorkerChange = TRUE;
                                }
                            }
                            else // unexpected response, ignore it
                            {
                                CopyStr(errBuf, reply, replySize);
                                TRACE_E("Unexpected reply: " << errBuf.c_str());
                            }
                        }
                    }
                    else // not an FTP server
                    {
                        CopyStr(errBuf, reply, replySize);
                        SetFormattedServerErrorDescr(IDS_NOTFTPSERVERERROR, errBuf);
                        CorrectErrorDescr();
                        closeSocket = TRUE; // close the connection (no point in continuing)

                        State = fwsConnectionError;
                        operStatusMaybeChanged = TRUE;
                        ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                        SubState = fwssNone;
                        postActivate = TRUE; // post an activation for the next worker state
                        reportWorkerChange = TRUE;
                    }
                    break;
                }

                case fweCmdConClosed:
                {
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConSendInitCmds: // send initialization commands (user-defined, see CFTPOperation::InitFTPCommands)
            {
                if (!Oper->GetInitFTPCommands(buf))
                {
                    failCommandAllocation();
                    break;
                }
                if (!buf.empty())
                {
                    char* next = buf.data();
                    char* s;
                    int i = 0;
                    BOOL selectedCommand = FALSE;
                    while (GetToken(&s, &next))
                    {
                        if (*s != 0 && *s <= ' ')
                            s++;     // remove only the first space (so commands can still start with spaces)
                        if (*s != 0) // if there is anything, send it to the server
                        {
                            if (i++ == NextInitCmd)
                            {
                                selectedCommand = TRUE;
                                NextInitCmd++; // take the next command next time (very inefficient, but only a few commands are expected here)
                                sendCmd = FTPFormatString(buf, "%s\r\n", s) &&
                                          FTPFormatString(errBuf, "%s", buf.c_str());
                                if (sendCmd)
                                    cmdLen = static_cast<int>(buf.size());
                                else
                                    failCommandAllocation();
                                break;
                            }
                        }
                    }
                    if (selectedCommand && !sendCmd)
                        break;
                }
                if (sendCmd) // sending another init-ftp command
                {
                    SubState = fwssConWaitForInitCmdRes;
                    // run = TRUE; // pointless (no event has occurred yet)
                }
                else // all init-ftp commands have been sent (or none exist)
                {
                    SubState = fwssConSendSyst; // determine the server system
                    run = TRUE;
                }
                break;
            }

            case fwssConWaitForInitCmdRes: // waiting for the result of the initialization command
            {
                switch (event)
                {
                // case fweCmdInfoReceived:  // ignore "1xx" replies (they are only written to the Log)
                case fweCmdReplyReceived:
                {
                    SubState = fwssConSendInitCmds; // send another init-ftp command (we do not care about the server replies)
                    run = TRUE;
                    break;
                }

                case fweCmdConClosed:
                {
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConSendSyst: // determine the server system (send the "SYST" command)
            {
                if (PrepareFTPCommand(buf, &errBuf, ftpcmdSystem, &cmdLen))
                {
                    sendCmd = TRUE;
                    SubState = fwssConWaitForSystRes;
                }
                else
                    failCommandAllocation();
                // run = TRUE; // pointless (no event has occurred yet)
                break;
            }

            case fwssConWaitForSystRes: // waiting for the server system (result of the "SYST" command)
            {
                switch (event)
                {
                // case fweCmdInfoReceived:  // ignore "1xx" replies (they are only written to the Log)
                case fweCmdReplyReceived:
                {
                    if (!Oper->SetServerSystem(reply, replySize))
                    {
                        failCommandAllocation();
                        break;
                    }

                    // the connection is established, start working
                    State = fwsWorking; // no need to call Oper->OperationStatusMaybeChanged(), the operation state does not change (it is not paused and will not be after this change)
                    if (UploadDirGetTgtPathListing)
                        TRACE_E("CFTPWorker::HandleEventInPreparingState(): UploadDirGetTgtPathListing==TRUE!");
                    SubState = fwssNone;
                    StatusType = wstNone;
                    postActivate = TRUE;      // post an activation for the next worker state
                    ConnectAttemptNumber = 1; // the connection is established, reset to one so the next reconnect attempt is ready again
                    ErrorDescr.clear();       // start collecting error messages again
                    reportWorkerChange = TRUE;
                    break;
                }

                case fweCmdConClosed:
                {
                    SubState = fwssConReconnect;
                    run = TRUE;
                    break;
                }
                }
                break;
            }

            case fwssConReconnect: // decide whether to perform a reconnect or report a worker error
            {
                if (ConnectAttemptNumber + 1 > Config.GetConnectRetries() + 1)
                {
                    State = fwsConnectionError; // NOTE: assumes ErrorDescr is set
                    operStatusMaybeChanged = TRUE;
                    ErrorOccurenceTime = Oper->GiveLastErrorOccurenceTime();
                    SubState = fwssNone;
                    postActivate = TRUE; // post an activation for the next worker state
                    reportWorkerChange = TRUE;
                }
                else
                {
                    // try to find a "sleeping" worker with an open connection to hand over the work to (instead of reconnecting unnecessarily)
                    HANDLES(LeaveCriticalSection(&WorkerCritSect));
                    BOOL workMoved = Oper->GiveWorkToSleepingConWorker(this);
                    HANDLES(EnterCriticalSection(&WorkerCritSect));

                    if (!workMoved)
                    {
                        // wait for a reconnect
                        ConnectAttemptNumber++;

                        // because we are already inside CSocketsThread::CritSect, this call is also possible
                        // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                        SocketsThread->DeleteTimer(UID, WORKER_RECONTIMEOUTTIMID);

                        // start a timer for the next connect attempt
                        int delayBetweenConRetries = Config.GetDelayBetweenConRetries() * 1000;
                        // because we are already inside CSocketsThread::CritSect, this call is also possible
                        // from within CSocket::SocketCritSect and CFTPWorker::WorkerCritSect (no risk of dead-lock)
                        SocketsThread->AddTimer(Msg, UID, GetTickCount() + delayBetweenConRetries,
                                                WORKER_RECONTIMEOUTTIMID, NULL); // ignore the error, at worst the user hits Stop

                        State = fwsWaitingForReconnect; // NOTE: assumes ErrorDescr is set; no need to call Oper->OperationStatusMaybeChanged(), the operation state does not change (it is not paused and will not be after this change)
                        SubState = fwssNone;
                        // postActivate = TRUE;  // no reason, waiting for the timeout
                        reportWorkerChange = TRUE;
                    }
                }
                break;
            }
            }
        }
        if (closeSocket)
        {
            HANDLES(LeaveCriticalSection(&WorkerCritSect));

            // because we are already inside CSocketsThread::CritSect, this call is also possible
            // from within CSocket::SocketCritSect (no risk of dead-lock)
            ForceClose();

            HANDLES(EnterCriticalSection(&WorkerCritSect));
        }
    } while (run);
}

BOOL CFTPWorker::ParseListingToFTPQueue(TIndirectArray<CFTPQueueItem>* ftpQueueItems,
                                        const char* allocatedListing, int allocatedListingLen,
                                        CServerType* serverType, BOOL* lowMem, BOOL isVMS, BOOL isAS400,
                                        int transferMode, CQuadWord* totalSize,
                                        BOOL* sizeInBytes, BOOL selFiles,
                                        BOOL selDirs, BOOL includeSubdirs, DWORD attrAndMask,
                                        DWORD attrOrMask, int operationsUnknownAttrs,
                                        int operationsHiddenFileDel, int operationsHiddenDirDel)
{
    BOOL ret = FALSE;
    *lowMem = FALSE;
    ftpQueueItems->DestroyMembers();
    totalSize->Set(0, 0);
    *sizeInBytes = TRUE;

    // compute the 'validDataMask'
    DWORD validDataMask = VALID_DATA_HIDDEN | VALID_DATA_ISLINK; // Name + NameLen + Hidden + IsLink
    int i;
    for (i = 0; i < serverType->Columns.Count; i++)
    {
        switch (serverType->Columns[i]->Type)
        {
        case stctExt:
            validDataMask |= VALID_DATA_EXTENSION;
            break;
        case stctSize:
            validDataMask |= VALID_DATA_SIZE;
            break;
        case stctDate:
            validDataMask |= VALID_DATA_DATE;
            break;
        case stctTime:
            validDataMask |= VALID_DATA_TIME;
            break;
        case stctType:
            validDataMask |= VALID_DATA_TYPE;
            break;
        }
    }

    BOOL err = FALSE;
    const CFtpTextCodec textCodec = TextPolicy.GetCodec();
    CFTPListingPluginDataInterface* dataIface = new CFTPListingPluginDataInterface(&(serverType->Columns), FALSE,
                                                                                   validDataMask, isVMS,
                                                                                   textCodec);
    if (dataIface != NULL)
    {
        DWORD* emptyCol = new DWORD[serverType->Columns.Count]; // helper preallocated array for GetNextItemFromListing
        if (dataIface->IsGood() && emptyCol != NULL)
        {
            validDataMask |= dataIface->GetPLValidDataMask();
            CFTPParser* parser = serverType->CompiledParser;
            if (parser == NULL)
            {
                parser = CompileParsingRules(HandleNULLStr(serverType->RulesForParsing), &(serverType->Columns),
                                             NULL, NULL, NULL);
                serverType->CompiledParser = parser; // we will not deallocate 'parser', it already lives in 'serverType'
            }
            if (parser != NULL)
            {
                CFileData file;
                const char* listing = allocatedListing;
                const char* listingEnd = allocatedListing + allocatedListingLen;
                BOOL isDir = FALSE;

                int rightsCol = -1; // index of the column with permissions (used to detect links)
                if (dataIface != NULL)
                    rightsCol = dataIface->FindRightsColumn();

                // variables for Copy and Move operations
                CQuadWord size(-1, -1); // variable for the current file size
                std::wstring localTargetPath;
                if (CurItem->Type == fqitCopyExploreDir ||
                    CurItem->Type == fqitMoveExploreDir ||
                    CurItem->Type == fqitMoveExploreDirLink)
                {
                    CFTPQueueItemCopyMoveExplore* cmItem = (CFTPQueueItemCopyMoveExplore*)CurItem;
                    localTargetPath = cmItem->LocalTgtPath;
                    SPLSalPathAppendOwned(localTargetPath,
                                          cmItem->LocalTgtName);
                }
                BOOL is_AS_400_QSYS_LIB_Path = isAS400 && FTPIsPrefixOfServerPath(ftpsptAS400, "/QSYS.LIB", WorkingPath.c_str());
                parser->BeforeParsing(listing, listingEnd, StartTimeOfListing.wYear, StartTimeOfListing.wMonth,
                                      StartTimeOfListing.wDay, FALSE); // parser initialization
                while (!err && parser->GetNextItemFromListing(&file, &isDir, dataIface, &(serverType->Columns), &listing,
                                                               listingEnd, NULL, &err, emptyCol, textCodec))
                {
                    std::wstring memberNameW;
                    const wchar_t* targetNameW = file.Name;
                    if (is_AS_400_QSYS_LIB_Path)
                    {
                        memberNameW = FTPAS400CutFileNamePartW(file.Name);
                        targetNameW = memberNameW.c_str();
                    }
                    std::string itemNameBytes;
                    if (!dataIface->GetWireName(file, textCodec, itemNameBytes))
                    {
                        err = TRUE;
                    }
                    if (!err && (!isDir || file.NameLen > 2 ||
                                 file.Name[0] != L'.' || (file.Name[1] != 0 && file.Name[1] != L'.'))) // ignore the "." and ".." directories
                    {
                        // add an item for the parsed file/directory to ftpQueueItems
                        CFTPQueueItem* item = NULL;
                        CFTPQueueItemType type;
                        BOOL ok = TRUE;
                        CFTPQueueItemState state = sqisWaiting;
                        DWORD problemID = ITEMPR_OK;
                        BOOL skip = FALSE;
                        switch (CurItem->Type)
                        {
                        case fqitDeleteExploreDir: // explore directories for delete (note: links to directories are deleted as a whole, the operation's purpose is fulfilled and nothing "extra" is removed) (object of class CFTPQueueItemDelExplore)
                        {
                            int skippedItems = 0;  // unused
                            int uiNeededItems = 0; // unused
                            item = CreateItemForDeleteOperation(&file, isDir, rightsCol, dataIface, &type, &ok, FALSE,
                                                                operationsHiddenFileDel, operationsHiddenDirDel,
                                                                &state, &problemID, &skippedItems, &uiNeededItems);
                            break;
                        }

                        case fqitCopyExploreDir:     // explore a directory or a link to a directory for copying (object of class CFTPQueueItemCopyMoveExplore)
                        case fqitMoveExploreDir:     // explore a directory for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveExplore)
                        case fqitMoveExploreDirLink: // explore a link to a directory for moving (deletes the directory link after completion) (object of class CFTPQueueItemCopyMoveExplore)
                        {
                            item = CreateItemForCopyOrMoveOperation(&file, isDir, rightsCol, dataIface,
                                                                    &type, transferMode, Oper,
                                                                    CurItem->Type == fqitCopyExploreDir,
                                                                    localTargetPath.c_str(), targetNameW,
                                                                    &size, sizeInBytes, totalSize);
                            break;
                        }

                        case fqitChAttrsExploreDir:     // explore directories for attribute changes (also adds an item for changing the directory attributes) (object of class CFTPQueueItemChAttrExplore)
                        case fqitChAttrsExploreDirLink: // explore a link to a directory for attribute changes (object of class CFTPQueueItem)
                        {
                            int skippedItems = 0;  // unused
                            int uiNeededItems = 0; // unused
                            item = CreateItemForChangeAttrsOperation(&file, isDir, rightsCol, dataIface,
                                                                     &type, &ok, &state, &problemID,
                                                                     &skippedItems, &uiNeededItems, &skip, selFiles,
                                                                     selDirs, includeSubdirs, attrAndMask,
                                                                     attrOrMask, operationsUnknownAttrs);
                            break;
                        }
                        }
                        if (item != NULL)
                        {
                            if (ok)
                            {
                                item->SetItem(-1, type, state, problemID, WorkingPath.c_str(), itemNameBytes.c_str());
                                ftpQueueItems->Add(item); // add the operation to the queue
                                if (!ftpQueueItems->IsGood())
                                {
                                    ftpQueueItems->ResetState();
                                    ok = FALSE;
                                }
                            }
                            if (!ok)
                            {
                                err = TRUE;
                                delete item;
                            }
                        }
                        else
                        {
                            if (!skip) // only if this is not skipping the item but a low-memory error
                            {
                                TRACE_E(LOW_MEMORY);
                                err = TRUE;
                            }
                        }
                    }
                    // release file or directory data
                    dataIface->ReleasePluginData(file, isDir);
                    SalamanderGeneral->Free(file.Name);

                    if (err)
                        break;
                }
                if (!err && listing == listingEnd)
                    ret = TRUE; // parsing finished successfully
            }
            else
                err = TRUE; // only a lack of memory can happen here
        }
        else
        {
            if (emptyCol == NULL)
                TRACE_E(LOW_MEMORY);
            err = TRUE; // low memory
        }
        if (emptyCol != NULL)
            delete[] emptyCol;
        if (dataIface != NULL)
            delete dataIface;
    }
    else
    {
        TRACE_E(LOW_MEMORY);
        err = TRUE; // low memory
    }
    *lowMem = err;
    return ret;
}

//
// ****************************************************************************
// CExploredPaths
//

BOOL CExploredPaths::ContainsPath(const char* path)
{
    int pathLen = (int)strlen(path);
    int index;
    return GetPathIndex(path, pathLen, index);
}

BOOL CExploredPaths::AddPath(const char* path)
{
    int pathLen = (int)strlen(path);
    int index;
    if (!GetPathIndex(path, pathLen, index))
    {
        char* p = (char*)malloc(sizeof(short) + pathLen + 1);
        if (p != NULL)
        {
            *((short*)p) = pathLen;
            memcpy(((short*)p) + 1, path, pathLen + 1);
            Paths.Insert(index, p);
            if (!Paths.IsGood())
            {
                Paths.ResetState();
                free(p);
            }
        }
        else
            TRACE_E(LOW_MEMORY);
        return TRUE;
    }
    else
        return FALSE; // already present in the array
}

BOOL CExploredPaths::GetPathIndex(const char* path, int pathLen, int& index)
{
    if (Paths.Count == 0)
    {
        index = 0;
        return FALSE;
    }

    int l = 0, r = Paths.Count - 1, m;
    int res;
    while (1)
    {
        m = (l + r) / 2;
        const short* pathM = (const short*)Paths[m];
        res = *pathM - pathLen;
        if (res == 0)
            res = memcmp((const char*)(pathM + 1), path, pathLen);
        if (res == 0) // found
        {
            index = m;
            return TRUE;
        }
        else if (res > 0)
        {
            if (l == r || l > m - 1) // not found
            {
                index = m; // should be at this position
                return FALSE;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                index = m + 1; // should come after this position
                return FALSE;
            }
            l = m + 1;
        }
    }
}
