/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/Dpi.h"
#include "utils/WinUtil.h"

#include "Layout.h"

bool gEnableDebugLayout = false;

void dbglayoutf(const char* fmt, ...) {
    if (!gEnableDebugLayout) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    AutoFree s = str::FmtV(fmt, args);
    OutputDebugStringA(s.Get());
    va_end(args);
}

static void LogAppendNum(str::Str& s, int n, const char* suffix) {
    if (n == Inf) {
        s.Append("Inf");
    } else {
        s.AppendFmt("%d", n);
    }
    if (suffix) {
        s.AppendView(suffix);
    }
}

void LogConstraints(Constraints c, const char* suffix) {
    str::Str s;
    if (c.min.dx == c.max.dx) {
        dbglayoutf("dx: ");
        LogAppendNum(s, c.min.dx, " ");
    } else {
        dbglayoutf("dx: ");
        LogAppendNum(s, c.min.dx, " - ");
        LogAppendNum(s, c.max.dx, " ");
    }
    if (c.min.dy == c.max.dy) {
        dbglayoutf("dy: ");
        LogAppendNum(s, c.min.dy, " ");
    } else {
        dbglayoutf("dy: ");
        LogAppendNum(s, c.min.dy, " - ");
        LogAppendNum(s, c.max.dy, " ");
    }
    s.AppendView(suffix);
    dbglayoutf("%s", s.c_str());
}

int Rect::Right() const {
    return x + dx;
}

int Rect::Bottom() const {
    return y + dy;
}

bool Rect::empty() const {
    return Dx() == 0 || Dy() == 0;
}

RECT RectToRECT(const Rect r) {
    LONG left = r.x;
    LONG top = r.y;
    LONG right = left + r.dx;
    LONG bottom = top + r.dy;
    return RECT{left, top, right, bottom};
}

int clamp(int v, int vmin, int vmax) {
    if (v > vmax) {
        return vmax;
    }
    if (v < vmin) {
        return vmin;
    }
    return v;
}

int scale(int v, i64 num, i64 den) {
    i64 res = (i64(v) * num) / den;
    return int(res);
}

int guardInf(int a, int b) {
    if (a == Inf) {
        return Inf;
    }
    return b;
}

Constraints ExpandInf() {
    Size min{0, 0};
    Size max{Inf, Inf};
    return Constraints{min, max};
}

Constraints ExpandHeight(int width) {
    Size min{width, 0};
    Size max{width, Inf};
    return Constraints{min, max};
}

Constraints ExpandWidth(int height) {
    Size min{0, height};
    Size max{Inf, height};
    return Constraints{min, max};
}

Constraints Loose(const Size size) {
    return Constraints{Size{}, size};
}

Constraints Tight(const Size size) {
    return Constraints{size, size};
}

Constraints TightHeight(int height) {
    Size min{0, height};
    Size max{Inf, height};
    return Constraints{min, max};
}

Size Constraints::Constrain(Size size) const {
    int w = clamp(size.dx, this->min.dx, this->max.dx);
    int h = clamp(size.dy, this->min.dy, this->max.dy);
    return Size{w, h};
}

Size Constraints::ConstrainAndAttemptToPreserveAspectRatio(const Size size) const {
    if (this->IsTight()) {
        return this->min;
    }

    int width = size.dx;
    int height = size.dy;

    if (width > this->max.dx) {
        width = this->max.dx;
        height = scale(width, size.dy, size.dx);
    }
    if (height > this->max.dy) {
        height = this->max.dy;
        width = scale(height, size.dx, size.dy);
    }

    if (width < this->min.dx) {
        width = this->min.dx;
        height = scale(width, size.dy, size.dx);
    }

    if (height < this->min.dy) {
        height = this->min.dy;
        width = scale(height, size.dx, size.dy);
    }

    Size c{width, height};
    return this->Constrain(c);
}

int Constraints::ConstrainHeight(int height) const {
    return clamp(height, this->min.dy, this->max.dy);
}

int Constraints::ConstrainWidth(int width) const {
    return clamp(width, this->min.dx, this->max.dx);
}

bool Constraints::HasBoundedHeight() const {
    return this->max.dy < Inf;
}

bool Constraints::HasBoundedWidth() const {
    return this->max.dx < Inf;
}

bool Constraints::HasTightWidth() const {
    return this->min.dx >= this->max.dx;
}

bool Constraints::HasTightHeight() const {
    return this->min.dy >= this->max.dy;
}

