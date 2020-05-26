/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include "utils/BaseUtil.h"
#include "utils/BitManip.h"
#include "utils/Log.h"
#include "utils/LogDbg.h"
#include "utils/FileUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"
#include "utils/Dpi.h"

#include "wingui/WinGui.h"
#include "wingui/TreeModel.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"
#include "wingui/StaticCtrl.h"
#include "wingui/ButtonCtrl.h"
#include "wingui/ListBoxCtrl.h"
#include "wingui/DropDownCtrl.h"
#include "wingui/EditCtrl.h"

#include "Annotation.h"
#include "EngineBase.h"
#include "EnginePdf.h"
#include "EngineMulti.h"
#include "EngineManager.h"

#include "SumatraConfig.h"
#include "SettingsStructs.h"
#include "Controller.h"
#include "DisplayModel.h"
#include "ProgressUpdateUI.h"
#include "Notifications.h"
#include "WindowInfo.h"
#include "TabInfo.h"
#include "EditAnnotations.h"

using std::placeholders::_1;

// clang-format off
// TODO: more
const char* gAnnotationTypes = "Text\0Free Text\0Stamp\0Caret\0Ink\0Square\0Circle\0Line\0Polygon\0";

const char* gTextIcons = "Comment\0Help\0Insert\0Key\0NewParagraph\0Note\0Paragraph\0";

const char *gFileAttachmentUcons = "Graph\0Paperclip\0PushPin\0Tag\0";

const char *gSoundIcons = "Speaker\0Mic\0";

const char *gStampIcons = "Approved\0AsIs\0Confidential\0Departmental\0Draft\0Experimental\0Expired\0Final\0ForComment\0ForPublicRelease\0NotApproved\0NotForPublicRelease\0Sold\0TopSecret\0";

const char* gColors = "None\0Aqua\0Black\0Blue\0Fuchsia\0Gray\0Green\0Lime\0Maroon\0Navy\0Olive\0Orange\0Purple\0Red\0Silver\0Teal\0White\0Yellow\0";


// COLORREF is abgr format
static COLORREF gColorsValues[] = {
    //0x00000000, /* transparent */
    ColorUnset, /* transparent */
    //0xff00ffff, /* aqua */
    0xffffff00, /* aqua */
    0xff000000, /* black */
    //0xff0000ff, /* blue */
    0xffff0000, /* blue */
    //0xffff00ff, /* fuchsia */
    0xffff00ff, /* fuchsia */
    0xff808080, /* gray */
    0xff008000, /* green */
    0xff00ff00, /* lime */
    //0xff800000, /* maroon */
    0xff000080, /* maroon */
    //0xff000080, /* navy */
    0xff800000, /* navy */
    //0xff808000, /* olive */
    0xff008080, /* olive */
    //0xffffa500, /* orange */
    0xff00a5ff, /* orange */
    0xff800080, /* purple */
    //0xffff0000, /* red */
    0xff0000ff, /* red */
    0xffc0c0c0, /* silver */
    //0xff008080, /* teal */
    0xff808000, /* teal */
    0xffffffff, /* white */
    //0xffffff00, /* yellow */
    0xff00ffff, /* yellow */
};

AnnotationType gAnnotsWithBorder[] = {
    AnnotationType::FreeText,  AnnotationType::Ink,    AnnotationType::Line,
    AnnotationType::Square,    AnnotationType::Circle, AnnotationType::Polygon,
    AnnotationType::PolyLine,
};

AnnotationType gAnnotsWithInteriorColor[] = {
    AnnotationType::Line, AnnotationType::Square, AnnotationType::Circle,
};

AnnotationType gAnnotsWithColor[] = {
    AnnotationType::Stamp,     AnnotationType::Text,      AnnotationType::FileAttachment,
    AnnotationType::Sound,     AnnotationType::Caret,     AnnotationType::FreeText,
    AnnotationType::Ink,       AnnotationType::Line,      AnnotationType::Square,
    AnnotationType::Circle,    AnnotationType::Polygon,   AnnotationType::PolyLine,
    AnnotationType::Highlight, AnnotationType::Underline, AnnotationType::StrikeOut,
    AnnotationType::Squiggly,
};
// clang-format on

