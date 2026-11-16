// Hot paths of the directory line: the places where a click shortens the shown path ("sftp:@site/home/test/docs" can be
// cut after "sftp:@site/", "sftp:@site/home", "sftp:@site/home/test"). Pure string work; no Sally, no I/O.
#pragma once

#include <string>

// Finds the next place where the path text 'text' (the whole text of the directory line starting with "sftp:", only the
// first 'pathLen' characters are the path, the rest is a filter) can be cut. 'offset' goes in as the position to search
// from (0 for the first call) and comes out as the end of the next path part, so text[0..offset) is the shortened path.
// The first result is the end of the root ("sftp:@site/"), the next ones end before each following '/'.
// False when there is nothing more to find (offset >= pathLen). The result can be pathLen itself: the whole path is
// not a place to cut, the caller ignores it (same convention as the FTP plugin).
bool NextHotPath(const wchar_t* text, int pathLen, int& offset);

// A path shortened at a hot path is "sftp:" + the server part, or with the root slash missing when the cut was right
// after the server part. Adds the '/' so that it is a path ChangePath understands. Anything else stays as it is.
void CompleteHotPath(std::wstring& path);