Constraints Constraints::Inset(int width, int height) const {
    int minw = this->min.dx;
    int deflatedMinWidth = guardInf(minw, std::max(0, minw - width));
    int minh = this->min.dy;
    int deflatedMinHeight = guardInf(minh, std::max(0, minh - height));
    Size min{deflatedMinWidth, deflatedMinHeight};
    int maxw = this->max.dx;
    int maxh = this->max.dy;
    Size max{
        std::max(deflatedMinWidth, guardInf(maxw, maxw - width)),
        std::max(deflatedMinHeight, guardInf(maxh, maxh - height)),
    };
    return Constraints{min, max};
}

bool Constraints::IsBounded() const {
    return this->HasBoundedWidth() && this->HasBoundedHeight();
}

bool Constraints::IsNormalized() const {
    return this->min.dx >= 0.0 && this->min.dx <= this->max.dx && this->min.dy >= 0.0 && this->min.dy <= this->max.dy;
}

bool Constraints::IsSatisfiedBy(Size size) const {
    return this->min.dx <= size.dx && size.dx <= this->max.dx && this->min.dy <= size.dy && size.dy <= this->max.dy &&
           size.dx != Inf && size.dy != Inf;
}

bool Constraints::IsTight() const {
    return this->HasTightWidth() && this->HasTightHeight();
}

bool Constraints::IsZero() const {
    return this->min.dx == 0 && this->min.dy == 0 && this->max.dx == 0 && this->max.dy == 0;
}

Constraints Constraints::Loosen() const {
    return Constraints{Size{}, this->max};
}

Constraints Constraints::LoosenHeight() const {
    return Constraints{Size{this->min.dx, 0}, this->max};
}

Constraints Constraints::LoosenWidth() const {
    return Constraints{Size{0, this->min.dy}, this->max};
}

Constraints Constraints::Tighten(Size size) const {
    Constraints bc = *this;
    bc.min.dx = clamp(size.dx, bc.min.dx, bc.max.dx);
    bc.max.dx = bc.min.dx;
    bc.min.dy = clamp(size.dy, bc.min.dy, bc.max.dy);
    bc.max.dy = bc.min.dy;
    return bc;
}

Constraints Constraints::TightenHeight(int height) const {
    Constraints bc = *this;
    bc.min.dy = clamp(height, bc.min.dy, bc.max.dy);
    bc.max.dy = bc.min.dy;
    return bc;
}

Constraints Constraints::TightenWidth(int width) const {
    Constraints bc = *this;

    bc.min.dx = clamp(width, bc.min.dx, bc.max.dx);
    bc.max.dx = bc.min.dx;
    return bc;
}

void ILayout::SetVisibility(Visibility newVisibility) {
    visibility = newVisibility;
}

bool IsLayoutOfKind(ILayout* l, Kind kind) {
    if (l == nullptr) {
        return false;
    }
    return l->kind == kind;
}

// padding.go

Kind paddingKind = "padding";
bool IsPadding(Kind kind) {
    return kind == paddingKind;
}
bool IsPadding(ILayout* l) {
    return IsLayoutOfKind(l, paddingKind);
}

Padding::Padding(ILayout* child, const Insets& insets) {
    this->kind = paddingKind;
    this->child = child;
    this->insets = insets;
}

Padding::~Padding() {
    delete child;
}

Size Padding::Layout(const Constraints bc) {
    dbglayoutf("Padding::Layout() ");
    LogConstraints(bc, "\n");

    auto hinset = this->insets.left + this->insets.right;
    auto vinset = this->insets.top + this->insets.bottom;

    auto innerConstraints = bc.Inset(hinset, vinset);
    this->childSize = this->child->Layout(innerConstraints);
    return Size{
        this->childSize.dx + hinset,
        this->childSize.dy + vinset,
    };
}

int Padding::MinIntrinsicHeight(int width) {
    auto vinset = this->insets.top + this->insets.bottom;
    return this->child->MinIntrinsicHeight(width) + vinset;
}

int Padding::MinIntrinsicWidth(int height) {
    auto hinset = this->insets.left + this->insets.right;
    return this->child->MinIntrinsicWidth(height) + hinset;
}

void Padding::SetBounds(Rect bounds) {
    dbglayoutf("Padding:SetBounds() %d,%d - %d, %d\n", bounds.x, bounds.y, bounds.dx, bounds.dy);
    lastBounds = bounds;
    bounds.x += insets.left;
    bounds.y += insets.top;
    bounds.dx -= (insets.right + insets.left);
    bounds.dy -= (insets.bottom + insets.top);
    this->child->SetBounds(bounds);
}

// layout.go
ILayout::ILayout(Kind k) {
    kind = k;
}

