/* Copyright (c) 2018, Sijmen J. Mulder (see LICENSE.md) */

#include <windows.h>
#include <tchar.h>

#define LEN(a)	(sizeof(a)/sizeof(*(a)))

#define ERR_NONE	(0)
#define ERR_UNKNOWN	(-1)
#define ERR_NOEDITOR	(-2)
#define ERR_EDITORLONG	(-3)
#define ERR_NOFOCUS	(-4)
#define ERR_NOSTYLE	(-5)
#define ERR_NOCONTROL	(-6)
#define ERR_NOMEM	(-7)
#define ERR_TMPDIR	(-8)
#define ERR_TMPFILE	(-9)
#define ERR_TMPCREATE	(-10)
#define ERR_TMPREOPEN	(-11)
#define ERR_TMPREAD	(-12)
#define ERR_TMPWRITE	(-13)
#define ERR_TMPTIME	(-14)
#define ERR_TMPSIZE	(-15)
#define ERR_TOUTF8	(-16)
#define ERR_FROMUTF8	(-17)
#define ERR_EDITORCMD	(-18)
#define ERR_EDITORSTART	(-19)
#define ERR_WINDOWGONE	(-20)

static const TCHAR *
errstr(int errnum)
{
	switch (errnum) {
	case ERR_UNKNOWN:
	case ERR_NOEDITOR:
		return _T("The EDITOR environment variable must be set to ")
		    _T("the path or name of an editor.");
	case ERR_EDITORLONG:
		return _T("The EDITOR environment variable's value is too ")
		   _T("long.");
	case ERR_NOFOCUS:
	case ERR_NOCONTROL:
		return _T("No active input field could not be found. The ")
		    _T("current program may not be supported by Editor ")
		    _T("Here.");
	case ERR_NOSTYLE:
		return _T("Error getting the input field's details.");
	case ERR_NOMEM:
		return _T("Error allocating memory.");
	case ERR_TMPDIR:
		return _T("Error getting the temporary directory path.");
	case ERR_TMPFILE:
		return _T("Error generating a temporary file path.");
	case ERR_TMPCREATE:
		return _T("Error creating a temporary file.");
	case ERR_TMPREOPEN:
		return _T("Error reopening the temporary file.");
	case ERR_TMPREAD:
		return _T("Error writing from the temporary file.");
	case ERR_TMPWRITE:
		return _T("Error writing to the temporary file.");
	case ERR_TMPTIME:
		return _T("Error getting the tempory file's modification ")
		    _T("time.");
	case ERR_TMPSIZE:
		return _T("Error getting the tempory file's size.");
	case ERR_TOUTF8:
		return _T("Error converting text to UTF-8.");
	case ERR_FROMUTF8:
		return _T("Error converting text from UTF-8.");
	case ERR_EDITORCMD:
		return _T("Error constructing the editor command line.");
	case ERR_EDITORSTART:
		return _T("Error launching the editor.");
	case ERR_WINDOWGONE:
		return _T("The original input field is gone.");
	default:
		return _T("An internal error occured.");
	}
}

static void
warn(int errnum)
{
	MessageBox(NULL, errstr(errnum), _T("Editor Here"),
	    MB_ICONEXCLAMATION | MB_OK);
}

__declspec(noreturn) static void
fatal(int errnum)
{
	MessageBox(NULL, errstr(errnum), _T("Editor Here"),
	    MB_ICONEXCLAMATION | MB_OK);
	ExitProcess(1);
}

