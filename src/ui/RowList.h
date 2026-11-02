#pragma once

#include "UiTheme.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

// Settings-style list: rows grouped into white cards under small section headers.
// A row can carry a file icon, a title with an optional detail line, a status pill,
// a toggle switch, one text button and icon buttons.
//
// Design rule, the same on every page: from the right, the expand button and other
// icon buttons, the toggle, then a text button such as Choose... or
// Settings. Each of them stands in one column per card. A row with only a text button,
// such as Add or Reset, has it at the far right. Clicks arrive at the parent as
// WM_COMMAND(MAKEWPARAM(id, notification), hwnd); NotifiedRow() and
// NotifiedIconButton() say which row and icon button were used.
//
// A row with expandHeight > 0 shows an extra area below its top line. The parent
// creates edit controls there as children of Handle(), places them with
// ExpansionRect() and moves them again on kLayoutChanged (scrolling, resizing).
// Their WM_COMMAND messages are passed on to the parent unchanged.
//
// A row with choices shows them as an inline segmented control in the text button's place and column, for a
// choice among a few short values; clicking one sends kChoice with NotifiedChoice() the segment. With choiceFlags
// each segment switches on its own and choice holds a bit per segment, as a bar of toggle buttons.
//
// A row with a slider (sliderMax above sliderMin) shows it in the same place, its value as text beside it;
// dragging it, clicking its track or the arrow keys send kSlider with the row's slider the new value.
//
// Rows that share a dragGroup can be put in another order by dragging one by the grip at its left, six dots, up or
// down: a line shows where it goes, and on release the parent gets kMoved with NotifiedRow() the dragged row and
// DropRow() the row whose place it takes. A row of the group that is not draggable has no grip and stays put as
// others pass it; its text still lines up with theirs.
// Row icon buttons, the same on every page (Segoe Fluent Icons / Segoe MDL2 Assets).
namespace Glyph
{
    inline constexpr wchar_t Expand = L'\uE70D', Collapse = L'\uE70E', Up = L'\uE74A', Down = L'\uE74B', Remove = L'\uE74D', Reset = L'\uE7A7', More = L'\uE712';
}

class RowList
{
public:
    enum Notification : WORD
    {
        kToggled = 1,
        kButton = 2,
        kIconButton = 3,
        kActivated = 4,
        kDeleteRequested = 5,
        // Rows moved (scrolled, resized or replaced); reposition controls in expansions.
        kLayoutChanged = 6,
        // A row was dragged to another place in its group.
        kMoved = 7,
        // A segment of a row's choices was clicked, or chosen with the arrow keys.
        kChoice = 8,
        // A row's slider moved to another value.
        kSlider = 9
    };

    enum class Tone
    {
        Neutral,
        Active,
        Warning,
        Accent
    };

    struct Row
    {
        // A header starts a new card; the rows after it belong to that card.
        bool header{};
        std::wstring title;
        std::wstring detail;
        std::wstring pill;
        Tone pillTone{Tone::Neutral};
        bool showIcon{};
        std::wstring iconPath;
        // -1: no toggle, 0: off, 1: on.
        int toggle{-1};
        std::wstring toggleLabel;
        bool toggleEnabled{true};
        std::wstring button;
        bool buttonEnabled{true};
        bool buttonDanger{}; // Red, for what cannot be undone: removing.
        // Instead of a button: the selected one of these short values, or with choiceFlags a bit per value that is on.
        std::vector<std::wstring> choices;
        int choice{-1};
        bool choiceFlags{};
        // Instead of a button: a slider from sliderMin to sliderMax in steps of sliderStep, at slider, with its unit.
        int slider{}, sliderMin{}, sliderMax{}, sliderStep{1};
        std::wstring sliderUnit;
        // Glyphs from Segoe Fluent Icons / Segoe MDL2 Assets; [0] is the rightmost:
        // expand first, then the others. Every row of a card with a toggle or icon
        // buttons reserves as many slots as the row with the most, so toggles and
        // buttons line up. A 0 glyph leaves its slot empty, which keeps
        // the same button in the same column when only some rows have, say, an arrow.
        std::vector<wchar_t> iconButtons;
        bool muted{};
        bool selected{}; // A light accent fill and a bold title.
        int indent{}; // DIPs: child rows inside an expandable group.
        // Height in DIPs of the expanded area below the row; 0 when collapsed.
        int expandHeight{};
        // Rows with the same nonzero group can be reordered by dragging; draggable says whether this one can be picked up.
        int dragGroup{};
        bool draggable{true};
        bool operator==(const Row&) const = default;
    };

    RowList() = default;
    RowList(const RowList&) = delete;
    RowList& operator=(const RowList&) = delete;
    ~RowList() { ReleaseResources(); }