int calculateHGap(ILayout* previous, ILayout* current) {
    // The vertical gap between most controls is 11 relative pixels.  However,
    // there are different rules for between a label and its associated control,
    // or between related controls.  These relationship do not appear in the
    // model provided by this package, so these relationships need to be
    // inferred from the order and type of controls.
    //
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn742486.aspx#sizingandspacing
    if (IsButton(previous)) {
        if (IsButton(current)) {
            // Any pair of successive buttons will be assumed to be in a
            // related group.
            return DpiScale(8);
        }
    }

    // The spacing between unrelated controls.
    return DpiScale(11);
}

int calculateVGap(ILayout* previous, ILayout* current) {
    // The vertical gap between most controls is 11 relative pixels.  However,
    // there are different rules for between a label and its associated control,
    // or between related controls.  These relationship do not appear in the
    // model provided by this package, so these relationships need to be
    // inferred from the order and type of controls.
    //
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn742486.aspx#sizingandspacing

    // Unwrap and Expand widgets.
    if (IsExpand(previous)) {
        Expand* expand = (Expand*)previous;
        previous = expand->child;
    }
    if (IsExpand(current)) {
        Expand* expand = (Expand*)current;
        current = expand->child;
    }

    // Apply layout rules.
    if (IsLabel(previous)) {
        // Any label immediately preceding any other control will be assumed to
        // be 'associated'.
        return DpiScale(2);
    }

    if (IsCheckbox(previous)) {
        if (IsCheckbox(current)) {
            // Any pair of successive checkboxes will be assumed to be in a
            // related group.
            // return DpiScale(2);
            return 0;
        }
    }

    // The spacing between unrelated controls.  This is also the default space
    // between paragraphs of text.
    return DpiScale(8);
}

// vbox.go

Kind kindVBox = "vbox";
bool IsVBox(Kind kind) {
    return kind == kindVBox;
}

VBox::VBox() {
    kind = kindVBox;
}

VBox::~VBox() {
    for (auto& c : children) {
        delete c.layout;
    }
}

int VBox::ChildrenCount() {
    return children.isize();
}

int updateFlex(Vec<boxElementInfo>& children, MainAxisAlign alignMain) {
    if (alignMain == MainAxisAlign::Homogeneous) {
        return 0;
    }
    int totalFlex = 0;
    for (auto& i : children) {
        if (i.layout->visibility != Visibility::Collapse) {
            totalFlex += i.flex;
        }
    }
    return totalFlex;
}

Size VBox::Layout(const Constraints bc) {
    auto n = ChildrenCount();
    if (n == 0) {
        totalHeight = 0;
        return bc.Constrain(Size{});
    }
    totalFlex = updateFlex(this->children, this->alignMain);

    dbglayoutf("VBox::Layout() %d children, %d totalFlex ", n, totalFlex);
    LogConstraints(bc, "\n");

    // Determine the constraints for layout of child elements.
    auto cbc = bc;

    if (this->alignMain == MainAxisAlign::Homogeneous) {
        auto count = (i64)this->ChildrenCount();
        auto gap = calculateVGap(nullptr, nullptr);
        cbc.TightenHeight(scale(cbc.max.dy, 1, count) - scale(gap, count - 1, count));
    } else {
        cbc.min.dy = 0;
        cbc.max.dy = Inf;
    }

    if (this->alignCross == CrossAxisAlign::Stretch) {
        if (cbc.HasBoundedWidth()) {
            cbc = cbc.TightenWidth(cbc.max.dx);
        } else {
            cbc = cbc.TightenWidth(this->MinIntrinsicWidth(Inf));
        }
    } else {
        cbc = cbc.LoosenWidth();
    }
    auto height = int(0);
    auto width = int(0);
    ILayout* previous = nullptr;

    for (int i = 0; i < n; i++) {
        auto& v = this->children.at(i);
        if (v.layout->visibility == Visibility::Collapse) {
            continue;
        }
        // Determine what gap needs to be inserted between the elements.
        if (i > 0) {
            if (IsPacked(this->alignMain)) {
                height += calculateVGap(previous, v.layout);
            } else {
                height += calculateVGap(nullptr, nullptr);
            }
        }
        previous = v.layout;

        // Perform layout of the element.  Track impact on width and height.
        auto size = v.layout->Layout(cbc);
        v.size = size; // TODO: does that work?
        height += size.dy;
        width = std::max(width, size.dx);
    }
    totalHeight = height;

    // Need to adjust width to any widgets that have flex
    if (totalFlex > 0) {
        auto extraHeight = int(0);
        if (bc.HasBoundedHeight() && bc.max.dy > this->totalHeight) {
            extraHeight = bc.max.dy - this->totalHeight;
        } else if (bc.min.dy > this->totalHeight) {
            extraHeight = bc.min.dy - this->totalHeight;
        }

        if (extraHeight > 0) {
            for (auto& v : children) {
                if (v.flex > 0) {
                    auto oldHeight = v.size.dy;
                    auto extra = scale(extraHeight, v.flex, totalFlex);
                    auto fbc = cbc.TightenHeight(v.size.dy + extra);
                    auto size = v.layout->Layout(fbc);
                    v.size = size;
                    this->totalHeight += size.dy - oldHeight;
                }
            }
        }
    }
    if (this->alignCross == CrossAxisAlign::Stretch) {
        return bc.Constrain(Size{cbc.min.dx, height});
    }

    return bc.Constrain(Size{width, height});
}

