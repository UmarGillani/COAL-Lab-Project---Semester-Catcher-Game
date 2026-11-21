//Semester Catcher GUI (C++/Win32 GDI)
#include <windows.h>
#include <cstdio>
#include <cstring>

// Project level linker settings (so no manual linker changes are needed)
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/SAFESEH:NO")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

//constants
const int WIN_W = 800, WIN_H = 600;
const int BASKET_Y = 540, BASKET_W = 120, BASKET_H = 20;
const int ITEM_W = 100, ITEM_H = 32;
const int MAX_ITEMS = 30;
const int MAX_ENT = 50, NAME_LEN = 16;

// assembly side
extern "C" {
    // game logic
    void GameInit(int level, int seed);
    int  GameUpdate(int dir);                 // returns lives left
    void ScoreToStr(int n, char* buf);        // recursive digit conversion
    const char* GetItemName(int type);
    const char* GetLevelName(int level);

    // file handling / leaderboard
    void LoadScores();                        // read highscores.txt
    int  QualifiesForBoard(int score);        // 1 if score makes the board
    int  SaveScore(const char* name, int score, int level);   // returns rank, writes file
    void ClearScores();                       // wipe board + file

    extern int gScore, gLives, gLevel, gBasketX;
    extern int itemX[30], itemY[30], itemType[30], itemActive[30];
    extern int gCount;
    extern int bScore[50], bLevel[50];
    extern char bName[800];                   // 50 names * 16 bytes
}

enum State { MENU, HOWTO, PLAY, NAME, OVER, BOARD };

static State state = MENU, boardReturn = MENU;
static int  sel = 0;            // menu selection 0..4
static int  level = 1;          // 0 Easy, 1 Medium, 2 Hard
static int  finalScore = 0;
static int  savedRank = 0;      // rank achieved in the last game (0 = not saved)
static int  tab = 0;            // leaderboard filter: 0 All, 1 Easy, 2 Medium, 3 Hard
static bool confirmClear = false;
static char nameBuf[16] = "";
static int  nameLen = 0;

static HFONT fSmall, fMed, fBig, fTitle;

// drawing helpers
static const COLORREF NAVY = RGB(30, 60, 120);
static const COLORREF WHITE = RGB(255, 255, 255);
static const COLORREF GRAYT = RGB(90, 90, 90);
static const COLORREF GOLDC = RGB(240, 200, 60);

