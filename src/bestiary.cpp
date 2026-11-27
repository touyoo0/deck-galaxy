#include "bestiary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

namespace rg::bestiary
{
namespace
{

// Static globals and table offsets from SCUS-97490 (GetWorldDataMgr_1fedf0,
// WorldDataMgrInit_3f3060, WorldSubsysInit_40c5c0 / 419ae0,
// DataTableCmdEntryDefine_41b010, BattleCalcResults_3a2100,
// BestiaryCountFindMaybe_3c3440).
constexpr uint32_t kWorldDataMgr = 0x004B8A80u;
constexpr uint32_t kItemSubsysOffset = 0x40u;
constexpr uint32_t kItemTableOffset = 0x08u;
constexpr uint32_t kMonsterSubsysOffset = 0x64u;
constexpr uint32_t kMonsterTableOffset = 0x0Cu;

constexpr uint32_t kTableCountOffset = 0x7Cu;
constexpr uint32_t kTableEntriesOffset = 0x8Cu;

constexpr uint32_t kItemEntrySize = 0x34u;
constexpr uint32_t kItemIdOffset = 0x00u;
constexpr uint32_t kItemKindOffset = 0x02u;
constexpr uint32_t kItemCharaIdOffset = 0x08u;
constexpr uint32_t kItemNameOffset = 0x0Cu;
constexpr uint32_t kItemWeaponDataOffset = 0x20u;
constexpr uint32_t kItemIconOffset = 0x2Cu;
constexpr uint32_t kWeaponPowerOffset = 0x06u;
constexpr int16_t kFirstMainCharaId = 0x2AF9; // Jaster..Deego (8 playable characters)

constexpr uint32_t kMonsterEntrySize = 0xA4u;
constexpr uint32_t kMonsterIdOffset = 0x00u;
constexpr uint32_t kMonsterNameOffset = 0x04u;
constexpr uint32_t kMonsterModelPtrOffset = 0x20u;
constexpr uint32_t kMonModelChrNameOffset = 0x0Cu;
constexpr uint32_t kMonsterDropCountOffset = 0x9Au;
constexpr uint32_t kMonsterDropsPtrOffset = 0x9Cu;
constexpr uint32_t kDropEntrySize = 0x04u;

constexpr uint32_t kPartyPtr = 0x00483820u;
constexpr uint32_t kPartyBestiaryOffset = 0x17Cu;
constexpr uint32_t kBestiaryCapacity = 0x00465B70u;

constexpr int kMaxTableEntries = 4096;
constexpr int kMaxDropsPerMonster = 32;
constexpr size_t kMaxGuestString = 128u;

template <typename T>
bool readGuest(const uint8_t *rdram, uint32_t address, T &out)
{
    if (!rdram || address == 0u)
    {
        return false;
    }
    const uint32_t phys = address & 0x1FFFFFFFu;
    if (phys == 0u || phys + sizeof(T) > PS2_RAM_SIZE)
    {
        return false;
    }
    std::memcpy(&out, rdram + phys, sizeof(T));
    return true;
}

std::string readGuestString(const uint8_t *rdram, uint32_t address)
{
    if (!rdram || address == 0u)
    {
        return {};
    }
    const uint32_t phys = address & 0x1FFFFFFFu;
    if (phys == 0u || phys >= PS2_RAM_SIZE)
    {
        return {};
    }
    const size_t limit = std::min<size_t>(kMaxGuestString, PS2_RAM_SIZE - phys);
    const char *start = reinterpret_cast<const char *>(rdram + phys);
    size_t len = 0u;
    while (len < limit && start[len] != '\0')
    {
        ++len;
    }
    return std::string(start, len);
}

// European accent tags as used in the PAL texts (src/text_language.cpp).
struct AccentTag
{
    const char *tag;
    const char *utf8;
};

constexpr AccentTag kAccentTags[] = {
    {"<9>", "'"},
    {"<6>", "'"},
    {"<66>", "\""},
    {"<99>", "\""},
    {"<99G>", "\""},
    {"<->", "-"},
    {"<-->", "\xE2\x80\x93"},
    {"<--->", "\xE2\x80\x94"},
    {"<^!>", "\xC2\xA1"},
    {"<^?>", "\xC2\xBF"},
    {"<<<>", "\xC2\xAB"},
    {"<>>>", "\xC2\xBB"},
    {"<degree>", "\xC2\xB0"},
    {"<OE>", "\xC5\x92"},
    {"<oe>", "\xC5\x93"},
    {"<`A>", "\xC3\x80"},
    {"<'A>", "\xC3\x81"},
    {"<^A>", "\xC3\x82"},
    {"<:A>", "\xC3\x84"},
    {"<,C>", "\xC3\x87"},
    {"<`E>", "\xC3\x88"},
    {"<'E>", "\xC3\x89"},
    {"<^E>", "\xC3\x8A"},
    {"<:E>", "\xC3\x8B"},
    {"<`I>", "\xC3\x8C"},
    {"<'I>", "\xC3\x8D"},
    {"<^I>", "\xC3\x8E"},
    {"<:I>", "\xC3\x8F"},
    {"<~N>", "\xC3\x91"},
    {"<`O>", "\xC3\x92"},
    {"<'O>", "\xC3\x93"},
    {"<^O>", "\xC3\x94"},
    {"<:O>", "\xC3\x96"},
    {"<`U>", "\xC3\x99"},
    {"<'U>", "\xC3\x9A"},
    {"<^U>", "\xC3\x9B"},
    {"<:U>", "\xC3\x9C"},
    {"<`a>", "\xC3\xA0"},
    {"<'a>", "\xC3\xA1"},
    {"<^a>", "\xC3\xA2"},
    {"<:a>", "\xC3\xA4"},
    {"<,c>", "\xC3\xA7"},
    {"<`e>", "\xC3\xA8"},
    {"<'e>", "\xC3\xA9"},
    {"<^e>", "\xC3\xAA"},
    {"<:e>", "\xC3\xAB"},
    {"<`i>", "\xC3\xAC"},
    {"<'i>", "\xC3\xAD"},
    {"<^i>", "\xC3\xAE"},
    {"<:i>", "\xC3\xAF"},
    {"<~n>", "\xC3\xB1"},
    {"<`o>", "\xC3\xB2"},
    {"<'o>", "\xC3\xB3"},
    {"<^o>", "\xC3\xB4"},
    {"<:o>", "\xC3\xB6"},
    {"<~O>", "\xC3\x95"},
    {"<~o>", "\xC3\xB5"},
    {"<`u>", "\xC3\xB9"},
    {"<'u>", "\xC3\xBA"},
    {"<^u>", "\xC3\xBB"},
    {"<:u>", "\xC3\xBC"},
    {"<ss>", "\xC3\x9F"},
    {"<a>", "\xC2\xAA"},
    {"<0>", "\xC2\xBA"},
};

std::string decodeGameText(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0u; i < raw.size();)
    {
        if (raw[i] == '<')
        {
            bool matched = false;
            for (const AccentTag &accent : kAccentTags)
            {
                const size_t tagLen = std::strlen(accent.tag);
                if (i + tagLen <= raw.size() && raw.compare(i, tagLen, accent.tag) == 0)
                {
                    out += accent.utf8;
                    i += tagLen;
                    matched = true;
                    break;
                }
            }
            if (matched)
            {
                continue;
            }
            const size_t close = raw.find('>', i + 1u);
            if (close != std::string::npos && close - i <= 16u)
            {
                i = close + 1u;
                continue;
            }
        }
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c >= 0x20u && c != 0x7Fu)
        {
            out.push_back(raw[i]);
        }
        else if (c == '\n' || c == '\t')
        {
            out.push_back(' ');
        }
        ++i;
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos)
    {
        return {};
    }
    const size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1u);
}

bool lessIgnoreCase(const std::string &a, const std::string &b)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0u; i < n; ++i)
    {
        const unsigned char ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
        const unsigned char cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb)
        {
            return ca < cb;
        }
    }
    return a.size() < b.size();
}

bool sameDrops(const std::vector<Drop> &a, const std::vector<Drop> &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0u; i < a.size(); ++i)
    {
        if (a[i].itemId != b[i].itemId || a[i].ratePercent != b[i].ratePercent)
        {
            return false;
        }
    }
    return true;
}

struct RawFusion
{
    int16_t idA = -1;
    int16_t idB = -1;
    int16_t outId = -1;
    int charaIndex = -1;
    bool subWeapon = false;
    bool bonus = false;
};