int VBox::MinIntrinsicWidth(int height) {
    auto n = this->ChildrenCount();
    if (n == 0) {
        return 0;
    }
    if (this->alignMain == MainAxisAlign::Homogeneous) {
        height = guardInf(height, scale(height, 1, i64(n)));
        auto size = children[0].layout->MinIntrinsicWidth(height);
        for (int i = 1; i < n; i++) {
            auto& v = children[i];
            size = std::max(size, v.layout->MinIntrinsicWidth(height));
        }
        return size;
    }
    auto size = children[0].layout->MinIntrinsicWidth(Inf);
    for (int i = 1; i < n; i++) {
        auto& v = this->children[i];
        size = std::max(size, v.layout->MinIntrinsicWidth(Inf));
    }
    return size;
}

int VBox::MinIntrinsicHeight(int width) {
    auto n = this->ChildrenCount();
    if (n == 0) {
        return 0;
    }
    auto size = children[0].layout->MinIntrinsicHeight(width);
    if (IsPacked(this->alignMain)) {
        auto previous = children[0].layout;
        for (int i = 1; i < n; i++) {
            auto& v = children[i];
            // Add the preferred gap between this pair of widgets
            size += calculateVGap(previous, v.layout);
            previous = v.layout;
            // Find minimum size for this widget, and update
            size += v.layout->MinIntrinsicHeight(width);
        }
        return size;
    }

    if (this->alignMain == MainAxisAlign::Homogeneous) {
        for (int i = 1; i < n; i++) {
            auto& v = this->children[i];
            size = std::max(size, v.layout->MinIntrinsicHeight(width));
        }

        // Add a minimum gap between the controls.
        auto vgap = calculateVGap(nullptr, nullptr);
        size = scale(size, i64(n), 1) + scale(vgap, i64(n) - 1, 1);
        return size;
    }

    for (int i = 1; i < n; i++) {
        auto& v = this->children[i];
        size += v.layout->MinIntrinsicHeight(width);
    }

    // Add a minimum gap between the controls.
    auto vgap = calculateVGap(nullptr, nullptr);
    if (this->alignMain == MainAxisAlign::SpaceBetween) {
        size += scale(vgap, i64(n) - 1, 1);
    } else {
        size += scale(vgap, i64(n) + 1, 1);
    }

    return size;
}

void VBox::SetBounds(Rect bounds) {
    lastBounds = bounds;

    auto n = ChildrenCount();
    if (n == 0) {
        return;
    }
    dbglayoutf("VBox:SetBounds() %d,%d - %d, %d %d children\n", bounds.x, bounds.y, bounds.dx, bounds.dy, n);

    if (this->alignMain == MainAxisAlign::Homogeneous) {
        auto gap = calculateVGap(nullptr, nullptr);
        auto dy = bounds.Dy() + gap;
        auto count = i64(n);

        for (int i = 0; i < n; i++) {
            auto& v = children[i];
            auto y1 = bounds.y + scale(dy, i, count);
            auto y2 = bounds.y + scale(dy, i + 1, count) - gap;
            SetBoundsForChild(i, v.layout, bounds.x, y1, bounds.Right(), y2);
        }
        return;
    }

    // Adjust the bounds so that the minimum Y handles vertical alignment
    // of the controls.  We also calculate 'extraGap' which will adjust
    // spacing of the controls for non-packed alignments.
    auto extraGap = int(0);
    if (totalFlex == 0) {
        switch (alignMain) {
            case MainAxisAlign::MainStart:
                // Do nothing
                break;
            case MainAxisAlign::MainCenter:
                bounds.y += (bounds.Dy() - totalHeight) / 2;
                break;
            case MainAxisAlign::MainEnd:
                bounds.y = bounds.Bottom() - totalHeight;
                break;
            case MainAxisAlign::SpaceAround: {
                int l = (bounds.Dy() - totalHeight);
                extraGap = scale(l, 1, i64(n) + 1);
                bounds.y += extraGap;
                extraGap += calculateVGap(nullptr, nullptr);
                break;
            }
            case MainAxisAlign::SpaceBetween:
                if (n > 1) {
                    int l = (bounds.Dy() - totalHeight);
                    extraGap = scale(l, 1, i64(n) - 1);
                    extraGap += calculateVGap(nullptr, nullptr);
                } else {
                    // There are no controls between which to put the extra space.
                    // The following essentially convert SpaceBetween to SpaceAround
                    bounds.y += (bounds.Dy() - totalHeight) / 2;
                }
                break;
        }
    }

    // Position all of the child controls.
    auto posY = bounds.y;
    ILayout* previous = nullptr;
    for (int i = 0; i < n; i++) {
        auto& v = children[i];
        if (v.layout->visibility == Visibility::Collapse) {
            continue;
        }
        if (IsPacked(alignMain)) {
            if (i > 0) {
                posY += calculateVGap(previous, v.layout);
            }
            previous = v.layout;
        }

        auto dy = v.size.dy;
        SetBoundsForChild(i, v.layout, bounds.x, posY, bounds.Right(), posY + dy);
        posY += dy + extraGap;
    }
}

