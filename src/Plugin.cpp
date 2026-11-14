#include <RE/Skyrim.h>

#include <SKSE/SKSE.h>



#include "EnchantmentProbe.h"

#include "NativeRead.h"

#include "HitPolicy.h"

#include "HardcoreState.h"

#include "MagickaReset.h"

#include "Panel.h"
#include "EquipmentClaims.h"
#include "EquipmentLookup.h"

#include "ProgressionControl.h"

#include "MeleeGrowth.h"

#include "GrowthAbilityData.h"

#include "EngineEffectsNative.h"

#include "DeveloperGate.h"



#include <algorithm>

#include <cmath>

#include <fstream>

#include <thread>

#include <sstream>

#include <iomanip>

#include <atomic>

#include <filesystem>



std::string tdsFileSha(const wchar_t* filename);



namespace {

using namespace tds;

using namespace std::chrono_literals;



constexpr auto esp = "TULL Dibella says.esp";

constexpr REL::Version pluginVersion{1, 0, 4, 0};



constexpr bool acceptsRuntime(REL::Version version)

{

    return tds::runtime::accepts({version[0],version[1],version[2],version[3]});

}



static_assert(acceptsRuntime({1, 5, 97, 0}));

static_assert(!acceptsRuntime({1, 5, 96, 0}));

static_assert(acceptsRuntime({1, 6, 640, 0}));

static_assert(acceptsRuntime({1, 6, 1170, 0}));

static_assert(!acceptsRuntime({1, 7, 0, 0}));

static_assert(!acceptsRuntime({1, 4, 15, 0}));  // VR

constexpr std::array names{"WeaponGrowth", "WeaponDecay", "AccessoryGrowth", "AccessoryDecay"};

constexpr std::array<Group, 4> allGroups{Group::WeaponGrowth, Group::WeaponDecay,

                                       Group::AccessoryGrowth, Group::AccessoryDecay};

using AbilityTable = std::array<std::array<RE::SpellItem*, 41>, 4>;



std::ofstream logFile;

std::unique_ptr<StatLogger> logger;

EnchantmentRecords enchantments{};

AbilityTable abilities{};

std::array<RE::SpellItem*, 4> owned{};

std::array<RE::SpellItem*, tds::growth_forms::effects.size()> growthAbilities{};

static_assert(tds::growth_forms::effects.size() == 5 + 3 * (tds::progression::corruptionThresholds.size()-1)

    + tds::progression::positiveSteps.size() + 2*tds::progression::negativeSteps.size()

    + 3*(tds::progression::corruptionThresholds.size()-1) + 8*tds::progression::negativeSteps.size() + 2);

std::optional<int> count;

std::jthread reader;

std::atomic_bool expiryArmed{};

std::atomic_bool expiryPending{};

std::string providerSha;

std::uint64_t session{};

bool formsReady{};

using HitCall = void(RE::ScriptEventSourceHolder*, RE::NiPointer<RE::TESObjectREFR>&,

                     RE::NiPointer<RE::TESObjectREFR>&, RE::FormID, RE::FormID, RE::HitData&);

REL::Relocation<HitCall*> originalHit;

using AeHitCall = void(RE::AIProcess*, RE::HitData&);
REL::Relocation<AeHitCall*> originalAeHit;



bool sessionReady{};

bool readActive{};

bool endQueued{};

std::optional<Event> queuedUiRead;

bool statsGap{};

HardcoreState hardcore;

std::optional<bool> savedHardcore;
std::optional<tds::SaveSettings> savedSettings;
std::atomic_bool auraEnabled{};

const char* hardcoreResult = "NONE";

std::optional<float> hardcoreBefore;

std::optional<float> hardcoreAfter;

std::int64_t lastValidMs{};

constexpr std::uint32_t hardcoreRecord = 0x56534454;

constexpr std::uint32_t progressionRecord = 0x31504B58;

constexpr std::uint32_t recentBlocksRecord = 0x31525854;

using RecentBlockPayload = std::array<std::int64_t,7>;

namespace prog = tds::progression;

prog::ProgressionControl progression;

std::atomic<prog::MeleeGrowth> liveMelee{};

static_assert(decltype(liveMelee)::is_always_lock_free);

std::optional<prog::ProgressionState> savedProgression;

bool progressionRecordInvalid{};

using ProgressionHeader = std::array<std::int64_t, 4>;

using ProgressionMetadata = std::array<std::int32_t, prog::CounterCount + 3>;

constexpr std::uint32_t progressionVersion = 3;

static_assert(sizeof(prog::SatisfactionBlock) == 16);

constexpr std::uint32_t maxProgressionBlocks = 100'000;

constexpr std::uint32_t serializationId = 0x31534454;
std::uint32_t gearClaims{}, mockGearClaims{};
std::optional<std::uint32_t> savedGearClaims;
unsigned gearGrantResult{},gearGrantGroup{},gearGrantSerial{};



struct DynamicState {

    const char* state;

    const char* compatibility;

    std::optional<float> growth;

    bool allowReset;

    RE::TESGlobal* growthForm{};

    RE::TESGlobal* xpForm{};

