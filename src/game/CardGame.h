#pragma once
// =============================================================================
// CardGame.h – Card templates, deck, hand, play area, card library
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace myu::game {

// ─── Card Rarity ────────────────────────────────────────────────────────────

enum class CardRarity { Common, Uncommon, Rare, Epic, Legendary, kCount };

inline const char* rarityName(CardRarity r) {
    static const char* n[] = {"Common","Uncommon","Rare","Epic","Legendary"};
    return n[static_cast<int>(r)];
}
inline uint32_t rarityColorHex(CardRarity r) {
    static const uint32_t c[] = {0xB0B0B0, 0x30FF30, 0x3090FF, 0xA030FF, 0xFFAA00};
    return c[static_cast<int>(r)];
}

// ─── Card Effect ────────────────────────────────────────────────────────────

enum class EffectType {
    None, DealDamage, Heal, Buff, Debuff,
    DrawCard, Summon, MovePiece, DestroyPiece, Shield, Custom,
    kCount
};

inline const char* effectTypeName(EffectType t) {
    static const char* n[] = {
        "None","DealDamage","Heal","Buff","Debuff",
        "DrawCard","Summon","MovePiece","DestroyPiece","Shield","Custom"
    };
    return n[static_cast<int>(t)];
}

struct CardEffect {
    EffectType  type  = EffectType::None;
    int         value = 0;
    std::string target = "selected"; // "selected","all_enemy","all_ally","self","random"
    std::string description;
};

// ─── Card Template ──────────────────────────────────────────────────────────

struct CardTemplate {
    int         id = 0;
    std::string name        = "Card";
    std::string description;
    std::string artPath;
    CardRarity  rarity = CardRarity::Common;
    int         cost   = 1;
    std::string type   = "Spell"; // "Spell","Unit","Equipment","Trap"

    // Stats (Unit cards)
    int attack  = 0;
    int defense = 0;
    int hp      = 1;

    // Effects
    std::vector<CardEffect> effects;

    // Visual
    float colorR = 0.2f, colorG = 0.2f, colorB = 0.3f;

    // Tags
    std::vector<std::string> tags;
    bool hasTag(const std::string& t) const {
        return std::find(tags.begin(), tags.end(), t) != tags.end();
    }
};

// ─── Card Instance ──────────────────────────────────────────────────────────

struct Card {
    int  instanceId = 0;
    int  templateId = 0;
    int  ownerPlayer = 0;
    bool faceUp = false;
    int  costMod = 0, atkMod = 0, defMod = 0;

    int effectiveCost(const CardTemplate& t) const {
        return std::max(0, t.cost + costMod);
    }
};

// ─── Card Zone ──────────────────────────────────────────────────────────────

enum class CardZone { Deck, Hand, Field, Graveyard, Exile, Shop, kCount };

inline const char* zoneName(CardZone z) {
    static const char* n[] = {"Deck","Hand","Field","Graveyard","Exile","Shop"};
    return n[static_cast<int>(z)];
}

// ─── Player Card State ──────────────────────────────────────────────────────

struct PlayerCards {
    std::vector<Card> deck;
    std::vector<Card> hand;
    std::vector<Card> field;
    std::vector<Card> graveyard;
    int maxHandSize = 7;
    int nextInstanceId = 1;

    // Resources
    int mana = 0, maxMana = 0, manaPerTurn = 1;

    Card createCard(int templateId) {
        Card c; c.instanceId = nextInstanceId++; c.templateId = templateId;
        return c;
    }

    void buildDeck(const std::vector<int>& templateIds) {
        deck.clear();
        for (int tid : templateIds) deck.push_back(createCard(tid));
    }

    void shuffleDeck() {
        static std::mt19937 rng(std::random_device{}());
        std::shuffle(deck.begin(), deck.end(), rng);
    }

    bool drawCard() {
        if (deck.empty() || (int)hand.size() >= maxHandSize) return false;
        Card c = deck.back(); deck.pop_back();
        c.faceUp = true;
        hand.push_back(c);
        return true;
    }

    bool drawCards(int n) {
        bool ok = true;
        for (int i = 0; i < n; ++i) if (!drawCard()) ok = false;
        return ok;
    }

    bool playCard(int instanceId, const std::vector<CardTemplate>& templates) {
        for (auto it = hand.begin(); it != hand.end(); ++it) {
            if (it->instanceId == instanceId) {
                if (it->templateId >= 0 && it->templateId < (int)templates.size()) {
                    int cost = it->effectiveCost(templates[it->templateId]);
                    if (mana < cost) return false;
                    mana -= cost;
                }
                field.push_back(*it);
                hand.erase(it);
                return true;
            }
        }
        return false;
    }

    void discardCard(int instanceId) {
        for (auto it = hand.begin(); it != hand.end(); ++it) {
            if (it->instanceId == instanceId) {
                graveyard.push_back(*it); hand.erase(it); return;
            }
        }
    }

    void startTurn() {
        maxMana = std::min(maxMana + manaPerTurn, 10);
        mana = maxMana;
    }
};

// ─── Card Library ───────────────────────────────────────────────────────────

struct CardLibrary {
    std::vector<CardTemplate> templates;
    int nextId = 0;

    int addTemplate(const std::string& name,
                    const std::string& type = "Spell") {
        int id = nextId++;
        CardTemplate ct; ct.id = id; ct.name = name; ct.type = type;
        templates.push_back(ct);
        return id;
    }

    CardTemplate* get(int id) {
        return (id >= 0 && id < (int)templates.size()) ? &templates[id] : nullptr;
    }

    std::vector<CardTemplate*> byType(const std::string& type) {
        std::vector<CardTemplate*> r;
        for (auto& t : templates) if (t.type == type) r.push_back(&t);
        return r;
    }
    std::vector<CardTemplate*> byRarity(CardRarity rar) {
        std::vector<CardTemplate*> r;
        for (auto& t : templates) if (t.rarity == rar) r.push_back(&t);
        return r;
    }
};

} // namespace myu::game