void VBox::SetBoundsForChild(int i, ILayout* v, int posX, int posY, int posX2, int posY2) {
    auto dx = children[i].size.dx;
    Rect r{};
    switch (alignCross) {
        case CrossAxisAlign::CrossStart:
            r = Rect{
                Point{posX, posY},
                Point{posX + dx, posY2},
            };
            break;
        case CrossAxisAlign::CrossCenter:
            r = Rect{
                Point{posX + (posX2 - posX - dx) / 2, posY},
                Point{posX + (posX2 - posX + dx) / 2, posY2},
            };
            break;
        case CrossAxisAlign::CrossEnd:
            r = Rect{
                Point{posX2 - dx, posY},
                Point{posX2, posY2},
            };
            break;
        case CrossAxisAlign::Stretch:
            r = Rect{
                Point{posX, posY},
                Point{posX2, posY2},
            };
            break;
    }
    v->SetBounds(r);
}

boxElementInfo& VBox::AddChild(ILayout* child, int flex) {
    boxElementInfo v{};
    v.layout = child;
    v.flex = flex;
    children.Append(v);
    auto n = children.size();
    return children[n - 1];
}

boxElementInfo& VBox::AddChild(ILayout* child) {
    return AddChild(child, 0);
}

// hbox.go
Kind kindHBox = "hbox";

bool IsHBox(Kind kind) {
    return kind == kindHBox;
}

bool IsHBox(ILayout* l) {
    return IsLayoutOfKind(l, kindHBox);
}

HBox::~HBox() {
    for (auto& c : children) {
        delete c.layout;
    }
}

int HBox::ChildrenCount() {
    return children.isize();
}

Size HBox::Layout(const Constraints bc) {
    auto n = ChildrenCount();
    if (n == 0) {
        totalWidth = 0;
        return bc.Constrain(Size{});
    }
    totalFlex = updateFlex(this->children, this->alignMain);
    dbglayoutf("HBox::Layout() %d children, %d totalFlex ", n, totalFlex);
    LogConstraints(bc, "\n");

    // Determine the constraints for layout of child elements.
    auto cbc = bc;
    if (alignMain == MainAxisAlign::Homogeneous) {
        auto count = i64(n);
        auto gap = calculateHGap(nullptr, nullptr);
        auto maxw = cbc.max.dx;
        cbc.TightenWidth(scale(maxw, 1, count) - scale(gap, count - 1, count));
    } else {
        cbc.min.dx = 0;
        cbc.max.dx = Inf;
    }

    if (alignCross == CrossAxisAlign::Stretch) {
        if (cbc.HasBoundedHeight()) {
            cbc = cbc.TightenHeight(cbc.max.dy);
        } else {
            cbc = cbc.TightenHeight(MinIntrinsicHeight(Inf));
        }
    } else {
        cbc = cbc.LoosenHeight();
    }
    auto width = int(0);
    auto height = int(0);
    ILayout* previous = nullptr;

    for (int i = 0; i < n; i++) {
        auto& v = children[i];
        if (v.layout->visibility == Visibility::Collapse) {
            continue;
        }
        // Determine what gap needs to be inserted between the elements.
        if (i > 0) {
            if (IsPacked(alignMain)) {
                width += calculateHGap(previous, v.layout);
            } else {
                width += calculateHGap(nullptr, nullptr);
            }
        }
        previous = v.layout;

        // Perform layout of the element.  Track impact on width and height.
        auto size = v.layout->Layout(cbc);
        v.size = size;
        width += size.dx;
        height = std::max(height, size.dy);
    }
    totalWidth = width;

    // Need to adjust height to any widgets that have flex
    if (totalFlex > 0) {
        auto extraWidth = int(0);
        if (bc.HasBoundedWidth() && bc.max.dx > totalWidth) {
            extraWidth = bc.max.dx - totalWidth;
        } else if (bc.min.dx > totalWidth) {
            extraWidth = bc.min.dx - totalWidth;
        }

        if (extraWidth > 0) {
            for (int i = 0; i < n; i++) {
                auto& v = children[i];
                if (v.flex > 0) {
                    auto oldWidth = v.size.dx;
                    auto nw = v.size.dx + extraWidth;
                    auto fbc = cbc.TightenWidth(scale(nw, v.flex, totalFlex));
                    auto size = v.layout->Layout(fbc);
                    v.size = size;
                    totalWidth += size.dx - oldWidth;
                }
            }
        }
    }
    if (alignCross == CrossAxisAlign::Stretch) {
        return bc.Constrain(Size{width, cbc.min.dy});
    }
    return bc.Constrain(Size{width, height});
}

