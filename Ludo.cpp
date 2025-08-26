#include "raylib.h"
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cmath>

// =================== CONFIG ===================
static const int BOARD_N = 15;             // 15x15 grid
static const int CELL = 44;
static const int SIDEBAR_W = 300;
static const int BOARD_W = BOARD_N * CELL;
static const int SCR_W = BOARD_W + SIDEBAR_W;
static const int SCR_H = BOARD_W;

// =================== UTILS ====================
float LerpF(float a, float b, float t) { return a + (b - a) * t; }
float EaseOutCubic(float t) { return 1.0f - powf(1.0f - t, 3.0f); }

struct Vec2i { int r, c; };

struct MoveAnim {
    bool active = false;
    Vector2 from{}, to{};
    float t = 0.0f;
    float duration = 0.35f;
};

struct Piece {
    int player = 0;           // 0=YELLOW, 1=BLUE, 2=GREEN, 3=RED
    bool inHome = true;
    bool inFinal = false;
    int  outerIdx = -1;
    int  finalIdx = -1;
    MoveAnim anim;
};

struct Player {
    Color color = WHITE;
    std::vector<Piece> pieces;
    bool isAI = false;
    bool finished = false;
    int  finishPlace = 0;
};

// =================== GLOBALS ==================
Rectangle boardRect{ 0,0,(float)BOARD_W,(float)BOARD_W };
std::vector<Vec2i> outerPath;               // loop squares
std::vector<std::vector<Vec2i>> finalPaths(4);
int startIndexFor[4] = { 0, 11, 23, 35 };     // per-player entry into loop
int cellFlags[BOARD_N][BOARD_N] = { 0 };      // 0=normal, 2=safe

std::vector<Player> players;
int currentPlayer = 3;                      // YOU (RED)
int rollResult = 0;
bool hasRolled = false;

bool diceRollingAnim = false;
float diceAnimTime = 0.0f;
float diceAnimDuration = 1.0f;
int diceFaceDuring = 1;

float globalTime = 0.0f;

int playersFinishedCount = 0;
int nextFinishPlace = 1;

// =================== DRAW HELPERS =============
void DrawStarOutline(int cx, int cy, float radius, Color color) {
    Vector2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float ang = i * 36 * DEG2RAD - 90 * DEG2RAD;
        float r = (i % 2 == 0) ? radius : radius / 2.5f;
        pts[i] = { cx + r * cosf(ang), cy + r * sinf(ang) };
    }
    for (int i = 0; i < 10; ++i) DrawLineV(pts[i], pts[(i + 1) % 10], color);
}

void DrawTrophy(int cx, int cy, float size, Color color) {
    DrawRectangle(cx - size / 2, cy - size, size, size / 2, color);
    DrawRectangle(cx - size / 8, cy - size / 2, size / 4, size / 2, color);
    DrawRectangle(cx - size / 2, cy, size, size / 6, color);
    DrawCircle(cx - size / 2, cy - size + size / 4, size / 4, color);
    DrawCircle(cx + size / 2, cy - size + size / 4, size / 4, color);
}

void DrawDiceFace(int face, float x, float y, float size, Color bg, Color fg) {
    DrawRectangleRounded({ x, y, size, size }, 0.2f, 6, bg);
    float r = size / 8.0f, a = size / 4.0f, b = size / 2.0f, c = size - a;
    auto dot = [&](float dx, float dy) { DrawCircle(x + dx, y + dy, r, fg); };
    if (face == 0) return;
    if (face == 1) { dot(b, b); }
    if (face == 2) { dot(a, c); dot(c, a); }
    if (face == 3) { dot(a, c); dot(b, b); dot(c, a); }
    if (face == 4) { dot(a, a); dot(a, c); dot(c, a); dot(c, c); }
    if (face == 5) { dot(a, a); dot(a, c); dot(b, b); dot(c, a); dot(c, c); }
    if (face == 6) { dot(a, a); dot(a, b); dot(a, c); dot(c, a); dot(c, b); dot(c, c); }
}