// in SumatraPDF.cpp
extern void RerenderForWindowInfo(WindowInfo*);

const char* GetKnownColorName(COLORREF c) {
    // TODO: handle this better?
    COLORREF c2 = ColorSetAlpha(c, 0xff);
    int n = (int)dimof(gColorsValues);
    for (int i = 1; i < n; i++) {
        if (c2 == gColorsValues[i]) {
            const char* s = seqstrings::IdxToStr(gColors, i);
            return s;
        }
    }
    return nullptr;
}

struct EditAnnotationsWindow {
    TabInfo* tab = nullptr;
    Window* mainWindow = nullptr;
    ILayout* mainLayout = nullptr;

    DropDownCtrl* dropDownAdd = nullptr;

    ListBoxCtrl* listBox = nullptr;
    StaticCtrl* staticRect = nullptr;
    StaticCtrl* staticAuthor = nullptr;
    StaticCtrl* staticModificationDate = nullptr;

    StaticCtrl* staticPopup = nullptr;
    StaticCtrl* staticContents = nullptr;
    EditCtrl* editContents = nullptr;
    StaticCtrl* staticIcon = nullptr;
    DropDownCtrl* dropDownIcon = nullptr;
    StaticCtrl* staticColor = nullptr;
    DropDownCtrl* dropDownColor = nullptr;
    ButtonCtrl* buttonDelete = nullptr;

    ButtonCtrl* buttonSavePDF = nullptr;

    ListBoxModel* lbModel = nullptr;

    Vec<Annotation*>* annotations = nullptr;
    // currently selected annotation
    Annotation* annot = nullptr;

    ~EditAnnotationsWindow();
};

static int FindStringInArray(const char* items, const char* toFind, int valIfNotFound = -1) {
    int i = 0;
    while (*items) {
        if (str::Eq(items, toFind)) {
            return i;
        }
        i++;
        items = seqstrings::SkipStr(items);
    }
    return valIfNotFound;
}

static void DropDownItemsFromStringArray(Vec<std::string_view>& items, const char* strings) {
    while (*strings) {
        items.Append(strings);
        strings = seqstrings::SkipStr(strings);
    }
}

void DeleteEditAnnotationsWindow(EditAnnotationsWindow* w) {
    delete w;
}

EditAnnotationsWindow::~EditAnnotationsWindow() {
    int nAnnots = annotations->isize();
    for (int i = 0; i < nAnnots; i++) {
        Annotation* a = annotations->at(i);
        if (a->pdf) {
            // hacky: only annotations with pdf_annot set belong to us
            delete a;
        }
    }
    delete annotations;
    delete mainWindow;
    delete mainLayout;
    delete lbModel;
}

static void CloseWindow(EditAnnotationsWindow* w) {
    // TODO: more?
    w->tab->editAnnotsWindow = nullptr;
    delete w;
}

static void WndCloseHandler(EditAnnotationsWindow* w, WindowCloseEvent* ev) {
    // CrashIf(w != ev->w);
    CloseWindow(w);
}

static void ButtonDeleteHandler(EditAnnotationsWindow* w) {
    MessageBoxNYI(w->mainWindow->hwnd);
}

static void ButtonSavePDFHandler(EditAnnotationsWindow* w) {
    EngineBase* engine = w->tab->AsFixed()->GetEngine();
    EnginePdfSaveUpdated(engine, {});
    // TODO: show a notification if saved or error message if failed to save
}

void ShowAnnotationRect(EditAnnotationsWindow* w, Annotation* annot) {
    bool isVisible = (annot != nullptr);
    w->staticRect->SetIsVisible(isVisible);
    if (!isVisible) {
        return;
    }
    str::Str s;
    RectD rect = annot->Rect();
    int x = (int)rect.x;
    int y = (int)rect.y;
    int dx = (int)rect.Dx();
    int dy = (int)rect.Dy();
    s.AppendFmt("Rect: %d %d %d %d", x, y, dx, dy);
    w->staticRect->SetText(s.as_view());
}