    std::optional<float> xp;

};



bool dynamicFilesVerified{};

bool dynamicVisible{};

bool dynamicProbeError{};



void inspectDynamicFiles()

{

    std::error_code error;

    dynamicVisible = std::filesystem::exists(L"Data/DM&SG.esp", error);

    dynamicProbeError = bool(error);

    dynamicFilesVerified =

        tdsFileSha(L"Data/DM&SG.esp") == "25a1297e3addb605b23c5569fb8b8cd7d957f82ee3b669505c797f3bad979f35" &&

        tdsFileSha(L"Data/Scripts/DynamicMagicka.pex") == "0fa6c147dffb2e183297adf0221c13709f7e34f08ff87f09704b29af84b073f1" &&

        tdsFileSha(L"Data/Scripts/DynamicMagickaConfig.pex") == "edcc9873002b41962967b254074b37e0729b8fe9b86f8b633ad18ad113a9f196";

}



DynamicState dynamicState(RE::PlayerCharacter* player)

{

    auto* handler = RE::TESDataHandler::GetSingleton();

    if (!handler || !player) return {"UNKNOWN", "BLOCKED", {}, false};

    if (dynamicProbeError) return {"VFS_PROBE_ERROR", "BLOCKED", {}, false};

    constexpr auto plugin = "DM&SG.esp";

    if (!handler->LookupLoadedModByName(plugin) && !handler->LookupLoadedLightModByName(plugin)) {

        return dynamicVisible ? DynamicState{"INSTALLED_NOT_LOADED", "NO_ACTIVE_PLUGIN", {}, true}

                              : DynamicState{"NOT_VISIBLE_IN_VFS", "NO_ACTIVE_PLUGIN", {}, true};

    }

    if (!dynamicFilesVerified) return {"LOADED_UNKNOWN_VERSION", "BLOCKED", {}, false};

    auto* ability = handler->LookupForm<RE::SpellItem>(0x182D, plugin);

    auto* effect = handler->LookupForm<RE::EffectSetting>(0x182C, plugin);

    auto* growth = handler->LookupForm<RE::TESGlobal>(0x182A, plugin);

    auto* xp = handler->LookupForm<RE::TESGlobal>(0x0D65, plugin);

    if (!ability || !effect || !growth || !xp || !std::isfinite(growth->value) ||

        !std::isfinite(xp->value) || growth->value < 0 || xp->value < 0 ||

        !growth->GetFormEditorID() || !xp->GetFormEditorID() ||

        std::string_view(growth->GetFormEditorID()) != "DynamicManaGrowth" ||

        std::string_view(xp->GetFormEditorID()) != "DynamicManaXP") {

        return {"FORM_OR_VALUE_ERROR", "BLOCKED", {}, false};

    }

    const bool owns = player->HasSpell(ability);

    const bool active = player->AsMagicTarget()->HasMagicEffect(effect);

    if (owns != active) {

        return {"ABILITY_EFFECT_MISMATCH", "BLOCKED", growth->value, false, nullptr, nullptr, xp->value};

    }

    const auto* state = active ? "ACTIVE" : "INACTIVE";

    auto* ui = RE::UI::GetSingleton();

    if (!ui || ui->IsMenuOpen("Journal Menu") || !player->Is3DLoaded() || player->IsDead()) {

        return {state, "BLOCKED_MENU_OR_PLAYER_NOT_READY", growth->value, false, nullptr, nullptr, xp->value};

    }

    if (active) {

        using Source = RE::MagicSystem::CastingSource;

        using State = RE::MagicCaster::State;

        for (const auto source : {Source::kLeftHand, Source::kRightHand, Source::kOther, Source::kInstant}) {

            const auto* caster = player->GetMagicCaster(source);

            if (caster && caster->state.get() != State::kNone && caster->state.get() != State::kReady) {

                return {state, "BLOCKED_CAST_IN_PROGRESS", growth->value, false, nullptr, nullptr, xp->value};

            }

        }

    }

    return {state, "VERIFIED_LEDGER", growth->value, true, growth, xp, xp->value};

}



void saveProgression(SKSE::SerializationInterface* serial)

{

    if (progressionRecordInvalid || !sessionReady) return;

    if (const auto* calendar = RE::Calendar::GetSingleton()) {

        progression.expire(calendar->GetHoursPassed());

    }

    const auto state = progression.stateForSave();

    if (!state) return;

    const auto& blocks = state->satisfaction.records();

    if (blocks.size() > maxProgressionBlocks) {

        logFile << "TDS_PROGRESSION_SERIAL status=BLOCK_LIMIT\n";

        return;

    }

    std::int64_t mask{};

    for (unsigned i = 0; i < state->nodes.purchased.size(); ++i) {

        if (state->nodes.purchased[i]) mask |= std::int64_t{1} << i;

    }

    const ProgressionHeader header{state->nodes.enabled, mask, state->corruption, state->soloProgress};

    ProgressionMetadata metadata{};

    for (unsigned i=0; i<prog::CounterCount; ++i) metadata[i] = state->lastValid ? (*state->lastValid)[i] : -1;

    for (unsigned i=0; i<state->history.size(); ++i) metadata[prog::CounterCount+i] = static_cast<int>(state->history[i]);

    if (!serial->OpenRecord(progressionRecord, progressionVersion) || !serial->WriteRecordData(header) ||

        !serial->WriteRecordData(metadata) ||

        (!blocks.empty() && !serial->WriteRecordData(blocks.data(),

            static_cast<std::uint32_t>(blocks.size() * sizeof(blocks.front()))))) {

        logFile << "TDS_PROGRESSION_SERIAL status=SAVE_FAILED\n";

        return;

    }

    RecentBlockPayload recent{};

    for (const auto& award : state->recentBlocks) {

        if (!award) break;

        const auto index=static_cast<unsigned>(recent[0]++);

        recent[1+index*2]=award->satisfaction;

        recent[2+index*2]=award->corruption;

    }

    if (recent[0] && !serial->WriteRecord(recentBlocksRecord,1,recent)) {

        logFile << "TDS_RECENT_BLOCKS status=SAVE_FAILED\n";

    }

}



std::optional<prog::ProgressionState> readProgression(SKSE::SerializationInterface* serial,

                                                      std::uint32_t version, std::uint32_t length)

{

    if (version != 1 && version != 2 && version != progressionVersion) return std::nullopt;

    const auto headerSize = sizeof(ProgressionHeader) + (version >= 2 ? sizeof(ProgressionMetadata) : 0);

    if (length < headerSize) return std::nullopt;

    const auto payload = length - headerSize;

    if (payload % sizeof(prog::SatisfactionBlock) != 0 ||

        payload / sizeof(prog::SatisfactionBlock) > maxProgressionBlocks) return std::nullopt;

    ProgressionHeader header{};

    if (serial->ReadRecordData(header) != sizeof(header)) return std::nullopt;

    const auto [enabled, mask, corruption, solo] = header;

    prog::ProgressionState state;

    if (version >= 2) {

        ProgressionMetadata metadata{};

        if (serial->ReadRecordData(metadata) != sizeof(metadata)) return std::nullopt;

        prog::Counters baseline{};

        std::copy_n(metadata.begin(),baseline.size(),baseline.begin());

        if (std::all_of(baseline.begin(),baseline.end(),[](int count){return count>=0;})) {

            state.lastValid = baseline;

        } else if (!std::all_of(baseline.begin(),baseline.end(),[](int count){return count==-1;})) {

            return std::nullopt;

        }

        for (unsigned i=0; i<state.history.size(); ++i) {

            const int event=metadata[prog::CounterCount+i];

            if (event<0 || event>static_cast<int>(prog::Incident::GxpRecovered)) return std::nullopt;

            state.history[i]=static_cast<prog::Incident>(event);

        }

    }

    if ((enabled != 0 && enabled != 1) || mask < 0 ||

        mask >= (std::int64_t{1} << state.nodes.purchased.size()) ||

        corruption < 0 || solo < 0 || solo > 12) return std::nullopt;

    state.nodes.enabled = enabled != 0;

    state.corruption = corruption;

    state.soloProgress = static_cast<int>(solo);

    if(version<3) logFile << "TDS_CR2_MIGRATION prior_version=" << version

        << " s_ramp_preserved=" << solo << " historical_nonzero_n_s=UNTRACKED_NO_REPLAY\n";

    for (unsigned i = 0; i < state.nodes.purchased.size(); ++i) {

        state.nodes.purchased[i] = (mask & (std::int64_t{1} << i)) != 0;

    }

    if (state.nodes.spent() > 50) return std::nullopt;

    for (const auto [node, required] : prog::prerequisites) {

        if (state.nodes.owns(node) && !state.nodes.owns(required)) return std::nullopt;

    }

    for (const auto [left, right] : prog::exclusions) {

        if (state.nodes.owns(left) && state.nodes.owns(right)) return std::nullopt;

    }

    for (std::size_t i = 0; i < payload / sizeof(prog::SatisfactionBlock); ++i) {

        prog::SatisfactionBlock block{};

        if (serial->ReadRecordData(block) != sizeof(block) || !state.satisfaction.restore(block)) {

            return std::nullopt;

        }

    }

    return state;

}



void saveHardcore(SKSE::SerializationInterface* serial)

{

    if (progression.developerMode()) return;

    auto preferences=panel::captureSettings();
    preferences[7]=auraEnabled ? 1u:0u;
    if (const auto record=tds::settingsForSave(progression.developerMode(),preferences)) {
        const bool stored=serial->WriteRecord(tds::settingsRecord,1,*record);
        logFile << "TDS_SETTINGS event=SAVE status=" << (stored ? "OK" : "FAILED")
                << " hotkey=" << (*record)[0] << " hud=" << (*record)[1]
                << " layout=" << tds::hudLayout(*record) << " aura=" << (*record)[7] << '\n';
    }

    saveProgression(serial);
    if(!serial->WriteRecord(tds::equipmentClaimRecord,1,gearClaims))
        logFile << "TDS_GEAR_CLAIM status=SAVE_FAILED\n";

    const std::array<std::int32_t, 2> data{hardcore.enabled ? 1 : 0, hardcore.lastV.value_or(-1)};

    if (!serial->WriteRecord(hardcoreRecord, 1, data)) {

        logFile << "TDS_HARDCORE_SERIAL status=SAVE_FAILED\n";

    }

}



void loadHardcore(SKSE::SerializationInterface* serial)

{

    savedHardcore.reset();
    savedSettings.reset();
    bool seenSettings=false;

    std::uint32_t type{}, version{}, length{};

    bool seen = false;

    bool seenProgression = false;

    bool seenRecent=false;
    bool seenGearClaims=false;
    savedGearClaims.reset();

    std::optional<RecentBlockPayload> recent;

    savedProgression.reset();

    progressionRecordInvalid = false;

    while (serial->GetNextRecordInfo(type, version, length)) {

        if (type==tds::settingsRecord) {
            tds::SaveSettings value{};
            const auto read=length==sizeof(value) ? serial->ReadRecordData(value) : 0;
            savedSettings=tds::decodeSettings(version,length,read,value,seenSettings);
            seenSettings=true;
            if (!savedSettings) logFile << "TDS_SETTINGS event=LOAD status=INVALID_RECORD_DEFAULTS\n";
            continue;
        }

        if(type==tds::equipmentClaimRecord) {
            std::uint32_t value{};
            const auto read=length==sizeof(value)?serial->ReadRecordData(value):0;
            savedGearClaims=tds::decodeEquipmentClaim(version,length,read,value,seenGearClaims);
            if(seenGearClaims || version!=1 || length!=sizeof(value) || read!=sizeof(value) || (value & ~tds::equipmentClaimAll))
                logFile << "TDS_GEAR_CLAIM status=INVALID_RECORD grant_blocked=1\n";
            seenGearClaims=true;
            continue;
        }
        if (type == progressionRecord) {

            savedProgression = seenProgression ? std::nullopt : readProgression(serial, version, length);

            seenProgression = true;

            progressionRecordInvalid = !savedProgression;

            if (progressionRecordInvalid) logFile << "TDS_PROGRESSION_SERIAL status=INVALID_RECORD\n";

            continue;

        }

        if (type==recentBlocksRecord) {

            RecentBlockPayload data{};

            if (!seenRecent && version==1 && length==sizeof(data) &&

                serial->ReadRecordData(data)==sizeof(data) && data[0]>=1 && data[0]<=3) {

                bool valid=true;

                for (unsigned i=0;i<static_cast<unsigned>(data[0]);++i) {

                    const auto x=data[1+i*2], k=data[2+i*2];

                    valid &= x>=-1'000'000'000'000 && x<=1'000'000'000'000 && k>=0;

                }

                if (valid) recent=data;

            } else recent.reset();

            seenRecent=true;

            continue;

        }

        if (type != hardcoreRecord) continue;

        std::array<std::int32_t, 2> data{};

        if (seen || version != 1 || length != sizeof(data) ||

            serial->ReadRecordData(data) != sizeof(data) ||

            (data[0] != 0 && data[0] != 1) || data[1] < -1) {

            savedHardcore.reset();

            logFile << "TDS_HARDCORE_SERIAL status=INVALID_RECORD\n";

        } else {

            savedHardcore = data[0] == 1;

        }

        seen = true;

    }

    if (savedProgression && recent) {

        for (unsigned i=0;i<static_cast<unsigned>((*recent)[0]);++i) {

            savedProgression->recentBlocks[i]=prog::SceneAward{(*recent)[1+i*2],(*recent)[2+i*2],0,0};

        }

    }

}



void revertHardcore(SKSE::SerializationInterface*)
{
    savedSettings.reset();
    gearClaims=mockGearClaims=0;
    savedGearClaims.reset();
    gearGrantResult=gearGrantGroup=gearGrantSerial=0;

    savedHardcore.reset();

    savedProgression.reset();

    progressionRecordInvalid=false;

    hardcore = {};

}



constexpr std::array eventNames{"LOAD", "NEW_GAME", "SCENE_END", "MANUAL", "UI_OPEN", "RECHECK"};



void observeHardcore(Event event, const Snapshot& sample)

{

    if (sample.status == ReadStatus::Stale) return;

    if (sample.status != ReadStatus::Valid) {

        hardcore.observe(std::nullopt);

        return;

    }

    constexpr std::size_t victimSlot = 6;

    static_assert(statNames[victimSlot] == "V");

    if (!hardcore.observe(sample.values[victimSlot])) return;



    auto* player = RE::PlayerCharacter::GetSingleton();

    auto* av = player ? player->AsActorValueOwner() : nullptr;

    const auto dynamic = dynamicState(player);

    hardcoreBefore.reset();

    hardcoreAfter.reset();

    if (!dynamic.allowReset) {

        hardcoreResult = dynamic.compatibility;

    } else if (!av || !player->Is3DLoaded()) {

        hardcoreResult = "PLAYER_NOT_READY";

    } else {

        using AV = RE::ActorValue;

        const float before = av->GetBaseActorValue(AV::kMagicka);

        if (!std::isfinite(before)) {

            hardcoreResult = "INVALID_BASE_MAGICKA";

        } else {

            hardcoreBefore = before;

            const auto read = [&] {

                return MagickaLedger{av->GetBaseActorValue(AV::kMagicka),

                    dynamic.growthForm ? dynamic.growthForm->value : 0.0f,

                    dynamic.xpForm ? dynamic.xpForm->value : 0.0f};

            };

            const auto ledgerBefore = read();

            // Replacing the entire base removes any included growth once.

            // Clear the ledger so MCM OFF/ON cannot subtract or restore that growth.

            const auto write = [&](MagickaLedger value) {

                if (dynamic.growthForm) dynamic.growthForm->value = value.growth;

                if (dynamic.xpForm) dynamic.xpForm->value = value.xp;

                av->SetBaseActorValue(AV::kMagicka, value.base);

            };

            constexpr std::array results{"BASE_AND_LEDGER_RESET", "INVALID_LEDGER", "VERIFY_FAILED_ROLLED_BACK",

                "ROLLBACK_FAILED"};

            const auto result = resetMagickaLedger(read, write);

            hardcoreAfter = av->GetBaseActorValue(AV::kMagicka);

            hardcoreResult = results[static_cast<std::size_t>(result)];

            const auto ledgerAfter = read();

            logFile << "TDS_DM_RESET status=" << hardcoreResult << " dynamic=" << dynamic.state

                    << " growth_before=" << ledgerBefore.growth << " xp_before=" << ledgerBefore.xp

                    << " growth=" << ledgerAfter.growth << " xp=" << ledgerAfter.xp << '\n';

        }

    }

    logFile << "TDS_HARDCORE V_CHANGE_OBSERVED_BY=" << eventNames[static_cast<std::size_t>(event)]

            << " v=" << *hardcore.lastV << " delta=" << *hardcore.delta

            << " status=" << hardcoreResult << " base_before=" << hardcoreBefore.value_or(NAN)

            << " base_after=" << hardcoreAfter.value_or(NAN) << '\n';

}

bool refreshQueued{};

ReadStatus lastStatus{ReadStatus::NotReady};

std::uint64_t observations{};

std::optional<Stats> delta;



bool resolveGrowthAbilities(RE::TESDataHandler& data)

{

    const auto& expected=tds::growth_forms::effects;

    decltype(growthAbilities) verified{};

    RE::FormID effectId=tds::growth_forms::firstEffectId;

    for (unsigned i=0; i<verified.size(); ++i) {

        auto* spell=data.LookupForm<RE::SpellItem>(tds::growth_forms::spellIds[i],esp);

        if (!spell || spell->GetSpellType()!=RE::MagicSystem::SpellType::kAbility ||

            spell->GetCastingType()!=RE::MagicSystem::CastingType::kConstantEffect ||

            spell->GetDelivery()!=RE::MagicSystem::Delivery::kSelf ||

            spell->effects.size()!=expected[i].size()) return false;

        for (unsigned j=0; j<spell->effects.size(); ++j,++effectId) {

            const auto [av,amount]=expected[i][j];

            const auto* item=spell->effects[j];

            const auto* effect=data.LookupForm<RE::EffectSetting>(effectId,esp);

            if (!item || !effect || item->baseEffect!=effect ||

                static_cast<int>(effect->GetArchetype())!=0 ||

                static_cast<int>(effect->data.primaryAV)!=av || effect->IsDetrimental()!=std::signbit(amount) ||

                item->effectItem.magnitude!=std::abs(amount) || item->effectItem.area!=0 ||

                item->effectItem.duration!=0) return false;

        }

        verified[i]=spell;

    }

    growthAbilities=verified;

    return true;

}



bool resolveForms()

{

    auto* data = RE::TESDataHandler::GetSingleton();

    if (!data) {

        return false;

    }

    const auto* light = data->LookupLoadedLightModByName(esp);

    if (!light || !light->IsLight() || light->GetCompileIndex() != 0xFE ||

        light->GetSmallFileCompileIndex() > 0xFFF) {

        logFile << "TDS_INIT light_plugin=NOT_LOADED\n";

        return false;

    }

    logFile << "TDS_INIT light_plugin=FE:" << std::hex << std::setw(3)

            << std::setfill('0') << light->GetSmallFileCompileIndex()

            << std::dec << std::setfill(' ') << '\n';

    for (std::size_t g = 0; g < names.size(); ++g) {

        enchantments[g] = data->LookupForm<RE::EnchantmentItem>(static_cast<RE::FormID>(0x810 + g), esp);

        if (!enchantments[g]) {

            return false;

        }

        for (int stage = 1; stage <= 40; ++stage) {

            auto*& ability = abilities[g][stage];

            ability = data->LookupForm<RE::SpellItem>(static_cast<RE::FormID>(0x900 + g * 0x40 + stage), esp);

            if (!ability || ability->effects.empty()) {

                return false;

            }

        }

    }

    return true;

}



bool wornByHand(const RE::ExtraDataList& list, bool left)

{

    return left ? list.HasType<RE::ExtraWornLeft>() : list.HasType<RE::ExtraWorn>();

}



std::optional<Group> weaponGroup(RE::InventoryEntryData* entry, bool left)

{

    const auto* weapon = entry && entry->object ? entry->object->As<RE::TESObjectWEAP>() : nullptr;

    if (!weapon) {

        return std::nullopt;

    }

    const RE::ExtraDataList* worn{};

    if (entry->extraLists) {

        for (const auto* list : *entry->extraLists) {

            if (list && wornByHand(*list, left)) {

                if (worn) {

                    return std::nullopt;

                }

                worn = list;

            }

        }

        // A stack may contain enchantments for other items. Never borrow them.

        if (!worn) {

            for (const auto* list : *entry->extraLists) {

                if (list && list->HasType<RE::ExtraEnchantment>()) {

                    return std::nullopt;

                }

            }

        }

    }

    return classifyEnchantment(worn, weapon->formEnchanting, true, enchantments);

}



std::vector<Group> equippedGroups(RE::PlayerCharacter& player)

{

    std::vector<Group> result;

    for (const bool left : {false, true}) {

        if (const auto group = weaponGroup(player.GetEquippedEntryData(left), left)) {

            result.push_back(*group);

        }

    }

    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;

    const auto accessories = player.GetInventory([](RE::TESBoundObject& object) {

        const auto* armor = object.As<RE::TESObjectARMO>();

        return armor && armor->GetSlotMask().any(Slot::kRing, Slot::kAmulet);

    });

    for (const auto& [object, item] : accessories) {

        const auto* entry = item.second.get();

        if (!entry || !entry->extraLists || !entry->IsWorn()) {

            continue;

        }

        const auto* armor = object->As<RE::TESObjectARMO>();

        for (const auto* list : *entry->extraLists) {

            if (list && (wornByHand(*list, false) || wornByHand(*list, true))) {

                if (const auto group = classifyEnchantment(list, armor->formEnchanting, false, enchantments)) {

                    result.push_back(*group);

                }

            }

        }

    }

    return result;

}



auto growthSelection(const prog::ProgressionState& state, std::optional<int> verifiedCount)

{

    std::array<bool,tds::growth_forms::effects.size()> desired{};

    if (verifiedCount) {

        desired[0]=state.nodes.active(prog::Node::Guard);

        desired[1]=state.nodes.active(prog::Node::Ward);

        desired[2]=state.nodes.active(prog::Node::Predator);

        desired[3]=state.nodes.active(prog::Node::Carapace);

        desired[4]=*verifiedCount==0;

        desired[tds::growth_forms::puritySkillSlot]=*verifiedCount==0;

        desired[tds::growth_forms::karmaSkillSlot]=state.corruption>=prog::corruptionThresholds.back();

        const auto karmaTier=prog::corruptionTier(static_cast<int>(std::clamp<std::int64_t>(

            state.corruption,0,prog::corruptionThresholds.back())));

        if (karmaTier>0) desired[tds::growth_forms::karmaSpeechSlot+karmaTier-1]=true;

        if (const auto slot=prog::karmaAbilityIndex(state.nodes,state.corruption)) desired[*slot]=true;

        if (const auto slot=prog::satisfactionAbilityIndex(state.nodes,state.satisfaction.total(),state.corruption)) desired[*slot]=true;

    }

    return desired;

}



bool mockHardcore{};

bool ownsDeveloperSaveFlag{};

std::atomic_int effectiveCombatCount{-1};



bool armDeveloperSaveFlag(RE::PlayerCharacter& player)

{

    using Flag=RE::PlayerCharacter::ByCharGenFlag;

    auto& flags=player.GetGameStatsData().byCharGenFlag;

    if (flags.any(Flag::kDisableSaving)) return false;

    flags.set(Flag::kDisableSaving);

    ownsDeveloperSaveFlag=flags.any(Flag::kDisableSaving);

    return ownsDeveloperSaveFlag;

}



bool developerEffectsAllowed(const RE::PlayerCharacter& player)

{

    return !progression.developerMode() || (ownsDeveloperSaveFlag &&

        player.GetGameStatsData().byCharGenFlag.any(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving));

}



void releaseDeveloperSaveFlag()

{

    if (!ownsDeveloperSaveFlag) return;

    if (auto* player=RE::PlayerCharacter::GetSingleton()) {

        player->GetGameStatsData().byCharGenFlag.reset(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);

    }

    ownsDeveloperSaveFlag=false;

}



std::optional<int> effectCount(RE::PlayerCharacter& player)

{

    if (progression.developerMode() && ownsDeveloperSaveFlag && sessionReady && !developerEffectsAllowed(player)) {

        player.GetGameStatsData().byCharGenFlag.set(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);

        logFile << "TDS_QA status=SAVE_GUARD_REARMED\n";

    }

    if (!developerEffectsAllowed(player) || (!progression.developerMode() && progressionRecordInvalid)) return std::nullopt;

    return progression.effectiveCount(count);

}

RE::SpellItem* resourcePenaltySpell{};

RE::SpellItem* offeringPower{};

constexpr std::array resourceValues{RE::ActorValue::kHealth,RE::ActorValue::kMagicka,RE::ActorValue::kStamina};

std::array<float,resourceValues.size()> resourcePenaltyMagnitudes{};

int resourcePenaltyRegistered{};

std::atomic_bool resourceCheck{};

std::optional<long long> lastCountdownTick;



bool resolveResourcePenalty(RE::TESDataHandler& data)

{

    auto* spell=data.LookupForm<RE::SpellItem>(0xFE8,esp);

    if (!spell || spell->GetSpellType()!=RE::MagicSystem::SpellType::kAbility ||

        spell->GetCastingType()!=RE::MagicSystem::CastingType::kConstantEffect ||

        spell->GetDelivery()!=RE::MagicSystem::Delivery::kSelf ||

        spell->effects.size()!=resourceValues.size()) return false;

    for (unsigned i=0;i<resourceValues.size();++i) {

        const auto* item=spell->effects[i];

        const auto* effect=data.LookupForm<RE::EffectSetting>(0xFE9+i,esp);

        if (!item || !effect || item->baseEffect!=effect || !effect->IsDetrimental() ||

            static_cast<int>(effect->GetArchetype())!=0 || effect->data.primaryAV!=resourceValues[i] ||

            item->effectItem.magnitude!=0 || item->effectItem.duration!=0 || item->effectItem.area!=0) return false;

    }

    resourcePenaltySpell=spell;

    return true;

}



bool reconcileResourcePenalty(RE::PlayerCharacter& player,bool clear=false)

{

    if (!resourcePenaltySpell || !player.Is3DLoaded() || player.IsDead()) return false;

    const auto n=clear ? std::nullopt : effectCount(player);

    const int percent=n ? prog::passiveEffects(progression.current().nodes,*n).baseResourceReductionPercent : 0;

    const auto* values=player.AsActorValueOwner();

    if (!values) return false;

    std::array<float,resourceValues.size()> desired{};

    if (percent>0) {

        for (unsigned i=0;i<desired.size();++i) {

            const auto base=values->GetBaseActorValue(resourceValues[i]);

            if (!std::isfinite(base) || base<0) return false;

            const float floor=i==0 ? 1.0f : 0.0f;

            desired[i]=std::clamp(base*percent/100.0f,0.0f,std::max(0.0f,base-floor));

        }

    }

    const bool present=player.HasSpell(resourcePenaltySpell);

    if (desired==resourcePenaltyMagnitudes && percent==resourcePenaltyRegistered &&

        present==(percent>0)) return false;

    const std::array before{player.GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary,resourceValues[0]),

        player.GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary,resourceValues[1]),