// =================== PATHS & RULES ============
Vector2 CellCenter(int row, int col) {
    return { boardRect.x + col * CELL + CELL * 0.5f,
             boardRect.y + row * CELL + CELL * 0.5f };
}

void BuildBoardAndPaths() {
    // Clear flags
    for (int r = 0; r < BOARD_N; ++r) for (int c = 0; c < BOARD_N; ++c) cellFlags[r][c] = 0;

    // --- Loop path (data-driven & consistent with visuals) ---
    // The loop runs around the central cross. (Total count here = 43 cells.)
    outerPath.clear();
    for (int c = 3; c <= 11; c++) outerPath.push_back({ 1, c });    // across top
    for (int r = 2; r <= 6; r++) outerPath.push_back({ r, 11 });   // down right
    for (int c = 10; c >= 6; c--) outerPath.push_back({ 6, c });    // left mid
    for (int r = 7; r <= 11; r++) outerPath.push_back({ r, 6 });    // down center
    for (int c = 5; c >= 1; c--) outerPath.push_back({ 11, c });    // left bottom
    for (int r = 10; r >= 6; r--) outerPath.push_back({ r, 1 });    // up left
    for (int c = 2; c <= 6; c++) outerPath.push_back({ 6, c });     // right mid
    for (int r = 5; r >= 2; r--) outerPath.push_back({ r, 6 });     // up center

    // Start indices (entry from each quadrant)
    startIndexFor[0] = 0;   // Yellow (top-left)
    startIndexFor[1] = 11;  // Blue   (top-right)
    startIndexFor[2] = 23;  // Green  (bottom-left)
    startIndexFor[3] = 35;  // Red    (bottom-right)

    // Safe cells: all starts + central crossing
    for (int p = 0; p < 4; ++p) { Vec2i s = outerPath[startIndexFor[p]]; cellFlags[s.r][s.c] = 2; }
    cellFlags[6][6] = 2;

    // Final home paths (6 cells; index 5 is the "home stop")
    finalPaths[0] = { {5,6},{4,6},{3,6},{2,6},{1,6},{0,6} };       // Yellow up
    finalPaths[1] = { {6,5},{6,4},{6,3},{6,2},{6,1},{6,0} };       // Blue left
    finalPaths[2] = { {7,6},{8,6},{9,6},{10,6},{11,6},{12,6} };    // Green down
    finalPaths[3] = { {6,7},{6,8},{6,9},{6,10},{6,11},{6,12} };    // Red right
}

void SetupPlayers() {
    players.clear(); players.resize(4);
    players[0].color = YELLOW; players[0].isAI = true;   // TL
    players[1].color = BLUE;   players[1].isAI = true;   // TR
    players[2].color = GREEN;  players[2].isAI = true;   // BL
    players[3].color = RED;    players[3].isAI = false;  // BR (You)

    for (int p = 0; p < 4; ++p) {
        players[p].pieces.resize(4);
        for (int i = 0; i < 4; ++i) {
            auto& pc = players[p].pieces[i];
            pc.player = p; pc.inHome = true; pc.inFinal = false;
            pc.outerIdx = -1; pc.finalIdx = -1; pc.anim.active = false;
        }
        players[p].finished = false; players[p].finishPlace = 0;
    }
    currentPlayer = 3; rollResult = 0; hasRolled = false;
    diceRollingAnim = false; diceAnimTime = 0.0f;
    playersFinishedCount = 0; nextFinishPlace = 1;
}

bool IsSafe(const Vec2i& v) {
    if (v.r < 0 || v.r >= BOARD_N || v.c < 0 || v.c >= BOARD_N) return false;
    return cellFlags[v.r][v.c] == 2;
}

Vector2 GetPieceScreenPos(const Piece& pc, int idx) {
    if (pc.anim.active) {
        float t = EaseOutCubic(fminf(pc.anim.t / pc.anim.duration, 1.0f));
        return { LerpF(pc.anim.from.x, pc.anim.to.x, t),
                 LerpF(pc.anim.from.y, pc.anim.to.y, t) };
    }
    if (pc.inHome) {
        int p = pc.player; int hr = (p <= 1) ? 0 : (BOARD_N - 3), hc = (p == 0 || p == 2) ? 0 : (BOARD_N - 3);
        return CellCenter(hr + (idx / 2), hc + (idx % 2));
    }
    if (pc.inFinal) {
        Vec2i s = finalPaths[pc.player][pc.finalIdx];
        return CellCenter(s.r, s.c);
    }
    Vec2i s = outerPath[pc.outerIdx];
    return CellCenter(s.r, s.c);
}