const std::vector<RawFusion> &loadRawFusions();

} // namespace

Snapshot read(const uint8_t *rdram)
{
    Snapshot out;
    if (!rdram)
    {
        return out;
    }

    uint32_t itemSubsys = 0u, itemTable = 0u, itemEntries = 0u;
    int16_t itemCount = 0;
    if (!readGuest(rdram, kWorldDataMgr + kItemSubsysOffset, itemSubsys) ||
        !readGuest(rdram, itemSubsys + kItemTableOffset, itemTable) ||
        !readGuest(rdram, itemTable + kTableCountOffset, itemCount) ||
        !readGuest(rdram, itemTable + kTableEntriesOffset, itemEntries) || itemCount <= 0 ||
        itemCount > kMaxTableEntries || itemEntries == 0u)
    {
        return out;
    }

    uint32_t monsterSubsys = 0u, monsterTable = 0u, monsterEntries = 0u;
    int16_t monsterCount = 0;
    if (!readGuest(rdram, kWorldDataMgr + kMonsterSubsysOffset, monsterSubsys) ||
        !readGuest(rdram, monsterSubsys + kMonsterTableOffset, monsterTable) ||
        !readGuest(rdram, monsterTable + kTableCountOffset, monsterCount) ||
        !readGuest(rdram, monsterTable + kTableEntriesOffset, monsterEntries) || monsterCount <= 0 ||
        monsterCount > kMaxTableEntries || monsterEntries == 0u)
    {
        return out;
    }

    struct ItemInfo
    {
        std::string name;
        int16_t iconId = -1;
    };
    std::unordered_map<int16_t, ItemInfo> itemInfos;
    itemInfos.reserve(static_cast<size_t>(itemCount));
    for (int i = 0; i < itemCount; ++i)
    {
        const uint32_t entry = itemEntries + static_cast<uint32_t>(i) * kItemEntrySize;
        int16_t id = -1;
        uint32_t namePtr = 0u;
        int16_t iconId = -1;
        if (!readGuest(rdram, entry + kItemIdOffset, id) || id < 0 ||
            !readGuest(rdram, entry + kItemNameOffset, namePtr))
        {
            continue;
        }
        readGuest(rdram, entry + kItemIconOffset, iconId);
        std::string name = decodeGameText(readGuestString(rdram, namePtr));
        if (!name.empty())
        {
            int8_t kind = -1;
            int16_t charaId = -1;
            if (readGuest(rdram, entry + kItemKindOffset, kind) && (kind == 0 || kind == 1) &&
                readGuest(rdram, entry + kItemCharaIdOffset, charaId) && charaId >= kFirstMainCharaId &&
                charaId < kFirstMainCharaId + 8)
            {
                int attack = 0;
                uint32_t weaponData = 0u;
                uint16_t powerWord = 0u;
                if (readGuest(rdram, entry + kItemWeaponDataOffset, weaponData) &&
                    readGuest(rdram, weaponData + kWeaponPowerOffset, powerWord))
                {
                    attack = static_cast<int>(powerWord & 0x3FFu);
                }
                out.weapons.push_back({id, iconId, static_cast<int>(charaId - kFirstMainCharaId), kind == 1, attack,
                                       name});
            }
            itemInfos.emplace(id, ItemInfo{std::move(name), iconId});
        }
    }

    std::unordered_map<int16_t, int> defeatedById;
    uint32_t party = 0u, bestiaryArr = 0u;
    int32_t bestiaryCap = 0;
    if (readGuest(rdram, kPartyPtr, party) && readGuest(rdram, party + kPartyBestiaryOffset, bestiaryArr) &&
        readGuest(rdram, kBestiaryCapacity, bestiaryCap) && bestiaryCap > 0 && bestiaryCap <= kMaxTableEntries)
    {
        for (int i = 0; i < bestiaryCap; ++i)
        {
            const uint32_t slot = bestiaryArr + static_cast<uint32_t>(i) * 4u;
            int16_t monId = -1;
            int16_t kills = 0;
            if (readGuest(rdram, slot, monId) && readGuest(rdram, slot + 2u, kills) && monId >= 0 && kills > 0)
            {
                defeatedById[monId] = kills;
            }
        }
    }

    std::vector<Monster> rawMonsters;
    rawMonsters.reserve(static_cast<size_t>(monsterCount));
    for (int i = 0; i < monsterCount; ++i)
    {
        const uint32_t entry = monsterEntries + static_cast<uint32_t>(i) * kMonsterEntrySize;
        int16_t id = -1;
        uint32_t namePtr = 0u;
        if (!readGuest(rdram, entry + kMonsterIdOffset, id) || id < 0 ||
            !readGuest(rdram, entry + kMonsterNameOffset, namePtr))
        {
            continue;
        }
        std::string name = decodeGameText(readGuestString(rdram, namePtr));
        if (name.empty())
        {
            continue;
        }

        Monster monster;
        monster.id = id;
        monster.name = std::move(name);
        uint32_t modelPtr = 0u, chrNamePtr = 0u;
        if (readGuest(rdram, entry + kMonsterModelPtrOffset, modelPtr) &&
            readGuest(rdram, modelPtr + kMonModelChrNameOffset, chrNamePtr))
        {
            std::string chr = readGuestString(rdram, chrNamePtr);
            for (char &c : chr)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            monster.modelName = std::move(chr);
        }
        if (const auto it = defeatedById.find(id); it != defeatedById.end())
        {
            monster.defeated = it->second;
        }

        int16_t dropCount = 0;
        uint32_t dropsPtr = 0u;
        if (readGuest(rdram, entry + kMonsterDropCountOffset, dropCount) &&
            readGuest(rdram, entry + kMonsterDropsPtrOffset, dropsPtr) && dropCount > 0 &&
            dropCount <= kMaxDropsPerMonster && dropsPtr != 0u)
        {
            for (int d = 0; d < dropCount; ++d)
            {
                const uint32_t dropAddr = dropsPtr + static_cast<uint32_t>(d) * kDropEntrySize;
                int16_t itemId = -1;
                int16_t rate = 0;
                if (!readGuest(rdram, dropAddr, itemId) || !readGuest(rdram, dropAddr + 2u, rate) || itemId < 0 ||
                    rate <= 0)
                {
                    continue;
                }
                std::string itemName;
                int16_t iconId = -1;
                if (const auto it = itemInfos.find(itemId); it != itemInfos.end())
                {
                    itemName = it->second.name;
                    iconId = it->second.iconId;
                }
                else
                {
                    char fallback[32];
                    std::snprintf(fallback, sizeof(fallback), "Item #%d", static_cast<int>(itemId));
                    itemName = fallback;
                }
                auto existing = std::find_if(monster.drops.begin(), monster.drops.end(),
                                             [itemId](const Drop &entry) { return entry.itemId == itemId; });
                if (existing != monster.drops.end())
                {
                    existing->ratePercent = std::max(existing->ratePercent, static_cast<int>(rate));
                }
                else
                {
                    monster.drops.push_back({itemId, iconId, std::move(itemName), static_cast<int>(rate)});
                }
            }
            std::sort(monster.drops.begin(), monster.drops.end(), [](const Drop &a, const Drop &b) {
                if (a.ratePercent != b.ratePercent)
                {
                    return a.ratePercent > b.ratePercent;
                }
                return lessIgnoreCase(a.itemName, b.itemName);
            });
        }

        // Merge duplicate definitions that share both name and exact drop table.
        auto dup = std::find_if(rawMonsters.begin(), rawMonsters.end(), [&](const Monster &other) {
            return other.name == monster.name && sameDrops(other.drops, monster.drops);
        });
        if (dup != rawMonsters.end())
        {
            dup->defeated += monster.defeated;
        }
        else
        {
            rawMonsters.push_back(std::move(monster));
        }
    }

    std::sort(rawMonsters.begin(), rawMonsters.end(), [](const Monster &a, const Monster &b) {
        if (lessIgnoreCase(a.name, b.name))
        {
            return true;
        }
        if (lessIgnoreCase(b.name, a.name))
        {
            return false;
        }
        return a.id < b.id;
    });

    // Build reverse index: item -> monsters that drop it.
    std::unordered_map<int16_t, size_t> itemIndex;
    for (const Monster &monster : rawMonsters)
    {
        for (const Drop &drop : monster.drops)
        {
            auto [it, inserted] = itemIndex.emplace(drop.itemId, out.items.size());
            if (inserted)
            {
                out.items.push_back({drop.itemId, drop.iconId, drop.itemName, {}});
            }
            Item &item = out.items[it->second];
            auto existing = std::find_if(item.sources.begin(), item.sources.end(), [&](const ItemSource &src) {
                return src.monsterName == monster.name && src.ratePercent == drop.ratePercent;
            });
            if (existing != item.sources.end())
            {
                existing->monsterDefeated += monster.defeated;
            }
            else
            {
                item.sources.push_back({monster.id, monster.name, monster.modelName, drop.ratePercent, monster.defeated});
            }
        }
    }

    for (Item &item : out.items)
    {
        std::sort(item.sources.begin(), item.sources.end(), [](const ItemSource &a, const ItemSource &b) {
            if (a.ratePercent != b.ratePercent)
            {
                return a.ratePercent > b.ratePercent;
            }
            return lessIgnoreCase(a.monsterName, b.monsterName);
        });
    }

    std::sort(out.items.begin(), out.items.end(), [](const Item &a, const Item &b) {
        if (lessIgnoreCase(a.name, b.name))
        {
            return true;
        }
        if (lessIgnoreCase(b.name, a.name))
        {
            return false;
        }
        return a.id < b.id;
    });

    std::sort(out.weapons.begin(), out.weapons.end(), [](const Weapon &a, const Weapon &b) {
        if (a.charaIndex != b.charaIndex)
            return a.charaIndex < b.charaIndex;
        if (a.subWeapon != b.subWeapon)
            return !a.subWeapon;
        if (a.attack != b.attack)
            return a.attack < b.attack;
        return lessIgnoreCase(a.name, b.name);
    });

    // Resolve cached Weapon Combine (Toady synthesis) recipes from
    // rc\param\purpose\gama_2_*.pac -> gama_matrix2.cfg.bin against out.weapons.
    std::unordered_map<int16_t, int> weaponIndexById;
    weaponIndexById.reserve(out.weapons.size());
    for (size_t i = 0; i < out.weapons.size(); ++i)
    {
        weaponIndexById.emplace(out.weapons[i].id, static_cast<int>(i));
    }
    const std::vector<RawFusion> &rawFusions = loadRawFusions();
    out.fusions.reserve(rawFusions.size());
    for (const RawFusion &rf : rawFusions)
    {
        const auto itA = weaponIndexById.find(rf.idA);
        const auto itB = weaponIndexById.find(rf.idB);
        const auto itO = weaponIndexById.find(rf.outId);
        if (itA == weaponIndexById.end() || itB == weaponIndexById.end() || itO == weaponIndexById.end())
        {
            continue;
        }
        const int wA = std::min(itA->second, itB->second);
        const int wB = std::max(itA->second, itB->second);
        out.fusions.push_back({wA, wB, itO->second, rf.charaIndex, rf.subWeapon, rf.bonus});
    }
    std::sort(out.fusions.begin(), out.fusions.end(), [&](const Fusion &a, const Fusion &b) {
        if (a.charaIndex != b.charaIndex)
            return a.charaIndex < b.charaIndex;
        if (a.subWeapon != b.subWeapon)
            return !a.subWeapon;
        if (a.output != b.output)
            return a.output < b.output;
        if (a.inputA != b.inputA)
            return a.inputA < b.inputA;
        return a.inputB < b.inputB;
    });

    out.monsters = std::move(rawMonsters);
    out.ready = !out.monsters.empty();
    return out;
}