static void
warn_recover(int errnum, const TCHAR *editorcmd)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	TCHAR msg[4096];
	DWORD dw;

	msg[0] = '\0';
	_stprintf_s(msg, LEN(msg), _T("%s\n\nDo you want to reopen your ")
	    _T("editor to recover your work?"), errstr(errnum));

	dw = MessageBox(NULL, msg, _T("Editor Here"), MB_ICONEXCLAMATION | MB_YESNO);

	if (dw == IDYES) {
		ZeroMemory(&pi, sizeof(pi));
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);

		dw = CreateProcess(NULL, editorcmd, NULL, NULL, FALSE, 0, NULL,
		    NULL, &si, &pi);
		if (!dw) {
			warn(ERR_EDITORSTART);
			return;
		}

		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int cmdshow)
{
	HANDLE heap;
	GUITHREADINFO gti;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD style;
	SIZE_T textlen;
	SIZE_T textsz;
	TCHAR *text;
	TCHAR editor[MAX_PATH];
	TCHAR tmpdir[MAX_PATH];
	TCHAR tmppath[MAX_PATH];
	TCHAR editorcmd[(MAX_PATH*2)+3];
	HANDLE tmpfile;
	FILETIME ftbefore;
	FILETIME ftafter;
	LARGE_INTEGER tmpfilesz;
	DWORD dw;

#ifdef UNICODE
	CHAR *mbtext;
	DWORD mbtextsz;
#endif

	heap = GetProcessHeap();

	dw = GetEnvironmentVariable(_T("EDITOR"), editor, LEN(editor));
	if (!dw)
		fatal(ERR_NOEDITOR);
	if (dw > LEN(editor))
		fatal(ERR_EDITORLONG);

	/*
	 * Shortcut invocation is not yet supported so give the user some time to
	 * focus on a text field.
	 */
	Sleep(3000);

	ZeroMemory(&gti, sizeof(gti));
	gti.cbSize = sizeof(gti);

	if (!GetGUIThreadInfo(0, &gti))
		fatal(ERR_NOFOCUS);

	style = GetWindowLong(gti.hwndFocus, GWL_STYLE);
	if (!style)
		fatal(ERR_NOSTYLE);
	if (!(style & WS_CHILD))
		fatal(ERR_NOCONTROL);

	textlen = (SIZE_T)SendMessage(gti.hwndFocus, WM_GETTEXTLENGTH, 0, 0);
	textsz = (textlen + 1) * sizeof(*text);

	if (!(text = HeapAlloc(heap, 0, textsz)))
		fatal(ERR_NOMEM);

	SendMessage(gti.hwndFocus, WM_GETTEXT, textlen+1, (LPARAM)text);

	dw = GetTempPath(LEN(tmpdir), tmpdir);
	if (!dw || dw > LEN(tmpdir))
		fatal(ERR_TMPDIR);

	if (!GetTempFileName(tmpdir, _T("edh"), 0, tmppath))
		fatal(ERR_TMPFILE);

	tmpfile = CreateFile(tmppath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	    FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (tmpfile == INVALID_HANDLE_VALUE)
		fatal(ERR_TMPCREATE);

#if UNICODE
	mbtextsz = WideCharToMultiByte(CP_UTF8, 0, text, (int)textlen, NULL,
	    0, NULL, NULL);
	if (!mbtextsz) {
		CloseHandle(tmpfile);
		DeleteFile(tmpfile);
		fatal(ERR_TOUTF8);
	}

	if (!(mbtext = HeapAlloc(heap, 0, mbtextsz))) {
		CloseHandle(tmpfile);
		DeleteFile(tmpfile);
		fatal(ERR_NOMEM);
	}

	dw = WideCharToMultiByte(CP_UTF8, 0, text, (int)textlen, mbtext,
	    mbtextsz, NULL, NULL);
	if (!dw) {
		CloseHandle(tmpfile);
		DeleteFile(tmpfile);
		fatal(ERR_TOUTF8);
	}

	if (!WriteFile(tmpfile, mbtext, mbtextsz, NULL, NULL)) {
		CloseHandle(tmpfile);
		DeleteFile(tmpfile);
		fatal(ERR_TMPWRITE);
	}

	HeapFree(heap, 0, text);
	HeapFree(heap, 0, mbtext);
	text = (void*)mbtext = NULL;
#else
	if (!WriteFile(tmpfile, text, (DWORD)textsz, NULL, NULL)) {
		CloseHandle(tmpfile);
		DeleteFile(tmpfile);
		fatal(ERR_TMPWRITE);
	}

	HeapFree(heap, 0, text);
	text = NULL;
#endif

	if (!GetFileTime(tmpfile, NULL, NULL, &ftbefore)) {
		CloseHandle(tmpfile);
		DeleteFile(tmpfile);
		fatal(ERR_TMPWRITE);
	}

	CloseHandle(tmpfile);
	tmpfile = NULL;

	dw = _stprintf_s(editorcmd, LEN(editorcmd), _T("\"%s\" %s"), editor,
	    tmppath);
	if (dw <= 0) {
		DeleteFile(tmppath);
		fatal(ERR_EDITORCMD);
	}

	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	dw = CreateProcess(NULL, editorcmd, NULL, NULL, FALSE, 0, NULL,
	    NULL, &si, &pi);
	if (!dw) {
		DeleteFile(tmppath);
		fatal(ERR_EDITORSTART);
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	tmpfile = CreateFile(tmppath, GENERIC_READ, 0, NULL, OPEN_EXISTING,
	    FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (tmpfile == INVALID_HANDLE_VALUE) {
		DeleteFile(tmppath);
		fatal(ERR_TMPREOPEN);
	}

	if (!GetFileTime(tmpfile, NULL, NULL, &ftafter)) {
		CloseHandle(tmpfile);
		warn_recover(ERR_TMPTIME, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	if (ftbefore.dwHighDateTime == ftafter.dwHighDateTime &&
	    ftbefore.dwLowDateTime == ftafter.dwLowDateTime) {
		CloseHandle(tmpfile);
		DeleteFile(tmppath);
		return 0;
	}

	if (!GetFileSizeEx(tmpfile, &tmpfilesz)) {
		CloseHandle(tmpfile);
		warn_recover(ERR_TMPSIZE, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

#ifdef UNICODE
	if (!(mbtext = HeapAlloc(heap, 0, tmpfilesz.QuadPart))) {
		CloseHandle(tmpfile);
		warn_recover(ERR_NOMEM, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	if (!ReadFile(tmpfile, mbtext, (DWORD)tmpfilesz.QuadPart, &dw, NULL) ||
	    dw < tmpfilesz.QuadPart) {
		CloseHandle(tmpfile);
		warn_recover(ERR_TMPREAD, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	textlen = MultiByteToWideChar(CP_UTF8, 0, mbtext, (int)tmpfilesz.QuadPart,
	    NULL, 0);
	if (!textlen) {
		CloseHandle(tmpfile);
		warn_recover(ERR_NOMEM, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	textsz = sizeof(*text) * (textlen+1);
	if (!(text = HeapAlloc(heap, 0, textsz))) {
		CloseHandle(tmpfile);
		warn_recover(ERR_NOMEM, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	dw = MultiByteToWideChar(CP_UTF8, 0, mbtext, (int)tmpfilesz.QuadPart,
	    text, textlen);
	if (!dw) {
		CloseHandle(tmpfile);
		warn_recover(ERR_FROMUTF8, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	text[textlen] = '\0';

	HeapFree(heap, 0, mbtext);
	mbtext = NULL;
#else
	if (!(text = HeapAlloc(heap, 0, tmpfilesz.QuadPart + 1))) {
		CloseHandle(tmpfile);
		warn_recover(ERR_NOMEM, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	if (!ReadFile(tmpfile, mbtext, tmpfilesz.QuadPart, &dw, NULL) ||
	    dw < tmpfilesz.QuadPart) {
		CloseHandle(tmpfile);
		warn_recover(ERR_TMPREAD, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	text[tmpfilesz.QuadPart] = '\0';
#endif

	SendMessage(gti.hwndFocus, WM_SETTEXT, 0, (LPARAM)text);

	HeapFree(heap, 0, text);
	text = NULL;

	if (!IsWindow(gti.hwndFocus)) {
		CloseHandle(tmpfile);
		warn_recover(ERR_WINDOWGONE, editorcmd);
		DeleteFile(tmppath);
		return 1;
	}

	CloseHandle(tmpfile);
	DeleteFile(tmppath);

	return 0;
}