static void ShowAnnotationAuthor(EditAnnotationsWindow* w, Annotation* annot) {
    bool isVisible = (annot != nullptr) && !annot->Author().empty();
    w->staticAuthor->SetIsVisible(isVisible);
    if (!isVisible) {
        return;
    }
    str::Str s;
    s.AppendFmt("Author: %s", annot->Author().data());
    w->staticAuthor->SetText(s.as_view());
}

static void AppendPdfDate(str::Str& s, time_t secs) {
    struct tm* tm = gmtime(&secs);
    char buf[100];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M UTC", tm);
    s.Append(buf);
}

static void ShowAnnotationModificationDate(EditAnnotationsWindow* w, Annotation* annot) {
    bool isVisible = (annot != nullptr) && (annot->ModificationDate() != 0);
    w->staticModificationDate->SetIsVisible(isVisible);
    if (!isVisible) {
        return;
    }
    str::Str s;
    s.Append("Date: ");
    AppendPdfDate(s, annot->ModificationDate());
    w->staticModificationDate->SetText(s.as_view());
}

static void ShowAnnotationsPopup(EditAnnotationsWindow* w, Annotation* annot) {
    int popupId = annot ? annot->PopupId() : -1;
    bool isVisible = popupId >= 0;
    if (!isVisible) {
        w->staticPopup->SetIsVisible(isVisible);
        return;
    }
    str::Str s;
    s.AppendFmt("Popup: %d 0 R", popupId);
    w->staticPopup->SetIsVisible(isVisible);
    w->staticPopup->SetText(s.as_view());
}

static void ShowAnnotationsContents(EditAnnotationsWindow* w, Annotation* annot) {
    bool isVisible = (annot != nullptr);
    w->staticContents->SetIsVisible(isVisible);
    w->editContents->SetIsVisible(isVisible);
    if (!isVisible) {
        return;
    }
    str::Str s = annot->Contents();
    // TODO: don't replace if already is "\r\n"
    s.Replace("\n", "\r\n");
    w->editContents->SetText(s.as_view());
}

static void ShowAnnotationsIcon(EditAnnotationsWindow* w, Annotation* annot) {
    std::string_view iconName;
    if (annot) {
        iconName = annot->IconName();
    }
    bool isVisible = !iconName.empty();
    const char* icons = nullptr;
    if (isVisible) {
        switch (annot->type) {
            case AnnotationType::Text:
                icons = gTextIcons;
                break;
            case AnnotationType::FileAttachment:
                icons = gFileAttachmentUcons;
                break;
            case AnnotationType::Sound:
                icons = gSoundIcons;
                break;
            case AnnotationType::Stamp:
                icons = gStampIcons;
                break;
        }
    }
    if (!icons) {
        isVisible = false;
    }
    w->staticIcon->SetIsVisible(isVisible);
    w->dropDownIcon->SetIsVisible(isVisible);
    if (!isVisible) {
        return;
    }
    Vec<std::string_view> strings;
    DropDownItemsFromStringArray(strings, icons);
    w->dropDownIcon->SetItems(strings);
    int idx = FindStringInArray(icons, iconName.data(), 0);
    w->dropDownIcon->SetCurrentSelection(idx);
}

static bool IsAnnotationTypeInArray(AnnotationType* arr, size_t arrSize, AnnotationType toFind) {
    for (size_t i = 0; i < arrSize; i++) {
        if (toFind == arr[i]) {
            return true;
        }
    }
    return false;
}

// static
int ShouldEditBorder(AnnotationType subtype) {
    size_t n = dimof(gAnnotsWithBorder);
    return IsAnnotationTypeInArray(gAnnotsWithBorder, n, subtype);
}

// static
int ShouldEditInteriorColor(AnnotationType subtype) {
    size_t n = dimof(gAnnotsWithInteriorColor);
    return IsAnnotationTypeInArray(gAnnotsWithInteriorColor, n, subtype);
}