IconAtlas loadIconAtlas()
{
    IconAtlas atlas;
    std::filesystem::path root = PS2Runtime::getIoPaths().cdRoot;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root / "DATA0_0.HD6", ec))
    {
        root = "disc";
    }
    const std::filesystem::path hd6Path = root / "DATA0_0.HD6";
    const std::filesystem::path datPath = root / "DATA0_0.DAT";

    std::ifstream hd6File(hd6Path, std::ios::binary);
    if (!hd6File)
    {
        return atlas;
    }
    const std::vector<uint8_t> hd6((std::istreambuf_iterator<char>(hd6File)), std::istreambuf_iterator<char>());
    if (hd6.size() < 0x30u || std::memcmp(hd6.data(), "HD6\0", 4) != 0)
    {
        return atlas;
    }
    const auto u32 = [&](size_t off) {
        uint32_t v = 0;
        std::memcpy(&v, hd6.data() + off, 4);
        return v;
    };
    const auto u64 = [&](size_t off) {
        uint64_t v = 0;
        std::memcpy(&v, hd6.data() + off, 8);
        return v;
    };
    const uint32_t tokOff = u32(0x04u);
    const uint32_t tokSize = u32(0x08u);
    const uint32_t tokCount = u32(0x0Cu);
    const uint32_t nameOff = u32(0x14u);
    const uint32_t unit = u32(0x20u);
    const uint32_t count = u32(0x24u);
    const uint32_t tableOff = u32(0x28u);
    if (tokOff + tokSize > hd6.size() || tableOff + static_cast<size_t>(count) * 8u > hd6.size())
    {
        return atlas;
    }

    std::vector<std::string_view> tokens;
    tokens.reserve(tokCount);
    size_t pos = tokOff;
    const size_t tokEnd = tokOff + tokSize;
    while (tokens.size() < tokCount && pos < tokEnd)
    {
        const size_t start = pos;
        while (pos < tokEnd && hd6[pos] != 0)
        {
            ++pos;
        }
        tokens.emplace_back(reinterpret_cast<const char *>(hd6.data() + start), pos - start);
        if (pos < tokEnd)
        {
            ++pos;
        }
    }

    uint64_t fileOffset = 0;
    uint64_t fileSize = 0;
    bool found = false;
    std::string name;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t v = u64(tableOff + static_cast<size_t>(i) * 8u);
        size_t p = nameOff + static_cast<size_t>(v & 0x3FFFFu);
        name.clear();
        while (p < hd6.size() && hd6[p] != 0)
        {
            uint32_t token = 0;
            uint32_t shift = 0;
            while (p < hd6.size())
            {
                const uint8_t b = hd6[p++];
                token |= static_cast<uint32_t>(b & 0x7Fu) << shift;
                shift += 7u;
                if ((b & 0x80u) == 0u)
                {
                    break;
                }
            }
            if (token < tokens.size())
            {
                name.append(tokens[token]);
            }
        }
        for (char &c : name)
        {
            if (c == '/')
            {
                c = '\\';
            }
            else
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        if (name == "menu\\icon_all2.img")
        {
            fileOffset = ((v >> 18u) & 0x3FFFFFu) * 2048ull;
            fileSize = (v >> 40u) * static_cast<uint64_t>(unit);
            found = true;
            break;
        }
    }
    if (!found || fileSize < 0x80u || fileSize > 8u * 1024u * 1024u)
    {
        return atlas;
    }

    std::ifstream datFile(datPath, std::ios::binary);
    if (!datFile)
    {
        return atlas;
    }
    datFile.seekg(static_cast<std::streamoff>(fileOffset), std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
    if (!datFile.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(buf.size())))
    {
        return atlas;
    }

    // Find TIM2 header inside the IM3 wrapper.
    size_t tim2 = std::string::npos;
    for (size_t i = 0; i + 0x40u <= buf.size(); ++i)
    {
        if (buf[i] == 'T' && buf[i + 1] == 'I' && buf[i + 2] == 'M' && buf[i + 3] == '2')
        {
            tim2 = i;
            break;
        }
    }
    if (tim2 == std::string::npos)
    {
        return atlas;
    }
    const uint8_t fmt = buf[tim2 + 5u];
    const size_t head = tim2 + (fmt == 0u ? 0x10u : 0x80u);
    if (head + 48u > buf.size())
    {
        return atlas;
    }
    uint32_t imgSz = 0;
    uint16_t hsize = 0, ccols = 0, w = 0, h = 0;
    std::memcpy(&imgSz, buf.data() + head + 8u, 4);
    std::memcpy(&hsize, buf.data() + head + 12u, 2);
    std::memcpy(&ccols, buf.data() + head + 14u, 2);
    const uint8_t ctype = buf[head + 18u];
    const uint8_t itype = buf[head + 19u];
    std::memcpy(&w, buf.data() + head + 20u, 2);
    std::memcpy(&h, buf.data() + head + 22u, 2);

    // Expect T8 (5) + CT32 (3) 256-color palette.
    const size_t imgOff = head + hsize;
    const size_t clutOff = imgOff + imgSz;
    if ((itype & 0x3Fu) != 5u || (ctype & 0x3Fu) != 3u || ccols != 256u || w == 0u || h == 0u ||
        imgOff + static_cast<size_t>(w) * static_cast<size_t>(h) > buf.size() || clutOff + 1024u > buf.size())
    {
        return atlas;
    }

    const bool csm2 = (ctype & 0x80u) != 0u;
    uint8_t palette[256][4];
    for (int i = 0; i < 256; ++i)
    {
        const int si = csm2 ? i : ((i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1));
        const size_t p = clutOff + static_cast<size_t>(si) * 4u;
        palette[i][0] = buf[p + 0u];
        palette[i][1] = buf[p + 1u];
        palette[i][2] = buf[p + 2u];
        const int a = static_cast<int>(buf[p + 3u]) * 2;
        palette[i][3] = static_cast<uint8_t>(a > 255 ? 255 : a);
    }

    atlas.width = w;
    atlas.height = h;
    atlas.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t idx = buf[imgOff + i];
        std::memcpy(atlas.rgba.data() + i * 4u, palette[idx], 4);
    }
    return atlas;
}