int HBox::MinIntrinsicHeight(int width) {
    auto n = ChildrenCount();
    if (n == 0) {
        return 0;
    }

    if (alignMain == MainAxisAlign::Homogeneous) {
        width = guardInf(width, scale(width, 1, i64(n)));
        auto size = children[0].layout->MinIntrinsicHeight(width);
        for (int i = 1; i < n; i++) {
            auto v = children[i];
            size = std::max(size, v.layout->MinIntrinsicHeight(width));
        }
        return size;
    }

    auto size = children[0].layout->MinIntrinsicHeight(Inf);
    for (int i = 1; i < n; i++) {
        auto& v = children[i];
        size = std::max(size, v.layout->MinIntrinsicHeight(Inf));
    }
    return size;
}

int HBox::MinIntrinsicWidth(int height) {
    auto n = ChildrenCount();
    if (n == 0) {
        return 0;
    }

    auto size = children[0].layout->MinIntrinsicWidth(height);
    if (IsPacked(alignMain)) {
        auto previous = children[0].layout;
        for (int i = 1; i < n; i++) {
            auto& v = children[i];
            // Add the preferred gap between this pair of widgets
            size += calculateHGap(previous, v.layout);
            previous = v.layout;
            // Find minimum size for this widget, and update
            size += v.layout->MinIntrinsicWidth(height);
        }
        return size;
    }

    if (alignMain == MainAxisAlign::Homogeneous) {
        for (int i = 1; i < n; i++) {
            auto& v = children[i];
            size = std::max(size, v.layout->MinIntrinsicWidth(height));
        }

        // Add a minimum gap between the controls.
        auto hgap = calculateHGap(nullptr, nullptr);
        size = scale(size, i64(n), 1) + scale(hgap, i64(n) - 1, 1);
        return size;
    }

    for (int i = 1; i < n; i++) {
        auto v = children[i].layout;
        size += v->MinIntrinsicWidth(height);
    }

    // Add a minimum gap between the controls.
    auto hgap = calculateHGap(nullptr, nullptr);
    if (alignMain == MainAxisAlign::SpaceBetween) {
        size += scale(hgap, i64(n) - 1, 1);
    } else {
        size += scale(hgap, i64(n) + 1, 1);
    }

    return size;
}