static void ShowAnnotationsColor(EditAnnotationsWindow* w, Annotation* annot) {
    auto annotType = annot ? annot->Type() : AnnotationType::Unknown;
    bool isVisible = IsAnnotationTypeInArray(gAnnotsWithColor, dimof(gAnnotsWithColor), annotType);
    w->staticColor->SetIsVisible(isVisible);
    w->dropDownColor->SetIsVisible(isVisible);
    if (!isVisible) {
        return;
    }
    COLORREF col = annot->Color();
    Vec<std::string_view> strings;
    DropDownItemsFromStringArray(strings, gColors);
    w->dropDownColor->SetItems(strings);
    const char* colorName = GetKnownColorName(col);
    int idx = FindStringInArray(gColors, colorName, 0);
    if (idx == -1) {
        // TODO: not a known color name, so add hex version to the list
    }
    w->dropDownColor->SetCurrentSelection(idx);
}

static void ListBoxSelectionChanged(EditAnnotationsWindow* w, ListBoxSelectionChangedEvent* ev) {
    // TODO: finish me
    int itemNo = ev->idx;
    w->annot = nullptr;
    if (itemNo >= 0) {
        w->annot = w->annotations->at(itemNo);
    }
    // TODO: mupdf shows it in 1.6 but not 1.7. Why?
    // ShowAnnotationRect(this, annot);
    ShowAnnotationAuthor(w, w->annot);
    ShowAnnotationModificationDate(w, w->annot);
    ShowAnnotationsPopup(w, w->annot);
    ShowAnnotationsContents(w, w->annot);
    // TODO: PDF_ANNOT_FREE_TEXT
    // TODO: PDF_ANNOT_LINE
    ShowAnnotationsIcon(w, w->annot);
    // TODO: border
    ShowAnnotationsColor(w, w->annot);
    // TODO: icolor
    // TODO: quad points
    // TODO: vertices
    // TODO: ink list
    // TODO: PDF_ANNOT_FILE_ATTACHMENT
    w->buttonDelete->SetIsVisible(w->annot != nullptr);
    // TODO: get from client size
    auto currBounds = w->mainLayout->lastBounds;
    int dx = currBounds.Dx();
    int dy = currBounds.Dy();
    LayoutAndSizeToContent(w->mainLayout, dx, dy, w->mainWindow->hwnd);
    // TODO: go to page with selected annotation
}

static void EnableSaveIfAnnotationsChanged(EditAnnotationsWindow* w) {
    bool didChange = false;
    if (w->annotations) {
        for (auto& annot : *w->annotations) {
            if (annot->isChanged || annot->isDeleted) {
                didChange = true;
                break;
            }
        }
    }
    w->buttonSavePDF->SetIsEnabled(didChange);
}

static void DropDownAddSelectionChanged(EditAnnotationsWindow* w, DropDownSelectionChangedEvent* ev) {
    UNUSED(ev);
    // TODO: implement me
    MessageBoxNYI(w->mainWindow->hwnd);
}

static void DropDownIconSelectionChanged(EditAnnotationsWindow* w, DropDownSelectionChangedEvent* ev) {
    w->annot->SetIconName(ev->item);
    EnableSaveIfAnnotationsChanged(w);
    // TODO: a better way
    RerenderForWindowInfo(w->tab->win);
}

static void DropDownColorSelectionChanged(EditAnnotationsWindow* w, DropDownSelectionChangedEvent* ev) {
    // get known color name
    int nColors = (int)dimof(gColorsValues);
    COLORREF col = ColorUnset;
    if (ev->idx < nColors) {
        col = gColorsValues[ev->idx];
    } else {
        // TODO: parse color from hex
    }
    // TODO: also opacity?
    w->annot->SetColor(col);
    EnableSaveIfAnnotationsChanged(w);
    RerenderForWindowInfo(w->tab->win);
}

static void WndSizeHandler(EditAnnotationsWindow* w, SizeEvent* ev) {
    int dx = ev->dx;
    int dy = ev->dy;
    HWND hwnd = ev->hwnd;
    if (dx == 0 || dy == 0) {
        return;
    }
    ev->didHandle = true;
    InvalidateRect(hwnd, nullptr, false);
    if (false && w->mainLayout->lastBounds.EqSize(dx, dy)) {
        // avoid un-necessary layout
        return;
    }
    LayoutToSize(w->mainLayout, {dx, dy});
}