namespace
{

struct ArchiveFile
{
    int layer = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

struct DiscCache
{
    bool loaded = false;
    std::filesystem::path datPaths[2];
    std::unordered_map<std::string, ArchiveFile> files;
};

DiscCache &discCache()
{
    static DiscCache cache;
    if (cache.loaded)
    {
        return cache;
    }
    cache.loaded = true;
    std::filesystem::path root = PS2Runtime::getIoPaths().cdRoot;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root / "DATA0_0.HD6", ec))
    {
        root = "disc";
    }
    const char *names[2] = {"DATA0_0", "DATA1_0"};
    for (int layer = 0; layer < 2; ++layer)
    {
        cache.datPaths[layer] = root / (std::string(names[layer]) + ".DAT");
        std::ifstream hd6File(root / (std::string(names[layer]) + ".HD6"), std::ios::binary);
        if (!hd6File)
        {
            continue;
        }
        const std::vector<uint8_t> hd6((std::istreambuf_iterator<char>(hd6File)), std::istreambuf_iterator<char>());
        if (hd6.size() < 0x30u || std::memcmp(hd6.data(), "HD6\0", 4) != 0)
        {
            continue;
        }
        const auto u32 = [&](size_t off) {
            uint32_t v = 0;
            std::memcpy(&v, hd6.data() + off, 4);
            return v;
        };
        const auto u64 = [&](size_t off) {
            uint64_t v = 0;
            std::memcpy(&v, hd6.data() + off, 8);
            return v;
        };
        const uint32_t tokOff = u32(0x04u);
        const uint32_t tokSize = u32(0x08u);
        const uint32_t tokCount = u32(0x0Cu);
        const uint32_t nameOff = u32(0x14u);
        const uint32_t unit = u32(0x20u);
        const uint32_t count = u32(0x24u);
        const uint32_t tableOff = u32(0x28u);
        if (tokOff + tokSize > hd6.size() || tableOff + static_cast<size_t>(count) * 8u > hd6.size())
        {
            continue;
        }
        std::vector<std::string_view> tokens;
        tokens.reserve(tokCount);
        size_t pos = tokOff;
        const size_t tokEnd = tokOff + tokSize;
        while (tokens.size() < tokCount && pos < tokEnd)
        {
            const size_t start = pos;
            while (pos < tokEnd && hd6[pos] != 0)
            {
                ++pos;
            }
            tokens.emplace_back(reinterpret_cast<const char *>(hd6.data() + start), pos - start);
            if (pos < tokEnd)
            {
                ++pos;
            }
        }
        std::string name;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint64_t v = u64(tableOff + static_cast<size_t>(i) * 8u);
            size_t p = nameOff + static_cast<size_t>(v & 0x3FFFFu);
            name.clear();
            while (p < hd6.size() && hd6[p] != 0)
            {
                uint32_t token = 0;
                uint32_t shift = 0;
                while (p < hd6.size())
                {
                    const uint8_t b = hd6[p++];
                    token |= static_cast<uint32_t>(b & 0x7Fu) << shift;
                    shift += 7u;
                    if ((b & 0x80u) == 0u)
                    {
                        break;
                    }
                }
                if (token < tokens.size())
                {
                    name.append(tokens[token]);
                }
            }
            for (char &c : name)
            {
                c = (c == '/') ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if ((name.rfind("chara\\mob\\", 0) == 0 && name.size() > 4 &&
                 name.compare(name.size() - 4, 4, ".chr") == 0) ||
                name == "menu\\rc\\menu_static_img_en.pac" ||
                name.rfind("rc\\param\\purpose\\gama_2_", 0) == 0)
            {
                cache.files.emplace(name, ArchiveFile{layer, ((v >> 18u) & 0x3FFFFFu) * 2048ull,
                                                      (v >> 40u) * static_cast<uint64_t>(unit)});
            }
        }
    }
    return cache;
}