void HBox::SetBounds(Rect bounds) {
    dbglayoutf("HBox:SetBounds() %d,%d - %d, %d\n", bounds.x, bounds.y, bounds.dx, bounds.dy);
    lastBounds = bounds;
    auto n = ChildrenCount();
    if (n == 0) {
        return;
    }

    if (alignMain == MainAxisAlign::Homogeneous) {
        auto gap = calculateHGap(nullptr, nullptr);
        auto dx = bounds.Dx() + gap;
        auto count = i64(n);

        for (int i = 0; i < n; i++) {
            auto v = children[i].layout;
            auto x1 = bounds.x + scale(dx, i, count);
            auto x2 = bounds.x + scale(dx, i + 1, count) - gap;
            SetBoundsForChild(i, v, x1, bounds.y, x2, bounds.Bottom());
        }
        return;
    }

    // Adjust the bounds so that the minimum Y handles vertical alignment
    // of the controls.  We also calculate 'extraGap' which will adjust
    // spacing of the controls for non-packed alignments.
    auto extraGap = int(0);
    if (totalFlex == 0) {
        switch (alignMain) {
            case MainAxisAlign::MainStart:
                // Do nothing
                break;
            case MainAxisAlign::MainCenter:
                bounds.x += (bounds.Dx() - totalWidth) / 2;
                break;
            case MainAxisAlign::MainEnd:
                bounds.x = bounds.Right() - totalWidth;
                break;
            case MainAxisAlign::SpaceAround: {
                auto eg = (bounds.Dx() - totalWidth);
                extraGap = scale(eg, 1, i64(n) + 1);
                bounds.x += extraGap;
                extraGap += calculateHGap(nullptr, nullptr);
            } break;
            case MainAxisAlign::SpaceBetween:
                if (n > 1) {
                    auto eg = (bounds.Dx() - totalWidth);
                    extraGap = scale(eg, 1, i64(n) - 1);
                    extraGap += calculateHGap(nullptr, nullptr);
                } else {
                    // There are no controls between which to put the extra space.
                    // The following essentially convert SpaceBetween to SpaceAround
                    bounds.x += (bounds.Dx() - totalWidth) / 2;
                }
                break;
        }
    }

    // Position all of the child controls.
    auto posX = bounds.x;
    ILayout* previous = nullptr;
    for (int i = 0; i < n; i++) {
        auto& v = children[i];
        if (IsPacked(alignMain)) {
            if (i > 0) {
                posX += calculateHGap(previous, v.layout);
            }
            previous = v.layout;
        }

        auto dx = children[i].size.dx;
        SetBoundsForChild(i, v.layout, posX, bounds.y, posX + dx, bounds.Bottom());
        posX += dx + extraGap;
    }
}

void HBox::SetBoundsForChild(int i, ILayout* v, int posX, int posY, int posX2, int posY2) {
    auto dy = children[i].size.dy;
    switch (alignCross) {
        case CrossAxisAlign::CrossStart:
            v->SetBounds(Rect{
                Point{posX, posY},
                Point{posX2, posY + dy},
            });
            break;
        case CrossAxisAlign::CrossCenter:
            v->SetBounds(Rect{
                Point{posX, posY + (posY2 - posY - dy) / 2},
                Point{posX2, posY + (posY2 - posY + dy) / 2},
            });
            break;
        case CrossAxisAlign::CrossEnd:
            v->SetBounds(Rect{
                Point{posX, posY2 - dy},
                Point{posX2, posY2},
            });
            break;
        case CrossAxisAlign::Stretch:
            v->SetBounds(Rect{
                Point{posX, posY},
                Point{posX2, posY2},
            });
            break;
    }
}

boxElementInfo& HBox::AddChild(ILayout* child, int flex) {
    boxElementInfo v{};
    v.layout = child;
    v.flex = flex;
    children.Append(v);
    auto n = children.size();
    return children[n - 1];
}

boxElementInfo& HBox::AddChild(ILayout* child) {
    return AddChild(child, 0);
}

// align.go

Kind kindAlign = "align";

bool IsAlign(Kind kind) {
    return kind == kindAlign;
}

bool IsAlign(ILayout* l) {
    return IsLayoutOfKind(l, kindAlign);
}

Align::Align(ILayout* c) {
    Child = c;
    kind = kindAlign;
}

Align::~Align() {
}

Size Align::Layout(const Constraints bc) {
    dbglayoutf("Align::Layout() ");
    LogConstraints(bc, "\n");

    Size size = this->Child->Layout(bc.Loosen());
    this->childSize = size;
    auto f = this->WidthFactor;
    if (f > 0) {
        size.dx = i32(float(size.dx) * f);
    }
    f = this->HeightFactor;
    if (f > 0) {
        size.dy = i32(float(size.dy) * f);
    }
    return bc.Constrain(size);
}

int Align::MinIntrinsicHeight(int width) {
    int height = this->Child->MinIntrinsicHeight(width);
    auto f = this->HeightFactor;
    if (f > 0) {
        return int(float(height) * f);
    }
    return height;
}

int Align::MinIntrinsicWidth(int height) {
    int width = this->Child->MinIntrinsicHeight(height);
    auto f = this->WidthFactor;
    if (f > 0) {
        return int(float(width) * f);
    }
    return width;
}