static void Box(HDC dc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void Outline(HDC dc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    SetDCBrushColor(dc, c);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void Txt(HDC dc, const char* s, int x, int y, int w, int h,
    HFONT f, COLORREF c, UINT align) {
    RECT r = { x, y, x + w, y + h };
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextA(dc, s, -1, &r, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

static void TxtC(HDC dc, const char* s, int y, int h, HFONT f, COLORREF c) {
    Txt(dc, s, 0, y, WIN_W, h, f, c, DT_CENTER);
}

// leaderboard dashboard
static void Card(HDC dc, int x, const char* label, const char* value) {
    Box(dc, x, 105, 220, 72, WHITE);
    Outline(dc, x, 105, 220, 72, RGB(170, 190, 220));
    Txt(dc, label, x, 110, 220, 22, fSmall, GRAYT, DT_CENTER);
    Txt(dc, value, x, 130, 220, 44, fBig, NAVY, DT_CENTER);
}

static void DrawBoard(HDC dc) {
    static const char* tabNames[4] = { "ALL", "EASY", "MEDIUM", "HARD" };
    char buf[32], line[96];

    // entries that match the current filter (board is already sorted)
    int idx[MAX_ENT], n = 0;
    for (int i = 0; i < gCount; i++)
        if (tab == 0 || bLevel[i] == tab - 1) idx[n++] = i;

    int best = 0, sum = 0;
    for (int k = 0; k < n; k++) sum += bScore[idx[k]];
    if (n > 0) best = bScore[idx[0]];
    int avg = (n > 0) ? sum / n : 0;

    // header
    Box(dc, 0, 0, WIN_W, 90, NAVY);
    TxtC(dc, "LEADERBOARD", 8, 58, fTitle, WHITE);
    TxtC(dc, "saved in highscores.txt", 62, 24, fSmall, RGB(200, 220, 255));

    // summary cards
    ScoreToStr(n, buf);     Card(dc, 40, "RECORDS", buf);
    ScoreToStr(best, buf);  Card(dc, 290, "HIGHEST SCORE", buf);
    ScoreToStr(avg, buf);   Card(dc, 540, "AVERAGE SCORE", buf);

    // filter tabs
    for (int t = 0; t < 4; t++) {
        int x = 100 + t * 150;
        bool on = (t == tab);
        Box(dc, x, 190, 140, 36, on ? NAVY : RGB(190, 205, 230));
        Txt(dc, tabNames[t], x, 190, 140, 36, fMed, on ? WHITE : RGB(60, 60, 80), DT_CENTER);
    }

    // table header
    Box(dc, 40, 240, 720, 32, RGB(60, 90, 150));
    Txt(dc, "RANK", 50, 240, 70, 32, fSmall, WHITE, DT_CENTER);
    Txt(dc, "NAME", 140, 240, 280, 32, fSmall, WHITE, DT_LEFT);
    Txt(dc, "SCORE", 430, 240, 120, 32, fSmall, WHITE, DT_RIGHT);
    Txt(dc, "LEVEL", 580, 240, 170, 32, fSmall, WHITE, DT_CENTER);

    // rows (top 10 of the filtered list)
    for (int r = 0; r < 10; r++) {
        int y = 272 + r * 28;
        Box(dc, 40, y, 720, 28, (r % 2) ? RGB(235, 243, 252) : RGB(250, 252, 255));
        if (r >= n) continue;
        int i = idx[r];

        COLORREF rc = RGB(50, 50, 50);
        if (r == 0) rc = RGB(200, 150, 0);          // gold
        else if (r == 1) rc = RGB(130, 130, 145);   // silver
        else if (r == 2) rc = RGB(165, 100, 40);    // bronze

        snprintf(line, sizeof(line), "#%d", r + 1);
        Txt(dc, line, 50, y, 70, 28, fMed, rc, DT_CENTER);
        Txt(dc, bName + i * NAME_LEN, 140, y, 280, 28, fSmall, RGB(30, 30, 30), DT_LEFT);
        ScoreToStr(bScore[i], buf);
        Txt(dc, buf, 430, y, 120, 28, fSmall, NAVY, DT_RIGHT);

        COLORREF lc = (bLevel[i] == 0) ? RGB(20, 120, 20)
            : (bLevel[i] == 1) ? RGB(200, 120, 0) : RGB(180, 30, 30);
        Txt(dc, GetLevelName(bLevel[i]), 580, y, 170, 28, fSmall, lc, DT_CENTER);
    }
    if (n == 0)
        TxtC(dc, "No records yet - play a game!", 360, 40, fMed, GRAYT);

    if (confirmClear)
        TxtC(dc, "Press C again to CLEAR ALL records  (any other key cancels)", 560, 30, fSmall, RGB(180, 30, 30));
    else
        TxtC(dc, "LEFT / RIGHT: filter      C: clear records      ESC: back", 560, 30, fSmall, GRAYT);
}

// render one full frame
static void Render(HDC dc) {
    char buf[32], line[96];
    SetBkMode(dc, TRANSPARENT);
    Box(dc, 0, 0, WIN_W, WIN_H, RGB(220, 235, 250));

    if (state == MENU) {
        Box(dc, 0, 0, WIN_W, 150, NAVY);
        TxtC(dc, "SEMESTER CATCHER", 25, 80, fTitle, WHITE);
        TxtC(dc, "Catch the work, dodge the bunks!", 105, 30, fMed, RGB(200, 220, 255));

        snprintf(line, sizeof(line), "Difficulty:  <  %s  >", GetLevelName(level));
        const char* labels[5] = { "Play", line, "Leaderboard", "Instructions", "Exit" };
        for (int i = 0; i < 5; i++) {
            int y = 180 + i * 62;
            bool on = (i == sel);
            if (on) Box(dc, 180, y, 440, 52, NAVY);
            TxtC(dc, labels[i], y, 52, fBig, on ? WHITE : RGB(60, 60, 60));
        }
        TxtC(dc, "UP / DOWN: select     ENTER: confirm     LEFT / RIGHT: difficulty",
            555, 30, fSmall, GRAYT);
    }
    else if (state == HOWTO) {
        TxtC(dc, "INSTRUCTIONS", 40, 60, fTitle, NAVY);
        TxtC(dc, "Move the basket with LEFT / RIGHT arrow keys (or A / D).", 140, 34, fMed, RGB(50, 50, 50));
        TxtC(dc, "Catch GREEN items:  Assignment +10,  Quiz +20,  Exam +50", 195, 34, fMed, RGB(20, 110, 20));
        TxtC(dc, "(points are multiplied by the difficulty level)", 230, 30, fSmall, GRAYT);
        TxtC(dc, "Missing a GREEN item costs a life!", 270, 34, fMed, RGB(170, 30, 30));
        TxtC(dc, "Catching RED items (Bunk Class, Proxy) also costs a life!", 315, 34, fMed, RGB(170, 30, 30));
        TxtC(dc, "The game ends when all your lives are gone.", 365, 34, fMed, RGB(50, 50, 50));
        TxtC(dc, "Good scores are saved with your name on the Leaderboard.", 410, 34, fMed, RGB(50, 50, 50));
        TxtC(dc, "Easy: slow, 3 lives, x1     Medium: 3 lives, x2     Hard: fast, 2 lives, x3",
            465, 30, fSmall, GRAYT);
        TxtC(dc, "Press ENTER to go back", 520, 34, fMed, GRAYT);
    }
    else if (state == BOARD) {
        DrawBoard(dc);
    }
    else if (state == NAME) {                       // name is asked BEFORE the game starts
        Box(dc, 0, 0, WIN_W, 150, NAVY);
        TxtC(dc, "SEMESTER CATCHER", 25, 80, fTitle, WHITE);
        snprintf(line, sizeof(line), "Difficulty: %s", GetLevelName(level));
        TxtC(dc, line, 105, 30, fMed, RGB(200, 220, 255));

        Box(dc, 150, 200, 500, 260, RGB(25, 30, 55));
        Outline(dc, 150, 200, 500, 260, RGB(230, 170, 30));
        TxtC(dc, "ENTER YOUR NAME", 225, 56, fBig, GOLDC);
        Box(dc, 250, 335, 300, 46, WHITE);
        Outline(dc, 250, 335, 300, 46, GOLDC);
        snprintf(line, sizeof(line), "%s%s", nameBuf, ((GetTickCount() / 500) % 2) ? "_" : " ");
        Txt(dc, line, 262, 335, 280, 46, fMed, RGB(20, 20, 20), DT_LEFT);
        TxtC(dc, "ENTER: start game        ESC: back", 405, 30, fSmall, RGB(200, 210, 235));
    }
    else { // PLAY or OVER: show the play field underneath
        for (int i = 0; i < MAX_ITEMS; i++) {
            if (!itemActive[i]) continue;
            bool good = itemType[i] < 3;
            Box(dc, itemX[i], itemY[i], ITEM_W, ITEM_H,
                good ? RGB(150, 215, 150) : RGB(235, 120, 120));
            Outline(dc, itemX[i], itemY[i], ITEM_W, ITEM_H, RGB(60, 60, 60));
            Txt(dc, GetItemName(itemType[i]), itemX[i], itemY[i], ITEM_W, ITEM_H,
                fSmall, RGB(0, 0, 0), DT_CENTER);
        }
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCBrushColor(dc, RGB(230, 170, 30));
        SetDCPenColor(dc, RGB(120, 70, 10));
        RoundRect(dc, gBasketX, BASKET_Y, gBasketX + BASKET_W, BASKET_Y + BASKET_H, 12, 12);

        // HUD bar
        Box(dc, 0, 0, WIN_W, 40, NAVY);
        ScoreToStr(gScore, buf);
        snprintf(line, sizeof(line), "Score: %s", buf);
        Txt(dc, line, 15, 0, 250, 40, fMed, WHITE, DT_LEFT);
        snprintf(line, sizeof(line), "Level: %s", GetLevelName(gLevel));
        Txt(dc, line, 0, 0, WIN_W, 40, fMed, WHITE, DT_CENTER);
        snprintf(line, sizeof(line), "Lives: %d", gLives);
        Txt(dc, line, WIN_W - 265, 0, 250, 40, fMed, WHITE, DT_RIGHT);

        if (state == OVER) {
            Box(dc, 100, 110, 600, 360, RGB(25, 30, 55));
            Outline(dc, 100, 110, 600, 360, RGB(230, 170, 30));
            TxtC(dc, "GAME OVER", 125, 80, fTitle, WHITE);
            ScoreToStr(finalScore, buf);
            snprintf(line, sizeof(line), "Final Score: %s", buf);
            TxtC(dc, line, 220, 48, fBig, GOLDC);
            snprintf(line, sizeof(line), "%s   -   %s", nameBuf, GetLevelName(level));
            TxtC(dc, line, 272, 34, fMed, WHITE);
            if (savedRank > 0) {
                snprintf(line, sizeof(line), "Saved to Leaderboard  -  Rank #%d", savedRank);
                TxtC(dc, line, 316, 34, fMed, RGB(120, 230, 120));
            }
            TxtC(dc, "ENTER - Play Again          M - Main Menu", 385, 32, fMed, WHITE);
            TxtC(dc, "L - Leaderboard", 420, 30, fSmall, RGB(200, 210, 235));
        }
    }
}

static void StartGame() {
    GameInit(level, (int)GetTickCount());
    state = PLAY;
}

static void OpenBoard(State from) {
    boardReturn = from;
    tab = 0;
    confirmClear = false;
    state = BOARD;
}

// window procedure
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 16, NULL);              // ~60 frames per second
        return 0;

    case WM_TIMER:
        if (state == PLAY) {
            int dir = 0;
            if (GetForegroundWindow() == hwnd) {
                if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) dir -= 1;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) dir += 1;
            }
            int lives = GameUpdate(dir);
            if (lives <= 0) {
                finalScore = gScore;
                savedRank = 0;
                if (QualifiesForBoard(finalScore))        // asks the assembly code
                    savedRank = SaveScore(nameBuf, finalScore, level);   // writes highscores.txt
                state = OVER;
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_CHAR:                                  // typing the player's name
        if (state == NAME) {
            if (wp == 8) {                         // backspace
                if (nameLen > 0) nameBuf[--nameLen] = 0;
            }
            else if (wp >= 32 && wp <= 126 && nameLen < 15) {
                nameBuf[nameLen++] = (char)wp;
                nameBuf[nameLen] = 0;
            }
        }
        return 0;

    case WM_KEYDOWN: {
        bool repeat = (lp & 0x40000000) != 0;
        if (state == MENU) {
            if (wp == VK_DOWN) sel = (sel + 1) % 5;
            else if (wp == VK_UP) sel = (sel + 4) % 5;
            else if (sel == 1 && wp == VK_RIGHT && !repeat) level = (level + 1) % 3;
            else if (sel == 1 && wp == VK_LEFT && !repeat)  level = (level + 2) % 3;
            else if (wp == VK_RETURN && !repeat) {
                if (sel == 0) { nameLen = (int)strlen(nameBuf); state = NAME; }   // ask name first
                else if (sel == 1) level = (level + 1) % 3;
                else if (sel == 2) OpenBoard(MENU);
                else if (sel == 3) state = HOWTO;
                else DestroyWindow(hwnd);
            }
        }
        else if (state == HOWTO) {
            if ((wp == VK_RETURN || wp == VK_ESCAPE) && !repeat) state = MENU;
        }
        else if (state == PLAY) {
            if (wp == VK_ESCAPE && !repeat) state = MENU;
        }
        else if (state == NAME) {
            if (wp == VK_RETURN && !repeat) {
                if (nameLen == 0) { strcpy_s(nameBuf, sizeof(nameBuf), "Player"); nameLen = 6; }
                StartGame();                                         // name entered -> game starts
            }
            else if (wp == VK_ESCAPE && !repeat) {
                state = MENU;
            }
        }
        else if (state == OVER) {
            if (wp == VK_RETURN && !repeat) StartGame();
            else if ((wp == 'M' || wp == VK_ESCAPE) && !repeat) state = MENU;
            else if (wp == 'L' && !repeat) OpenBoard(OVER);
        }
        else if (state == BOARD) {
            if (wp == 'C' && !repeat) {
                if (confirmClear) { ClearScores(); confirmClear = false; }
                else confirmClear = true;
            }
            else if (!repeat) {
                confirmClear = false;
                if (wp == VK_LEFT)  tab = (tab + 3) % 4;
                else if (wp == VK_RIGHT) tab = (tab + 1) % 4;
                else if (wp == VK_ESCAPE || wp == VK_RETURN) state = boardReturn;
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;                                  // we paint everything ourselves

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc);         // double buffer: no flicker
        HBITMAP bmp = CreateCompatibleBitmap(hdc, WIN_W, WIN_H);
        HGDIOBJ old = SelectObject(mem, bmp);
        Render(mem);
        BitBlt(hdc, 0, 0, WIN_W, WIN_H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// entry point
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    LoadScores();                                  // read highscores.txt (assembly)

    fSmall = CreateFontA(16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    fMed = CreateFontA(24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    fBig = CreateFontA(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    fTitle = CreateFontA(60, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "SemesterCatcherWnd";
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, WIN_W, WIN_H };              // client area must be 800x600
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "Semester Catcher", style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}