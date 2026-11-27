#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rg::bestiary
{

struct Drop
{
    int16_t itemId = -1;
    int16_t iconId = -1;
    std::string itemName;
    int ratePercent = 0;
};

struct Monster
{
    int16_t id = -1;
    std::string name;
    std::string modelName;
    int defeated = 0;
    std::vector<Drop> drops;
};

struct ItemSource
{
    int16_t monsterId = -1;
    std::string monsterName;
    std::string modelName;
    int ratePercent = 0;
    int monsterDefeated = 0;
};

struct Item
{
    int16_t id = -1;
    int16_t iconId = -1;
    std::string name;
    std::vector<ItemSource> sources;
};

struct Snapshot
{
    bool ready = false;
    std::vector<Monster> monsters;
    std::vector<Item> items;
};

struct IconAtlas
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Reads the monster definitions, item definitions, drop tables and defeated
// counts currently loaded in guest RAM. Returns `ready = false` before a game's
// world data tables have been loaded.
Snapshot read(const uint8_t *rdram);

// Decodes the item icon atlas (menu\icon_all2.img) directly from the player's
// disc archive (DATA0_0.HD6 / DATA0_0.DAT) into an RGBA32 image.
IconAtlas loadIconAtlas();

// Renders a 32x32 anti-aliased 3D thumbnail of a monster's .chr model directly
// from the player's disc archives (DATA0_0.DAT / DATA1_0.DAT).
IconAtlas renderMonsterIcon(const std::string &modelName);

} // namespace rg::bestiary