void Align::SetBounds(Rect bounds) {
    dbglayoutf("Align:SetBounds() %d,%d - %d, %d\n", bounds.x, bounds.y, bounds.dx, bounds.dy);

    lastBounds = bounds;
    int bminx = bounds.x;
    int bmaxx = bounds.Right();
    int cw = this->childSize.dx;
    i64 twm = AlignStart - AlignEnd;
    i64 tw = AlignEnd - AlignStart;
    int x = scale(bminx, this->HAlign - AlignEnd, twm) + scale(bmaxx - cw, this->HAlign - AlignStart, tw);
    int ch = this->childSize.dy;
    int bminy = bounds.y;
    int bmaxy = bounds.Bottom();
    int y = scale(bminy, this->VAlign - AlignEnd, twm) + scale(bmaxy - ch, this->VAlign - AlignStart, tw);
    Rect b{Point{x, y}, Point{x + cw, y + ch}};
    this->Child->SetBounds(b);
}

// expand.go

Kind kindExpand = "expand";

bool IsExpand(Kind kind) {
    return kind == kindExpand;
}

bool IsExpand(ILayout* l) {
    return IsLayoutOfKind(l, kindExpand);
}

Expand::Expand(ILayout* c, int f) {
    kind = kindExpand;
    child = c;
    factor = f;
}

Expand* CreateExpand(ILayout* child, int factor) {
    return new Expand{child, factor};
}

Expand::~Expand() {
}

Size Expand::Layout(const Constraints bc) {
    dbglayoutf("Expand::Layout() ");
    LogConstraints(bc, "\n");

    return child->Layout(bc);
}

int Expand::MinIntrinsicHeight(int width) {
    return child->MinIntrinsicHeight(width);
}

int Expand::MinIntrinsicWidth(int height) {
    return child->MinIntrinsicWidth(height);
}

void Expand::SetBounds(Rect bounds) {
    dbglayoutf("Expand:SetBounds() %d,%d - %d, %d\n", bounds.x, bounds.y, bounds.dx, bounds.dy);
    lastBounds = bounds;
    return child->SetBounds(bounds);
}

Kind kindLabel = "label";

bool IsLabeL(Kind kind) {
    return kind == kindLabel;
}

extern bool IsStatic(Kind);

bool IsLabel(ILayout* l) {
    if (!l) {
        return false;
    }
    return IsLayoutOfKind(l, kindLabel);
}

void LayoutAndSizeToContent(ILayout* layout, int minDx, int minDy, HWND hwnd) {
    dbglayoutf("\nLayoutAndSizeToContent() %d,%d\n", minDx, minDy);

    Constraints c = ExpandInf();
    c.min = {minDx, minDy};
    auto size = layout->Layout(c);
    Point min{0, 0};
    Point max{size.dx, size.dy};
    Rect bounds{min, max};
    layout->SetBounds(bounds);
    ResizeHwndToClientArea(hwnd, size.dx, size.dy, false);
    InvalidateRect(hwnd, nullptr, false);
}

Size LayoutToSize(ILayout* layout, const Size size) {
    dbglayoutf("\nLayoutToSize() %d,%d\n", size.dx, size.dy);
    auto c = Tight(size);
    auto newSize = layout->Layout(c);
    Rect bounds{0, 0, newSize.dx, newSize.dy};
    layout->SetBounds(bounds);
    return newSize;
}

Size Relayout(ILayout* layout) {
    auto b = layout->lastBounds;
    return LayoutToSize(layout, b.Size());
}

// TODO: probably not needed
Insets DefaultInsets() {
    const int padding = 8;
    return Insets{padding, padding, padding, padding};
}

// TODO: probably not needed
Insets UniformInsets(int l) {
    return Insets{l, l, l, l};
}

Insets DpiScaledInsets(HWND hwnd, int top, int right, int bottom, int left) {
    CrashIf(top < 0);
    if (right == -1) {
        // only first given, consider all to be the same
        right = top;
        bottom = top;
        left = top;
    }
    if (bottom == -1) {
        // first 2 given, consider them top / bottom, left / right
        bottom = top;
        left = right;
    }
    CrashIf(left == -1);
    Insets res = {DpiScale(hwnd, top), DpiScale(hwnd, right), DpiScale(hwnd, bottom), DpiScale(hwnd, left)};
    return res;
}

Kind kindSpacer = "spacer";

Spacer::Spacer(int dx, int dy) {
    kind = kindSpacer;
    this->dx = dx;
    this->dy = dy;
}

Spacer::~Spacer() {
    // do nothing
}

Size Spacer::Layout(const Constraints bc) {
    // do nothing
    return bc.Constrain({dx, dy});
}

int Spacer::MinIntrinsicHeight(int width) {
    return dy;
}
int Spacer::MinIntrinsicWidth(int height) {
    return dx;
}
void Spacer::SetBounds(Rect) {
    // do nothing
}