static std::tuple<StaticCtrl*, ILayout*> CreateStatic(HWND parent, std::string_view sv = {}) {
    auto w = new StaticCtrl(parent);
    bool ok = w->Create();
    CrashIf(!ok);
    w->SetText(sv);
    w->SetIsVisible(false);
    auto l = NewStaticLayout(w);
    return {w, l};
}

static void CreateMainLayout(EditAnnotationsWindow* aw) {
    HWND parent = aw->mainWindow->hwnd;
    auto vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;

    ILayout* l = nullptr;

    {
        auto w = new DropDownCtrl(parent);
        bool ok = w->Create();
        CrashIf(!ok);
        aw->dropDownAdd = w;
        w->onSelectionChanged = std::bind(DropDownAddSelectionChanged, aw, _1);
        l = NewDropDownLayout(w);
        vbox->AddChild(l);
        Vec<std::string_view> annotTypes;
        DropDownItemsFromStringArray(annotTypes, gAnnotationTypes);
        w->SetItems(annotTypes);
        w->SetCueBanner("Add annotation...");
    }

    {
        auto w = new ListBoxCtrl(parent);
        w->idealSizeLines = 5;
        bool ok = w->Create();
        CrashIf(!ok);
        aw->listBox = w;
        w->onSelectionChanged = std::bind(ListBoxSelectionChanged, aw, _1);
        l = NewListBoxLayout(w);
        vbox->AddChild(l);

        aw->lbModel = new ListBoxModelStrings();
        aw->listBox->SetModel(aw->lbModel);
    }

    {
        std::tie(aw->staticRect, l) = CreateStatic(parent);
        vbox->AddChild(l);
    }

    {
        std::tie(aw->staticAuthor, l) = CreateStatic(parent);
        vbox->AddChild(l);
    }

    {
        std::tie(aw->staticModificationDate, l) = CreateStatic(parent);
        vbox->AddChild(l);
    }

    {
        std::tie(aw->staticPopup, l) = CreateStatic(parent);
        vbox->AddChild(l);
    }

    {
        std::tie(aw->staticContents, l) = CreateStatic(parent, "Contents:");
        vbox->AddChild(l);
    }

    {
        auto w = new EditCtrl(parent);
        w->isMultiLine = true;
        w->idealSizeLines = 5;
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetIsVisible(false);
        aw->editContents = w;
        // TODO: hookup change request
        l = NewEditLayout(w);
        vbox->AddChild(l);
    }

    {
        std::tie(aw->staticIcon, l) = CreateStatic(parent, "Icon:");
        vbox->AddChild(l);
    }

    {
        auto w = new DropDownCtrl(parent);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetIsVisible(false);
        aw->dropDownIcon = w;
        w->onSelectionChanged = std::bind(DropDownIconSelectionChanged, aw, _1);
        l = NewDropDownLayout(w);
        vbox->AddChild(l);
    }

    {
        std::tie(aw->staticColor, l) = CreateStatic(parent, "Color:");
        vbox->AddChild(l);
    }

    {
        auto w = new DropDownCtrl(parent);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetIsVisible(false);
        aw->dropDownColor = w;
        w->onSelectionChanged = std::bind(DropDownColorSelectionChanged, aw, _1);
        l = NewDropDownLayout(w);
        vbox->AddChild(l);
        Vec<std::string_view> strings;
        DropDownItemsFromStringArray(strings, gColors);
        w->SetItems(strings);
    }

    {
        auto w = new ButtonCtrl(parent);
        w->SetText("Delete annotation");
        w->onClicked = std::bind(&ButtonDeleteHandler, aw);
        bool ok = w->Create();
        w->SetIsVisible(false);
        CrashIf(!ok);
        aw->buttonDelete = w;
        l = NewButtonLayout(w);
        vbox->AddChild(l);
    }

    {
        // used to take all available space between the what's above and below
        l = new Spacer(0, 0);
        vbox->AddChild(l, 1);
    }

    {
        auto w = new ButtonCtrl(parent);
        // TODO: maybe show file name e.g. "Save changes to foo.pdf"
        w->SetText("Save changes to PDF");
        w->onClicked = std::bind(&ButtonSavePDFHandler, aw);
        bool ok = w->Create();
        CrashIf(!ok);
        w->SetIsEnabled(false); // only enable if there are changes
        aw->buttonSavePDF = w;
        l = NewButtonLayout(w);
        vbox->AddChild(l);
    }

    auto padding = new Padding(vbox, DpiScaledInsets(parent, 4, 8));
    aw->mainLayout = padding;
}

