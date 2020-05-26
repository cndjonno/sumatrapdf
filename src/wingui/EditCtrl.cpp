/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/WinUtil.h"
#include "utils/BitManip.h"
#include "utils/Dpi.h"
#include "utils/Log.h"
#include "utils/LogDbg.h"

#include "wingui/WinGui.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"
#include "wingui/EditCtrl.h"

// https://docs.microsoft.com/en-us/windows/win32/controls/edit-controls

// TODO:
// - expose EN_UPDATE
// (http://msdn.microsoft.com/en-us/library/windows/desktop/bb761687(v=vs.85).aspx)
// - add border and possibly other decorations by handling WM_NCCALCSIZE, WM_NCPAINT and
// WM_NCHITTEST
//   etc., http://www.catch22.net/tuts/insert-buttons-edit-control
// - include value we remember in WM_NCCALCSIZE in GetIdealSize()

Kind kindEdit = "edit";

bool IsEdit(Kind kind) {
    return kind == kindEdit;
}

bool IsEdit(ILayout* l) {
    return IsLayoutOfKind(l, kindEdit);
}

ILayout* NewEditLayout(EditCtrl* w) {
    return new WindowBaseLayout(w, kindEdit);
}

static void HandleWM_COMMAND(EditCtrl* w, WndEvent* ev) {
    CrashIf(ev->msg != WM_COMMAND);

    // https://docs.microsoft.com/en-us/windows/win32/controls/en-change
    auto code = HIWORD(ev->wparam);
    if (EN_CHANGE == code) {
        if (w->OnTextChanged) {
            EditTextChangedEvent a;
            CopyWndEvent cp(&a, ev);
            a.text = w->GetText();
            w->OnTextChanged(&a);
            if (a.didHandle) {
                return;
            }
        }
    }
}

static void DispatchWM_COMMAND(void* user, WndEvent* ev) {
    auto w = (EditCtrl*)user;
    HandleWM_COMMAND(w, ev);
}

// https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcoloredit
static void HandleWM_CTLCOLOREDIT(EditCtrl* w, WndEvent* ev) {
    CrashIf(ev->msg != WM_CTLCOLOREDIT);
    HWND hwndCtrl = (HWND)ev->lparam;
    CrashIf(hwndCtrl != w->hwnd);
    if (w->bgBrush == nullptr) {
        return;
    }
    HDC hdc = (HDC)ev->wparam;
    // SetBkColor(hdc, w->bgCol);
    SetBkMode(hdc, TRANSPARENT);
    if (w->textColor != ColorUnset) {
        ::SetTextColor(hdc, w->textColor);
    }
    ev->didHandle = true;
    ev->result = (INT_PTR)w->bgBrush;
}

static void DispatchWM_CTLCOLOREDIT(void* user, WndEvent* ev) {
    auto w = (EditCtrl*)user;
    HandleWM_CTLCOLOREDIT(w, ev);
}

#if 0
void EditCtrl::SetColors(COLORREF txtCol, COLORREF bgCol) {
    DeleteObject(this->bgBrush);
    this->bgBrush = nullptr;
    if (txtCol != NO_CHANGE) {
        this->txtCol = txtCol;
    }
    if (bgCol != NO_CHANGE) {
        this->bgCol = bgCol;
    }
    if (this->bgCol != NO_COLOR) {
        this->bgBrush = CreateSolidBrush(bgCol);
    }
}
#endif

static bool EditSetCueText(HWND hwnd, std::string_view s) {
    if (!hwnd) {
        return false;
    }
    auto* ws = strconv::Utf8ToWstr(s);
    bool ok = Edit_SetCueBannerText(hwnd, ws) == TRUE;
    free(ws);
    return ok;
}

bool EditCtrl::SetCueText(std::string_view s) {
    cueText.Set(s);
    return EditSetCueText(hwnd, cueText.AsView());
}

void EditCtrl::SetSelection(int start, int end) {
    Edit_SetSel(hwnd, start, end);
}

EditCtrl::EditCtrl(HWND p) : WindowBase(p) {
    // https://docs.microsoft.com/en-us/windows/win32/controls/edit-control-styles
    dwStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT;
    dwStyle |= WS_BORDER;
    winClass = WC_EDIT;
    kind = kindEdit;
}

bool EditCtrl::Create() {
    // Note: has to remember this here because when I GetWindowStyle() later on,
    // WS_BORDER is not set, which is a mystery, because it is being drawn.
    // also, WS_BORDER seems to be painted in client area
    hasBorder = bit::IsMaskSet<DWORD>(dwStyle, WS_BORDER);
    if (isMultiLine) {
        dwStyle |= ES_MULTILINE | WS_VSCROLL | ES_WANTRETURN;
    } else {
        // ES_AUTOHSCROLL disable wrapping in multi-line setup
        dwStyle |= ES_AUTOHSCROLL;
    }

    bool ok = WindowBase::Create();
    if (!ok) {
        return false;
    }

    void* user = this;
    RegisterHandlerForMessage(hwnd, WM_COMMAND, DispatchWM_COMMAND, user);
    RegisterHandlerForMessage(hwnd, WM_CTLCOLOREDIT, DispatchWM_CTLCOLOREDIT, user);
    // TODO: handle WM_CTLCOLORSTATIC for read-only/disabled controls

    EditSetCueText(hwnd, cueText.AsView());
    return true;
}

EditCtrl::~EditCtrl() {
    DeleteObject(bgBrush);
}

#if 0
    RECT curr = params->rgrc[0];
    w->ncDx = RectDx(orig) - RectDx(curr);
    w->ncDy = RectDy(orig) - RectDy(curr);
    return res;
#endif

#if 0
static void NcCalcSize(HWND hwnd, NCCALCSIZE_PARAMS* params) {
    WPARAM wp = (WPARAM)TRUE;
    LPARAM lp = (LPARAM)params;
    SendMessageW(hwnd, WM_NCCALCSIZE, wp, lp);
}
#endif

Size EditCtrl::GetIdealSize() {
    Size s1 = HwndMeasureText(hwnd, L"Minimal", hfont);
    // dbglogf("EditCtrl::GetIdealSize: s1.dx=%d, s2.dy=%d\n", (int)s1.cx, (int)s1.cy);
    AutoFreeWstr txt = win::GetText(hwnd);
    Size s2 = HwndMeasureText(hwnd, txt, hfont);
    // dbglogf("EditCtrl::GetIdealSize: s2.dx=%d, s2.dy=%d\n", (int)s2.cx, (int)s2.cy);

    int dx = std::max(s1.dx, s2.dx);
    // for multi-line text, this measures multiple line.
    // TODO: maybe figure out better protocol
    int dy = std::min(s1.dy, s2.dy);
    if (dy == 0) {
        dy = std::max(s1.dy, s2.dy);
    }
    dy = dy * idealSizeLines;
    // dbglogf("EditCtrl::GetIdealSize: dx=%d, dy=%d\n", (int)dx, (int)dy);

    LRESULT margins = SendMessage(hwnd, EM_GETMARGINS, 0, 0);
    int lm = (int)LOWORD(margins);
    int rm = (int)HIWORD(margins);
    dx += lm + rm;

    if (this->hasBorder) {
        dx += DpiScale(hwnd, 4);
        dy += DpiScale(hwnd, 4);
    }
    // logf("EditCtrl::GetIdealSize(): dx=%d, dy=%d\n", int(res.cx), int(res.cy));
    return {dx, dy};
}
