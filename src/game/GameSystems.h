#pragma once
// =============================================================================
// GameSystems.h – Turn manager, timer, score, shop, audio config
// =============================================================================

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace myu::game {

// ─── Turn Phases ────────────────────────────────────────────────────────────

enum class TurnPhase { Start, Main1, Battle, Main2, End, kCount };

inline const char* phaseName(TurnPhase p) {
    static const char* n[] = {"Start","Main 1","Battle","Main 2","End"};
    return n[static_cast<int>(p)];
}

// ─── Turn Manager ───────────────────────────────────────────────────────────

struct TurnManager {
    int       currentTurn   = 0;
    int       currentPlayer = 0;
    int       numPlayers    = 2;
    TurnPhase currentPhase  = TurnPhase::Start;

    std::function<void(int turn, int player)> onTurnStart;
    std::function<void(int turn, int player)> onTurnEnd;
    std::function<void(TurnPhase)>            onPhaseChange;

    void startGame() {
        currentTurn = 1; currentPlayer = 0;
        currentPhase = TurnPhase::Start;
        if (onTurnStart) onTurnStart(currentTurn, currentPlayer);
    }

    void nextPhase() {
        int next = static_cast<int>(currentPhase) + 1;
        if (next >= static_cast<int>(TurnPhase::kCount)) { endTurn(); return; }
        currentPhase = static_cast<TurnPhase>(next);
        if (onPhaseChange) onPhaseChange(currentPhase);
    }

    void endTurn() {
        if (onTurnEnd) onTurnEnd(currentTurn, currentPlayer);
        currentPlayer = (currentPlayer + 1) % numPlayers;
        if (currentPlayer == 0) currentTurn++;
        currentPhase = TurnPhase::Start;
        if (onTurnStart) onTurnStart(currentTurn, currentPlayer);
    }

    bool isPlayerTurn(int p) const { return currentPlayer == p; }
};

// ─── Game Timer ─────────────────────────────────────────────────────────────

struct GameTimer {
    float turnTimeLimit    = 60.0f;
    float gameTimeLimit    = 0;    // 0 = no limit
    float turnTimeRemaining = 60.0f;
    float player1Time = 0, player2Time = 0;
    bool  running = false, expired = false;

    std::function<void(int player)> onTimeExpired;

    void start()   { running = true; expired = false; turnTimeRemaining = turnTimeLimit; }
    void pause()   { running = false; }
    void resume()  { running = true; }
    void resetTurn() { turnTimeRemaining = turnTimeLimit; expired = false; }

    void update(float dt, int currentPlayer) {
        if (!running || expired) return;
        turnTimeRemaining -= dt;
        if (currentPlayer == 0) player1Time += dt; else player2Time += dt;
        if (turnTimeRemaining <= 0) {
            turnTimeRemaining = 0; expired = true;
            if (onTimeExpired) onTimeExpired(currentPlayer);
        }
    }

    std::string formatTime(float seconds) const {
        int m = static_cast<int>(seconds) / 60;
        int s = static_cast<int>(seconds) % 60;
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
        return buf;
    }
};

// ─── Score System ───────────────────────────────────────────────────────────

struct ScoreEvent { int player; int delta; std::string reason; };

struct ScoreSystem {
    int  player1Score = 0, player2Score = 0;
    int  winScore = 0;    // 0 = no score limit
    bool gameOver = false;
    int  winner   = -1;
    std::vector<ScoreEvent> history;

    std::function<void(int player, int score)> onScoreChange;
    std::function<void(int winner)>            onGameOver;

    void addScore(int player, int delta, const std::string& reason = "") {
        if (gameOver) return;
        int& s = (player == 0) ? player1Score : player2Score;
        s += delta;
        history.push_back({player, delta, reason});
        if (onScoreChange) onScoreChange(player, s);
        if (winScore > 0 && s >= winScore) {
            gameOver = true; winner = player;
            if (onGameOver) onGameOver(winner);
        }
    }

    int  getScore(int p) const { return (p == 0) ? player1Score : player2Score; }
    void reset() {
        player1Score = player2Score = 0;
        gameOver = false; winner = -1; history.clear();
    }
};

// ─── Shop / Store ───────────────────────────────────────────────────────────

struct ShopItem {
    int         id = 0;
    std::string name;
    std::string description;
    std::string iconPath;
    int         price = 0;
    std::string category; // "card","piece","cosmetic","boost"
    int         stock    = -1; // -1 = infinite
    int         dataId   = -1; // reference to template ID
    bool        available = true;
};

struct Shop {
    std::vector<ShopItem> items;
    int nextItemId = 1;
    int player1Gold = 100, player2Gold = 100;

    std::function<void(int player, const ShopItem&)> onPurchase;

    int addItem(const std::string& name, int price,
                const std::string& cat = "", int dataId = -1) {
        int id = nextItemId++;
        items.push_back({id, name, "", "", price, cat, -1, dataId, true});
        return id;
    }

    ShopItem* getItem(int id) {
        for (auto& i : items) if (i.id == id) return &i;
        return nullptr;
    }

    bool canAfford(int player, int itemId) const {
        int gold = (player == 0) ? player1Gold : player2Gold;
        for (auto& i : items)
            if (i.id == itemId)
                return gold >= i.price && i.available && i.stock != 0;
        return false;
    }

    bool purchase(int player, int itemId) {
        auto* item = getItem(itemId);
        if (!item || !item->available) return false;
        if (item->stock == 0) return false;
        int& gold = (player == 0) ? player1Gold : player2Gold;
        if (gold < item->price) return false;
        gold -= item->price;
        if (item->stock > 0) item->stock--;
        if (onPurchase) onPurchase(player, *item);
        return true;
    }

    std::vector<ShopItem*> byCategory(const std::string& cat) {
        std::vector<ShopItem*> r;
        for (auto& i : items) if (i.category == cat && i.available) r.push_back(&i);
        return r;
    }

    void refresh() { for (auto& i : items) i.available = true; }
};

// ─── Audio Config ───────────────────────────────────────────────────────────

struct SFXEntry { std::string name, path; float volume = 1.0f; };

struct AudioConfig {
    std::string bgmPath;
    float bgmVolume = 0.5f;
    bool  bgmLoop   = true;
    std::vector<SFXEntry> sfx;

    void addSFX(const std::string& name, const std::string& path, float vol = 1.0f) {
        sfx.push_back({name, path, vol});
    }
    const SFXEntry* findSFX(const std::string& name) const {
        for (auto& s : sfx) if (s.name == name) return &s;
        return nullptr;
    }
};

} // namespace myu::game
