#pragma once

#include "UiTheme.h"

#include <string>
#include <vector>
#include <windows.h>
#include <windowsx.h>

// The colored monitoring banner at the top of the main window, with a bar of toggle buttons at its right, drawn
// like a row's segmented choices: each switches on its own. A click sends WM_COMMAND to the main window with the
// button id and the segment as its notification code.
class StatusPanel
{
public:
    enum class Tone
    {
        Neutral,
        Active,
        Busy
    };

    StatusPanel() = default;
    StatusPanel(const StatusPanel&) = delete;
    StatusPanel& operator=(const StatusPanel&) = delete;
    ~StatusPanel()
    {
        if (brush_) DeleteObject(brush_);
    }

    bool Create(HINSTANCE instance, HWND parent, int buttonId, HFONT titleFont, HFONT textFont)
    {
        WNDCLASSW windowClass{};
        if (!GetClassInfoW(instance, kClassName, &windowClass))
        {
            windowClass.lpfnWndProc = WindowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.lpszClassName = kClassName;
            if (!RegisterClassW(&windowClass)) return false;
        }
        titleFont_ = titleFont;
        textFont_ = textFont;
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 100, 56, parent, nullptr, instance, this);
        if (!window_) return false;
        buttonId_ = buttonId;
        SetState(Tone::Neutral, L"", L"");
        return true;
    }

    HWND Handle() const noexcept { return window_; }

    void SetFonts(HFONT titleFont, HFONT textFont)
    {
        titleFont_ = titleFont;
        textFont_ = textFont;
        InvalidateRect(window_, nullptr, TRUE);
    }

    // The toggle buttons, and a bit for each that is on.
    void SetToggles(const std::vector<std::wstring>& labels, int on)
    {
        if (labels == labels_ && on == on_) return;
        labels_ = labels;
        on_ = on;
        InvalidateRect(window_, nullptr, TRUE);
    }

    void SetState(Tone tone, const std::wstring& title, const std::wstring& detail)
    {
        const bool changed = tone != tone_ || title != title_ || detail != detail_;
        if (tone != tone_ || !brush_)
        {
            tone_ = tone;
            if (brush_) DeleteObject(brush_);
            brush_ = CreateSolidBrush(Colors().background);
            brushDark_ = UiTheme::Dark;
        }
        title_ = title;
        detail_ = detail;
        if (changed) InvalidateRect(window_, nullptr, TRUE);
    }

    // One action button instead of toggles, as Race Control's Go live; a disabled one is grey and ignores clicks.
    void SetState(Tone tone, const std::wstring& title, const std::wstring& detail, const std::wstring& buttonText, bool buttonEnabled)
    {
        SetState(tone, title, detail);
        if (buttonEnabled != enabled_) { enabled_ = buttonEnabled; InvalidateRect(window_, nullptr, TRUE); }
        SetToggles(buttonText.empty() ? std::vector<std::wstring>{} : std::vector<std::wstring>{buttonText}, 0);
    }