        player.GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary,resourceValues[2])};

    if (present) {

        player.RemoveSpell(resourcePenaltySpell);

        if (player.HasSpell(resourcePenaltySpell)) return false;

    }

    resourcePenaltyMagnitudes={};

    resourcePenaltyRegistered=0;

    if (percent>0) {

        for (unsigned i=0;i<desired.size();++i) resourcePenaltySpell->effects[i]->effectItem.magnitude=desired[i];

        player.AddSpell(resourcePenaltySpell);

        if (!player.HasSpell(resourcePenaltySpell)) {

            logFile << "TDS_RESOURCE status=ADD_FAILED\n";

            return true;

        }

        resourcePenaltyMagnitudes=desired;

        resourcePenaltyRegistered=percent;

    }

    logFile << "TDS_RESOURCE percent=" << resourcePenaltyRegistered;

    for (unsigned i=0;i<desired.size();++i) {

        logFile << " amount_" << i << '=' << desired[i] << " observed_delta_" << i << '='

            << player.GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary,resourceValues[i])-before[i];

    }

    logFile << '\n';

    return true;

}



void grantStarterSpells(RE::PlayerCharacter& player)
{
    if (!sessionReady || !formsReady || !player.Is3DLoaded() || player.IsDead()) return;
    auto* data=RE::TESDataHandler::GetSingleton();
    if (!data) return;
    auto* quest=data->LookupForm<RE::TESQuest>(0xF66,esp);
    if (!quest) return;
    if (!quest->IsRunning() && !quest->Start()) {
        logFile << "TDS_HELPER status=QUEST_NOT_READY\n";
        return;
    }
    for (const auto id : {0xF60u,0xF61u,0xF62u}) {
        auto* spell=data->LookupForm<RE::SpellItem>(id,esp);
        if (!spell || spell->effects.size()!=1 || !spell->effects[0] || !spell->effects[0]->baseEffect) continue;
        if (id==0xF62 ? spell->GetSpellType()!=RE::MagicSystem::SpellType::kLesserPower :
                       spell->GetSpellType()!=RE::MagicSystem::SpellType::kSpell) continue;
        if (!player.HasSpell(spell)) {
            player.AddSpell(spell);
            logFile << "TDS_HELPER spell=" << std::hex << id << std::dec << " registered=" << player.HasSpell(spell) << '\n';
        }
    }
}

void grantEquipment(unsigned group)
{
    ++gearGrantSerial;
    gearGrantGroup=group;
    gearGrantResult=3;
    auto* player=RE::PlayerCharacter::GetSingleton();
    auto* data=RE::TESDataHandler::GetSingleton();
    if(group>=4 || !sessionReady || !formsReady || !player || !data || !player->Is3DLoaded() || player->IsDead()) return;
    auto& claims=progression.developerMode()?mockGearClaims:gearClaims;
    if(claims & (1u<<group)) { gearGrantResult=2; return; }
    static_assert(RE::TESBoundObject::FORMTYPE==RE::FormType::None);
    static_assert(RE::TESObjectWEAP::FORMTYPE==RE::FormType::Weapon && RE::TESObjectARMO::FORMTYPE==RE::FormType::Armor);
    auto* item=tds::lookupEquipmentSample<RE::TESBoundObject,RE::TESObjectWEAP,RE::TESObjectARMO>(*data,group,esp);
    auto* weapon=item?item->As<RE::TESObjectWEAP>():nullptr;
    auto* armor=item?item->As<RE::TESObjectARMO>():nullptr;
    if(!item || (group<2 ? (!weapon || weapon->formEnchanting!=enchantments[group]) : (!armor || armor->formEnchanting!=enchantments[group]))) {
        gearGrantResult=4;
        logFile << "TDS_GEAR_GRANT status=INVALID_FORM group=" << group << " local_id=" << std::hex << (0x800+group)
            << " item=" << (item?item->GetFormID():0) << " expected_enchantment=" << (enchantments[group]?enchantments[group]->GetFormID():0)
            << " actual_enchantment=" << (weapon&&weapon->formEnchanting?weapon->formEnchanting->GetFormID():armor&&armor->formEnchanting?armor->formEnchanting->GetFormID():0)
            << std::dec << " claimed=0\n"; return;
    }
    const auto before=player->GetItemCount(item);
    if(before<0 || before==std::numeric_limits<std::int32_t>::max()) return;
    // Reserve before inventory events can re-enter. Neither discarding nor disenchanting refunds this claim.
    if(!tds::reserveEquipmentClaim(claims,group)) { gearGrantResult=2; return; }
    player->AddObjectToContainer(item,nullptr,1,nullptr);
    const auto after=player->GetItemCount(item);
    gearGrantResult=after==before+1?1:5;
    logFile << "TDS_GEAR_GRANT group=" << group << " qa=" << progression.developerMode()
        << " requested=1 observed=" << (static_cast<std::int64_t>(after)-before)
        << " claim_mask=" << claims << " status=" << (gearGrantResult==1?"DELIVERED":"DELIVERY_UNVERIFIED_CLAIM_RETAINED") << '\n';
}

void reconcileGrowthAbilities(RE::PlayerCharacter& player,bool clear=false)

{
    if (!clear) grantStarterSpells(player);

    reconcileResourcePenalty(player,clear);

    if (!offeringPower) if (auto* data=RE::TESDataHandler::GetSingleton()) {

        auto* spell=data->LookupForm<RE::SpellItem>(0xFEC,esp);

        auto* marker=data->LookupForm<RE::EffectSetting>(0xFED,esp);

        if (spell && marker && spell->GetSpellType()==RE::MagicSystem::SpellType::kLesserPower &&

            spell->effects.size()==1 && spell->effects[0] && spell->effects[0]->baseEffect==marker)

            offeringPower=spell;

    }

    if (offeringPower) {

        const bool wanted=!clear && effectCount(player).has_value() && progression.current().nodes.active(prog::Node::Recovery1);

        if (wanted && player.Is3DLoaded() && !player.IsDead() && !player.HasSpell(offeringPower)) player.AddSpell(offeringPower);

        else if (!wanted && player.HasSpell(offeringPower)) player.RemoveSpell(offeringPower);

    }

    const auto verified=clear ? std::nullopt : effectCount(player);

    const auto& state=progression.current();

    const auto rates=verified ? prog::engineRates(state.nodes,state.satisfaction.total(),state.corruption)

                              : prog::EngineRates{};

    if (tds::engine_effects::costPerk) {
        const auto before=tds::engine_effects::costValue ? tds::engine_effects::costValue->data : 1.0f;
        const bool costOK=tds::engine_effects::reconcileCost(player,rates.spellCostBasisPoints);
        if (!costOK || before!=(tds::engine_effects::costValue ? tds::engine_effects::costValue->data : 1.0f))
            logFile << "TDS_COST status=" << (costOK?"VALUE_APPLIED":"REGISTER_FAILED")
                << " requested_bp=" << rates.spellCostBasisPoints << " factor="
                << (tds::engine_effects::costValue ? tds::engine_effects::costValue->data : 1.0f) << '\n';
    }

    if (!growthAbilities.front()) return;

    const auto desired=growthSelection(progression.current(),clear ? std::nullopt : effectCount(player));

    std::array<bool,tds::growth_forms::effects.size()> prior{};

    for (unsigned i=0; i<prior.size(); ++i) prior[i]=player.HasSpell(growthAbilities[i]);

    if (prior==desired) return;

    const auto set=[&](const auto& target) {

        bool all=true;

        for (const bool presence : {true,false}) {

            for (unsigned i=0; i<target.size(); ++i) {

                auto* spell=growthAbilities[i];

                if (target[i]!=presence || player.HasSpell(spell)==presence) continue;

                if (presence) player.AddSpell(spell);

                else player.RemoveSpell(spell);

                all &= player.HasSpell(spell)==presence;

            }

            if (!all) return false;

        }

        return all;

    };

    if (!set(desired)) {

        logFile << "TDS_GROWTH_AV status=TRANSITION_FAILED rollback=" << set(prior) << '\n';

    }

}