void StartAnim(Piece& pc, Vector2 dest) {
    pc.anim.active = true; pc.anim.from = GetPieceScreenPos(pc, 0);
    pc.anim.to = dest; pc.anim.t = 0.0f; pc.anim.duration = 0.35f;
}

void SpawnFromHome(int player, int pieceIdx) {
    Piece& pc = players[player].pieces[pieceIdx];
    pc.inHome = false; pc.inFinal = false; pc.outerIdx = startIndexFor[player]; pc.finalIdx = -1;
    int hr = (player <= 1) ? 0 : (BOARD_N - 3), hc = (player == 0 || player == 2) ? 0 : (BOARD_N - 3);
    Vector2 from = CellCenter(hr + (pieceIdx / 2), hc + (pieceIdx % 2));
    Vec2i startCell = outerPath[pc.outerIdx];
    Vector2 to = CellCenter(startCell.r, startCell.c);
    pc.anim = { true,from,to,0.0f,0.35f };
}

bool MovePieceBySteps(int player, int idx, int steps) {
    Piece& pc = players[player].pieces[idx];

    if (pc.inHome) {
        if (steps == 6) { SpawnFromHome(player, idx); return true; }
        return false;
    }
    if (pc.inFinal) {
        int nf = pc.finalIdx + steps;
        if (nf > 5) return false;
        pc.finalIdx = nf;
        Vec2i s = finalPaths[player][nf];
        StartAnim(pc, CellCenter(s.r, s.c));
        if (nf == 5) {
            bool allDone = true;
            for (auto& q : players[player].pieces)
                if (!(q.inFinal && q.finalIdx == 5)) { allDone = false; break; }
            if (allDone && !players[player].finished) {
                players[player].finished = true;
                players[player].finishPlace = nextFinishPlace++;
                playersFinishedCount++;
            }
        }
        return true;
    }
    // on loop
    int ni = pc.outerIdx + steps;
    if (ni < (int)outerPath.size()) {
        pc.outerIdx = ni;
        Vec2i dst = outerPath[pc.outerIdx];
        StartAnim(pc, CellCenter(dst.r, dst.c));
        if (!IsSafe(dst)) {
            for (int op = 0; op < 4; ++op) if (op != player) {
                for (auto& opp : players[op].pieces) {
                    if (!opp.inHome && !opp.inFinal && opp.outerIdx == pc.outerIdx) {
                        opp.inHome = true; opp.inFinal = false; opp.outerIdx = -1; opp.finalIdx = -1; opp.anim.active = false;
                    }
                }
            }
        }
        return true;
    }
    else {
        int excess = ni - ((int)outerPath.size() - 1); // 1..6 to enter final
        if (excess >= 1 && excess <= 6) {
            pc.inFinal = true; pc.outerIdx = -1; pc.finalIdx = excess - 1;
            Vec2i s = finalPaths[player][pc.finalIdx];
            StartAnim(pc, CellCenter(s.r, s.c));
            if (pc.finalIdx == 5) {
                bool allDone = true;
                for (auto& q : players[player].pieces)
                    if (!(q.inFinal && q.finalIdx == 5)) { allDone = false; break; }
                if (allDone && !players[player].finished) {
                    players[player].finished = true;
                    players[player].finishPlace = nextFinishPlace++;
                    playersFinishedCount++;
                }
            }
            return true;
        }
        return false;
    }
}

std::vector<int> GetLegal(int player, int dice) {
    std::vector<int> r;
    for (int i = 0; i < 4; ++i) {
        Piece& pc = players[player].pieces[i];
        if (pc.inHome) { if (dice == 6) r.push_back(i); }
        else if (pc.inFinal) { if (dice <= (5 - pc.finalIdx)) r.push_back(i); }
        else {
            int ni = pc.outerIdx + dice;
            if (ni < (int)outerPath.size() || (ni - ((int)outerPath.size() - 1)) <= 6) r.push_back(i);
        }
    }
    return r;
}