static void RebuildAnnotations(EditAnnotationsWindow* w) {
    auto model = new ListBoxModelStrings();
    int n = 0;
    if (w->annotations) {
        n = w->annotations->isize();
    }

    str::Str s;
    for (int i = 0; i < n; i++) {
        auto annot = w->annotations->at(i);
        s.Reset();
        s.AppendFmt("page %d, ", annot->pageNo);
        s.AppendView(AnnotationName(annot->type));
        model->strings.Append(s.AsView());
    }

    w->listBox->SetModel(model);
    delete w->lbModel;
    w->lbModel = model;
}

void StartEditAnnotations(TabInfo* tab) {
    if (tab->editAnnotsWindow) {
        HWND hwnd = tab->editAnnotsWindow->mainWindow->hwnd;
        BringWindowToTop(hwnd);
        return;
    }
    DisplayModel* dm = tab->AsFixed();
    CrashIf(!dm);
    if (!dm) {
        return;
    }

    Vec<Annotation*>* annots = new Vec<Annotation*>();
    // those annotations are owned by us
    dm->GetEngine()->GetAnnotations(annots);

    // those annotations are owned by DisplayModel
    // TODO: for uniformity, make a copy of them
    if (dm->userAnnots) {
        for (auto a : *dm->userAnnots) {
            annots->Append(a);
        }
    }

    auto win = new EditAnnotationsWindow();
    win->tab = tab;
    tab->editAnnotsWindow = win;
    win->annotations = annots;

    auto w = new Window();
    w->isDialog = true;
    HMODULE h = GetModuleHandleW(nullptr);
    LPCWSTR iconName = MAKEINTRESOURCEW(GetAppIconID());
    w->hIcon = LoadIconW(h, iconName);

    // w->isDialog = true;
    w->backgroundColor = MkRgb((u8)0xee, (u8)0xee, (u8)0xee);
    w->SetTitle("Annotations");
    // int dx = DpiScale(nullptr, 480);
    // int dy = DpiScale(nullptr, 640);
    // w->initialSize = {dx, dy};
    // PositionCloseTo(w, args->hwndRelatedTo);
    // SIZE winSize = {w->initialSize.dx, w->initialSize.Height};
    // LimitWindowSizeToScreen(args->hwndRelatedTo, winSize);
    // w->initialSize = {winSize.cx, winSize.cy};
    bool ok = w->Create();
    CrashIf(!ok);

    win->mainWindow = w;

    w->onClose = std::bind(WndCloseHandler, win, _1);
    w->onSize = std::bind(WndSizeHandler, win, _1);

    CreateMainLayout(win);
    RebuildAnnotations(win);

    // size our editor window to be the same height as main window
    int minDy = 720;
    // TODO: this is slightly less that wanted
    HWND hwnd = tab->win->hwndCanvas;
    auto rc = ClientRect(hwnd);
    if (rc.Dy() > 0) {
        minDy = rc.Dy();
        // if it's a tall window, up the number of items in list box
        // from 5 to 14
        if (minDy > 1024) {
            win->listBox->idealSizeLines = 14;
        }
    }
    LayoutAndSizeToContent(win->mainLayout, 520, minDy, w->hwnd);
    // TODO: position to the right of tab->win->hwndFrame

    // important to call this after hooking up onSize to ensure
    // first layout is triggered
    w->SetIsVisible(true);

    CrashIf(!ok);
}