const std::vector<RawFusion> &loadRawFusions()
{
    static bool loaded = false;
    static std::vector<RawFusion> cached;
    if (loaded)
    {
        return cached;
    }
    loaded = true;

    const DiscCache &cache = discCache();
    auto it = cache.files.find("rc\\param\\purpose\\gama_2_en.pac");
    if (it == cache.files.end())
    {
        for (auto fit = cache.files.begin(); fit != cache.files.end(); ++fit)
        {
            if (fit->first.rfind("rc\\param\\purpose\\gama_2_", 0) == 0)
            {
                it = fit;
                break;
            }
        }
    }
    if (it == cache.files.end() || it->second.size < 0x60u || it->second.size > 4u * 1024u * 1024u)
    {
        return cached;
    }

    std::ifstream datFile(cache.datPaths[it->second.layer], std::ios::binary);
    if (!datFile)
    {
        return cached;
    }
    datFile.seekg(static_cast<std::streamoff>(it->second.offset), std::ios::beg);
    std::vector<uint8_t> blob(static_cast<size_t>(it->second.size));
    if (!datFile.read(reinterpret_cast<char *>(blob.data()), static_cast<std::streamsize>(blob.size())))
    {
        return cached;
    }

    uint32_t cfgOff = 0u, cfgSize = 0u;
    std::memcpy(&cfgOff, blob.data() + 0x40u, 4);
    std::memcpy(&cfgSize, blob.data() + 0x44u, 4);
    if (cfgOff + cfgSize > blob.size() || cfgSize < 16u)
    {
        return cached;
    }

    const uint8_t *cfg = blob.data() + cfgOff;
    uint32_t ncmd = 0u;
    std::memcpy(&ncmd, cfg, 4);

    struct CombineRec
    {
        uint8_t byteId = 0;
        uint8_t groupKey = 0;
        uint8_t specialPartner = 0;
        uint8_t family = 0;
        int16_t itemId = -1;
    };

    int bonusStep = 0;
    int curChara = -1;
    int curKind = -1;
    uint8_t curKeyA = 0;
    std::vector<CombineRec> recs;
    std::unordered_map<uint8_t, int> recByByteId;
    std::unordered_map<uint16_t, uint8_t> pairResult;
    std::unordered_set<uint64_t> seen;

    const auto flushGroup = [&]() {
        if (curChara < 0 || curChara >= 8 || (curKind != 0 && curKind != 1) || recs.empty() || pairResult.empty())
        {
            recs.clear();
            recByByteId.clear();
            pairResult.clear();
            return;
        }
        for (size_t i = 0; i < recs.size(); ++i)
        {
            for (size_t j = i; j < recs.size(); ++j)
            {
                const uint16_t kAB =
                    static_cast<uint16_t>((static_cast<uint16_t>(recs[i].groupKey) << 8u) | recs[j].groupKey);
                const uint16_t kBA =
                    static_cast<uint16_t>((static_cast<uint16_t>(recs[j].groupKey) << 8u) | recs[i].groupKey);
                auto pit = pairResult.find(kAB);
                if (pit == pairResult.end())
                {
                    pit = pairResult.find(kBA);
                    if (pit == pairResult.end())
                        continue;
                }
                const uint8_t resByteId = pit->second;
                const auto rit = recByByteId.find(resByteId);
                if (rit == recByByteId.end())
                    continue;
                int resIdx = rit->second;
                const bool isBonus =
                    (recs[i].specialPartner == recs[j].byteId) || (recs[j].specialPartner == recs[i].byteId);
                if (isBonus && bonusStep > 0)
                {
                    const uint8_t targetFamily = recs[resIdx].family;
                    const int maxByteId = static_cast<int>(resByteId) + bonusStep;
                    int curByteId = static_cast<int>(resByteId);
                    for (size_t k = 0; k < recs.size(); ++k)
                    {
                        const int b = static_cast<int>(recs[k].byteId);
                        if (b > curByteId && b <= maxByteId && recs[k].family == targetFamily)
                        {
                            resIdx = static_cast<int>(k);
                            curByteId = b;
                            if (curByteId == maxByteId)
                                break;
                        }
                    }
                }
                const int16_t idA = std::min(recs[i].itemId, recs[j].itemId);
                const int16_t idB = std::max(recs[i].itemId, recs[j].itemId);
                const int16_t outId = recs[resIdx].itemId;
                const uint64_t key = (static_cast<uint64_t>(static_cast<uint16_t>(idA)) << 32u) |
                                     (static_cast<uint64_t>(static_cast<uint16_t>(idB)) << 16u) |
                                     static_cast<uint64_t>(static_cast<uint16_t>(outId));
                if (seen.insert(key).second)
                {
                    cached.push_back({idA, idB, outId, curChara, curKind == 1, isBonus});
                }
            }
        }
        recs.clear();
        recByByteId.clear();
        pairResult.clear();
    };

    size_t pos = 16u;
    for (uint32_t ci = 0; ci < ncmd && pos + 4u <= cfgSize; ++ci)
    {
        uint16_t cmd = 0u;
        std::memcpy(&cmd, cfg + pos, 2);
        const uint8_t argc = cfg[pos + 2u];
        const size_t maskBytes = (static_cast<size_t>(argc) + 3u) / 4u;
        size_t hdr = 3u + maskBytes;
        if (hdr % 4u != 0u)
            hdr += 4u - (hdr % 4u);
        const size_t argsBase = pos + hdr;
        if (argsBase + static_cast<size_t>(argc) * 4u > cfgSize)
            break;
        const auto arg = [&](int idx) -> int32_t {
            int32_t v = 0;
            std::memcpy(&v, cfg + argsBase + static_cast<size_t>(idx) * 4u, 4);
            return v;
        };
        pos = argsBase + static_cast<size_t>(argc) * 4u;

        if (cmd == 0x0064u && argc >= 1)
        {
            bonusStep = arg(0);
        }
        else if (cmd == 0x0066u && argc >= 1)
        {
            flushGroup();
            curChara = static_cast<int>(arg(0)) - static_cast<int>(kFirstMainCharaId);
            curKind = -1;
        }
        else if (cmd == 0x0068u && argc >= 1)
        {
            flushGroup();
            curKind = static_cast<int>(arg(0));
        }
        else if (cmd == 0x006Au && argc >= 6)
        {
            CombineRec r;
            r.byteId = static_cast<uint8_t>(arg(0));
            r.itemId = static_cast<int16_t>(arg(1));
            r.specialPartner = static_cast<uint8_t>(arg(3));
            r.family = static_cast<uint8_t>(arg(4) & 0x0F);
            r.groupKey = static_cast<uint8_t>(arg(5));
            recByByteId.emplace(r.byteId, static_cast<int>(recs.size()));
            recs.push_back(r);
        }
        else if (cmd == 0x006Cu && argc >= 1)
        {
            curKeyA = static_cast<uint8_t>(arg(0));
        }
        else if (cmd == 0x006Eu && argc >= 2)
        {
            const uint8_t keyB = static_cast<uint8_t>(arg(0));
            const uint8_t resByteId = static_cast<uint8_t>(arg(1));
            const uint16_t pairKey = static_cast<uint16_t>((static_cast<uint16_t>(curKeyA) << 8u) | keyB);
            pairResult.emplace(pairKey, resByteId);
        }
    }
    flushGroup();
    return cached;
}

struct SoftTex
{
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
};

bool decodeTim2(const uint8_t *buf, size_t len, size_t tim2, SoftTex &out)
{
    if (tim2 + 0x40u > len || std::memcmp(buf + tim2, "TIM2", 4) != 0)
    {
        return false;
    }
    const uint8_t fmt = buf[tim2 + 5u];
    const size_t head = tim2 + (fmt == 0u ? 0x10u : 0x80u);
    if (head + 48u > len)
    {
        return false;
    }
    uint32_t imgSz = 0;
    uint16_t hsize = 0, ccols = 0, w = 0, h = 0;
    std::memcpy(&imgSz, buf + head + 8u, 4);
    std::memcpy(&hsize, buf + head + 12u, 2);
    std::memcpy(&ccols, buf + head + 14u, 2);
    const uint8_t ctype = buf[head + 18u];
    const uint8_t itype = buf[head + 19u];
    std::memcpy(&w, buf + head + 20u, 2);
    std::memcpy(&h, buf + head + 22u, 2);
    if (w == 0u || h == 0u || w > 1024u || h > 1024u)
    {
        return false;
    }
    const size_t imgOff = head + hsize;
    const size_t clutOff = imgOff + imgSz;
    const uint8_t imgFmt = itype & 0x3Fu;
    const uint8_t clutFmt = ctype & 0x3Fu;
    const bool csm2 = (ctype & 0x80u) != 0u;
    const size_t pixCount = static_cast<size_t>(w) * static_cast<size_t>(h);

    auto readClutEntry = [&](int idx, uint8_t rgba[4]) {
        if (clutFmt == 3u)
        {
            const size_t p = clutOff + static_cast<size_t>(idx) * 4u;
            rgba[0] = buf[p + 0u];
            rgba[1] = buf[p + 1u];
            rgba[2] = buf[p + 2u];
            const int a = static_cast<int>(buf[p + 3u]) * 2;
            rgba[3] = static_cast<uint8_t>(a > 255 ? 255 : a);
        }
        else
        {
            uint16_t c16 = 0;
            std::memcpy(&c16, buf + clutOff + static_cast<size_t>(idx) * 2u, 2);
            rgba[0] = static_cast<uint8_t>((c16 & 0x1Fu) * 255u / 31u);
            rgba[1] = static_cast<uint8_t>(((c16 >> 5u) & 0x1Fu) * 255u / 31u);
            rgba[2] = static_cast<uint8_t>(((c16 >> 10u) & 0x1Fu) * 255u / 31u);
            rgba[3] = (c16 & 0x8000u) ? 255u : ((c16 == 0u) ? 0u : 255u);
        }
    };

    out.w = w;
    out.h = h;
    out.rgba.resize(pixCount * 4u);
    if (imgFmt == 5u && ccols == 256u)
    {
        const size_t clutBytes = (clutFmt == 3u) ? 1024u : 512u;
        if (imgOff + pixCount > len || clutOff + clutBytes > len)
        {
            return false;
        }
        uint8_t pal[256][4];
        for (int i = 0; i < 256; ++i)
        {
            const int si = csm2 ? i : ((i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1));
            readClutEntry(si, pal[i]);
        }
        for (size_t i = 0; i < pixCount; ++i)
        {
            std::memcpy(out.rgba.data() + i * 4u, pal[buf[imgOff + i]], 4);
        }
        return true;
    }
    if (imgFmt == 4u && ccols == 16u)
    {
        const size_t clutBytes = (clutFmt == 3u) ? 64u : 32u;
        if (imgOff + (pixCount + 1u) / 2u > len || clutOff + clutBytes > len)
        {
            return false;
        }
        uint8_t pal[16][4];
        for (int i = 0; i < 16; ++i)
        {
            readClutEntry(i, pal[i]);
        }
        for (size_t i = 0; i < pixCount; ++i)
        {
            const uint8_t b = buf[imgOff + (i >> 1u)];
            const uint8_t idx = (i & 1u) ? (b >> 4u) : (b & 0x0Fu);
            std::memcpy(out.rgba.data() + i * 4u, pal[idx], 4);
        }
        return true;
    }
    if (imgFmt == 3u && imgOff + pixCount * 4u <= len)
    {
        for (size_t i = 0; i < pixCount; ++i)
        {
            const size_t p = imgOff + i * 4u;
            out.rgba[i * 4u + 0u] = buf[p + 0u];
            out.rgba[i * 4u + 1u] = buf[p + 1u];
            out.rgba[i * 4u + 2u] = buf[p + 2u];
            const int a = static_cast<int>(buf[p + 3u]) * 2;
            out.rgba[i * 4u + 3u] = static_cast<uint8_t>(a > 255 ? 255 : a);
        }
        return true;
    }
    return false;
}

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Vec2
{
    float u = 0.0f, v = 0.0f;
};
struct Tri
{
    Vec3 v0, v1, v2;
    Vec2 u0, u1, u2;
    const SoftTex *tex = nullptr;
};

} // namespace