void publishMeleeGrowth()

{

    const auto& state=progression.current();

    auto* player=RE::PlayerCharacter::GetSingleton();

    const auto n=sessionReady && player ? effectCount(*player) : std::nullopt;

    effectiveCombatCount.store(n.value_or(-1),std::memory_order_relaxed);

    const auto incoming=n && (progression.developerMode() || !statsGap)

        ? prog::engineRates(state.nodes,state.satisfaction.total(),state.corruption).incomingDamageBasisPoints : 0;

    tds::engine_effects::incomingRate.store(incoming,std::memory_order_relaxed);

    tds::spell_native::setBonus(prog::destructionDamageBonus(n,state.corruption));

    tds::spell_native::setResistance(n && (progression.developerMode() || !statsGap) && state.stats

        ? state.nodes : prog::Investment{});

    liveMelee.store(n && (progression.developerMode() || !statsGap) && state.stats

        ? prog::meleeGrowth(state.nodes,*n,state.satisfaction.total(),state.corruption)

        : prog::MeleeGrowth{},std::memory_order_relaxed);

}



void updateExpiryArm()

{

    publishMeleeGrowth();

    resourceCheck=sessionReady && resourcePenaltySpell &&

        (progression.developerMode() || count.value_or(0)>0);

    expiryArmed=sessionReady && !progression.current().satisfaction.records().empty();

}



std::array<RE::BGSArtObject*,9> auraArts{};


bool auraReady{};



void syncAura(bool clear=false)

{

    if (!auraReady) return;

    auto* player=RE::PlayerCharacter::GetSingleton();

    auto* lists=RE::ProcessLists::GetSingleton();

    if (!player || !lists) return;

    const auto& state=progression.current();

    RE::BGSArtObject* desired=nullptr;

    if (!clear && auraEnabled && sessionReady && player->Is3DLoaded() && !player->IsDead() &&

        player->AsActorState()->IsWeaponDrawn()) {

        const auto n=progression.effectiveCount(count);

        if (n) {

            desired=auraArts[*n==0 ? 0 : prog::corruptionAuraSlot(state.corruption)];

        }

    }

    const auto handle=player->CreateRefHandle();

    bool present=false;

    bool retiring=false;

    lists->ForEachModelEffect([&](RE::ModelReferenceEffect* effect) {

        if (effect && effect->target==handle && effect->artObject &&

            std::ranges::find(auraArts,effect->artObject)!=auraArts.end()) {

            if (effect->finished) retiring|=effect->artObject==desired;

            else if (effect->artObject==desired && !present) present=true;

            else effect->finished=true;

        }

        return RE::BSContainer::ForEachResult::kContinue;

    });

    if (desired && !present && !retiring) {

        if (player->ApplyArtObject(desired)) logFile << "TDS_AURA art=" << desired->GetLocalFormID() << '\n';

    }

}



void reconcile();



void startExpiryService()

{

    const auto* tasks=SKSE::GetTaskInterface();

    if (!tasks) return;

    // Only the queued game-thread task reads game time or touches actors.

    static std::jthread worker([tasks](std::stop_token stop) {

        while (!stop.stop_requested()) {

            std::this_thread::sleep_for(250ms);

            if (stop.stop_requested() || (!expiryArmed && !auraEnabled && !resourceCheck && !panel::hudTickNeeded()) || expiryPending.exchange(true)) continue;

            tasks->AddTask([] {

                expiryPending=false;

                if (!sessionReady) return;

                if (panel::hudTickNeeded()) panel::tickHud();

                if (auraEnabled) syncAura();

                if (!expiryArmed) {

                    auto* player=RE::PlayerCharacter::GetSingleton();

                    if (resourceCheck && player) {

                        if (progression.developerMode() && !developerEffectsAllowed(*player)) {

                            reconcile();

                            panel::publish();

                        } else if (reconcileResourcePenalty(*player)) panel::publish();

                    }

                    return;

                }

                const auto* clock=RE::Calendar::GetSingleton();

                if (!clock) return;

                const auto& blocks=progression.current().satisfaction;

                const auto before=blocks.total();

                const auto previousCount=blocks.records().size();

                progression.expire(clock->GetHoursPassed());

                if (auto* player=RE::PlayerCharacter::GetSingleton()) reconcileGrowthAbilities(*player);

                updateExpiryArm();

                const double now=progression.clockHours(clock->GetHoursPassed());

                std::optional<long long> countdownTick;

                for (const auto& block : blocks.records()) {

                    const auto tick=static_cast<long long>(std::ceil(std::max(0.0,block.expiresAt-now)*10.0));

                    if (!countdownTick || tick<*countdownTick) countdownTick=tick;

                }

                const bool countdownChanged=lastCountdownTick!=countdownTick;

                lastCountdownTick=countdownTick;

                if (previousCount!=blocks.records().size() || (progression.developerMode() && countdownChanged)) {

                    logFile << "TDS_QA_CLOCK game_h=" << clock->GetHoursPassed() << " effective_h=" << now

                            << " blocks=" << blocks.records().size() << " remaining_tenths=" << countdownTick.value_or(-1) << '\n';

                }

                if (before!=blocks.total() || previousCount!=blocks.records().size() || countdownChanged) panel::publish();

            });

        }

    });

}



void reconcile()

{

    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!sessionReady || !formsReady || !player) return;

    const auto n=effectCount(*player);

    reconcileGrowthAbilities(*player);

    syncAura();

    updateExpiryArm();

    const auto equipped = equippedGroups(*player);

    const auto planned = targets(equipped, n);

    const auto* av=player->AsActorValueOwner();

    if (av && progression.developerMode()) {

        logFile << "TDS_QA_AV speech=" << av->GetActorValue(RE::ActorValue::kSpeech)

                << " health=" << av->GetActorValue(RE::ActorValue::kHealth)

                << " magicka=" << av->GetActorValue(RE::ActorValue::kMagicka)

                << " stamina=" << av->GetActorValue(RE::ActorValue::kStamina);

        auto* magic=player->AsMagicTarget();

        if (auto* effects=magic ? magic->GetActiveEffectList() : nullptr) {

            for (const auto* effect : *effects) {

                if (!effect || effect->spell!=resourcePenaltySpell ||

                    effect->flags.any(RE::ActiveEffect::Flag::kInactive,RE::ActiveEffect::Flag::kDispelled)) continue;

                logFile << " penalty_active_magnitude=" << effect->magnitude;

            }

        }

        logFile << '\n';

    }

    logFile << "TDS_QA_STATE qa=" << progression.developerMode() << " n=" << n.value_or(-1)

            << " x=" << progression.current().satisfaction.total() << " k=" << progression.current().corruption

            << " resource_registered=" << resourcePenaltyRegistered << " equipped=" << equipped.size() << '\n';

    for (std::size_t i = 0; i < names.size(); ++i) {

        const auto stage = planned[i] ? planned[i]->stage : 0;

        auto* desired = abilities[i][stage];

        auto*& active = owned[i];

        if (desired == active && (!desired || player->HasSpell(desired))) {

            continue;

        }



        // Add before removing the old stage to avoid a transient drop in max resources.

        if (desired && !player->HasSpell(desired)) {

            player->AddSpell(desired);

            if (!player->HasSpell(desired)) {

                logFile << "TDS_EQUIP group=" << names[i] << " status=ADD_FAILED\n";

                continue;

            }

        }

        if (active && active != desired && player->HasSpell(active)) {

            player->RemoveSpell(active);

            if (player->HasSpell(active)) {

                if (desired) {

                    player->RemoveSpell(desired);

                }

                logFile << "TDS_EQUIP group=" << names[i] << " status=REMOVE_FAILED\n";

                continue;

            }

        }

        active = desired;

        logFile << "TDS_EQUIP group=" << names[i] << " count=" << n.value_or(-1)

                << " stage=" << stage << " status=" << (active ? "SPELL_PRESENT" : "WITHDRAWN") << '\n';

    }

}



template <class Range>

void jsonArray(std::ostream& out, const Range& range)

{

    out << '[';

    bool comma{};

    for (auto value : range) {

        if (std::exchange(comma, true)) {

            out << ',';

        }

        out << value;

    }

    out << ']';

}



std::string panelSnapshot()

