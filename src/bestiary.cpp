#include "bestiary.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

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
constexpr uint32_t kItemNameOffset = 0x0Cu;
constexpr uint32_t kItemIconOffset = 0x2Cu;

constexpr uint32_t kMonsterEntrySize = 0xA4u;
constexpr uint32_t kMonsterIdOffset = 0x00u;
constexpr uint32_t kMonsterNameOffset = 0x04u;
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
                item.sources.push_back({monster.id, monster.name, drop.ratePercent, monster.defeated});
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

} // namespace rg::bestiary
