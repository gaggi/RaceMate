#pragma once

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <cwchar>

namespace UiTheme
{
    // The window's colors, light or dark: every control reads them when it paints, so SetDark() switches them all.
    struct Palette
    {
        COLORREF background, surface, text;
        COLORREF muted, heading, strong, faint, disabled, mutedTitle; // Secondary inks, from darkest use to lightest.
        COLORREF border, divider, hover, selected, accent, link;
        COLORREF buttonBorder, buttonBorderDisabled, buttonHover, iconHover, chosen, chosenInk;
        COLORREF toggleOff, toggleOffHover, toggleOnDisabled, toggleOffDisabled, knob;
        COLORREF nav, navHover, segmentTrack, segmentBorder, segmentHover;
        COLORREF pillNeutral, pillNeutralInk, pillActive, pillActiveInk, pillWarning, pillWarningInk, pillAccent, pillAccentInk;
        COLORREF pillDanger, pillDangerInk, playerRow, playerInk;
    };
    inline constexpr Palette LightPalette{
        RGB(246, 247, 249), RGB(255, 255, 255), RGB(40, 47, 58),
        RGB(110, 112, 118), RGB(95, 99, 108), RGB(80, 85, 95), RGB(160, 164, 172), RGB(160, 162, 168), RGB(136, 135, 128),
        RGB(220, 222, 225), RGB(234, 236, 239), RGB(248, 249, 251), RGB(232, 240, 252), RGB(55, 138, 221), RGB(24, 95, 165),
        RGB(190, 194, 201), RGB(226, 228, 231), RGB(243, 244, 246), RGB(238, 240, 243), RGB(220, 234, 250), RGB(12, 68, 124),
        RGB(165, 169, 176), RGB(140, 144, 152), RGB(160, 196, 234), RGB(214, 216, 220), RGB(255, 255, 255),
        RGB(236, 238, 241), RGB(226, 229, 233), RGB(232, 234, 238), RGB(210, 213, 218), RGB(224, 227, 231),
        RGB(241, 239, 232), RGB(95, 94, 90), RGB(234, 243, 222), RGB(39, 80, 10), RGB(250, 238, 218), RGB(133, 79, 11), RGB(230, 241, 251), RGB(12, 68, 124),
        RGB(252, 235, 235), RGB(121, 31, 31), RGB(250, 244, 228), RGB(140, 96, 0)};
    inline constexpr Palette DarkPalette{
        RGB(24, 26, 30), RGB(34, 37, 42), RGB(230, 232, 235),
        RGB(155, 161, 170), RGB(150, 155, 163), RGB(196, 200, 206), RGB(110, 116, 124), RGB(110, 115, 122), RGB(120, 124, 130),
        RGB(56, 60, 67), RGB(46, 50, 56), RGB(40, 44, 50), RGB(30, 52, 80), RGB(76, 154, 236), RGB(122, 180, 240),
        RGB(84, 90, 98), RGB(52, 56, 62), RGB(46, 50, 57), RGB(48, 52, 58), RGB(30, 58, 92), RGB(207, 227, 250),
        RGB(78, 84, 92), RGB(98, 104, 112), RGB(46, 84, 128), RGB(56, 60, 66), RGB(236, 238, 241),
        RGB(19, 21, 24), RGB(36, 39, 44), RGB(30, 33, 38), RGB(60, 64, 71), RGB(44, 48, 54),
        RGB(52, 52, 48), RGB(200, 198, 190), RGB(32, 52, 22), RGB(192, 221, 151), RGB(64, 46, 18), RGB(250, 199, 117), RGB(28, 50, 78), RGB(181, 212, 244),
        RGB(72, 30, 30), RGB(247, 193, 193), RGB(58, 50, 28), RGB(240, 196, 100)};
    inline Palette Colors = LightPalette;
    inline bool Dark = false;
    // The colors most code reads by name; SetDark() keeps them in step with Colors.
    inline COLORREF Background = LightPalette.background;
    inline COLORREF Surface = LightPalette.surface;
    inline COLORREF Text = LightPalette.text;
    inline constexpr int TabHeight = 28;

    class Brush
    {
    public:
        explicit Brush(COLORREF color) : handle_(CreateSolidBrush(color)) {}
        ~Brush() { DeleteObject(handle_); }
        HBRUSH Get() const { return handle_; }
        Brush(const Brush&) = delete;
        Brush& operator=(const Brush&) = delete;
    private:
        HBRUSH handle_{};
    };