{

    constexpr std::array status{"VALID", "NOT_READY", "ERROR", "STALE"};

    auto* player = RE::PlayerCharacter::GetSingleton();

    const auto equipped = player && sessionReady && formsReady

        ? equippedGroups(*player) : std::vector<Group>{};

    const bool qa=progression.developerMode();

    const auto& state = progression.current();

    const auto displayCount=progression.effectiveCount(count);

    const auto appliedCount=player && sessionReady ? effectCount(*player) : std::nullopt;

    const auto planned = targets(equipped, appliedCount);

    std::ostringstream out;

    out.imbue(std::locale::classic());

    out << "{\"status\":\"" << status[static_cast<std::size_t>(qa ? ReadStatus::Valid : lastStatus)]

        << "\",\"sequence\":" << observations << ",\"stage\":";

    if (displayCount) {

        out << growthStage(*displayCount);

    } else {

        out << "null";

    }

    if (const auto* calendar = RE::Calendar::GetSingleton()) {

        const auto previous=state.satisfaction.records().size();

        progression.expire(calendar->GetHoursPassed());

        if (previous!=state.satisfaction.records().size()) {

            if (player && sessionReady) reconcileGrowthAbilities(*player);

            updateExpiryArm();

        }

    }

    out << ",\"qa\":" << (qa ? "true" : "false")

        << ",\"auraEnabled\":" << (auraEnabled ? "true" : "false")

        << ",\"auraReady\":" << (auraReady ? "true" : "false");

    const auto claimMask=qa?mockGearClaims:gearClaims;
    out << ",\"gearClaims\":[";
    for(unsigned i=0;i<4;++i) { if(i)out<<',';out<<((claimMask&(1u<<i))?"true":"false"); }
    out << "],\"gearGrantReady\":" << (sessionReady && formsReady && player && player->Is3DLoaded() && !player->IsDead()?"true":"false")
        << ",\"gearGrantResult\":" << gearGrantResult << ",\"gearGrantGroup\":" << gearGrantGroup
        << ",\"gearGrantSerial\":" << gearGrantSerial << ",\"gearGrantSession\":" << session;
    const auto* calendar=RE::Calendar::GetSingleton();

    const double now=progression.clockHours(calendar ? calendar->GetHoursPassed() : 0.0);

    out << ",\"satisfactionBlocks\":[";

    bool firstBlock=true;

    for (const auto& block : state.satisfaction.records()) {

        if (!firstBlock) out << ',';

        firstBlock=false;

        out << '[' << block.value << ',' << std::max(0.0,block.expiresAt-now) << ']';

    }

    out << ']';

    const auto selected=growthSelection(state, player ? effectCount(*player) : std::nullopt);

    using EffectValues=std::array<float,158>;

    std::array<EffectValues,4> effectGroups{};

    const auto satisfactionSlot=prog::satisfactionAbilityIndex(state.nodes,state.satisfaction.total(),state.corruption);

    // Query registered spells through the existing SE-compatible actor path.

    // This snapshot does not inspect the runtime MagicTarget active-effect list.

    for (unsigned i=0; i<selected.size(); ++i) {

        if (!selected[i] || !player || !sessionReady || !growthAbilities[i] ||

            !player->HasSpell(growthAbilities[i])) continue;

        const auto group=i<4 ? 0 : i==4 || i==tds::growth_forms::puritySkillSlot ? 1 :

            satisfactionSlot==i ? 2 : 3;

        for (const auto& [av,amount] : tds::growth_forms::effects[i]) {

            effectGroups[group][av]+=amount;

        }

    }

    EffectValues overview{};

    for (unsigned i=0;i<overview.size();++i) overview[i]=effectGroups[0][i]+effectGroups[3][i];

    const auto emit=[&](const char* name,const EffectValues& values) {

        out << ",\"" << name << "\":[";

        bool first=true;

        for (unsigned i=0;i<values.size();++i) {

            if (values[i]==0) continue;

            if (!first) out << ',';

            first=false;

            out << '[' << i << ',' << values[i] << ']';

        }

        out << ']';

    };

    emit("growthValues",overview);

    emit("nodeValues",effectGroups[0]);

    emit("purityValues",effectGroups[1]);

    emit("satisfactionValues",effectGroups[2]);

    emit("karmaValues",effectGroups[3]);

    // Report configured effects separately from installation readiness.

    const int n=displayCount.value_or(-1);

    const auto passive=prog::passiveEffects(state.nodes,n);

    const auto karmaFlat=prog::meleeGrowth({},1,0,state.corruption).flat;

    const auto satisfactionFlat=prog::meleeGrowth(state.nodes,1,state.satisfaction.total(),state.corruption).flat

        -prog::meleeGrowth(state.nodes,1,0,state.corruption).flat;

    out << ",\"sceneWeights\":[";

    if (state.stats) {

        bool firstWeight=true;

        for (unsigned i=0;i<state.stats->size();++i) {

            if (!firstWeight) out << ',';

            firstWeight=false;

            auto next=*state.stats;

            if (next[i]==std::numeric_limits<int>::max()) {

                out << '[' << next[i] << ",null,null]";

                continue;

            }

            ++next[i];

            const auto award=prog::sceneAward(*state.stats,next,state.corruption,state.soloProgress,state.nodes);

            out << '[' << (*state.stats)[i] << ',' << (award ? award->satisfaction : 0) << ','

                << (award ? award->corruption : 0) << ']';

        }

    }

    out << "],\"counterKProgress\":[";

    if (state.stats) for (unsigned i=0;i<prog::CounterCount;++i) {

        if (i) out << ',';

        const auto count=(*state.stats)[i];

        const int stage= i==prog::S ? std::min(12,state.soloProgress+1) : (count>=12?12:count+1);

        const auto base11=i==prog::S ? std::int64_t{11}*((*state.stats)[prog::N]==0 ? 3*stage : prog::soloCorruption((*state.stats)[prog::N]))

            : prog::karmaNumerator11(i,stage-1,stage);

        out << '[' << stage << ',' << base11 << ',' << (i==prog::S?4*stage:0) << ']';

    }

    out << "],\"recentBlocks\":[";

    bool firstRecent=true;

    for (const auto& award : state.recentBlocks) {

        if (!award) continue;

        if (!firstRecent) out << ',';

        firstRecent=false;

        out << '[' << award->satisfaction << ',' << award->corruption << ',';

        if (award->actualBlock) out << *award->actualBlock;

        else out << "null";

        out << ',';

        if (award->effectiveChange) out << *award->effectiveChange;

        else out << "null";

        out << ',';

        if (award->soloBaseK) out << *award->soloBaseK; else out << "null";

        out << ',';

        if (award->soloExtraK) out << *award->soloExtraK; else out << "null";

        out << ']';

    }

    out << ']';

    out << ",\"resourcePenaltyRegistered\":" << resourcePenaltyRegistered;

    out << ",\"resourcePenaltyBase\":";

    if (resourcePenaltyRegistered>0 && player && player->Is3DLoaded()) {

        const auto* values=player->AsActorValueOwner();

        std::array<float,resourceValues.size()> bases{};

        if (values) {

            for (unsigned i=0;i<bases.size();++i) {

                const auto base=values->GetBaseActorValue(resourceValues[i]);

                bases[i]=std::isfinite(base) ? base : 0.0f;

            }

        }

        jsonArray(out,bases);

    } else out << "[]";

    out << ",\"resourcePenaltyAmounts\":";

    jsonArray(out,resourcePenaltyMagnitudes);

    out << ",\"weaponHookReady\":" << (originalHit.address() ? "true" : "false");

    const auto rate=displayCount ? prog::engineRates(state.nodes,state.satisfaction.total(),state.corruption)

                                 : prog::EngineRates{};

    const auto karmaRate=displayCount ? prog::engineRates(state.nodes,0,state.corruption) : prog::EngineRates{};

    out << ",\"engineEffects\":[" << rate.spellCostBasisPoints-karmaRate.spellCostBasisPoints << ','

        << karmaRate.spellCostBasisPoints << ',' << rate.incomingDamageBasisPoints << ','

        << (tds::engine_effects::costPerk ? "true" : "false") << ','

        << (tds::engine_effects::nextDamage ? "true" : "false") << ']';

    out << ",\"combatEffects\":";

    jsonArray(out,std::array{n==0?30.0f:0.0f,karmaFlat,satisfactionFlat,

        static_cast<float>(passive.flatWeaponDamage),static_cast<float>(passive.healthStealOnValidHit),

        static_cast<float>(passive.baseResourceReductionPercent)});

    out << ",\"gearPreview\":[";

    for (std::size_t i=0;i<allGroups.size();++i) {

        if (i) out << ',';

        if (!displayCount) { out << "null"; continue; }

        const auto t=calculate(allGroups[i],*displayCount);

        if (i<2) jsonArray(out,std::array{t.stage,t.weapon.flatDamage,t.weapon.damagePercent,

            t.weapon.health,t.weapon.stamina,t.weapon.lifeSteal});

        else jsonArray(out,std::array{t.stage,t.accessory.armor,t.accessory.magicka,t.accessory.stamina,

            t.accessory.disease,t.accessory.poison,t.accessory.magic,t.accessory.fire,t.accessory.frost,

            t.accessory.shock,t.accessory.spellAbsorption});

    }

    out << "],\"progression\":{\"recordValid\":" << (progressionRecordInvalid && !qa ? "false" : "true")

        << ",\"nodesEnabled\":" << (state.nodes.enabled ? "true" : "false")

        << ",\"satisfaction\":" << std::clamp(state.satisfaction.total(), std::int64_t{-100}, std::int64_t{100})

        << ",\"corruption\":" << state.corruption << ",\"spent\":" << state.nodes.spent()

        << ",\"available\":";

    if (const auto available = progression.availableGxp()) out << *available;

    else out << "null";

    out << ",\"purchased\":[";

    for (unsigned i = 0; i < state.nodes.purchased.size(); ++i) {

        if (i) out << ',';

        out << (state.nodes.purchased[i] ? "true" : "false");

    }

    out << "],\"purchaseStatus\":[";

    constexpr std::array purchaseLabels{"available","owned","disabled","unverified",

        "prerequisite","exclusive","insufficient","invalid"};

    const int verifiedCount=(qa || (lastStatus==ReadStatus::Valid && !statsGap && !progressionRecordInvalid))

        ? displayCount.value_or(-1) : -1;

    for (unsigned i=0; i<prog::nodeCosts.size(); ++i) {

        if (i) out << ',';

        const auto status=prog::purchaseStatus(state.nodes,static_cast<prog::Node>(i),verifiedCount);

        out << '"' << purchaseLabels[static_cast<unsigned>(status)] << '"';

    }

    out << "],\"history\":[";

    for (unsigned i=0; i<state.history.size(); ++i) {

        if (i) out << ',';

        out << static_cast<int>(state.history[i]);

    }

    out << "]}";

    out << ",\"stats\":";

    if (qa && state.stats) {

        jsonArray(out,*state.stats);

    } else if (logger->baseline()) {

        jsonArray(out, *logger->baseline());

    } else {

        out << "null";

    }

    out << ",\"delta\":";

    if (!qa && delta) {

        jsonArray(out, *delta);

    } else {

        out << "null";

    }



    const auto destruction=prog::destructionDamageBonus(appliedCount,state.corruption);

    out << ",\"destructionEffects\":[" << destruction.purityPercent << ',' << destruction.corruptionPercent

        << ',' << (tds::spell_native::next.front() ? "true" : "false") << ']';

    std::array<int, 11> bonuses{};

    out << ",\"groups\":[";

    for (std::size_t i = 0; i < names.size(); ++i) {

        if (i) {

            out << ',';

        }

        const auto stage = planned[i] ? planned[i]->stage : 0;

        auto* applied = player && sessionReady ? owned[i] : nullptr;

        const bool present = applied && player->HasSpell(applied);

        const bool matching = present && applied == abilities[i][stage];

        out << "{\"equipped\":" << std::boolalpha

            << (std::ranges::find(equipped, allGroups[i]) != equipped.end())

            << ",\"stage\":";

        if (appliedCount) {

            out << stage;

        } else {

            out << "null";

        }

        out << ",\"applied\":" << matching;

        out << ",\"liveStage\":" << (present ? std::ranges::find(abilities[i],applied)-abilities[i].begin() : 0);

        out << '}';

        if (!present) {

            continue;

        }

        const auto index = std::ranges::find(abilities[i], applied) - abilities[i].begin();

        if (index < 1 || index > 40) {

            continue;

        }

        const int asCount = thresholds[index - 1];

        const auto current = calculate(i < 2 ? Group::WeaponGrowth : Group::AccessoryGrowth, asCount);

        const auto& a = current.accessory;

        const std::array amounts{current.weapon.health, a.magicka, current.weapon.stamina + a.stamina,

            a.armor, a.disease, a.poison, a.magic, a.fire, a.frost, a.shock, a.spellAbsorption};

        for (std::size_t j = 0; j < amounts.size(); ++j) {

            bonuses[j] += amounts[j];

        }

    }

    out << "],\"bonuses\":";

    jsonArray(out, bonuses);

    out << ",\"hands\":[";

    for (const bool left : {false, true}) {

        if (left) {

            out << ',';

        }

        const auto group = player && sessionReady && formsReady && count

            ? weaponGroup(player->GetEquippedEntryData(left), left) : std::nullopt;

        if (group) {

            const auto w = calculate(*group, *count).weapon;

            jsonArray(out, std::array{w.flatDamage, w.damagePercent, w.lifeSteal});

        } else {

            out << "null";

        }

    }

    using AV = RE::ActorValue;

    constexpr std::array actorValues{AV::kHealth, AV::kMagicka, AV::kStamina, AV::kDamageResist,

        AV::kResistDisease, AV::kPoisonResist, AV::kResistMagic, AV::kResistFire,

        AV::kResistFrost, AV::kResistShock, AV::kAbsorbChance};

    const auto* actorValuesOwner = player && sessionReady && player->Is3DLoaded()

        ? player->AsActorValueOwner() : nullptr;

    out << "],\"actor\":[";

    for (std::size_t i = 0; i < actorValues.size(); ++i) {

        if (i) {

            out << ',';

        }

        const auto value = actorValuesOwner ? actorValuesOwner->GetActorValue(actorValues[i]) : NAN;

        if (std::isfinite(value)) {

            out << value;

        } else {

            out << "null";

        }

    }

    out << "],\"refreshing\":" << std::boolalpha << (readActive || queuedUiRead.has_value())

        << ",\"lastValidMs\":" << lastValidMs << ",\"hardcore\":{";

    const auto field = [&](const char* key, const auto& value) {

        out << ",\"" << key << "\":";

        if (value && std::isfinite(static_cast<double>(*value))) out << *value;

        else out << "null";

    };

    out << "\"enabled\":" << (qa ? mockHardcore : hardcore.enabled) << ",\"result\":\"" << hardcoreResult << '"';

    field("lastV", hardcore.lastV);

    field("delta", hardcore.delta);

    field("before", hardcoreBefore);

    field("after", hardcoreAfter);

    const auto dynamic = dynamicState(sessionReady ? player : nullptr);

    out << ",\"dynamic\":\"" << dynamic.state << "\",\"compatibility\":\"" << dynamic.compatibility << '"';

    field("growth", dynamic.growth);

    field("xp", dynamic.xp);

    using Mod = RE::ACTOR_VALUE_MODIFIER;

    const auto* av = player && sessionReady && player->Is3DLoaded() ? player->AsActorValueOwner() : nullptr;

    field("base", av ? std::optional{av->GetBaseActorValue(AV::kMagicka)} : std::optional<float>{});

    field("max", av ? std::optional{av->GetPermanentActorValue(AV::kMagicka) +

        player->GetActorValueModifier(Mod::kTemporary, AV::kMagicka)} : std::optional<float>{});

    field("current", av ? std::optional{av->GetActorValue(AV::kMagicka)} : std::optional<float>{});

    out << "}}";

    return out.str();

}



prog::Counters progressionCounters(const Stats& stats)

{

    static_assert(statNames[prog::N] == "SexCount" && statNames[prog::S] == "S" &&

                  statNames[prog::V] == "V" && statNames[prog::OR] == "OR");

    prog::Counters counters{};

    std::copy_n(stats.begin(), counters.size(), counters.begin());

    return counters;

}



void observeStats(Event event, Snapshot sample, std::span<const Group> equipped)

{

    ++observations;

    lastStatus = sample.status;

    delta.reset();

    if (sample.status == ReadStatus::Valid && logger->baseline() && !statsGap) {

        Stats changes{};

        for (std::size_t i = 0; i < changes.size(); ++i) {

            changes[i] = sample.values[i] - (*logger->baseline())[i];

        }

        delta = changes;

    }

    if (sample.status == ReadStatus::Valid && !progressionRecordInvalid) {

        const auto ticket = progression.beginRealRead();

        if (ticket && progression.acceptRealRead(*ticket, progressionCounters(sample.values))) {

            if (const auto* calendar = RE::Calendar::GetSingleton()) {

                const double hours = calendar->GetHoursPassed();

                progression.expire(hours);

                if (event == Event::SceneEnd) {

                    progression.settleScene(hours, !statsGap && !endQueued);

                } else if (statsGap) {

                    progression.settleScene(hours, false);

                }

            }

        }

    }

    logger->observe(session, event, sample, equipped, !statsGap);

    observeHardcore(event, sample);

    if (sample.status == ReadStatus::Valid) {

        statsGap = false;

        using namespace std::chrono;

        lastValidMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

    } else if (sample.status != ReadStatus::Stale) {

        statsGap = true;

    }

}

void requestRead(Event event, int attempt = 0)