private:
    static constexpr wchar_t kClassName[] = L"LaunchMateStatusPanel";

    struct Palette
    {
        COLORREF background;
        COLORREF border;
        COLORREF dot;
        COLORREF title;
        COLORREF detail;
    };

    Palette Colors() const
    {
        switch (tone_)
        {
        // Light: pale fill, dark text; dark: deep fill, light text.
        case Tone::Active: return UiTheme::Dark ? Palette{RGB(32, 52, 22), RGB(58, 92, 30), RGB(99, 153, 34), RGB(192, 221, 151), RGB(160, 200, 120)}
            : Palette{RGB(234, 243, 222), RGB(192, 221, 151), RGB(99, 153, 34), RGB(39, 80, 10), RGB(59, 109, 17)};
        case Tone::Busy: return UiTheme::Dark ? Palette{RGB(58, 44, 18), RGB(110, 80, 30), RGB(186, 117, 23), RGB(250, 199, 117), RGB(230, 180, 110)}
            : Palette{RGB(250, 238, 218), RGB(250, 199, 117), RGB(186, 117, 23), RGB(99, 56, 6), RGB(133, 79, 11)};
        default: return {UiTheme::Surface, UiTheme::Colors.border, RGB(180, 178, 169), UiTheme::Text, UiTheme::Colors.muted};
        }
    }

    int Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

    // The segments, as wide as their text, ending 12 DIPs from the right edge.
    std::vector<RECT> Segments(HDC dc) const
    {
        RECT client{};
        GetClientRect(window_, &client);
        const auto oldFont = SelectObject(dc, textFont_);
        std::vector<int> widths;
        int total = 0;
        for (const auto& label : labels_)
        {
            SIZE size{};
            GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
            widths.push_back(size.cx + Scale(24));
            total += widths.back();
        }
        SelectObject(dc, oldFont);
        std::vector<RECT> segments;
        const int top = (client.bottom - Scale(30)) / 2;
        int left = client.right - Scale(12) - total;
        for (const int width : widths) { segments.push_back({left, top, left + width, top + Scale(30)}); left += width; }
        return segments;
    }

    int SegmentAt(POINT point) const
    {
        const HDC dc = GetDC(window_);
        const auto segments = Segments(dc);
        ReleaseDC(window_, dc);
        for (size_t index = 0; index < segments.size(); ++index)
            if (PtInRect(&segments[index], point)) return static_cast<int>(index);
        return -1;
    }

    static void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF line)
    {
        const HBRUSH brush = CreateSolidBrush(fill);
        const HPEN pen = CreatePen(PS_SOLID, 1, line);
        const auto oldBrush = SelectObject(dc, brush);
        const auto oldPen = SelectObject(dc, pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    // One outline like a button's; the switched-on segments filled in the accent, hairlines between the others.
    void PaintToggles(HDC dc)
    {
        const auto segments = Segments(dc);
        if (segments.empty()) return;
        const auto& colors = UiTheme::Colors;
        const RECT bar{segments.front().left, segments.front().top, segments.back().right, segments.back().bottom};
        FillRounded(dc, bar, Scale(8), UiTheme::Surface, colors.buttonBorder);
        HRGN inside = CreateRoundRectRgn(bar.left + 1, bar.top + 1, bar.right, bar.bottom, Scale(8), Scale(8));
        SelectClipRgn(dc, inside);
        const auto oldFont = SelectObject(dc, textFont_);
        auto lit = [this](size_t index) { return (on_ >> index & 1) != 0; };
        for (size_t index = 0; index < segments.size(); ++index)
        {
            RECT segment = segments[index];
            if (lit(index) || (enabled_ && hover_ == static_cast<int>(index)))
            {
                const HBRUSH brush = CreateSolidBrush(lit(index) ? colors.chosen : colors.buttonHover);
                FillRect(dc, &segment, brush);
                DeleteObject(brush);
            }
            if (index > 0 && !lit(index) && !lit(index - 1))
            {
                const RECT line{segment.left, segment.top + Scale(7), segment.left + 1, segment.bottom - Scale(7)};
                const HBRUSH brush = CreateSolidBrush(colors.border);
                FillRect(dc, &line, brush);
                DeleteObject(brush);
            }
            SetTextColor(dc, lit(index) ? colors.chosenInk : enabled_ ? UiTheme::Text : colors.disabled);
            DrawTextW(dc, labels_[index].c_str(), -1, &segment, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, oldFont);
        SelectClipRgn(dc, nullptr);
        DeleteObject(inside);
    }

    void Paint(HDC dc)
    {
        const auto colors = Colors();
        // The fill follows a change of theme as well as of tone.
        if (brushDark_ != UiTheme::Dark || !brush_)
        {
            if (brush_) DeleteObject(brush_);
            brush_ = CreateSolidBrush(colors.background);
            brushDark_ = UiTheme::Dark;
        }
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(dc, &client, UiTheme::BackgroundBrush());
        const HPEN pen = CreatePen(PS_SOLID, 1, colors.border);
        const auto oldPen = SelectObject(dc, pen);
        const auto oldBrush = SelectObject(dc, brush_);
        const int radius = Scale(10);
        RoundRect(dc, client.left, client.top, client.right, client.bottom, radius, radius);

        const HBRUSH dotBrush = CreateSolidBrush(colors.dot);
        SelectObject(dc, dotBrush);
        SelectObject(dc, GetStockObject(NULL_PEN));
        const int dot = Scale(10);
        const int dotX = Scale(16);
        const int dotY = (client.bottom - dot) / 2;
        Ellipse(dc, dotX, dotY, dotX + dot, dotY + dot);

        const auto segments = Segments(dc);
        const RECT button{segments.empty() ? client.right : segments.front().left, 0, client.right, client.bottom};
        SetBkMode(dc, TRANSPARENT);
        const int textLeft = dotX + dot + Scale(12);
        const int middle = client.bottom / 2;
        RECT titleRect{textLeft, client.top, button.left - Scale(12), detail_.empty() ? client.bottom : middle + Scale(1)};
        RECT detailRect{textLeft, middle + Scale(1), button.left - Scale(12), client.bottom};
        const auto oldFont = SelectObject(dc, titleFont_);
        SetTextColor(dc, colors.title);
        DrawTextW(dc, title_.c_str(), -1, &titleRect,
            (detail_.empty() ? DT_VCENTER : DT_BOTTOM) | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (!detail_.empty())
        {
            SelectObject(dc, textFont_);
            SetTextColor(dc, colors.detail);
            DrawTextW(dc, detail_.c_str(), -1, &detailRect, DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        SelectObject(dc, oldFont);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(dotBrush);
        DeleteObject(pen);
        PaintToggles(dc);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<StatusPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<StatusPanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window, &paint);
            self->Paint(dc);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_MOUSEMOVE:
        {
            const int hover = self->SegmentAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hover != self->hover_) { self->hover_ = hover; InvalidateRect(window, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->hover_ >= 0) { self->hover_ = -1; InvalidateRect(window, nullptr, FALSE); }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && self->hover_ >= 0 && self->enabled_) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_LBUTTONDOWN:
            self->pressed_ = self->SegmentAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_LBUTTONUP:
        {
            const int segment = self->SegmentAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            const bool clicked = segment >= 0 && segment == self->pressed_ && self->enabled_;
            self->pressed_ = -1;
            if (clicked) SendMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(self->buttonId_, segment), reinterpret_cast<LPARAM>(window));
            return 0;
        }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HWND window_{};
    int buttonId_{};
    std::vector<std::wstring> labels_;
    int on_{};
    int hover_{-1};
    int pressed_{-1};
    bool enabled_{true};
    HFONT titleFont_{};
    HFONT textFont_{};
    HBRUSH brush_{};
    bool brushDark_{};
    Tone tone_{Tone::Neutral};
    std::wstring title_;
    std::wstring detail_;
};