IconAtlas renderMonsterIcon(const std::string &modelName)
{
    IconAtlas out;
    if (modelName.empty())
    {
        return out;
    }
    std::string key = "chara\\mob\\" + modelName;
    if (key.size() < 4u || key.compare(key.size() - 4u, 4u, ".chr") != 0)
    {
        key += ".chr";
    }
    DiscCache &cache = discCache();
    const auto fit = cache.files.find(key);
    if (fit == cache.files.end() || fit->second.size < 0x50u || fit->second.size > 16u * 1024u * 1024u)
    {
        return out;
    }
    std::ifstream datFile(cache.datPaths[fit->second.layer], std::ios::binary);
    if (!datFile)
    {
        return out;
    }
    datFile.seekg(static_cast<std::streamoff>(fit->second.offset), std::ios::beg);
    std::vector<uint8_t> chr(static_cast<size_t>(fit->second.size));
    if (!datFile.read(reinterpret_cast<char *>(chr.data()), static_cast<std::streamsize>(chr.size())))
    {
        return out;
    }

    // Unpack .pac to locate main .mds and .img.
    const uint8_t *mds = nullptr;
    size_t mdsLen = 0;
    const uint8_t *img = nullptr;
    size_t imgLen = 0;
    size_t off = 0;
    while (off + 0x50u <= chr.size())
    {
        uint32_t hdrSz = 0, dataSz = 0, stride = 0;
        std::memcpy(&hdrSz, chr.data() + off + 0x40u, 4);
        std::memcpy(&dataSz, chr.data() + off + 0x44u, 4);
        std::memcpy(&stride, chr.data() + off + 0x48u, 4);
        if (hdrSz == 0xFFFFFFFFu || dataSz == 0xFFFFFFFFu || off + hdrSz + dataSz > chr.size())
        {
            break;
        }
        size_t nlen = 0;
        while (nlen < 0x40u && chr[off + nlen] != 0)
        {
            ++nlen;
        }
        std::string ename(reinterpret_cast<const char *>(chr.data() + off), nlen);
        for (char &c : ename)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ename.size() > 4u && ename.compare(ename.size() - 4u, 4u, ".mds") == 0 &&
            !(ename.size() > 5u && ename.compare(ename.size() - 5u, 5u, "s.mds") == 0) &&
            ename.find('-') == std::string::npos && dataSz > mdsLen)
        {
            mds = chr.data() + off + hdrSz;
            mdsLen = dataSz;
        }
        else if (ename.size() > 4u && ename.compare(ename.size() - 4u, 4u, ".img") == 0 && dataSz > imgLen)
        {
            img = chr.data() + off + hdrSz;
            imgLen = dataSz;
        }
        if (stride == 0u)
        {
            break;
        }
        off += stride;
    }
    if (!mds || mdsLen < 0x50u || std::memcmp(mds, " MDS", 4) != 0)
    {
        return out;
    }

    // Decode IM3 textures.
    std::unordered_map<std::string, SoftTex> textures;
    if (img && imgLen >= 0x10u && std::memcmp(img, "IM3\0", 4) == 0)
    {
        uint32_t texCnt = 0;
        std::memcpy(&texCnt, img + 8u, 4);
        if (texCnt > 64u)
        {
            texCnt = 64u;
        }
        for (uint32_t i = 0; i < texCnt; ++i)
        {
            const size_t eo = 0x10u + static_cast<size_t>(i) * 0x40u;
            if (eo + 0x40u > imgLen)
            {
                break;
            }
            size_t tlen = 0;
            while (tlen < 0x20u && img[eo + tlen] != 0)
            {
                ++tlen;
            }
            std::string tname(reinterpret_cast<const char *>(img + eo), tlen);
            for (char &c : tname)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            uint32_t toff = 0;
            std::memcpy(&toff, img + eo + 0x24u, 4);
            SoftTex tex;
            if (decodeTim2(img, imgLen, toff, tex))
            {
                textures.emplace(std::move(tname), std::move(tex));
            }
        }
    }

    const auto rdU32 = [&](const uint8_t *p, size_t o) {
        uint32_t v = 0;
        std::memcpy(&v, p + o, 4);
        return v;
    };
    const auto rdU16 = [&](const uint8_t *p, size_t o) {
        uint16_t v = 0;
        std::memcpy(&v, p + o, 2);
        return v;
    };
    const auto rdS16 = [&](const uint8_t *p, size_t o) {
        int16_t v = 0;
        std::memcpy(&v, p + o, 2);
        return v;
    };
    const auto rdF32 = [&](const uint8_t *p, size_t o) {
        float v = 0.0f;
        std::memcpy(&v, p + o, 4);
        return v;
    };

    const uint32_t mdtCnt = rdU32(mds, 0x20u);
    const uint32_t mdtOff = rdU32(mds, 0x24u);
    const uint32_t matCnt = rdU32(mds, 0x2Cu);
    const uint32_t matSz = rdU32(mds, 0x30u);
    const uint32_t matOff = rdU32(mds, 0x34u);
    const uint32_t strtab = rdU32(mds, 0x38u);

    std::vector<const SoftTex *> matTex(matCnt, nullptr);
    for (uint32_t i = 0; i < matCnt && matSz >= 0x38u; ++i)
    {
        const size_t mo = static_cast<size_t>(matOff) + static_cast<size_t>(i) * matSz;
        if (mo + 0x38u > mdsLen)
        {
            break;
        }
        const uint32_t rel = rdU32(mds, mo + 0x34u);
        if (rel != 0u && strtab + rel < mdsLen)
        {
            size_t end = strtab + rel;
            while (end < mdsLen && mds[end] != 0)
            {
                ++end;
            }
            std::string tname(reinterpret_cast<const char *>(mds + strtab + rel), end - (strtab + rel));
            for (char &c : tname)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (const auto tit = textures.find(tname); tit != textures.end())
            {
                matTex[i] = &tit->second;
            }
        }
    }

    // Parse MDTs and rotate vertices into 3/4 view.
    const float yaw = 0.4363323f;   // 25 deg
    const float pitch = -0.2094395f; // -12 deg
    const float cyaw = std::cos(yaw), syaw = std::sin(yaw);
    const float cpit = std::cos(pitch), spit = std::sin(pitch);
    auto rotV = [&](Vec3 v) -> Vec3 {
        const float rx = v.x * cyaw + v.z * syaw;
        const float rz = -v.x * syaw + v.z * cyaw;
        const float ry = v.y * cpit - rz * spit;
        const float rz2 = v.y * spit + rz * cpit;
        return {rx, ry, rz2};
    };

    std::vector<Tri> tris;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    size_t cur = mdtOff;
    for (uint32_t m = 0; m < mdtCnt && cur + 0x90u <= mdsLen; ++m)
    {
        if (std::memcmp(mds + cur, " MDT", 4) != 0)
        {
            break;
        }
        const uint32_t hdrSz = rdU32(mds, cur + 8u);
        const uint32_t blkSz = rdU32(mds, cur + 12u);
        if (blkSz == 0u || cur + hdrSz + 0x30u > mdsLen)
        {
            break;
        }
        const float sx = rdF32(mds, cur + 0x20u) / 32768.0f;
        const float sy = rdF32(mds, cur + 0x24u) / 32768.0f;
        const float sz = rdF32(mds, cur + 0x28u) / 32768.0f;
        const float ox = rdF32(mds, cur + 0x30u);
        const float oy = rdF32(mds, cur + 0x34u);
        const float oz = rdF32(mds, cur + 0x38u);
        const float us = rdF32(mds, cur + 0x50u) / 32768.0f;
        const float vs = rdF32(mds, cur + 0x54u) / 32768.0f;
        const uint32_t primCnt = rdU32(mds, cur + 0x70u);
        const uint32_t primOff = rdU32(mds, cur + 0x74u);

        const size_t subHdr = cur + hdrSz;
        const uint32_t vCnt = rdU32(mds, subHdr + 0x00u);
        const uint32_t vOff = rdU32(mds, subHdr + 0x04u);
        const uint32_t uvCnt = rdU32(mds, subHdr + 0x10u);
        const uint32_t uvOff = rdU32(mds, subHdr + 0x14u);
        if (vCnt == 0u || vCnt > 32768u || subHdr + vOff + static_cast<size_t>(vCnt) * 6u > mdsLen)
        {
            cur += blkSz;
            continue;
        }
        std::vector<Vec3> verts(vCnt);
        for (uint32_t vi = 0; vi < vCnt; ++vi)
        {
            const size_t vo = subHdr + vOff + static_cast<size_t>(vi) * 6u;
            verts[vi] = rotV({rdS16(mds, vo) * sx + ox, rdS16(mds, vo + 2u) * sy + oy, rdS16(mds, vo + 4u) * sz + oz});
        }
        std::vector<Vec2> uvs;
        if (uvCnt > 0u && uvCnt <= 32768u && subHdr + uvOff + static_cast<size_t>(uvCnt) * 4u <= mdsLen)
        {
            uvs.resize(uvCnt);
            for (uint32_t ui = 0; ui < uvCnt; ++ui)
            {
                const size_t uo = subHdr + uvOff + static_cast<size_t>(ui) * 4u;
                uvs[ui] = {rdU16(mds, uo) * us, rdU16(mds, uo + 2u) * vs};
            }
        }

        size_t po = cur + primOff;
        for (uint32_t p = 0; p < primCnt && po + 0x30u <= mdsLen; ++p)
        {
            const uint32_t pSz = rdU32(mds, po + 0x00u);
            const uint16_t pType = rdU16(mds, po + 0x04u);
            const uint16_t idxCnt = rdU16(mds, po + 0x06u);
            const uint16_t matId = rdU16(mds, po + 0x0Au);
            const uint32_t pIdxOff = rdU32(mds, po + 0x10u);
            const uint32_t uvIdxOff = rdU32(mds, po + 0x24u);
            const SoftTex *tex = (matId < matTex.size()) ? matTex[matId] : nullptr;
            const bool hasPos = pIdxOff != 0u && po + pIdxOff + static_cast<size_t>(idxCnt) * 2u <= mdsLen;
            const bool hasUv =
                !uvs.empty() && uvIdxOff != 0u && po + uvIdxOff + static_cast<size_t>(idxCnt) * 2u <= mdsLen;
            if (hasPos && idxCnt >= 3u)
            {
                auto emitTri = [&](uint16_t i0, uint16_t i1, uint16_t i2, uint32_t k0, uint32_t k1, uint32_t k2) {
                    if (i0 == i1 || i1 == i2 || i0 == i2 || i0 >= vCnt || i1 >= vCnt || i2 >= vCnt)
                    {
                        return;
                    }
                    Vec2 t0{}, t1{}, t2{};
                    if (hasUv)
                    {
                        const uint16_t u0 = rdU16(mds, po + uvIdxOff + k0 * 2u);
                        const uint16_t u1 = rdU16(mds, po + uvIdxOff + k1 * 2u);
                        const uint16_t u2 = rdU16(mds, po + uvIdxOff + k2 * 2u);
                        if (u0 < uvs.size()) t0 = uvs[u0];
                        if (u1 < uvs.size()) t1 = uvs[u1];
                        if (u2 < uvs.size()) t2 = uvs[u2];
                    }
                    const Vec3 &a = verts[i0], &b = verts[i1], &c = verts[i2];
                    minX = std::min({minX, a.x, b.x, c.x});
                    maxX = std::max({maxX, a.x, b.x, c.x});
                    minY = std::min({minY, a.y, b.y, c.y});
                    maxY = std::max({maxY, a.y, b.y, c.y});
                    tris.push_back({a, b, c, t0, t1, t2, tex});
                };
                if (pType == 4u)
                {
                    for (uint32_t k = 0; k + 2u < idxCnt; ++k)
                    {
                        const uint16_t i0 = rdU16(mds, po + pIdxOff + k * 2u);
                        const uint16_t i1 = rdU16(mds, po + pIdxOff + (k + 1u) * 2u);
                        const uint16_t i2 = rdU16(mds, po + pIdxOff + (k + 2u) * 2u);
                        if (k & 1u)
                            emitTri(i0, i2, i1, k, k + 2u, k + 1u);
                        else
                            emitTri(i0, i1, i2, k, k + 1u, k + 2u);
                    }
                }
                else if (pType == 3u)
                {
                    for (uint32_t k = 0; k + 2u < idxCnt; k += 3u)
                    {
                        emitTri(rdU16(mds, po + pIdxOff + k * 2u), rdU16(mds, po + pIdxOff + (k + 1u) * 2u),
                                rdU16(mds, po + pIdxOff + (k + 2u) * 2u), k, k + 1u, k + 2u);
                    }
                }
            }
            if (pSz == 0u)
            {
                break;
            }
            po += pSz;
        }
        cur += blkSz;
    }
    if (tris.empty())
    {
        return out;
    }

    // Software rasterize at 64x64 with transparent background, then 2x2 downsample to 32x32.
    constexpr int kW = 64;
    constexpr int kH = 64;
    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    const float span = std::max({maxX - minX, maxY - minY, 1e-3f});
    const float sc = (kW * 0.88f) / span;

    std::vector<uint8_t> fb(kW * kH * 4u, 0u);
    std::vector<float> zb(kW * kH, -1e9f);

    for (const Tri &tr : tris)
    {
        const float p0x = kW * 0.5f + (tr.v0.x - cx) * sc;
        const float p0y = kH * 0.5f - (tr.v0.y - cy) * sc;
        const float p1x = kW * 0.5f + (tr.v1.x - cx) * sc;
        const float p1y = kH * 0.5f - (tr.v1.y - cy) * sc;
        const float p2x = kW * 0.5f + (tr.v2.x - cx) * sc;
        const float p2y = kH * 0.5f - (tr.v2.y - cy) * sc;

        const float denom = (p1y - p2y) * (p0x - p2x) + (p2x - p1x) * (p0y - p2y);
        if (std::fabs(denom) < 1e-4f)
        {
            continue;
        }
        const float invD = 1.0f / denom;
        const float ax = tr.v1.x - tr.v0.x, ay = tr.v1.y - tr.v0.y, az = tr.v1.z - tr.v0.z;
        const float bx = tr.v2.x - tr.v0.x, by = tr.v2.y - tr.v0.y, bz = tr.v2.z - tr.v0.z;
        const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f;
        const float ndotl = std::max(0.0f, (nx * 0.25f + ny * 0.55f + nz * 0.80f) / nl);
        const float light = 1.15f + 0.65f * ndotl;

        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({p0x, p1x, p2x}))));
        const int x1 = std::min(kW - 1, static_cast<int>(std::ceil(std::max({p0x, p1x, p2x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({p0y, p1y, p2y}))));
        const int y1 = std::min(kH - 1, static_cast<int>(std::ceil(std::max({p0y, p1y, p2y}))));

        for (int py = y0; py <= y1; ++py)
        {
            const float fy = static_cast<float>(py) + 0.5f;
            for (int px = x0; px <= x1; ++px)
            {
                const float fx = static_cast<float>(px) + 0.5f;
                const float w0 = ((p1y - p2y) * (fx - p2x) + (p2x - p1x) * (fy - p2y)) * invD;
                const float w1 = ((p2y - p0y) * (fx - p2x) + (p0x - p2x) * (fy - p2y)) * invD;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                {
                    continue;
                }
                const float z = w0 * tr.v0.z + w1 * tr.v1.z + w2 * tr.v2.z;
                const int idx = py * kW + px;
                if (z <= zb[idx])
                {
                    continue;
                }
                int r = 185, g = 190, b = 200;
                if (tr.tex && tr.tex->w > 0 && tr.tex->h > 0)
                {
                    const float u = w0 * tr.u0.u + w1 * tr.u1.u + w2 * tr.u2.u;
                    const float v = w0 * tr.u0.v + w1 * tr.u1.v + w2 * tr.u2.v;
                    int tu = static_cast<int>(std::floor(u * static_cast<float>(tr.tex->w))) % tr.tex->w;
                    int tv = static_cast<int>(std::floor(v * static_cast<float>(tr.tex->h))) % tr.tex->h;
                    if (tu < 0) tu += tr.tex->w;
                    if (tv < 0) tv += tr.tex->h;
                    const size_t tp = (static_cast<size_t>(tv) * static_cast<size_t>(tr.tex->w) + static_cast<size_t>(tu)) * 4u;
                    if (tr.tex->rgba[tp + 3u] < 64u)
                    {
                        continue;
                    }
                    r = tr.tex->rgba[tp + 0u];
                    g = tr.tex->rgba[tp + 1u];
                    b = tr.tex->rgba[tp + 2u];
                }
                zb[idx] = z;
                fb[idx * 4u + 0u] = static_cast<uint8_t>(std::min(255, static_cast<int>(r * light)));
                fb[idx * 4u + 1u] = static_cast<uint8_t>(std::min(255, static_cast<int>(g * light)));
                fb[idx * 4u + 2u] = static_cast<uint8_t>(std::min(255, static_cast<int>(b * light)));
                fb[idx * 4u + 3u] = 255u;
            }
        }
    }

    // Tight-crop to rendered pixels and resample into 32x32 so wide/thin models
    // fill the icon cleanly.
    int bx0 = kW, by0 = kH, bx1 = -1, by1 = -1;
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            if (fb[(static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4u + 3u] > 0u)
            {
                bx0 = std::min(bx0, x);
                by0 = std::min(by0, y);
                bx1 = std::max(bx1, x);
                by1 = std::max(by1, y);
            }
        }
    }
    if (bx1 < bx0 || by1 < by0)
    {
        return out;
    }
    const int bw = bx1 - bx0 + 1;
    const int bh = by1 - by0 + 1;
    const int side = std::max(bw, bh);
    const float padSide = static_cast<float>(side) * 1.08f;
    const float bcx = (bx0 + bx1 + 1) * 0.5f;
    const float bcy = (by0 + by1 + 1) * 0.5f;

    constexpr int kOutW = 32;
    constexpr int kOutH = 32;
    out.width = kOutW;
    out.height = kOutH;
    out.rgba.resize(kOutW * kOutH * 4u, 0u);
    for (int y = 0; y < kOutH; ++y)
    {
        for (int x = 0; x < kOutW; ++x)
        {
            int sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
            for (int sy = 0; sy < 2; ++sy)
            {
                for (int sx = 0; sx < 2; ++sx)
                {
                    const float u = (static_cast<float>(x) + (sx + 0.5f) * 0.5f) / static_cast<float>(kOutW) - 0.5f;
                    const float v = (static_cast<float>(y) + (sy + 0.5f) * 0.5f) / static_cast<float>(kOutH) - 0.5f;
                    const int px = static_cast<int>(std::floor(bcx + u * padSide));
                    const int py = static_cast<int>(std::floor(bcy + v * padSide));
                    if (px >= 0 && px < kW && py >= 0 && py < kH)
                    {
                        const size_t sp = (static_cast<size_t>(py) * kW + static_cast<size_t>(px)) * 4u;
                        const int a = fb[sp + 3u];
                        sa += a;
                        if (a > 0)
                        {
                            sr += fb[sp + 0u];
                            sg += fb[sp + 1u];
                            sb += fb[sp + 2u];
                            ++cnt;
                        }
                    }
                }
            }
            const size_t dp = (static_cast<size_t>(y) * kOutW + static_cast<size_t>(x)) * 4u;
            if (cnt > 0)
            {
                out.rgba[dp + 0u] = static_cast<uint8_t>(sr / cnt);
                out.rgba[dp + 1u] = static_cast<uint8_t>(sg / cnt);
                out.rgba[dp + 2u] = static_cast<uint8_t>(sb / cnt);
                out.rgba[dp + 3u] = static_cast<uint8_t>(sa / 4);
            }
        }
    }
    return out;
}