    // One brush per color and theme, kept for the program's life: window classes hold them as their background.
    inline HBRUSH BackgroundBrushFor(bool dark)
    {
        static const Brush light(LightPalette.background), darkBrush(DarkPalette.background);
        return dark ? darkBrush.Get() : light.Get();
    }
    inline HBRUSH BackgroundBrush()
    {
        return BackgroundBrushFor(Dark);
    }
    inline HBRUSH OtherBackgroundBrush() { return BackgroundBrushFor(!Dark); }

    // Windows 11 ships Segoe Fluent Icons; Windows 10 has the same code points in MDL2.
    inline const wchar_t* IconFontFace()
    {
        static const wchar_t* face = []
        {
            const HDC dc = GetDC(nullptr);
            LOGFONTW query{};
            query.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(query.lfFaceName, L"Segoe Fluent Icons");
            bool found = false;
            EnumFontFamiliesExW(dc, &query, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) -> int
            {
                *reinterpret_cast<bool*>(found) = true;
                return 0;
            }, reinterpret_cast<LPARAM>(&found), 0);
            ReleaseDC(nullptr, dc);
            return found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        }();
        return face;
    }

    inline HBRUSH SurfaceBrush()
    {
        static const Brush light(LightPalette.surface), dark(DarkPalette.surface);
        return Dark ? dark.Get() : light.Get();
    }

    // Native controls in the dark: Windows' own dark styles for edits, combo boxes, lists and scroll bars, and the
    // classic look for checkboxes, whose themed text ignores the text color. Light: the usual Explorer style.
    inline void StyleControl(HWND control)
    {
        wchar_t name[64]{};
        GetClassNameW(control, name, 64);
        const auto style = GetWindowLongPtrW(control, GWL_STYLE);
        const bool box = _wcsicmp(name, L"Button") == 0 && !(style & BS_PUSHLIKE)
            && ((style & BS_TYPEMASK) == BS_AUTOCHECKBOX || (style & BS_TYPEMASK) == BS_CHECKBOX);
        if (box) SetWindowTheme(control, Dark ? L"" : nullptr, Dark ? L"" : nullptr);
        else if (_wcsicmp(name, L"Edit") == 0 || _wcsicmp(name, WC_COMBOBOXW) == 0 || _wcsicmp(name, HOTKEY_CLASSW) == 0)
            SetWindowTheme(control, Dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
        else SetWindowTheme(control, Dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }

    // The title bar and, through uxtheme's unnamed export (as Notepad++ and others use), popup menus.
    inline void StyleFrame(HWND window)
    {
        using DwmSet = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        static const auto dwm = reinterpret_cast<DwmSet>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute"));
        const BOOL on = Dark;
        if (dwm) dwm(window, 20, &on, sizeof(on)); // DWMWA_USE_IMMERSIVE_DARK_MODE
        using SetMode = int(WINAPI*)(int);
        using Flush = void(WINAPI*)();
        static const HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
        static const auto setMode = uxtheme ? reinterpret_cast<SetMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))) : nullptr;
        static const auto flush = uxtheme ? reinterpret_cast<Flush>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))) : nullptr;
        if (setMode) setMode(Dark ? 2 : 0); // ForceDark or Default
        if (flush) flush();
    }

    // Switches every color; windows repaint with RefreshWindow().
    inline void SetDark(bool dark)
    {
        Dark = dark;
        Colors = dark ? DarkPalette : LightPalette;
        Background = Colors.background; Surface = Colors.surface; Text = Colors.text;
    }

    // A window and its children in the current theme: background brushes of our own classes, native control
    // styles, the frame, and a repaint.
    inline BOOL CALLBACK RestyleChild(HWND control, LPARAM)
    {
        const auto brush = GetClassLongPtrW(control, GCLP_HBRBACKGROUND);
        const bool ours = brush && (brush == reinterpret_cast<ULONG_PTR>(BackgroundBrush()) || brush == reinterpret_cast<ULONG_PTR>(OtherBackgroundBrush()));
        if (ours) SetClassLongPtrW(control, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(BackgroundBrush()));
        StyleControl(control);
        return TRUE;
    }
    inline void RefreshWindow(HWND window)
    {
        RestyleChild(window, 0);
        EnumChildWindows(window, RestyleChild, 0);
        StyleFrame(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    inline bool IsClass(HWND window, const wchar_t* name)
    {
        wchar_t className[64]{};
        GetClassNameW(window, className, 64);
        return _wcsicmp(className, name) == 0;
    }

    inline constexpr wchar_t TabSurfaceProperty[] = L"LaunchMate.TabSurface";

    inline bool IsTabSurface(HWND window)
    {
        for (HWND current = window; current; current = GetParent(current))
        {
            if (IsClass(current, WC_TABCONTROLW) || GetPropW(current, TabSurfaceProperty)) return true;
            if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD)) break;
        }
        return false;
    }

    inline HBRUSH PanelBrush(HWND window)
    {
        return IsTabSurface(window) ? SurfaceBrush() : BackgroundBrush();
    }

    inline void StyleListView(HWND list)
    {
        SetWindowTheme(list, L"Explorer", nullptr);
        ListView_SetBkColor(list, Surface);
        ListView_SetTextBkColor(list, Surface);
        ListView_SetTextColor(list, Text);
        SetWindowLongPtrW(list, GWL_EXSTYLE, GetWindowLongPtrW(list, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
        SetWindowLongPtrW(list, GWL_STYLE, GetWindowLongPtrW(list, GWL_STYLE) | WS_BORDER);
        SetWindowPos(list, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    inline void StyleTextInput(HWND control)
    {
        RECT rect{};
        GetWindowRect(control, &rect);
        // Use the same native, themed input frame for dialog controls and dynamically created fields.
        SetWindowTheme(control, Dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
        SetWindowLongPtrW(control, GWL_EXSTYLE, GetWindowLongPtrW(control, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
        SetWindowLongPtrW(control, GWL_STYLE, GetWindowLongPtrW(control, GWL_STYLE) & ~WS_BORDER);
        SetWindowPos(control, nullptr, 0, 0, rect.right - rect.left,
            MulDiv(22, GetDpiForWindow(control), 96),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    inline LRESULT CALLBACK TabPaintProc(HWND tab, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR)
    {
        if (message == WM_ERASEBKGND || message == WM_PRINTCLIENT)
        {
            // Themed checkboxes ask their parent to paint behind them, independently of WM_PAINT.
            RECT client{};
            GetClientRect(tab, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, SurfaceBrush());
            return 1;
        }
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(tab, &paint);
            RECT client{};
            GetClientRect(tab, &client);
            FillRect(dc, &client, BackgroundBrush());
            const HPEN pen = CreatePen(PS_SOLID, 1, Colors.border);
            const auto oldPen = SelectObject(dc, pen);
            const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            const auto oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(tab, WM_GETFONT, 0, 0)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, Text);
            RECT first{};
            TabCtrl_GetItemRect(tab, 0, &first);
            RECT body{client.left, first.bottom, client.right, client.bottom};
            FillRect(dc, &body, SurfaceBrush());
            Rectangle(dc, client.left, first.bottom, client.right, client.bottom);
            const int selected = TabCtrl_GetCurSel(tab);
            for (int index = 0; index < TabCtrl_GetItemCount(tab); ++index)
            {
                RECT rect{};
                TabCtrl_GetItemRect(tab, index, &rect);
                // Keep the native hit rectangles and keyboard navigation; only replace the painting.
                rect.top = first.top;
                rect.bottom = first.bottom + 1;
                FillRect(dc, &rect, index == selected ? SurfaceBrush() : BackgroundBrush());
                Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
                if (index == selected)
                {
                    RECT seam{rect.left + 1, rect.bottom - 1, rect.right - 1, rect.bottom + 1};
                    FillRect(dc, &seam, SurfaceBrush());
                }
                wchar_t label[256]{};
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = label;
                item.cchTextMax = 256;
                TabCtrl_GetItem(tab, index, &item);
                DrawTextW(dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                if (GetFocus() == tab && index == TabCtrl_GetCurFocus(tab) &&
                    !(SendMessageW(tab, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
                {
                    InflateRect(&rect, -4, -4);
                    DrawFocusRect(dc, &rect);
                }
            }
            SelectObject(dc, oldFont);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            EndPaint(tab, &paint);
            return 0;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(tab, TabPaintProc, subclassId);
        return DefSubclassProc(tab, message, wParam, lParam);
    }

    inline LRESULT CALLBACK GroupPaintProc(HWND group, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR headingFont)
    {
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(group, &paint);
            // Resource dialogs can place labels inside a group without making them its children.
            // Preserve those controls regardless of their sibling paint order.
            for (HWND sibling = GetWindow(GetParent(group), GW_CHILD); sibling;
                sibling = GetWindow(sibling, GW_HWNDNEXT))
            {
                if (sibling == group || !IsWindowVisible(sibling)) continue;
                RECT bounds{};
                GetWindowRect(sibling, &bounds);
                MapWindowPoints(nullptr, group, reinterpret_cast<POINT*>(&bounds), 2);
                ExcludeClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
            }
            RECT rect{};
            GetClientRect(group, &rect);
            FillRect(dc, &rect, PanelBrush(group));
            const auto oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(group, WM_GETFONT, 0, 0)));
            wchar_t caption[256]{};
            const int length = GetWindowTextW(group, caption, 256);
            SIZE size{};
            GetTextExtentPoint32W(dc, caption, length, &size);
            const int padding = MulDiv(7, GetDpiForWindow(group), 96);
            const HPEN pen = CreatePen(PS_SOLID, 1, Colors.border);
            const auto oldPen = SelectObject(dc, pen);
            const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, rect.left, size.cy / 2, rect.right, rect.bottom);
            RECT label{padding, 0, padding + size.cx + 2, size.cy};
            FillRect(dc, &label, PanelBrush(group));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, Text);
            DrawTextW(dc, caption, length, &label, DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldFont);
            DeleteObject(pen);
            EndPaint(group, &paint);
            return 0;
        }
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(group, GroupPaintProc, subclassId);
            DeleteObject(reinterpret_cast<HFONT>(headingFont));
        }
        return DefSubclassProc(group, message, wParam, lParam);
    }

    inline LRESULT CALLBACK ColorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR)
    {
        if (message == WM_CTLCOLORDLG) return reinterpret_cast<LRESULT>(PanelBrush(window));
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN ||
            message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)
        {
            const HWND control = reinterpret_cast<HWND>(lParam);
            const bool white = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ||
                IsClass(control, L"Edit") || IsTabSurface(control);
            const HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, IsWindowEnabled(control) ? Text : GetSysColor(COLOR_GRAYTEXT));
            SetBkColor(dc, white ? Surface : Background);
            return reinterpret_cast<LRESULT>(white ? SurfaceBrush() : BackgroundBrush());
        }
        if (message == WM_ERASEBKGND)
        {
            RECT rect{};
            GetClientRect(window, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, PanelBrush(window));
            return 1;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ColorProc, subclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }

    inline void Apply(HWND window)
    {
        SetWindowSubclass(window, ColorProc, 1, 0);
        EnumChildWindows(window, [](HWND control, LPARAM) -> BOOL
        {
            if (IsClass(control, WC_LISTVIEWW)) StyleListView(control);
            StyleControl(control);
            if ((IsClass(control, L"Edit") && !(GetWindowLongPtrW(control, GWL_STYLE) & ES_MULTILINE)) ||
                IsClass(control, HOTKEY_CLASSW))
                StyleTextInput(control);
            if (IsClass(control, WC_TABCONTROLW))
            {
                SetWindowLongPtrW(control, GWL_STYLE,
                    GetWindowLongPtrW(control, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
                SetWindowSubclass(control, TabPaintProc, 1, 0);
            }
            if (IsClass(control, L"Button") &&
                (GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
            {
                // Native group-box painting can restore a white background when shown from the tray.
                SetWindowLongPtrW(control, GWL_STYLE, GetWindowLongPtrW(control, GWL_STYLE) | WS_CLIPSIBLINGS);
                DWORD_PTR heading{};
                if (!GetWindowSubclass(control, GroupPaintProc, 1, &heading))
                {
                    LOGFONTW font{};
                    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)), sizeof(font), &font);
                    font.lfHeight = -MulDiv(10, GetDpiForWindow(control), 72);
                    font.lfWeight = FW_SEMIBOLD;
                    const HFONT handle = CreateFontIndirectW(&font);
                    heading = reinterpret_cast<DWORD_PTR>(handle);
                    if (handle) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(handle), FALSE);
                }
                SetWindowSubclass(control, GroupPaintProc, 1, heading);
            }
            return TRUE;
        }, 0);
    }
}