{

    if (!sessionReady || progression.developerMode()) return;

    if (readActive) {

        endQueued |= event == Event::SceneEnd;

        if (event == Event::UiOpen || event == Event::Refresh || event == Event::Recheck) {

            queuedUiRead = event;

        }

        return;

    }

    auto* tasks = SKSE::GetTaskInterface();

    if (!tasks) {

        return;

    }



    readActive = true;

    logFile << "TDS_STATS_REQUEST reason=" << eventNames[static_cast<std::size_t>(event)]

            << " attempt=" << attempt + 1 << '\n';

    const auto generation = session;

    auto launch = [tasks, generation, event, attempt](std::stop_token stop) {

        const int delay = event == Event::SceneEnd ? 10 + 10 * attempt : 6 + 10 * attempt;

        for (int step = 0; step < delay && !stop.stop_requested(); ++step) {

            std::this_thread::sleep_for(25ms);

        }

        if (stop.stop_requested()) {

            return;

        }



        tasks->AddTask([tasks, generation, event, attempt] {

            if (!sessionReady || generation != session || progression.developerMode()) {

                return;

            }

            auto pending = beginPlayerRead(providerSha);

            reader = std::jthread([tasks, generation, event, attempt, pending = std::move(pending)](std::stop_token token) {

                std::optional<Snapshot> result;

                for (int step = 0; step < 120 && !token.stop_requested(); ++step) {

                    result = completedRead(pending);

                    if (result) {

                        break;

                    }

                    std::this_thread::sleep_for(25ms);

                }

                if (token.stop_requested()) {

                    return;

                }

                auto sample = result.value_or(Snapshot{ReadStatus::Error, {}, "VM_CALLBACK_TIMEOUT"});

                tasks->AddTask([generation, event, attempt, sample]() mutable {

                    if (!sessionReady || generation != session || progression.developerMode()) {

                        return;

                    }

                    readActive = false;

                    const bool sameSceneSample = event == Event::SceneEnd && !statsGap &&

                        sample.status == ReadStatus::Valid &&

                        (progressionRecordInvalid ? logger->baseline() == sample.values :

                            progression.sceneUnchanged(progressionCounters(sample.values)));

                    if (sameSceneSample && attempt < 2) {

                        auto* player = RE::PlayerCharacter::GetSingleton();

                        const auto equipped = player && formsReady ? equippedGroups(*player) : std::vector<Group>{};

                        observeStats(event, {ReadStatus::Stale, {}}, equipped);

                        panel::publish();

                        requestRead(event, attempt + 1);

                        return;

                    }

                    auto* player = RE::PlayerCharacter::GetSingleton();

                    const auto equipped = player && formsReady ? equippedGroups(*player) : std::vector<Group>{};

                    if (sameSceneSample) {

                        logFile << "TDS_STATS outcome=UNCHANGED_AFTER_BOUNDED_RECHECK freshness=UNCONFIRMED\n";

                        sample.status = ReadStatus::Stale;

                    }

                    observeStats(event, sample, equipped);

                    if (statsGap) publishMeleeGrowth();

                    if (sample.status == ReadStatus::Valid) {

                        count = sample.values[0];

                        reconcile();

                    } else if (!sameSceneSample && attempt < 2 && !count) {

                        requestRead(event, attempt + 1);

                        return;

                    }

                    panel::publish();

                    if (std::exchange(endQueued, false)) {

                        requestRead(Event::SceneEnd);

                    } else if (const auto queued = std::exchange(queuedUiRead, std::nullopt)) {

                        requestRead(*queued);

                    }

                });

            });

        });

    };

    reader = std::jthread(std::move(launch));

}



void startSession(Event event)
{
    gearClaims=event==Event::Load?savedGearClaims.value_or(0):0;
    mockGearClaims=gearClaims;
    savedGearClaims.reset();
    gearGrantResult=gearGrantGroup=gearGrantSerial=0;
    panel::resetSession(); // Also covers NewGame without a PreLoadGame event.

    tds::engine_effects::incomingRate.store(0,std::memory_order_relaxed);
    if (tds::engine_effects::costValue) tds::engine_effects::costValue->data=1.0f;

    tds::spell_native::setResistance({});

    expiryArmed=false;

    resourceCheck=false;

    lastCountdownTick.reset();

    resourcePenaltyRegistered=0;

    resourcePenaltyMagnitudes={};

    liveMelee.store({},std::memory_order_relaxed);

    session = logger->beginSession();

    const bool discardedQa=progression.developerMode();

    releaseDeveloperSaveFlag();

    effectiveCombatCount.store(-1,std::memory_order_relaxed);

    auraEnabled=false;

    syncAura(true);

    mockHardcore=false;

    sessionReady = true;

    readActive = false;

    endQueued = false;

    queuedUiRead.reset();

    statsGap = false;

    lastValidMs = 0;

    hardcore = {};

    hardcore.enabled = event == Event::Load && savedHardcore.value_or(false);

    if (event != Event::Load) progressionRecordInvalid = false;

    progression.loadState(event == Event::Load && savedProgression

        ? std::move(*savedProgression) : prog::ProgressionState{});

    logFile << "TDS_SESSION scope_reset session=" << session << " discarded_qa=" << discardedQa

        << " aura=0 hardcore=" << hardcore.enabled << '\n';

    savedProgression.reset();

    savedHardcore.reset();

    hardcoreResult = "NONE";

    hardcoreBefore.reset();

    hardcoreAfter.reset();

    refreshQueued = false;

    count.reset();

    delta.reset();

    lastStatus = ReadStatus::NotReady;

    owned.fill(nullptr);

    panel::resetSession();

    if (auto* player = RE::PlayerCharacter::GetSingleton(); formsReady && player) {

        reconcileGrowthAbilities(*player);

        int removed{};

        for (auto& group : abilities) {

            for (auto* ability : group) {

                if (ability && player->HasSpell(ability)) {

                    player->RemoveSpell(ability);

                    removed += !player->HasSpell(ability);

                }

            }

        }

        logFile << "TDS_SESSION restored_owned_spells_removed=" << removed << '\n';

    }

    auto defaults=panel::captureSettings(); // resetSession has restored INI/defaults.
    defaults[7]=0;
    const bool restored=event==Event::Load && savedSettings.has_value();
    const auto preferences=tds::settingsForLoad(savedSettings,event==Event::Load,defaults);
    panel::restoreSettings(preferences);
    auraEnabled=preferences[7]!=0;
    savedSettings.reset();
    syncAura();
    logFile << "TDS_SETTINGS event=APPLY source=" << (restored ? "CO_SAVE" : "DEFAULTS")
            << " hotkey=" << preferences[0] << " hud=" << preferences[1]
            << " layout=" << tds::hudLayout(preferences) << " aura=" << preferences[7] << '\n';

    requestRead(event);

}



constexpr unsigned hitTraceLimit = 200;

std::atomic_uint hitTraceCount{};

std::atomic_uint hitEventCount{};

thread_local unsigned hitTraceInChain{};



class Events final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>, public RE::BSTEventSink<RE::TESSpellCastEvent>, public RE::BSTEventSink<RE::TESHitEvent>, public RE::BSTEventSink<RE::TESEquipEvent>,

                     public RE::BSTEventSink<SKSE::ModCallbackEvent> {

public:

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,

        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override

    {

        if (event && !event->opening && sessionReady && event->menuName==RE::SleepWaitMenu::MENU_NAME) {

            const auto generation=session;

            SKSE::GetTaskInterface()->AddTask([generation] {

                if (generation!=session || !sessionReady) return;

                if (const auto* calendar=RE::Calendar::GetSingleton()) {

                    progression.expire(calendar->GetHoursPassed());

                    logFile << "TDS_QA_WAIT game_h=" << calendar->GetHoursPassed()

                            << " effective_h=" << progression.clockHours(calendar->GetHoursPassed())

                            << " blocks=" << progression.current().satisfaction.records().size() << '\n';

                }

                reconcile();

                panel::publish();

            });

        }

        return RE::BSEventNotifyControl::kContinue;

    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* event,

        RE::BSTEventSource<RE::TESSpellCastEvent>*) override

    {

        static unsigned samples{};

        auto* player=RE::PlayerCharacter::GetSingleton();

        if (sessionReady && event && player && player->Is3DLoaded() && !player->IsDead() && event->object.get()==player && offeringPower && event->spell==offeringPower->GetFormID()) {

            if (const auto* calendar=RE::Calendar::GetSingleton()) progression.expire(calendar->GetHoursPassed());

            const auto before=prog::appliedSatisfaction(progression.current().satisfaction.total());

            const int goldRequested=prog::offeringGold(progression.current().nodes,before);

            auto* data=RE::TESDataHandler::GetSingleton();

            auto* gold=goldRequested>0 && data ? data->LookupForm<RE::TESObjectMISC>(0xF,"Skyrim.esm") : nullptr;

            if (goldRequested>0 && !gold) {

                logFile << "TDS_OFFERING status=GOLD_FORM_MISSING x_unchanged=" << before << '\n';

                return RE::BSEventNotifyControl::kContinue;

            }

            const int goldBefore=gold ? player->GetItemCount(gold) : 0;

            if (goldRequested>0 && goldBefore>std::numeric_limits<int>::max()-goldRequested) return RE::BSEventNotifyControl::kContinue;

            const auto gain=player->HasSpell(offeringPower) ? progression.offerSatisfaction() : -1;

            if (gain>=0 && gold && goldRequested>0) player->AddObjectToContainer(gold,nullptr,goldRequested,nullptr);

            logFile << "TDS_OFFERING source=" << (progression.developerMode()?"QA":"LIVE") << " x_before=" << before

                    << " k_gain=" << gain << " gold_requested=" << goldRequested

                    << " gold_observed=" << (gold?player->GetItemCount(gold)-goldBefore:0) << " x_after=" << prog::appliedSatisfaction(progression.current().satisfaction.total()) << '\n';

            if (gain>=0) {reconcile();panel::publish();}

            return RE::BSEventNotifyControl::kContinue;

        }

        if (samples>=120 || !sessionReady || !event || !player || event->object.get()!=player) {

            return RE::BSEventNotifyControl::kContinue;

        }

        const auto* spell=RE::TESForm::LookupByID<RE::SpellItem>(event->spell);

        const auto* av=player->AsActorValueOwner();

        if (spell && av && spell->GetSpellType()==RE::MagicSystem::SpellType::kSpell) {

            ++samples;

            logFile << "TDS_QA_CAST spell=" << event->spell << " engine_cost=" << spell->CalculateMagickaCost(player)

                    << " magicka_at_event=" << av->GetActorValue(RE::ActorValue::kMagicka) << '\n';

        }

        return RE::BSEventNotifyControl::kContinue;

    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event,

        RE::BSTEventSource<RE::TESHitEvent>*) override

    {

        auto* player = RE::PlayerCharacter::GetSingleton();

        if (event && player && event->cause.get() == player) {

            const auto id = ++hitEventCount;

            if (id <= hitTraceLimit) {

                logFile << "TDS_HIT_EVENT seq=" << id << " hook_seq=" << hitTraceInChain

                        << " source=" << event->source << " projectile=" << event->projectile

                        << " flags=" << static_cast<unsigned>(event->flags.underlying()) << '\n';

            }

        }

        return RE::BSEventNotifyControl::kContinue;

    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,

        RE::BSTEventSource<RE::TESEquipEvent>*) override

    {

        if (event && sessionReady && event->actor.get() == RE::PlayerCharacter::GetSingleton() &&

            !refreshQueued) {

            refreshQueued = true;

            const auto generation = session;

            SKSE::GetTaskInterface()->AddTask([generation] {

                if (generation == session) {

                    refreshQueued = false;

                    reconcile();

                    panel::publish();

                }

            });

        }

        return RE::BSEventNotifyControl::kContinue;

    }



    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,

        RE::BSTEventSource<SKSE::ModCallbackEvent>*) override

    {

        if (event && event->eventName=="TDSSceneRequest") {
            const std::string info=event->strArg.empty()?"":event->strArg.c_str();
            const float thread=event->numArg;
            SKSE::GetTaskInterface()->AddTask([info,thread] {
                logFile << "TDS_SCENE_REQUEST " << info << " thread=" << thread
                    << " qa=" << progression.developerMode() << '\n';
            });
        }
        if (event && event->eventName == "PlayerAnimationEnd") {

            SKSE::GetTaskInterface()->AddTask([] { requestRead(Event::SceneEnd); });

        }

        return RE::BSEventNotifyControl::kContinue;

    }

};



Events events;



// Skyrim SE call-site and ABI match TOAM WeaponHitSource and POT Combat.



bool isDirectRangedHit(const RE::TESObjectWEAP& weapon, RE::FormID source, RE::FormID projectile)

{

    if (!projectile || (!weapon.IsBow() && !weapon.IsCrossbow())) return false;

    const auto* projectileBase=RE::TESForm::LookupByID<RE::BGSProjectile>(projectile);

    if (!projectileBase) {

        const auto* projectileRef=RE::TESForm::LookupByID<RE::Projectile>(projectile);

        if (projectileRef) projectileBase=projectileRef->GetProjectileBase();

    }

    if (!projectileBase || !projectileBase->IsArrow()) return false;

    if (source==weapon.GetFormID()) return true;

    const auto* ammo=RE::TESForm::LookupByID<RE::TESAmmo>(source);

    return ammo && ammo->GetRuntimeData().data.projectile==projectileBase;

}



template <class Commit>
void processHit(RE::NiPointer<RE::TESObjectREFR>& target,
                RE::NiPointer<RE::TESObjectREFR>& aggressor, RE::FormID source,
                RE::FormID projectile, RE::HitData& data, Commit commit)