void UpdateAnims(float dt) {
    for (auto& pl : players)
        for (auto& pc : pl.pieces)
            if (pc.anim.active) {
                pc.anim.t += dt;
                if (pc.anim.t >= pc.anim.duration) { pc.anim.t = pc.anim.duration; pc.anim.active = false; }
            }
}

// =================== MAIN =====================
int main() {
    InitWindow(SCR_W, SCR_H, "Ludo (clean visuals + core mechanics)");
    SetTargetFPS(60);
    srand((unsigned)time(nullptr));

    BuildBoardAndPaths();
    SetupPlayers();

    Rectangle rollBtn = { (float)(BOARD_W + 40), (float)(SCR_H - 100), 200, 60 };

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        globalTime += dt;
        UpdateAnims(dt);

        // Dice animation tick
        if (diceRollingAnim) {
            diceAnimTime += dt;
            if (diceAnimTime >= diceAnimDuration) {
                diceRollingAnim = false; diceAnimTime = 0;
                rollResult = diceFaceDuring; hasRolled = true;
            }
            else if (fmod(globalTime, 0.08f) < 0.04f) {
                diceFaceDuring = (rand() % 6) + 1;
            }
        }

        Vector2 mouse = GetMousePosition();

        // Turn handling
        if (!players[currentPlayer].isAI) {
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                if (!hasRolled && CheckCollisionPointRec(mouse, rollBtn)) {
                    diceRollingAnim = true;
                    diceAnimDuration = 0.8f + (rand() % 4) * 0.08f;
                    diceFaceDuring = (rand() % 6) + 1;
                }
                else if (hasRolled) {
                    auto legal = GetLegal(currentPlayer, rollResult);
                    if (!legal.empty()) {
                        for (int idx : legal) {
                            Vector2 pos = GetPieceScreenPos(players[currentPlayer].pieces[idx], idx);
                            Rectangle r = { pos.x - 18, pos.y - 18, 36, 36 };
                            if (CheckCollisionPointRec(mouse, r)) {
                                if (MovePieceBySteps(currentPlayer, idx, rollResult)) {
                                    bool extra = (rollResult == 6);
                                    hasRolled = false; rollResult = 0;
                                    if (!extra) {
                                        int nxt = (currentPlayer + 1) % 4;
                                        while (players[nxt].finished) nxt = (nxt + 1) % 4;
                                        currentPlayer = nxt;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    else {
                        hasRolled = false; rollResult = 0;
                        int nxt = (currentPlayer + 1) % 4;
                        while (players[nxt].finished) nxt = (nxt + 1) % 4;
                        currentPlayer = nxt;
                    }
                }
            }
            if (IsKeyPressed(KEY_SPACE) && !hasRolled && !diceRollingAnim) {
                diceRollingAnim = true;
                diceAnimDuration = 0.8f + (rand() % 4) * 0.08f;
                diceFaceDuring = (rand() % 6) + 1;
            }
        }
        else {
            // Simple AI
            static float aiT = 0.0f; aiT += dt;
            if (aiT >= 0.6f) {
                aiT = 0.0f;
                if (!hasRolled && !diceRollingAnim) {
                    diceRollingAnim = true;
                    diceAnimDuration = 0.8f + (rand() % 4) * 0.08f;
                    diceFaceDuring = (rand() % 6) + 1;
                }
                else if (hasRolled) {
                    auto legal = GetLegal(currentPlayer, rollResult);
                    if (!legal.empty()) {
                        int pick = legal[0], best = -999;
                        for (int idx : legal) {
                            int score = 0;
                            Piece& pc = players[currentPlayer].pieces[idx];
                            if (pc.inHome && rollResult == 6) score += 10;
                            if (pc.inFinal && pc.finalIdx + rollResult == 5) score += 50;
                            if (!pc.inHome && !pc.inFinal) {
                                int ni = pc.outerIdx + rollResult;
                                if (ni < (int)outerPath.size()) {
                                    Vec2i dst = outerPath[ni];
                                    if (!IsSafe(dst)) score += 20; // possible capture
                                }
                            }
                            score += rand() % 3;
                            if (score > best) { best = score; pick = idx; }
                        }
                        MovePieceBySteps(currentPlayer, pick, rollResult);
                    }
                    bool extra = (rollResult == 6);
                    hasRolled = false; rollResult = 0;
                    if (!extra) {
                        int nxt = (currentPlayer + 1) % 4;
                        while (players[nxt].finished) nxt = (nxt + 1) % 4;
                        currentPlayer = nxt;
                    }
                }
            }
        }

        // If 3 finished, give last place automatically
        if (playersFinishedCount >= 3) {
            for (int p = 0; p < 4; ++p) if (!players[p].finished) {
                players[p].finished = true; players[p].finishPlace = nextFinishPlace++;
            }
        }

        // =================== DRAW ===================
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Board grid
        for (int r = 0; r < BOARD_N; ++r)
            for (int c = 0; c < BOARD_N; ++c)
                DrawRectangleLines(c * CELL, r * CELL, CELL, CELL, BLACK);

        // 4 home quadrants
        DrawRectangle(0, 0, 6 * CELL, 6 * CELL, RED);
        DrawRectangle(9 * CELL, 0, 6 * CELL, 6 * CELL, GREEN);
        DrawRectangle(0, 9 * CELL, 6 * CELL, 6 * CELL, BLUE);
        DrawRectangle(9 * CELL, 9 * CELL, 6 * CELL, 6 * CELL, YELLOW);

        // Loop cells visualized directly from outerPath
        for (size_t i = 0; i < outerPath.size(); ++i) {
            Vec2i p = outerPath[i];
            Rectangle rc{ (float)p.c * CELL, (float)p.r * CELL, (float)CELL, (float)CELL };
            DrawRectangleRec(rc, WHITE);
            DrawRectangleLinesEx(rc, 1.0f, BLACK);
            // Mark true safe cells only (starts + center)
            if (cellFlags[p.r][p.c] == 2) {
                DrawCircle(rc.x + CELL * 0.5f, rc.y + CELL * 0.5f, CELL * 0.18f, LIGHTGRAY);
                DrawStarOutline((int)(rc.x + CELL * 0.5f), (int)(rc.y + CELL * 0.5f), CELL * 0.20f, DARKGRAY);
            }
        }
        // Center safe (6,6) already part of loop visuals via flags

        // Final colored strips (use finalPaths; last square solid color)
        Color homeC[4] = { YELLOW, BLUE, GREEN, RED };
        for (int pl = 0; pl < 4; ++pl) {
            for (int k = 0; k < 6; ++k) {
                Vec2i p = finalPaths[pl][k];
                Rectangle rc{ (float)p.c * CELL, (float)p.r * CELL, (float)CELL, (float)CELL };
                Color col = (k == 5) ? homeC[pl] : ColorAlpha(homeC[pl], 0.55f);
                DrawRectangleRec(rc, col);
                DrawRectangleLinesEx(rc, 1.0f, BLACK);
            }
        }

        // Center base + triangles + trophy
        int cx = BOARD_W / 2, cy = BOARD_W / 2;
        DrawRectangle(6 * CELL, 6 * CELL, 3 * CELL, 3 * CELL, WHITE);
        DrawTriangle({ (float)cx,(float)cy }, { (float)6 * CELL,(float)6 * CELL }, { (float)9 * CELL,(float)6 * CELL }, RED);
        DrawTriangle({ (float)cx,(float)cy }, { (float)9 * CELL,(float)6 * CELL }, { (float)9 * CELL,(float)9 * CELL }, GREEN);
        DrawTriangle({ (float)cx,(float)cy }, { (float)6 * CELL,(float)9 * CELL }, { (float)9 * CELL,(float)9 * CELL }, YELLOW);
        DrawTriangle({ (float)cx,(float)cy }, { (float)6 * CELL,(float)6 * CELL }, { (float)6 * CELL,(float)9 * CELL }, BLUE);
        DrawTrophy(cx, cy, CELL * 0.9f, GOLD);

        // Occupancy for slight offsets
        std::map<std::pair<int, int>, int> occ, drew;
        auto keyOf = [&](const Piece& pc, int i) {
            if (pc.inHome) {
                int p = pc.player; int hr = (p <= 1) ? 0 : (BOARD_N - 3), hc = (p == 0 || p == 2) ? 0 : (BOARD_N - 3);
                return std::pair<int, int>{ hr + (i / 2), hc + (i % 2) };
            }
            else if (pc.inFinal) {
                Vec2i v = finalPaths[pc.player][pc.finalIdx];
                return std::pair<int, int>{ v.r, v.c };
            }
            else {
                Vec2i v = outerPath[pc.outerIdx];
                return std::pair<int, int>{ v.r, v.c };
            }
            };
        for (int p = 0; p < 4; ++p) for (int i = 0; i < 4; ++i) occ[keyOf(players[p].pieces[i], i)]++;

        // Draw pieces
        for (int p = 0; p < 4; ++p) {
            for (int i = 0; i < 4; ++i) {
                Piece& pc = players[p].pieces[i];
                Vector2 base = GetPieceScreenPos(pc, i);
                auto k = keyOf(pc, i);
                int count = occ[k], order = drew[k]++;
                float off = (order - (count - 1) / 2.0f) * 10.0f;
                Vector2 pos{ base.x + off, base.y - off };

                bool canPick = (!players[p].isAI && p == currentPlayer && hasRolled);
                if (canPick) {
                    auto legal = GetLegal(p, rollResult);
                    canPick = std::find(legal.begin(), legal.end(), i) != legal.end();
                }

                float rad = 14.0f + (pc.anim.active ? 2.0f * (1.0f - pc.anim.t / pc.anim.duration) : 0.0f);
                DrawCircleV(pos, rad, players[p].color);
                DrawStarOutline((int)pos.x, (int)pos.y, rad * 0.6f, WHITE);
                if (canPick) {
                    float ring = rad + 6 + (sinf(globalTime * 6.0f) + 1.0f) * 2.0f;
                    DrawCircleLines((int)pos.x, (int)pos.y, ring, ColorAlpha(players[p].color, 0.9f));
                }
            }
        }

        // Sidebar
        DrawRectangle(BOARD_W, 0, SIDEBAR_W, SCR_H, LIGHTGRAY);
        DrawText("Ludo", BOARD_W + 30, 24, 36, BLACK);
        DrawText(TextFormat("Turn: %s",
            currentPlayer == 3 ? "You (RED)" :
            currentPlayer == 2 ? "Green" :
            currentPlayer == 1 ? "Blue" : "Yellow"),
            BOARD_W + 30, 74, 22, BLACK);

        // Dice + roll button
        Rectangle diceBox = { BOARD_W + 40, SCR_H - 100, 60, 60 };
        DrawDiceFace(diceRollingAnim ? diceFaceDuring : (hasRolled ? rollResult : 0),
            diceBox.x, diceBox.y, diceBox.width, WHITE, BLACK);

        Color btnColor = ColorAlpha(GREEN, diceRollingAnim ? 0.9f : (hasRolled ? 0.55f : 0.85f));
        DrawRectangleRounded(rollBtn, 0.18f, 6, btnColor);
        DrawRectangleRoundedLines(rollBtn, 0.18f, 6, BLACK);
        DrawText(hasRolled ? "Select Piece" : "Roll Dice",
            rollBtn.x + 20, rollBtn.y + 18, 24, BLACK);

        // Finish order
        int y = 140;
        DrawText("Finish Order:", BOARD_W + 30, y, 20, BLACK); y += 28;
        for (int place = 1; place <= 4; ++place) {
            for (int p = 0; p < 4; ++p) if (players[p].finishPlace == place) {
                DrawText(TextFormat("%d) %s", place,
                    p == 3 ? "You (RED)" :
                    p == 2 ? "Green" :
                    p == 1 ? "Blue" : "Yellow"),
                    BOARD_W + 40, y, 18, players[p].color);
                y += 22;
            }
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
