#pragma once
// =============================================================================
// BoardGame.h – Grid board, cells, pieces, move validation & history
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace myu::game {

// ─── Cell ───────────────────────────────────────────────────────────────────

enum class CellType { Normal, Blocked, Special, Start, Goal, kCount };

inline const char* cellTypeName(CellType t) {
    static const char* n[] = {"Normal","Blocked","Special","Start","Goal"};
    return n[static_cast<int>(t)];
}

struct BoardCell {
    int      row = 0, col = 0;
    CellType type = CellType::Normal;
    std::string label;
    float colorR = 0.3f, colorG = 0.3f, colorB = 0.35f, colorA = 1.0f;
    bool  highlighted = false;
    int   occupantId  = -1;   // piece ID, -1 = empty
    std::string data; // custom JSON data

    bool isEmpty()   const { return occupantId < 0; }
    bool isPlayable() const { return type != CellType::Blocked; }
};

// ─── Piece ──────────────────────────────────────────────────────────────────

enum class PieceOwner { Player1, Player2, Neutral };

inline const char* ownerName(PieceOwner o) {
    static const char* n[] = {"Player 1","Player 2","Neutral"};
    return n[static_cast<int>(o)];
}

struct PieceType {
    int         id = 0;
    std::string name;
    std::string sprite;
    std::string description;
    float       colorR = 1, colorG = 1, colorB = 1;
    int         moveRange = 1;
    bool        canDiagonal = false;
    bool        canJump     = false;
    int         attack = 0, defense = 0, hp = 1;
};

struct Piece {
    int        id      = 0;
    int        typeId  = 0;
    PieceOwner owner   = PieceOwner::Player1;
    int        row = 0, col = 0;
    bool       alive    = true;
    bool       selected = false;
    int        currentHp = 1;
    int        movesMade = 0;
};

// ─── Move ───────────────────────────────────────────────────────────────────

struct Move {
    int  pieceId;
    int  fromRow, fromCol;
    int  toRow,   toCol;
    bool isCapture      = false;
    int  capturedPieceId = -1;
    std::string special; // "castle", "promote", etc.
};

// ─── Board ──────────────────────────────────────────────────────────────────

struct Board {
    int rows = 8, cols = 8;
    std::vector<BoardCell>  cells;
    std::vector<PieceType>  pieceTypes;
    std::vector<Piece>      pieces;
    std::vector<Move>       moveHistory;
    int nextPieceId = 1;

    // Checker-board colors
    float lightR = 0.85f, lightG = 0.82f, lightB = 0.70f;
    float darkR  = 0.55f, darkG  = 0.37f, darkB  = 0.23f;
    float cellSize = 1.0f;

    // ── Init ──

    void init(int r, int c) {
        rows = r; cols = c;
        cells.resize(rows * cols);
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                auto& cell = cells[i * cols + j];
                cell.row = i; cell.col = j;
                bool dark = (i + j) % 2 == 1;
                cell.colorR = dark ? darkR : lightR;
                cell.colorG = dark ? darkG : lightG;
                cell.colorB = dark ? darkB : lightB;
            }
    }

    // ── Cell access ──

    BoardCell*       getCell(int r, int c)       { return inBounds(r,c) ? &cells[r*cols+c] : nullptr; }
    const BoardCell* getCell(int r, int c) const { return inBounds(r,c) ? &cells[r*cols+c] : nullptr; }
    bool inBounds(int r, int c) const { return r>=0 && r<rows && c>=0 && c<cols; }

    // ── Piece Types ──

    int addPieceType(const std::string& name, const std::string& sprite = "") {
        int tid = static_cast<int>(pieceTypes.size());
        PieceType pt; pt.id = tid; pt.name = name; pt.sprite = sprite;
        pieceTypes.push_back(pt);
        return tid;
    }
    PieceType* getPieceType(int tid) {
        return (tid >= 0 && tid < (int)pieceTypes.size()) ? &pieceTypes[tid] : nullptr;
    }

    // ── Piece placement ──

    Piece* placePiece(int typeId, PieceOwner owner, int r, int c) {
        auto* cell = getCell(r, c);
        if (!cell || !cell->isEmpty()) return nullptr;
        Piece p;
        p.id = nextPieceId++; p.typeId = typeId; p.owner = owner;
        p.row = r; p.col = c;
        if (auto* pt = getPieceType(typeId)) p.currentHp = pt->hp;
        cell->occupantId = p.id;
        pieces.push_back(p);
        return &pieces.back();
    }

    Piece* getPiece(int id) {
        for (auto& p : pieces) if (p.id == id && p.alive) return &p;
        return nullptr;
    }
    Piece* getPieceAt(int r, int c) {
        auto* cell = getCell(r, c);
        return (cell && !cell->isEmpty()) ? getPiece(cell->occupantId) : nullptr;
    }

    // ── Movement ──

    std::vector<std::pair<int,int>> getValidMoves(int pieceId) const {
        std::vector<std::pair<int,int>> out;
        const Piece* p = nullptr;
        for (auto& pp : pieces) if (pp.id == pieceId && pp.alive) { p = &pp; break; }
        if (!p) return out;
        const PieceType* pt = (p->typeId < (int)pieceTypes.size()) ? &pieceTypes[p->typeId] : nullptr;
        if (!pt) return out;

        int range = pt->moveRange;
        for (int dr = -range; dr <= range; ++dr)
            for (int dc = -range; dc <= range; ++dc) {
                if (dr == 0 && dc == 0) continue;
                if (!pt->canDiagonal && dr != 0 && dc != 0) continue;
                int nr = p->row + dr, nc = p->col + dc;
                auto* cell = getCell(nr, nc);
                if (!cell || !cell->isPlayable()) continue;
                if (cell->isEmpty()) {
                    out.push_back({nr, nc});
                } else {
                    const Piece* occ = nullptr;
                    for (auto& pp : pieces)
                        if (pp.id == cell->occupantId && pp.alive) { occ = &pp; break; }
                    if (occ && occ->owner != p->owner) out.push_back({nr, nc});
                }
            }
        return out;
    }

    bool movePiece(int pieceId, int toR, int toC) {
        Piece* p = getPiece(pieceId);
        if (!p) return false;
        auto* from = getCell(p->row, p->col);
        auto* to   = getCell(toR, toC);
        if (!from || !to || !to->isPlayable()) return false;

        Move m{pieceId, p->row, p->col, toR, toC};
        if (!to->isEmpty()) {
            for (auto& pp : pieces)
                if (pp.id == to->occupantId && pp.alive) {
                    m.isCapture = true;
                    m.capturedPieceId = pp.id;
                    pp.alive = false;
                    break;
                }
        }
        from->occupantId = -1;
        to->occupantId   = pieceId;
        p->row = toR; p->col = toC; p->movesMade++;
        moveHistory.push_back(m);
        return true;
    }

    bool undoMove() {
        if (moveHistory.empty()) return false;
        auto& m = moveHistory.back();
        auto* piece = getPiece(m.pieceId);
        if (!piece) { moveHistory.pop_back(); return false; }

        auto* from = getCell(m.fromRow, m.fromCol);
        auto* to   = getCell(m.toRow,   m.toCol);
        to->occupantId   = -1;
        from->occupantId = m.pieceId;
        piece->row = m.fromRow; piece->col = m.fromCol; piece->movesMade--;

        if (m.isCapture) {
            for (auto& pp : pieces)
                if (pp.id == m.capturedPieceId) {
                    pp.alive = true;
                    to->occupantId = pp.id;
                    break;
                }
        }
        moveHistory.pop_back();
        return true;
    }

    void highlightMoves(int pieceId) {
        clearHighlights();
        for (auto [r, c] : getValidMoves(pieceId))
            if (auto* cell = getCell(r, c)) cell->highlighted = true;
    }
    void clearHighlights() { for (auto& c : cells) c.highlighted = false; }

    void clearPieces() {
        pieces.clear();
        for (auto& c : cells) c.occupantId = -1;
        nextPieceId = 1;
        moveHistory.clear();
    }
};

} // namespace myu::game