{

    auto* player = RE::PlayerCharacter::GetSingleton();

    auto* victim = target ? target->As<RE::Actor>() : nullptr;

    using Flag = RE::HitData::Flag;

    thread_local bool inside{};

    const bool outer = !inside;

    const bool playerHit = outer && player && player == aggressor.get();

    const unsigned traceId = playerHit ? ++hitTraceCount : 0;

    const bool trace = traceId && traceId <= hitTraceLimit;

    const float physicalBefore = data.physicalDamage;

    const float totalBefore = data.totalDamage;

    const auto flags = data.flags.underlying();

    const auto targetId = target ? target->GetFormID() : 0;

    const auto hitWeaponId = data.weapon ? data.weapon->GetFormID() : 0;

    const auto* attack = outer ? data.attackData.get() : nullptr;

    using AttackFlag = RE::AttackData::AttackFlag;

    const bool attackExcluded = attack &&

        attack->data.flags.any(AttackFlag::kBashAttack, AttackFlag::kIgnoreWeapon);

    const bool ranged = outer && data.weapon && isDirectRangedHit(*data.weapon,source,projectile);

    const bool armedMelee = data.weapon && data.weapon->IsMelee() && !data.weapon->IsHandToHandMelee();

    const bool directWeapon = !attackExcluded && (ranged ||

        !rejectWeaponOrigin(armedMelee,projectile,source,hitWeaponId,false));

    Hand hand = Hand::Mismatch;

    std::optional<Group> group;

    Target applied{};

    float fixedAdded{};

    float percentAdded{};

    prog::MeleeCommit growthPreview{};

    bool observedHit{};

    prog::MeleeGrowth growth{};

    bool eligible{};



    using AV = RE::ActorValue;

    using Modifier = RE::ACTOR_VALUE_MODIFIERS;

    float healthBefore = NAN;

    float healthAfter = NAN;

    float healthAfterFlat = NAN;

    float attackerHpBefore = NAN;

    float attackerHpAfter = NAN;

    float healApplied{};

    int healRequested{};

    const char* fixedStatus = "INELIGIBLE";

    const char* healStatus = "INELIGIBLE";

    RE::NiPointer<RE::TESObjectREFR> pinnedTarget;

    RE::NiPointer<RE::TESObjectREFR> pinnedAttacker;

    const auto* verdict = "OTHER";

    if (playerHit) {

        verdict = [&]() -> const char* {

            const int attackCount=effectiveCombatCount.load(std::memory_order_relaxed);

            if (!formsReady || attackCount<0) {

                return "NOT_READY";

            }

            if (!victim) {

                return "TARGET_NOT_ACTOR";

            }

            if (victim == player) {

                return "TARGET_IS_PLAYER";

            }

            if (victim->IsDead()) {

                return "TARGET_ALREADY_DEAD";

            }

            if (victim->IsGhost() || !victim->GetActorBase() || victim->GetActorBase()->IsInvulnerable()) {

                return "TARGET_INVULNERABLE_OR_UNKNOWN";

            }

            if (!data.weapon) {

                return "NO_HIT_WEAPON";

            }



            if (data.flags.any(Flag::kBlocked, Flag::kBash, Flag::kPredictDamage, Flag::kExplosion)) {

                return "EXCLUDED_FLAGS";

            }

            if (!std::isfinite(totalBefore) || !std::isfinite(physicalBefore) ||

                totalBefore <= 0.0f || physicalBefore <= 0.0f) {

                return "NONPOSITIVE_OR_INVALID_DAMAGE";

            }

            if (!directWeapon) return "UNVERIFIED_WEAPON_ORIGIN";



            const bool leftEvidence = data.flags.any(Flag::kLeftHand) || (attack && attack->IsLeftAttack());

            auto* rightEntry = player->GetEquippedEntryData(false);

            auto* leftEntry = player->GetEquippedEntryData(true);

            const bool rightMatches=rightEntry && rightEntry->object==data.weapon;

            const bool leftMatches=leftEntry && leftEntry->object==data.weapon;

            hand = ranged && rightMatches && !leftMatches ? Hand::Right :

                attackHand(leftEvidence, attack != nullptr,rightMatches,leftMatches);

            if (hand == Hand::Ambiguous) {

                return "AMBIGUOUS_ATTACK_HAND";

            }

            if (hand == Hand::Mismatch) {

                return "HAND_WEAPON_MISMATCH";

            }

            const bool left = hand == Hand::Left;

            group = weaponGroup(left ? leftEntry : rightEntry, left);

            growth=liveMelee.load(std::memory_order_relaxed);

            if (!group && !trace && growth.flat==0.0f && growth.steal==0) return "NO_APPLICABLE_EFFECT";

            if (group) applied=calculate(*group,attackCount);

            const auto& bonus = applied.weapon;

            fixedAdded = armedMelee ? static_cast<float>(bonus.flatDamage) : 0.0f;

            if (!armedMelee) growth.flat=0.0f;

            percentAdded = physicalBefore * static_cast<float>(bonus.damagePercent) * 0.01f;

            if (!std::isfinite(physicalBefore + percentAdded) || !std::isfinite(totalBefore + percentAdded)) {

                group.reset();

                return "PERCENT_OVERFLOW";

            }

            pinnedTarget = target;

            pinnedAttacker = aggressor;

            eligible=true;

            if (trace || growth.flat!=0.0f || growth.steal!=0) {

                const auto* values = victim->AsActorValueOwner();

                if (values) healthBefore = values->GetActorValue(AV::kHealth);

            }

            data.physicalDamage += percentAdded;

            data.totalDamage += percentAdded;

            // Signed modifiers affect only the TDS melee additional pool, never vanilla damage.

            return percentAdded > 0.0f ? "PERCENT_EDITED_COMMIT_UNVERIFIED" : "COMMIT_UNVERIFIED";

        }();

    }



    if (outer && player && victim==player && aggressor.get()!=player && directWeapon &&

        !data.flags.any(Flag::kBash,Flag::kPredictDamage,Flag::kExplosion) &&

        std::isfinite(totalBefore) && std::isfinite(physicalBefore) &&

        totalBefore>0.0f && physicalBefore>0.0f && aggressor &&

        tds::spell_native::resists(aggressor->As<RE::Actor>())) {

        data.totalDamage *= 0.5f;

    }

    const float physicalSent = data.physicalDamage;

    const float totalSent = data.totalDamage;

    if (outer) {

        hitTraceInChain = trace ? traceId : 0;

    }

    inside = true;

    commit();

    if (eligible) {

        const auto secondary = [&]() -> const char* {

            if (target != pinnedTarget || aggressor != pinnedAttacker ||

                RE::PlayerCharacter::GetSingleton() != player || !victim->Is3DLoaded() ||

                !player->Is3DLoaded() || player->IsDead() || victim->IsDead() || victim->IsGhost() ||

                victim->IsEssential() || victim->IsProtected() || victim->IsInKillMove() ||

                !victim->GetActorBase() || victim->GetActorBase()->IsInvulnerable() ||

                data.flags.any(Flag::kBlocked, Flag::kFatal)) {

                return "POST_CALL_UNSAFE";

            }

            auto* values = victim->AsActorValueOwner();

            if (!values) return "TARGET_AV_MISSING";

            healthAfter = values->GetActorValue(AV::kHealth);

            if (!std::isfinite(healthAfter) || healthAfter <= 0.0f) return "TARGET_HP_UNSAFE";

            observedHit=std::isfinite(healthBefore) && healthAfter<healthBefore;

            // Authorization comes from this validated direct weapon hit.

            // The event dispatcher returns before vanilla commits weapon HP loss.

            // Apply the signed net through the same exact-HP path as gear.

            growthPreview=prog::planMeleeCommit(fixedAdded,applied.weapon.lifeSteal,growth,true);

            const float resolvedFlat=growthPreview.damage;

            if (healthAfter <= resolvedFlat) return "FLAT_POTENTIALLY_LETHAL";



            fixedStatus = "ZERO";

            if (resolvedFlat > 0.0f) {

                // POT exact Health damage. Keep the TDS recursion guard set until recovery completes.

                values->ModActorValue(Modifier::kDamage, AV::kHealth, -resolvedFlat);

                healthAfterFlat = values->GetActorValue(AV::kHealth);

                fixedStatus = "AV_WRITE";

                if (!std::isfinite(healthAfterFlat) || healthAfterFlat >= healthAfter) {

                    return "TARGET_HP_WRITE_NOT_OBSERVED";

                }

            }



            healRequested=growthPreview.steal;

            const float requested=static_cast<float>(healRequested);

            if (requested <= 0.0f) return "ZERO";

            auto* attackerValues = player->AsActorValueOwner();

            if (!attackerValues || player->IsDead()) return "ATTACKER_AV_UNSAFE";

            attackerHpBefore = attackerValues->GetActorValue(AV::kHealth);

            const float before = attackerHpBefore;

            const float maximum = attackerValues->GetPermanentActorValue(AV::kHealth) +

                player->GetActorValueModifier(Modifier::kTemporary, AV::kHealth);

            if (!std::isfinite(before) || !std::isfinite(maximum) || before <= 0.0f) return "ATTACKER_HP_UNSAFE";

            if (before >= maximum) return "AT_MAX_HP";

            healApplied = (std::min)(requested, maximum - before);

            attackerValues->RestoreActorValue(AV::kHealth, healApplied);

            attackerHpAfter = attackerValues->GetActorValue(AV::kHealth);

            return "AV_WRITE";

        };

        healStatus = secondary();

        if (std::string_view(fixedStatus) == "INELIGIBLE") fixedStatus = healStatus;

    }

    if (outer) {

        inside = false;

        hitTraceInChain = 0;

    }

    if (trace) {

        logFile << "TDS_HIT seq=" << traceId << " verdict=" << verdict

                << " target=" << targetId << " source=" << source << " projectile=" << projectile

                << " hit_weapon=" << hitWeaponId << " flags=" << flags

                << " melee_bit=" << ((flags & static_cast<std::uint32_t>(Flag::kMeleeAttack)) != 0)

                << " attack_data=" << (attack != nullptr)

                << " hand=" << (hand == Hand::Left ? "L" : hand == Hand::Right ? "R" : "UNKNOWN")

                << " group=" << (group ? names[static_cast<std::size_t>(*group)] : "NONE")

                << " count=" << effectiveCombatCount.load(std::memory_order_relaxed) << " stage=" << applied.stage

                << " physical_in=" << physicalBefore << " flat=" << fixedAdded

                << " growth_observed=" << observedHit << " growth_offer_flat=" << growth.flat

                << " growth_offer_steal=" << growth.steal << " preview_flat=" << growthPreview.damage

                << " preview_steal=" << growthPreview.steal << " exact_growth_offer=" << growth.flat

                << " fixed_policy=MELEE_ADDITIONAL_POOL_MIN_ZERO melee=" << armedMelee

                << " pct=" << applied.weapon.damagePercent << " pct_added=" << percentAdded

                << " physical_sent=" << physicalSent << " physical_return=" << data.physicalDamage

                << " total_in=" << totalBefore << " total_sent=" << totalSent

                << " total_return=" << data.totalDamage << " steal_added=0"

                << " hp_before_chain=" << healthBefore << " hp_after_chain=" << healthAfter

                << " hp_loss_in_chain=" << healthBefore - healthAfter

                << " fixed_commit=" << fixedStatus << " hp_after_flat=" << healthAfterFlat

                << " fixed_observed=" << healthAfter - healthAfterFlat

                << " heal_commit=" << healStatus << " heal_requested=" << healRequested

                << " heal_applied=" << healApplied << " attacker_hp_before=" << attackerHpBefore

                << " attacker_hp_after=" << attackerHpAfter

                << " heal_observed=" << attackerHpAfter - attackerHpBefore << '\n';

    }

}

// SE event dispatcher and AE AIProcess have different function signatures.
// Keep one gameplay pipeline, but always forward the exact original ABI.
void onHit(RE::ScriptEventSourceHolder* holder, RE::NiPointer<RE::TESObjectREFR>& target,
           RE::NiPointer<RE::TESObjectREFR>& aggressor, RE::FormID source,
           RE::FormID projectile, RE::HitData& data)
{
    processHit(target,aggressor,source,projectile,data,[&]{
        originalHit(holder,target,aggressor,source,projectile,data);
    });
}

void onAeHit(RE::AIProcess* process, RE::HitData& data)
{
    // The process owner is authoritative; handle resolution pins references
    // across the original call and the common post-hit validation.
    auto* owner=process ? process->GetUserData() : nullptr;
    RE::NiPointer<RE::TESObjectREFR> target{owner};
    const auto actor=data.aggressor.get();
    RE::NiPointer<RE::TESObjectREFR> aggressor{actor.get()};
    const RE::FormID source=data.weapon ? data.weapon->GetFormID() : 0;
    const auto sourceRef=data.sourceRef.get();
    const auto* projectileRef=sourceRef ? sourceRef->As<RE::Projectile>() : nullptr;
    const auto* projectileBase=projectileRef ? projectileRef->GetProjectileBase() : nullptr;
    const RE::FormID projectile=projectileBase ? projectileBase->GetFormID() : 0;
    if (!owner) {
        originalAeHit(process,data);
        return;
    }
    processHit(target,aggressor,source,projectile,data,[&]{originalAeHit(process,data);});
}

static_assert(std::is_same_v<decltype(&onHit),HitCall*>);
static_assert(std::is_same_v<decltype(&onAeHit),AeHitCall*>);

void installWeaponHit()

{

    const auto runtime=REL::Module::get().version();

    const auto site=tds::runtime::hitSite({runtime[0],runtime[1],runtime[2],runtime[3]});

    if (!site.id) {

        logFile << "TDS_HOOK status=SKIPPED_RUNTIME runtime=" << runtime.string() << '\n';

        return;

    }

    const REL::Relocation<std::uintptr_t> anchor{REL::ID(site.id), static_cast<std::ptrdiff_t>(site.offset)};

    if (*reinterpret_cast<const std::uint8_t*>(anchor.address()) != 0xE8) {

        logFile << "TDS_HOOK status=SKIPPED_CALL_OPCODE\n";

        return;

    }

    SKSE::AllocTrampoline(32);

    if (site.ae) originalAeHit=SKSE::GetTrampoline().write_call<5>(anchor.address(),onAeHit);
    else originalHit=SKSE::GetTrampoline().write_call<5>(anchor.address(),onHit);

    logFile << "TDS_HOOK status=CHAIN_INSTALLED direct_weapon=1 runtime=" << runtime.string()
            << " abi=" << (site.ae ? "AE_AIProcess" : "SE_EventDispatcher")
            << " id=" << site.id << " offset=" << site.offset << '\n';

}