IconAtlas loadCharaPortraits()
{
    IconAtlas out;
    DiscCache &cache = discCache();
    const auto fit = cache.files.find("menu\\rc\\menu_static_img_en.pac");
    if (fit == cache.files.end() || fit->second.size < 0x100u || fit->second.size > 4u * 1024u * 1024u)
    {
        return out;
    }
    std::ifstream datFile(cache.datPaths[fit->second.layer], std::ios::binary);
    if (!datFile)
    {
        return out;
    }
    datFile.seekg(static_cast<std::streamoff>(fit->second.offset), std::ios::beg);
    std::vector<uint8_t> pac(static_cast<size_t>(fit->second.size));
    if (!datFile.read(reinterpret_cast<char *>(pac.data()), static_cast<std::streamsize>(pac.size())))
    {
        return out;
    }

    // Locate menu_pt_b_EN.img inside the pac.
    const uint8_t *img = nullptr;
    size_t imgLen = 0;
    size_t off = 0;
    while (off + 0x50u <= pac.size())
    {
        uint32_t hdrSz = 0, dataSz = 0, stride = 0;
        std::memcpy(&hdrSz, pac.data() + off + 0x40u, 4);
        std::memcpy(&dataSz, pac.data() + off + 0x44u, 4);
        std::memcpy(&stride, pac.data() + off + 0x48u, 4);
        if (hdrSz == 0xFFFFFFFFu || dataSz == 0xFFFFFFFFu || off + hdrSz + dataSz > pac.size())
        {
            break;
        }
        size_t nlen = 0;
        while (nlen < 0x40u && pac[off + nlen] != 0)
        {
            ++nlen;
        }
        std::string ename(reinterpret_cast<const char *>(pac.data() + off), nlen);
        for (char &c : ename)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ename == "menu_pt_b_en.img")
        {
            img = pac.data() + off + hdrSz;
            imgLen = dataSz;
            break;
        }
        if (stride == 0u)
        {
            break;
        }
        off += stride;
    }
    if (!img || imgLen < 0x60u || std::memcmp(img, "IM3\0", 4) != 0)
    {
        return out;
    }

    // First entry (Pty_01): 512x64 PSMT8-swizzled TIM2 with 256-color CSM1 CLUT.
    uint32_t toff = 0;
    std::memcpy(&toff, img + 0x10u + 0x24u, 4);
    if (toff + 0x40u > imgLen || std::memcmp(img + toff, "TIM2", 4) != 0)
    {
        return out;
    }
    const size_t head = toff + (img[toff + 5u] == 0u ? 0x10u : 0x80u);
    if (head + 48u > imgLen)
    {
        return out;
    }
    uint32_t imgSz = 0;
    uint16_t hsize = 0, ccols = 0, w = 0, h = 0;
    std::memcpy(&imgSz, img + head + 8u, 4);
    std::memcpy(&hsize, img + head + 12u, 2);
    std::memcpy(&ccols, img + head + 14u, 2);
    const uint8_t ctype = img[head + 18u];
    const uint8_t itype = img[head + 19u];
    std::memcpy(&w, img + head + 20u, 2);
    std::memcpy(&h, img + head + 22u, 2);
    const size_t imgOff = head + hsize;
    const size_t clutOff = imgOff + imgSz;
    if ((itype & 0x3Fu) != 5u || (ctype & 0x3Fu) != 3u || ccols != 256u || w == 0u || h == 0u ||
        imgOff + static_cast<size_t>(w) * static_cast<size_t>(h) > imgLen || clutOff + 1024u > imgLen)
    {
        return out;
    }

    uint8_t pal[256][4];
    for (int i = 0; i < 256; ++i)
    {
        const int si = (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        const size_t p = clutOff + static_cast<size_t>(si) * 4u;
        pal[i][0] = img[p + 0u];
        pal[i][1] = img[p + 1u];
        pal[i][2] = img[p + 2u];
        const int a = static_cast<int>(img[p + 3u]) * 2;
        pal[i][3] = static_cast<uint8_t>(a > 255 ? 255 : a);
    }

    out.width = w;
    out.height = h;
    out.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int block = (y & ~0xF) * w + (x & ~0xF) * 2;
            const int swap = (((y + 2) >> 2) & 1) * 4;
            const int row = (((y & ~3) >> 1) + (y & 1)) & 7;
            const int col = row * w * 2 + ((x + swap) & 7) * 4;
            const int byte = ((y >> 1) & 1) + ((x >> 2) & 2);
            const uint8_t idx = img[imgOff + static_cast<size_t>(block + col + byte)];
            std::memcpy(out.rgba.data() + (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4u, pal[idx], 4);
        }
    }
    return out;
}

} // namespace rg::bestiary