    bool Create(HINSTANCE instance, HWND parent, int id, HFONT titleFont, HFONT textFont)
    {
        WNDCLASSW windowClass{};
        if (!GetClassInfoW(instance, kClassName, &windowClass))
        {
            windowClass.style = CS_DBLCLKS;
            windowClass.lpfnWndProc = WindowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.lpszClassName = kClassName;
            if (!RegisterClassW(&windowClass)) return false;
        }
        id_ = id;
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 100, 100, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, this);
        if (!window_) return false;
        SetFonts(titleFont, textFont);
        return true;
    }

    HWND Handle() const noexcept { return window_; }
    int NotifiedRow() const noexcept { return notifiedRow_; }
    int NotifiedIconButton() const noexcept { return notifiedIconButton_; }
    int NotifiedChoice() const noexcept { return notifiedIconButton_; }
    int DropRow() const noexcept { return dropRow_; }
    const std::vector<Row>& Rows() const noexcept { return rows_; }

    void SetFonts(HFONT titleFont, HFONT textFont)
    {
        titleFont_ = titleFont;
        textFont_ = textFont;
        if (glyphFont_) DeleteObject(glyphFont_);
        glyphFont_ = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, UiTheme::IconFontFace());
        UpdateScrollRange();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void SetRows(std::vector<Row> rows)
    {
        if (rows == rows_) return;
        rows_ = std::move(rows);
        if (focused_ >= static_cast<int>(rows_.size())) focused_ = -1;
        hover_ = {};
        UpdateScrollRange();
        InvalidateRect(window_, nullptr, FALSE);
        NotifyLayout();
    }

    // Client rectangle of a row's expanded area; empty when the row is collapsed.
    RECT ExpansionRect(int index) const
    {
        const auto layouts = Compute();
        if (index < 0 || index >= static_cast<int>(layouts.size())) return {};
        RECT rect = layouts[static_cast<size_t>(index)].expansion;
        OffsetRect(&rect, 0, -scroll_);
        return rect;
    }
    // Client rectangle of a row's toggle; empty without one.
    RECT ToggleRect(int index) const
    {
        const auto layouts = Compute();
        if (index < 0 || index >= static_cast<int>(layouts.size()) || rows_[static_cast<size_t>(index)].toggle < 0) return {};
        RECT rect = layouts[static_cast<size_t>(index)].toggle;
        OffsetRect(&rect, 0, -scroll_);
        return rect;
    }

    // Client rectangle of a row's text button, its choices or its slider; empty without any.
    RECT ButtonRect(int index) const
    {
        const auto layouts = Compute();
        if (index < 0 || index >= static_cast<int>(layouts.size()) || !HasButton(rows_[static_cast<size_t>(index)])) return {};
        RECT rect = layouts[static_cast<size_t>(index)].button;
        OffsetRect(&rect, 0, -scroll_);
        return rect;
    }

    // Acts on a row's icon button as a click on it does.
    void PressIconButton(int index, int iconButton)
    {
        if (index >= 0 && index < static_cast<int>(rows_.size())) Activate({index, PartKind::IconButton, iconButton});
    }
    // Acts on a row's text button as a click on it does.
    void PressButton(int index)
    {
        if (index >= 0 && index < static_cast<int>(rows_.size())) Activate({index, PartKind::Button});
    }
    // Moves a row's slider to a value as dragging it does.
    void SlideTo(int index, int value)
    {
        if (index >= 0 && index < static_cast<int>(rows_.size()) && HasSlider(rows_[static_cast<size_t>(index)])) SetSlider(index, value);
    }
    // Acts on one of a row's choices as a click on it does.
    void PressChoice(int index, int choice)
    {
        if (index >= 0 && index < static_cast<int>(rows_.size()) && choice >= 0 && choice < static_cast<int>(rows_[static_cast<size_t>(index)].choices.size()))
            Activate({index, PartKind::Choice, choice});
    }

    // Client rectangle of a row's top line.
    RECT RowRect(int index) const
    {
        const auto layouts = Compute();
        if (index < 0 || index >= static_cast<int>(layouts.size())) return {};
        RECT rect = layouts[static_cast<size_t>(index)].bounds;
        rect.bottom = rect.top + LineHeight(rows_[static_cast<size_t>(index)]);
        OffsetRect(&rect, 0, -scroll_);
        return rect;
    }

    // Keeps the expanded area of a row in view, e.g. right after expanding it.
    void ScrollIntoView(int index)
    {
        EnsureVisible(index);
    }

    // Scrolls by the mouse wheel and the keyboard only, with no bar taking room at the right.
    void HideScrollBar()
    {
        noScrollBar_ = true;
        SetWindowLongPtrW(window_, GWL_STYLE, GetWindowLongPtrW(window_, GWL_STYLE) & ~WS_VSCROLL);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        UpdateScrollRange();
        InvalidateRect(window_, nullptr, FALSE);
    }