void developerInput(std::string_view payload)

{

    if (!progression.developerMode() || payload.size()<3 || payload[1]!=':') return;

    const char operation=payload.front();

    payload.remove_prefix(2);
    if (operation=='R') {
        if (payload=="0" && progression.resetMockStatistics())
            logFile << "TDS_QA status=VIRTUAL_STATISTICS_RESET counters=0 x=0 k=0 spent=0\n";
        return;
    }


    if (const auto* calendar=RE::Calendar::GetSingleton()) progression.expire(calendar->GetHoursPassed());

    const auto consume=[&](int& value) {

        if (payload.empty()) return false;

        const auto comma=payload.find(',');

        const auto field=payload.substr(0,comma);

        const auto end=field.data()+field.size();

        const auto [ptr,ec]=std::from_chars(field.data(),end,value);

        if (ec!=std::errc{} || ptr!=end) return false;

        payload=comma==std::string_view::npos ? std::string_view{} : payload.substr(comma+1);

        return true;

    };

    if (payload.back()==',') return;

    int value{};

    if (operation=='X' || operation=='K' || operation=='H') {

        if (!consume(value) || !payload.empty()) return;

        if (operation=='X') progression.setMockSatisfaction(value);

        else if (operation=='K') progression.setMockCorruption(value);

        else progression.advanceMockHours(value);

    } else if (operation=='C') {

        int slot{};

        if (!consume(slot) || !consume(value) || !payload.empty() ||

            slot<0 || slot>=static_cast<int>(prog::CounterCount)) return;

        auto counters=*progression.current().stats;

        counters[slot]=value;

        progression.setMockCounters(counters);

    } else if (operation=='S') {

        prog::Counters scene{};

        for (auto& increment : scene) if (!consume(increment)) return;

        if (!payload.empty()) return;

        if (!prog::validMockScene(scene) || !progression.mockScene(scene)) {

            logFile << "TDS_QA_SCENE result=REJECTED\n";

            return;

        }

        logFile << "TDS_QA_SCENE policy=CR2_45000 result=APPLIED delta=";

        for (const int value : scene) logFile << value << ',';

        const auto& state=progression.current();

        logFile << " x=" << state.satisfaction.total() << " k=" << state.corruption << '\n';

    }

}



void onMessage(SKSE::MessagingInterface::Message* message)

{

    if (!message) {

        return;

    }

    using M = SKSE::MessagingInterface;

    static bool extendedEslReady=false;

    if (message->type!=M::kDataLoaded && !extendedEslReady) return;

    switch (message->type) {

    case M::kDataLoaded: {

        const auto v=REL::Module::get().version();
        const tds::runtime::Version version{v[0],v[1],v[2],v[3]};
        const bool beesLoaded=!tds::runtime::nativeExtendedEsl(version) &&
            GetModuleHandleW(L"BackportedESLSupport.dll")!=nullptr;
        const auto esl=tds::runtime::eslSupport(version,beesLoaded);
        extendedEslReady=esl!=tds::runtime::EslSupport::Missing;

        if (!extendedEslReady) {

            logFile << "TDS_INIT status=BEES_MODULE_MISSING required_for=ESPFE_1_71 runtime="
                    << v.string() << '\n';

            return;

        }

        logFile << "TDS_INIT status=ESL_SUPPORT_READY source="
                << (esl==tds::runtime::EslSupport::Native ? "NATIVE_ENGINE" : "BEES")
                << " runtime=" << v.string() << '\n';

        providerSha = tdsFileSha(L"Data/Scripts/sslActorStats.pex");

        inspectDynamicFiles();

        formsReady = resolveForms();

        if (formsReady) {

            auto* data=RE::TESDataHandler::GetSingleton();

            logFile << "TDS_COST forms_verified=" << tds::engine_effects::resolveCost(*data,esp) << '\n';

            logFile << "TDS_GROWTH_AV forms_verified=" << resolveGrowthAbilities(*data) << '\n';

        }

        if (auto* data=RE::TESDataHandler::GetSingleton()) {

            for (unsigned i=0;i<auraArts.size();++i) auraArts[i]=data->LookupForm<RE::BGSArtObject>(0xFF0+i,esp);

            auraReady=std::ranges::all_of(auraArts,[](auto* art){return art!=nullptr;});

        }

        if (auto* data=RE::TESDataHandler::GetSingleton()) {

            logFile << "TDS_RESOURCE forms_verified=" << resolveResourcePenalty(*data) << '\n';

        }

        logFile << "TDS_INCOMING chain_installed=" << tds::engine_effects::installDamage() << '\n';

        logFile << "TDS_SPELL_NATIVE chain_installed=" << tds::spell_native::install() << '\n';

        panel::install(panelSnapshot, [](panel::Action action, unsigned node, std::string_view payload) {

            if (!sessionReady) return;

            if (progression.developerMode()) {

                if (action==panel::Action::Recheck) {

                    reconcile();

                    panel::publish();

                    return;

                }

                if (action==panel::Action::HardcoreOn || action==panel::Action::HardcoreOff) {

                    mockHardcore=action==panel::Action::HardcoreOn;

                    panel::publish();

                    return;

                }

            }

            const bool canEdit=progression.developerMode() ||

                (!progressionRecordInvalid && !statsGap && lastStatus==ReadStatus::Valid);

            switch (action) {

            case panel::Action::GearGrant:
                grantEquipment(node);
                panel::publish();
                return;
            case panel::Action::DeveloperEnter:

                if (!prog::developerPasswordAccepted(payload)) {

                    logFile << "TDS_QA status=PASSWORD_REJECTED\n";

                    return;

                }

                if (!progression.developerMode()) {

                    auto* player=RE::PlayerCharacter::GetSingleton();

                    if (!player || !player->Is3DLoaded() || !armDeveloperSaveFlag(*player)) {

                        logFile << "TDS_QA status=SAVE_GUARD_UNAVAILABLE\n";

                        return;

                    }

                    mockHardcore=hardcore.enabled;

                    mockGearClaims=gearClaims;
                    progression.enterDeveloperMode();

                    if (const auto* calendar=RE::Calendar::GetSingleton()) progression.expire(calendar->GetHoursPassed());

                    expiryArmed=false;

                    liveMelee.store({},std::memory_order_relaxed);

                    reader.request_stop();

                    readActive=false;

                    endQueued=false;

                    queuedUiRead.reset();

                    reconcile();

                }

                break;

            case panel::Action::AuraOn:

            case panel::Action::AuraOff:

                auraEnabled=auraReady && action==panel::Action::AuraOn;

                syncAura(!auraEnabled);

                break;

            case panel::Action::DeveloperInput:

                if (const auto* clock=RE::Calendar::GetSingleton()) progression.expire(clock->GetHoursPassed());

                developerInput(payload);

                reconcile();

                break;

            case panel::Action::Open:

                if (progression.developerMode()) reconcile();

                else requestRead(Event::UiOpen);

                break;

            case panel::Action::Refresh: requestRead(Event::Refresh); break;

            case panel::Action::Recheck: inspectDynamicFiles(); requestRead(Event::Recheck); break;

            case panel::Action::HardcoreOn:

                hardcore.setEnabled(true);

                requestRead(Event::Recheck);

                break;

            case panel::Action::HardcoreOff: hardcore.setEnabled(false); break;

            case panel::Action::NodesOn:

            case panel::Action::NodesOff:

                if (!progressionRecordInvalid || progression.developerMode()) progression.setNodesEnabled(action == panel::Action::NodesOn);

                break;

            case panel::Action::NodeBuy:

                if (canEdit && node<prog::nodeCosts.size()) {

                    const auto before=progression.current().nodes.spent();

                    progression.buy(static_cast<prog::Node>(node));

                    logFile << "TDS_NODE purchase=" << node << " spent_before=" << before

                        << " spent_after=" << progression.current().nodes.spent()

                        << " qa=" << progression.developerMode() << '\n';

                }

                break;

            case panel::Action::NodesReset:

                if (canEdit) {

                    progression.resetAll();

                }

                break;

            }

            if (action == panel::Action::NodesOn || action == panel::Action::NodesOff ||

                action == panel::Action::NodesReset || action == panel::Action::NodeBuy) {

                if (auto* player=RE::PlayerCharacter::GetSingleton()) reconcileGrowthAbilities(*player);

                updateExpiryArm();

            }

            panel::publish();

        }, [](const char* text) { logFile << text << '\n'; });

        logFile << "TDS_INIT forms_ready=" << formsReady << " stats_pex_sha="

                << (providerSha.empty() ? "UNAVAILABLE" : providerSha) << '\n';

        if (auto* source=RE::ScriptEventSourceHolder::GetSingleton()) {

            source->AddEventSink<RE::TESEquipEvent>(&events);

            source->AddEventSink<RE::TESHitEvent>(&events);

            source->AddEventSink<RE::TESSpellCastEvent>(&events);

        }

        if (auto* ui=RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&events);

        tds::engine_effects::traceDamage=[](float before,float after) {

            logFile << "TDS_QA_INCOMING input=" << before << " sent=" << after << '\n';

        };

        tds::spell_native::traceSpell=[](float before,float after) {

            logFile << "TDS_QA_SPELL_HP input=" << before << " sent=" << after << '\n';

        };

        logFile << "TDS_HIT_TRACE per_stream_limit=" << hitTraceLimit << '\n';

        if (auto* source=SKSE::GetModCallbackEventSource()) source->AddEventSink(&events);

        if (formsReady) {

            installWeaponHit();

        }

        break;

    }

    case M::kPreLoadGame:

        tds::engine_effects::incomingRate.store(0,std::memory_order_relaxed);
    if (tds::engine_effects::costValue) tds::engine_effects::costValue->data=1.0f;

        tds::spell_native::setBonus({});

        tds::spell_native::setResistance({});

        effectiveCombatCount.store(-1,std::memory_order_relaxed);

        if (progression.developerMode()) {

            if (auto* player=RE::PlayerCharacter::GetSingleton()) {

                reconcileGrowthAbilities(*player,true);

                for (auto*& spell : owned) {

                    if (spell && player->HasSpell(spell)) player->RemoveSpell(spell);

                    spell=nullptr;

                }

            }

        }

        auraEnabled=false;

        syncAura(true);

        savedGearClaims.reset();
        gearClaims=mockGearClaims=0;
        savedSettings.reset();
        savedHardcore.reset();

        savedProgression.reset();

        progressionRecordInvalid=false;

        liveMelee.store({},std::memory_order_relaxed);

        panel::resetSession();

        sessionReady = false;

        reader.request_stop();

        break;

    case M::kPostLoadGame:

        if (message->data) {

            startSession(Event::Load);

        }

        break;

    case M::kNewGame:

        startSession(Event::NewGame);

        break;

    default:

        break;

    }

}

}  // namespace



// AE SKSE reads this exported metadata before calling SKSEPlugin_Load.
// CommonLib NG runtime accessors handle pre/post-629 object layout differences.
SKSEPluginVersion = [] {
    SKSE::PluginVersionData data{};
    data.PluginVersion(REL::Version{1,0,4,0});
    data.PluginName("SexLabLifetimeProgression");
    data.AuthorName("TULL");
    data.UsesAddressLibrary();
    data.UsesNoStructs(); // NG StructCompatibility::Independent, as in POT.
    for (std::size_t i=0;i<tds::runtime::supported.size();++i) {
        const auto v=tds::runtime::supported[i];
        data.compatibleVersions[i]=REL::Version{static_cast<std::uint16_t>(v.major),
            static_cast<std::uint16_t>(v.minor),static_cast<std::uint16_t>(v.build),
            static_cast<std::uint16_t>(v.revision)}.pack();
    }
    return data;
}();

SKSE_EXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface* skse, SKSE::PluginInfo* info)

{

    info->infoVersion = SKSE::PluginInfo::kVersion;

    info->name = "SexLabLifetimeProgression";

    info->version = pluginVersion.pack();

    return !skse->IsEditor() && acceptsRuntime(skse->RuntimeVersion());

}



SKSEPluginLoad(const SKSE::LoadInterface* skse)

{

    if (!acceptsRuntime(skse->RuntimeVersion())) {

        return false;

    }

    SKSE::Init(skse);

    const auto dir = SKSE::log::log_directory();

    auto* messaging = SKSE::GetMessagingInterface();

    if (!dir || !messaging || !SKSE::GetTaskInterface() || !SKSE::GetModCallbackEventSource()) {

        return false;

    }

    logFile.open(*dir / "SexLabLifetimeProgression.log", std::ios::trunc);

    if (!logFile) {

        return false;

    }

    logFile << std::unitbuf << "TDS version=1.0.4 runtime=" << skse->RuntimeVersion().string()
            << " ESPFE_1_71 SE_AE_MULTI_RUNTIME MELEE_GROWTH_REQUIRE_OBSERVED_LOSS\n";

    logger = std::make_unique<StatLogger>(logFile);

    const auto* serial = SKSE::GetSerializationInterface();

    if (!serial || !messaging->RegisterListener(onMessage)) return false;

    serial->SetUniqueID(serializationId);

    serial->SetSaveCallback(saveHardcore);

    serial->SetLoadCallback(loadHardcore);

    serial->SetRevertCallback(revertHardcore);

    startExpiryService();

    return true;

}