private:
    static constexpr wchar_t kClassName[] = L"LaunchMateRowList";

    enum class PartKind { None, Body, Toggle, Button, IconButton, Grip, Choice, Slider };
    struct Part
    {
        int row{-1};
        PartKind kind{PartKind::None};
        int iconButton{-1};
        bool operator==(const Part&) const = default;
    };

    struct Layout
    {
        RECT bounds{};      // the row inside its card
        RECT toggle{};
        RECT button{};
        std::vector<RECT> iconButtons;
        std::vector<RECT> choices;
        RECT slider{};      // the track, the value's text right of it
        RECT pill{};
        RECT text{};
        RECT icon{};
        RECT grip{};
        RECT expansion{};
        bool firstInCard{};
        bool lastInCard{};
    };

    int Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }
    static bool HasSlider(const Row& row) { return row.sliderMax > row.sliderMin; }
    static bool HasButton(const Row& row) { return !row.button.empty() || !row.choices.empty() || HasSlider(row); }
    bool Chosen(const Row& row, size_t choice) const { return row.choiceFlags ? (row.choice >= 0 && (row.choice >> choice & 1)) : row.choice == static_cast<int>(choice); }

    void ReleaseResources()
    {
        for (auto& [path, icon] : icons_)
            if (icon) DestroyIcon(icon);
        icons_.clear();
        if (glyphFont_) DeleteObject(glyphFont_);
        glyphFont_ = nullptr;
    }

    HICON IconFor(const std::wstring& path)
    {
        const auto found = icons_.find(path);
        if (found != icons_.end()) return found->second;
        SHFILEINFOW info{};
        const DWORD_PTR result = path.empty()
            ? SHGetFileInfoW(L".exe", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES)
            : SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON);
        const HICON icon = result != 0 ? info.hIcon : nullptr;
        icons_.emplace(path, icon);
        return icon;
    }

    int TextWidth(HDC dc, HFONT font, const std::wstring& text) const
    {
        const auto old = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, old);
        return size.cx;
    }

    int RowHeight(const Row& row) const
    {
        if (row.header) return Scale(34);
        return LineHeight(row) + Scale(row.expandHeight);
    }

    // Height of the row's top line, without an expanded area.
    int LineHeight(const Row& row) const
    {
        return Scale(row.detail.empty() ? 44 : 58);
    }

    // Content coordinates (unscrolled) of every row.
    std::vector<Layout> Compute() const
    {
        RECT client{};
        GetClientRect(window_, &client);
        const int right = client.right - Scale(1);
        std::vector<Layout> layouts(rows_.size());
        const HDC dc = GetDC(window_);
        // Columns per card: rows between two headers share the icon button slots and,
        // when any of them has one, the toggle's column.
        std::vector<size_t> slots(rows_.size());
        std::vector<bool> toggleColumn(rows_.size());
        for (size_t first = 0; first < rows_.size();)
        {
            size_t last = first;
            size_t most = 0;
            bool toggles = false;
            for (; last < rows_.size() && !(rows_[last].header && last != first); ++last)
            {
                if (rows_[last].header) continue;
                // Only slots some row uses: trailing empty glyphs leave no gap.
                const auto& icons = rows_[last].iconButtons;
                size_t used = icons.size();
                while (used > 0 && icons[used - 1] == 0) --used;
                most = std::max(most, used);
                toggles = toggles || rows_[last].toggle >= 0;
            }
            for (size_t index = first; index < last; ++index) { slots[index] = most; toggleColumn[index] = toggles; }
            first = last;
        }
        int y = 0;
        bool inCard = false;
        for (size_t index = 0; index < rows_.size(); ++index)
        {
            const auto& row = rows_[index];
            auto& layout = layouts[index];
            const int height = RowHeight(row);
            if (row.header)
            {
                if (inCard) y += Scale(14);
                layout.bounds = {0, y, right, y + height};
                layout.text = {Scale(4), y, right, y + height - Scale(8)};
                inCard = false;
                y += height;
                continue;
            }
            layout.firstInCard = !inCard;
            inCard = true;
            layout.bounds = {0, y, right, y + height};
            const int lineHeight = LineHeight(row);
            if (row.expandHeight > 0)
                layout.expansion = {Scale(14), y + lineHeight, right - Scale(14), y + height - Scale(10)};
            int x = right - Scale(12);
            const int middle = y + lineHeight / 2;
            // A row with only a text button (Add, Reset) has it at the far right.
            const bool controls = row.toggle >= 0 || !row.iconButtons.empty();
            if (controls)
            {
                for (size_t button = 0; button < slots[index]; ++button)
                {
                    // Empty slots (0 glyphs, or slots only other rows use) get an empty rectangle.
                    const bool used = button < row.iconButtons.size() && row.iconButtons[button] != 0;
                    if (button < row.iconButtons.size())
                        layout.iconButtons.push_back(used ? RECT{x - Scale(30), middle - Scale(15), x, middle + Scale(15)} : RECT{});
                    x -= Scale(32);
                }
                if (slots[index] != 0) x -= Scale(4);
            }
            if (row.toggle >= 0)
            {
                layout.toggle = {x - Scale(40), middle - Scale(10), x, middle + Scale(10)};
                x -= Scale(40);
                if (!row.toggleLabel.empty())
                {
                    const int width = TextWidth(dc, textFont_, row.toggleLabel);
                    x -= width + Scale(10);
                }
                x -= Scale(12);
            }
            else if (controls && toggleColumn[index] && HasButton(row))
                x -= Scale(52); // The toggle's place, so this button stands with the others.
            if (!row.choices.empty())
            {
                // Segments as wide as their text, left to right, ending where a button would.
                std::vector<int> widths;
                int total = 0;
                for (const auto& choice : row.choices) { widths.push_back(TextWidth(dc, textFont_, choice) + Scale(22)); total += widths.back(); }
                layout.button = {x - total, middle - Scale(15), x, middle + Scale(15)};
                int left = x - total;
                for (const int width : widths) { layout.choices.push_back({left, middle - Scale(15), left + width, middle + Scale(15)}); left += width; }
                x -= total + Scale(10);
            }
            else if (HasSlider(row))
            {
                const int width = Scale(220);
                layout.button = {x - width, middle - Scale(15), x, middle + Scale(15)};
                layout.slider = {x - width, middle - Scale(12), x - Scale(56), middle + Scale(12)};
                x -= width + Scale(10);
            }
            else if (!row.button.empty())
            {
                const int width = TextWidth(dc, textFont_, row.button) + Scale(28);
                layout.button = {x - width, middle - Scale(15), x, middle + Scale(15)};
                x -= width + Scale(10);
            }
            if (!row.pill.empty())
            {
                const int width = TextWidth(dc, textFont_, row.pill) + Scale(18);
                layout.pill = {x - width, middle - Scale(11), x, middle + Scale(11)};
                x -= width + Scale(12);
            }
            int left = Scale(14 + row.indent);
            if (row.dragGroup)
            {
                if (row.draggable) layout.grip = {left - Scale(4), middle - Scale(12), left + Scale(14), middle + Scale(12)};
                left += Scale(20);
            }
            if (row.showIcon)
            {
                layout.icon = {left, middle - Scale(10), left + Scale(20), middle + Scale(10)};
                left += Scale(32);
            }
            layout.text = {left, y, std::max<int>(left, x), y + lineHeight};
            y += height;
        }
        ReleaseDC(window_, dc);
        for (size_t index = 0; index < rows_.size(); ++index)
        {
            if (rows_[index].header) continue;
            layouts[index].lastInCard = index + 1 == rows_.size() || rows_[index + 1].header;
        }
        return layouts;
    }

    int ContentHeight() const
    {
        const auto layouts = Compute();
        return layouts.empty() ? 0 : layouts.back().bounds.bottom + Scale(2);
    }

    void UpdateScrollRange()
    {
        RECT client{};
        GetClientRect(window_, &client);
        const int content = ContentHeight();
        scroll_ = std::clamp(scroll_, 0, std::max(0, content - static_cast<int>(client.bottom)));
        if (noScrollBar_) return;
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMax = std::max(0, content - 1);
        info.nPage = static_cast<UINT>(std::max<LONG>(0, client.bottom));
        info.nPos = scroll_;
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
    }

    void ScrollTo(int position)
    {
        RECT client{};
        GetClientRect(window_, &client);
        position = std::clamp(position, 0, std::max(0, ContentHeight() - static_cast<int>(client.bottom)));
        if (position == scroll_) return;
        scroll_ = position;
        if (!noScrollBar_) SetScrollPos(window_, SB_VERT, scroll_, TRUE);
        InvalidateRect(window_, nullptr, FALSE);
        NotifyLayout();
    }

    void NotifyLayout()
    {
        SendMessageW(GetParent(window_), WM_COMMAND, MAKEWPARAM(id_, kLayoutChanged), reinterpret_cast<LPARAM>(window_));
    }

    void EnsureVisible(int index)
    {
        const auto layouts = Compute();
        if (index < 0 || index >= static_cast<int>(layouts.size())) return;
        RECT client{};
        GetClientRect(window_, &client);
        const auto& bounds = layouts[static_cast<size_t>(index)].bounds;
        if (bounds.top < scroll_) ScrollTo(bounds.top);
        else if (bounds.bottom > scroll_ + client.bottom) ScrollTo(bounds.bottom - client.bottom);
    }

    Part HitTest(POINT point) const
    {
        point.y += scroll_;
        const auto layouts = Compute();
        for (size_t index = 0; index < layouts.size(); ++index)
        {
            const auto& row = rows_[index];
            const auto& layout = layouts[index];
            if (row.header || !PtInRect(&layout.bounds, point)) continue;
            // The expanded area belongs to the parent's controls.
            if (PtInRect(&layout.expansion, point)) return {};
            for (size_t button = 0; button < layout.iconButtons.size(); ++button)
                if (PtInRect(&layout.iconButtons[button], point)) return {static_cast<int>(index), PartKind::IconButton, static_cast<int>(button)};
            if (PtInRect(&layout.grip, point)) return {static_cast<int>(index), PartKind::Grip};
            for (size_t choice = 0; choice < layout.choices.size(); ++choice)
                if (PtInRect(&layout.choices[choice], point)) return {static_cast<int>(index), PartKind::Choice, static_cast<int>(choice)};
            if (!row.button.empty() && PtInRect(&layout.button, point)) return {static_cast<int>(index), PartKind::Button};
            if (HasSlider(row) && PtInRect(&layout.slider, point)) return {static_cast<int>(index), PartKind::Slider};
            RECT toggle = layout.toggle;
            InflateRect(&toggle, Scale(6), Scale(8));
            if (row.toggle >= 0 && PtInRect(&toggle, point)) return {static_cast<int>(index), PartKind::Toggle};
            return {static_cast<int>(index), PartKind::Body};
        }
        return {};
    }

    // The row of the dragged row's group whose place it takes when dropped at client y: the furthest row it has
    // passed the middle of, up or down.
    int DropTarget(int y) const
    {
        const auto layouts = Compute();
        const int group = rows_[static_cast<size_t>(dragRow_)].dragGroup;
        int target = dragRow_;
        for (size_t index = 0; index < rows_.size(); ++index)
        {
            if (rows_[index].dragGroup != group || static_cast<int>(index) == dragRow_) continue;
            const auto& bounds = layouts[index].bounds;
            const int middle = bounds.top + LineHeight(rows_[index]) / 2 - scroll_;
            const bool above = static_cast<int>(index) < dragRow_;
            if (above && y < middle && static_cast<int>(index) < target) target = static_cast<int>(index);
            if (!above && y > middle && (target <= dragRow_ || static_cast<int>(index) > target)) target = static_cast<int>(index);
        }
        return target;
    }

    // The value at client x on a row's track, in its steps; the knob's middle travels the track inside its radius.
    int SliderValue(int index, int x) const
    {
        const auto& row = rows_[static_cast<size_t>(index)];
        const RECT& track = Compute()[static_cast<size_t>(index)].slider;
        const int knob = Scale(9), span = std::max<int>(1, track.right - track.left - 2 * knob);
        const double at = std::clamp(static_cast<double>(x - track.left - knob) / span, 0.0, 1.0);
        const int steps = static_cast<int>(std::lround(at * (row.sliderMax - row.sliderMin) / std::max(1, row.sliderStep)));
        return std::clamp(row.sliderMin + steps * std::max(1, row.sliderStep), row.sliderMin, row.sliderMax);
    }
    void SetSlider(int index, int value)
    {
        auto& row = rows_[static_cast<size_t>(index)];
        value = std::clamp(value, row.sliderMin, row.sliderMax);
        if (value == row.slider) return;
        row.slider = value; // Show the change at once; the parent may refresh.
        InvalidateRect(window_, nullptr, FALSE);
        Notify(kSlider, index, value);
    }

    void Notify(Notification notification, int row, int iconButton = -1)
    {
        notifiedRow_ = row;
        notifiedIconButton_ = iconButton;
        SendMessageW(GetParent(window_), WM_COMMAND, MAKEWPARAM(id_, notification), reinterpret_cast<LPARAM>(window_));
    }

    void Activate(const Part& part)
    {
        if (part.row < 0 || part.row >= static_cast<int>(rows_.size())) return;
        auto& row = rows_[static_cast<size_t>(part.row)];
        switch (part.kind)
        {
        case PartKind::Toggle:
            if (!row.toggleEnabled) return;
            row.toggle = row.toggle == 1 ? 0 : 1; // Show the change at once; the parent may refresh.
            InvalidateRect(window_, nullptr, FALSE);
            Notify(kToggled, part.row);
            return;
        case PartKind::Button:
            if (row.buttonEnabled) Notify(kButton, part.row);
            return;
        case PartKind::IconButton:
            Notify(kIconButton, part.row, part.iconButton);
            return;
        case PartKind::Choice:
            // Show the change at once; the parent may refresh.
            if (row.choiceFlags) row.choice = std::max(0, row.choice) ^ (1 << part.iconButton);
            else row.choice = part.iconButton;
            InvalidateRect(window_, nullptr, FALSE);
            Notify(kChoice, part.row, part.iconButton);
            return;
        case PartKind::Body:
        case PartKind::Grip: // A click on the grip without dragging acts as one on the row.
            Notify(kActivated, part.row);
            return;
        default:
            return;
        }
    }

    static void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border)
    {
        const HBRUSH brush = CreateSolidBrush(fill);
        const HPEN pen = CreatePen(PS_SOLID, 1, border);
        const auto oldBrush = SelectObject(dc, brush);
        const auto oldPen = SelectObject(dc, pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    static void FillRect(HDC dc, const RECT& rect, COLORREF color)
    {
        const HBRUSH brush = CreateSolidBrush(color);
        ::FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    void PaintToggle(HDC dc, RECT rect, bool on, bool enabled, bool hover)
    {
        COLORREF track = on ? UiTheme::Colors.accent : hover ? UiTheme::Colors.toggleOffHover : UiTheme::Colors.toggleOff;
        if (!enabled) track = on ? UiTheme::Colors.toggleOnDisabled : UiTheme::Colors.toggleOffDisabled;
        FillRounded(dc, rect, rect.bottom - rect.top, track, track);
        const int inset = Scale(3);
        const int knob = rect.bottom - rect.top - 2 * inset;
        const int left = on ? rect.right - inset - knob : rect.left + inset;
        const RECT circle{left, rect.top + inset, left + knob, rect.top + inset + knob};
        const HBRUSH brush = CreateSolidBrush(UiTheme::Colors.knob);
        const auto oldBrush = SelectObject(dc, brush);
        const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, circle.left, circle.top, circle.right + 1, circle.bottom + 1);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    }

    void PaintRow(HDC dc, size_t index, const Layout& base, bool focused)
    {
        const auto& row = rows_[index];
        auto layout = base;
        const auto shift = [this](RECT& rect) { OffsetRect(&rect, 0, -scroll_); };
        shift(layout.bounds); shift(layout.text); shift(layout.toggle); shift(layout.button); shift(layout.pill); shift(layout.icon); shift(layout.grip);
        shift(layout.expansion);
        for (auto& rect : layout.iconButtons) shift(rect);
        for (auto& rect : layout.choices) shift(rect);
        shift(layout.slider);
        SetBkMode(dc, TRANSPARENT);

        if (row.header)
        {
            SelectObject(dc, textFont_);
            SetTextColor(dc, UiTheme::Colors.heading);
            DrawTextW(dc, row.title.c_str(), -1, &layout.text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            return;
        }

        const COLORREF border = UiTheme::Colors.border;
        const bool hovered = hover_.row == static_cast<int>(index);
        // Expanded rows stay white: the controls in them are drawn on white.
        COLORREF fill = row.selected ? UiTheme::Colors.selected
            : hovered && hover_.kind == PartKind::Body && row.expandHeight == 0 ? UiTheme::Colors.hover : UiTheme::Surface;
        // Rows of a card share one rounded outline; inner edges are hairlines.
        RECT outline = layout.bounds;
        const int radius = Scale(10);
        if (!layout.firstInCard) outline.top -= radius;
        if (!layout.lastInCard) outline.bottom += radius;
        HRGN clip = CreateRectRgn(layout.bounds.left, layout.bounds.top, layout.bounds.right + 1, layout.bounds.bottom);
        SelectClipRgn(dc, clip);
        FillRounded(dc, outline, radius, fill, border);
        if (!layout.firstInCard)
        {
            const RECT line{layout.bounds.left + Scale(14), layout.bounds.top, layout.bounds.right - Scale(14), layout.bounds.top + 1};
            FillRect(dc, line, UiTheme::Colors.divider);
        }
        if (row.expandHeight > 0)
        {
            const RECT line{layout.expansion.left, layout.expansion.top, layout.expansion.right, layout.expansion.top + 1};
            FillRect(dc, line, UiTheme::Colors.divider);
        }
        if (focused)
        {
            RECT focus = layout.bounds;
            InflateRect(&focus, -Scale(3), -Scale(3));
            const HPEN pen = CreatePen(PS_SOLID, Scale(2), UiTheme::Colors.accent);
            const auto oldPen = SelectObject(dc, pen);
            const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, focus.left, focus.top, focus.right, focus.bottom, Scale(8), Scale(8));
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);

        if (!IsRectEmpty(&layout.grip))
        {
            // Six dots, two across and three down, darker under the mouse.
            const bool gripHover = hovered && hover_.kind == PartKind::Grip;
            const COLORREF ink = gripHover || (dragging_ && dragRow_ == static_cast<int>(index)) ? UiTheme::Colors.strong : UiTheme::Colors.faint;
            const HBRUSH brush = CreateSolidBrush(ink);
            const auto oldBrush = SelectObject(dc, brush);
            const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            const int dot = std::max(2, Scale(3)), gap = Scale(5);
            const int x0 = (layout.grip.left + layout.grip.right) / 2 - (dot + gap) / 2, y0 = (layout.grip.top + layout.grip.bottom) / 2 - dot / 2 - gap;
            for (int across = 0; across < 2; ++across)
                for (int down = 0; down < 3; ++down)
                {
                    const int x = x0 + across * gap, y = y0 + down * gap;
                    Ellipse(dc, x, y, x + dot + 1, y + dot + 1);
                }
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        }
        if (row.showIcon)
        {
            if (const HICON icon = IconFor(row.iconPath))
                DrawIconEx(dc, layout.icon.left, layout.icon.top, icon, layout.icon.right - layout.icon.left,
                    layout.icon.bottom - layout.icon.top, 0, nullptr, DI_NORMAL);
        }

        const COLORREF titleColor = row.muted ? UiTheme::Colors.mutedTitle : UiTheme::Text;
        RECT title = layout.text;
        RECT detail = layout.text;
        if (!row.detail.empty())
        {
            const int middle = (layout.text.top + layout.text.bottom) / 2;
            title.bottom = middle + Scale(1);
            detail.top = middle + Scale(1);
        }
        SelectObject(dc, row.selected && titleFont_ ? titleFont_ : textFont_);
        SetTextColor(dc, titleColor);
        DrawTextW(dc, row.title.c_str(), -1, &title,
            (row.detail.empty() ? DT_VCENTER : DT_BOTTOM) | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, textFont_);
        if (!row.detail.empty())
        {
            SetTextColor(dc, UiTheme::Colors.muted);
            DrawTextW(dc, row.detail.c_str(), -1, &detail, DT_TOP | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        if (!row.pill.empty())
        {
            const auto& colors = UiTheme::Colors;
            COLORREF pillFill = colors.pillNeutral, pillText = colors.pillNeutralInk;
            if (row.pillTone == Tone::Active) { pillFill = colors.pillActive; pillText = colors.pillActiveInk; }
            else if (row.pillTone == Tone::Warning) { pillFill = colors.pillWarning; pillText = colors.pillWarningInk; }
            else if (row.pillTone == Tone::Accent) { pillFill = colors.pillAccent; pillText = colors.pillAccentInk; }
            FillRounded(dc, layout.pill, layout.pill.bottom - layout.pill.top, pillFill, pillFill);
            SetTextColor(dc, pillText);
            DrawTextW(dc, row.pill.c_str(), -1, &layout.pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (row.toggle >= 0)
        {
            if (!row.toggleLabel.empty())
            {
                // Centered on the toggle, not the row, so it stays put when the row expands.
                RECT label{layout.text.right, layout.toggle.top - Scale(10), layout.toggle.left - Scale(10), layout.toggle.bottom + Scale(10)};
                SetTextColor(dc, row.toggleEnabled ? UiTheme::Colors.strong : UiTheme::Colors.disabled);
                DrawTextW(dc, row.toggleLabel.c_str(), -1, &label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            PaintToggle(dc, layout.toggle, row.toggle == 1, row.toggleEnabled,
                hovered && hover_.kind == PartKind::Toggle);
        }

        if (!row.button.empty())
        {
            const bool buttonHover = hovered && hover_.kind == PartKind::Button && row.buttonEnabled;
            const auto& colors = UiTheme::Colors;
            const bool danger = row.buttonDanger && row.buttonEnabled;
            FillRounded(dc, layout.button, Scale(8), buttonHover ? (danger ? colors.pillDanger : colors.buttonHover) : UiTheme::Surface,
                danger ? colors.pillDangerInk : row.buttonEnabled ? colors.buttonBorder : colors.buttonBorderDisabled);
            SetTextColor(dc, danger ? colors.pillDangerInk : row.buttonEnabled ? UiTheme::Text : colors.disabled);
            DrawTextW(dc, row.button.c_str(), -1, &layout.button, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (!row.choices.empty())
        {
            // One outline like a button's; the chosen segments filled in the accent, hairlines between the others.
            FillRounded(dc, layout.button, Scale(8), UiTheme::Surface, UiTheme::Colors.buttonBorder);
            HRGN inside = CreateRoundRectRgn(layout.button.left + 1, layout.button.top + 1, layout.button.right, layout.button.bottom, Scale(8), Scale(8));
            SelectClipRgn(dc, inside);
            for (size_t choice = 0; choice < row.choices.size(); ++choice)
            {
                const RECT& segment = layout.choices[choice];
                const bool chosen = Chosen(row, choice);
                const bool choiceHover = hovered && hover_.kind == PartKind::Choice && hover_.iconButton == static_cast<int>(choice);
                if (chosen || choiceHover) FillRect(dc, segment, chosen ? UiTheme::Colors.chosen : UiTheme::Colors.buttonHover);
                if (choice > 0 && !chosen && !Chosen(row, choice - 1))
                    FillRect(dc, {segment.left, segment.top + Scale(7), segment.left + 1, segment.bottom - Scale(7)}, UiTheme::Colors.border);
                SetTextColor(dc, chosen ? UiTheme::Colors.chosenInk : UiTheme::Text);
                RECT text = segment;
                DrawTextW(dc, row.choices[choice].c_str(), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectClipRgn(dc, nullptr);
            DeleteObject(inside);
        }

        if (HasSlider(row))
        {
            // A thin track, filled in the accent up to the knob: a ring around an accent dot, larger under the mouse.
            const RECT& track = layout.slider;
            const int knob = Scale(9), mid = (track.top + track.bottom) / 2;
            const double at = static_cast<double>(row.slider - row.sliderMin) / (row.sliderMax - row.sliderMin);
            const int x = track.left + knob + static_cast<int>(std::lround(at * (track.right - track.left - 2 * knob)));
            FillRounded(dc, {track.left + knob, mid - Scale(2), track.right - knob, mid + Scale(2)}, Scale(4), UiTheme::Colors.toggleOff, UiTheme::Colors.toggleOff);
            FillRounded(dc, {track.left + knob, mid - Scale(2), x, mid + Scale(2)}, Scale(4), UiTheme::Colors.accent, UiTheme::Colors.accent);
            const bool sliderHover = (hovered && hover_.kind == PartKind::Slider) || sliding_ == static_cast<int>(index);
            const int dot = sliderHover ? Scale(6) : Scale(5);
            const HBRUSH ring = CreateSolidBrush(UiTheme::Surface), dotBrush = CreateSolidBrush(UiTheme::Colors.accent);
            const HPEN edge = CreatePen(PS_SOLID, 1, UiTheme::Colors.buttonBorder);
            const auto oldBrush = SelectObject(dc, ring);
            const auto oldPen = SelectObject(dc, edge);
            Ellipse(dc, x - knob, mid - knob, x + knob + 1, mid + knob + 1);
            SelectObject(dc, dotBrush); SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, x - dot, mid - dot, x + dot + 1, mid + dot + 1);
            SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
            DeleteObject(edge); DeleteObject(dotBrush); DeleteObject(ring);
            SelectObject(dc, textFont_);
            SetTextColor(dc, UiTheme::Text);
            RECT text{track.right + Scale(4), layout.button.top, layout.button.right, layout.button.bottom};
            DrawTextW(dc, (std::to_wstring(row.slider) + row.sliderUnit).c_str(), -1, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(dc, glyphFont_);
        for (size_t button = 0; button < row.iconButtons.size(); ++button)
        {
            if (row.iconButtons[button] == 0) continue;
            const bool buttonHover = hovered && hover_.kind == PartKind::IconButton && hover_.iconButton == static_cast<int>(button);
            if (buttonHover) FillRounded(dc, layout.iconButtons[button], Scale(6), UiTheme::Colors.iconHover, UiTheme::Colors.iconHover);
            SetTextColor(dc, buttonHover ? UiTheme::Text : UiTheme::Colors.muted);
            DrawTextW(dc, &row.iconButtons[button], 1, &layout.iconButtons[button], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    void Paint(HDC target)
    {
        RECT client{};
        GetClientRect(window_, &client);
        const HDC dc = CreateCompatibleDC(target);
        const HBITMAP bitmap = CreateCompatibleBitmap(target, std::max<LONG>(1, client.right), std::max<LONG>(1, client.bottom));
        const auto oldBitmap = SelectObject(dc, bitmap);
        ::FillRect(dc, &client, UiTheme::BackgroundBrush());
        const auto oldFont = SelectObject(dc, textFont_);
        SetBkMode(dc, TRANSPARENT);
        if (rows_.empty() && !emptyText_.empty())
        {
            SetTextColor(dc, UiTheme::Colors.muted);
            RECT text{Scale(4), Scale(8), client.right - Scale(4), client.bottom};
            DrawTextW(dc, emptyText_.c_str(), -1, &text, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        const bool hasFocus = GetFocus() == window_;
        const auto layouts = Compute();
        for (size_t index = 0; index < rows_.size(); ++index)
        {
            const auto& bounds = layouts[index].bounds;
            if (bounds.bottom - scroll_ < 0 || bounds.top - scroll_ > client.bottom) continue;
            PaintRow(dc, index, layouts[index], hasFocus && static_cast<int>(index) == focused_);
        }
        if (dragging_ && dropRow_ >= 0 && dropRow_ != dragRow_)
        {
            // A line where the dragged row goes: above the row it replaces going up, below it going down.
            const auto& bounds = layouts[static_cast<size_t>(dropRow_)].bounds;
            const int y = (dropRow_ < dragRow_ ? bounds.top : bounds.top + LineHeight(rows_[static_cast<size_t>(dropRow_)])) - scroll_;
            const RECT line{bounds.left + Scale(8), y - Scale(1), bounds.right - Scale(8), y + Scale(2)};
            FillRect(dc, line, UiTheme::Colors.accent);
        }
        SelectObject(dc, oldFont);
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    bool Draggable(int index) const
    {
        return index >= 0 && index < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(index)].dragGroup && rows_[static_cast<size_t>(index)].draggable;
    }

    void MoveFocus(int direction)
    {
        int index = focused_;
        for (size_t step = 0; step < rows_.size(); ++step)
        {
            index += direction;
            if (index < 0 || index >= static_cast<int>(rows_.size())) return;
            if (!rows_[static_cast<size_t>(index)].header) break;
        }
        if (index < 0 || index >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(index)].header) return;
        focused_ = index;
        EnsureVisible(focused_);
        InvalidateRect(window_, nullptr, FALSE);
    }

    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
        case WM_PRINTCLIENT:
        {
            // Themed child controls (checkboxes) ask their parent to paint behind them;
            // they always sit on a white card.
            const HDC dc = reinterpret_cast<HDC>(wParam);
            if (message == WM_PRINTCLIENT || WindowFromDC(dc) != window_)
            {
                RECT client{};
                GetClientRect(window_, &client);
                ::FillRect(dc, &client, UiTheme::SurfaceBrush());
            }
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            const HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);
            SetBkColor(dc, UiTheme::Surface);
            SetTextColor(dc, IsWindowEnabled(control) ? UiTheme::Text : UiTheme::Colors.disabled);
            return reinterpret_cast<LRESULT>(UiTheme::SurfaceBrush());
        }
        case WM_COMMAND:
            // Controls in expanded rows report to the page that created them.
            return SendMessageW(GetParent(window_), WM_COMMAND, wParam, lParam);
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window_, &paint);
            Paint(dc);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_SIZE:
            UpdateScrollRange();
            InvalidateRect(window_, nullptr, FALSE);
            NotifyLayout();
            return 0;
        case WM_VSCROLL:
        {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(window_, SB_VERT, &info);
            int position = scroll_;
            switch (LOWORD(wParam))
            {
            case SB_LINEUP: position -= Scale(40); break;
            case SB_LINEDOWN: position += Scale(40); break;
            case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = info.nMax; break;
            }
            ScrollTo(position);
            return 0;
        }
        case WM_MOUSEWHEEL:
            ScrollTo(scroll_ - GET_WHEEL_DELTA_WPARAM(wParam) * Scale(48) / WHEEL_DELTA);
            return 0;
        case WM_MOUSEMOVE:
        {
            if (sliding_ >= 0)
            {
                if (sliding_ < static_cast<int>(rows_.size())) SetSlider(sliding_, SliderValue(sliding_, GET_X_LPARAM(lParam)));
                return 0;
            }
            if (dragRow_ >= 0 && (wParam & MK_LBUTTON))
            {
                const int y = GET_Y_LPARAM(lParam);
                if (!dragging_ && std::abs(y - dragStart_) >= Scale(5)) { dragging_ = true; SetCapture(window_); }
                if (dragging_)
                {
                    // Near the edges the list scrolls along.
                    RECT client{};
                    GetClientRect(window_, &client);
                    if (y < Scale(16)) ScrollTo(scroll_ - Scale(12));
                    else if (y > client.bottom - Scale(16)) ScrollTo(scroll_ + Scale(12));
                    const int target = DropTarget(y);
                    if (target != dropRow_) { dropRow_ = target; InvalidateRect(window_, nullptr, FALSE); }
                    return 0;
                }
            }
            const auto part = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (!(part == hover_))
            {
                hover_ = part;
                InvalidateRect(window_, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            hover_ = {};
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            // The grip shows the up-and-down cursor, as does the whole list while a row is dragged.
            if (LOWORD(lParam) == HTCLIENT && (dragging_ || hover_.kind == PartKind::Grip))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            if (LOWORD(lParam) == HTCLIENT && hover_.kind != PartKind::None && hover_.kind != PartKind::Body)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            // Hit-test before taking focus: the first focus scrolls the focused row into view,
            // which would otherwise move another row under the mouse.
            pressed_ = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (pressed_.row >= 0) focused_ = pressed_.row;
            // Pressing a row's grip starts a drag once the mouse moves.
            dragRow_ = pressed_.kind == PartKind::Grip && Draggable(pressed_.row) ? pressed_.row : -1;
            dragStart_ = GET_Y_LPARAM(lParam); dragging_ = false; dropRow_ = -1;
            SetFocus(window_);
            // A slider follows the mouse from the press on, until it is released.
            if (pressed_.kind == PartKind::Slider)
            {
                sliding_ = pressed_.row; SetCapture(window_);
                SetSlider(sliding_, SliderValue(sliding_, GET_X_LPARAM(lParam)));
            }
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
        {
            if (sliding_ >= 0)
            {
                sliding_ = -1; pressed_ = {};
                ReleaseCapture();
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            if (dragging_)
            {
                const int from = dragRow_, to = dropRow_;
                dragging_ = false; dragRow_ = -1; pressed_ = {};
                ReleaseCapture();
                InvalidateRect(window_, nullptr, FALSE);
                if (to >= 0 && to != from) { dropRow_ = to; focused_ = to; Notify(kMoved, from); }
                dropRow_ = -1;
                return 0;
            }
            dragRow_ = -1;
            // The action comes last: it may close the page, and this list with it.
            const auto part = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            const auto pressed = pressed_;
            pressed_ = {};
            if (part == pressed) Activate(part);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (sliding_ >= 0 && reinterpret_cast<HWND>(lParam) != window_) { sliding_ = -1; InvalidateRect(window_, nullptr, FALSE); }
            if (dragging_ && reinterpret_cast<HWND>(lParam) != window_) { dragging_ = false; dragRow_ = -1; dropRow_ = -1; InvalidateRect(window_, nullptr, FALSE); }
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_UP: MoveFocus(-1); return 0;
            case VK_DOWN: MoveFocus(1); return 0;
            case VK_LEFT:
            case VK_RIGHT:
            {
                // The next or previous choice of a row with one choice at a time, or the slider a step.
                if (focused_ < 0) return 0;
                const auto& row = rows_[static_cast<size_t>(focused_)];
                if (HasSlider(row)) { SetSlider(focused_, row.slider + (wParam == VK_RIGHT ? row.sliderStep : -row.sliderStep)); return 0; }
                if (row.choices.empty() || row.choiceFlags) return 0;
                const int next = std::clamp(row.choice + (wParam == VK_RIGHT ? 1 : -1), 0, static_cast<int>(row.choices.size()) - 1);
                if (next != row.choice) Activate({focused_, PartKind::Choice, next});
                return 0;
            }
            case VK_SPACE:
            case VK_RETURN:
            {
                // Space switches; Enter takes the button, else opens what the row opens (its expansion),
                // and switches only a row that has nothing else.
                if (focused_ < 0) return 0;
                const auto& row = rows_[static_cast<size_t>(focused_)];
                const bool opens = std::any_of(row.iconButtons.begin(), row.iconButtons.end(), [](wchar_t glyph) { return glyph != 0; });
                const PartKind kind = wParam == VK_SPACE && row.toggle >= 0 ? PartKind::Toggle
                    : !row.button.empty() ? PartKind::Button : row.toggle >= 0 && !opens ? PartKind::Toggle : PartKind::Body;
                Activate({focused_, kind});
                return 0;
            }
            case VK_DELETE:
                if (focused_ >= 0) Notify(kDeleteRequested, focused_);
                return 0;
            }
            break;
        case WM_SETFOCUS:
            if (focused_ < 0) MoveFocus(1);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_NCDESTROY:
        {
            ReleaseResources();
            const HWND window = window_;
            window_ = nullptr;
            return DefWindowProcW(window, message, wParam, lParam);
        }
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<RowList*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<RowList*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        const HWND handle = window;
        const LRESULT result = self->Handle(message, wParam, lParam);
        if (message == WM_NCDESTROY) SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        return result;
    }

    HWND window_{};
    int id_{};
    bool noScrollBar_{};
    HFONT titleFont_{};
    HFONT textFont_{};
    HFONT glyphFont_{};
    std::vector<Row> rows_;
    std::wstring emptyText_;
    std::map<std::wstring, HICON> icons_;
    int scroll_{};
    int focused_{-1};
    Part hover_{};
    Part pressed_{};
    int notifiedRow_{-1};
    int notifiedIconButton_{-1};
    // Dragging: the row picked up, where the press started, whether it moved far enough to drag, and its drop row.
    int dragRow_{-1};
    int dragStart_{};
    bool dragging_{};
    int dropRow_{-1};
    // The row whose slider follows the mouse, -1 when none.
    int sliding_{-1};
};
