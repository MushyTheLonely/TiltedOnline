#ifdef TP_SKYRIM

#include <Games/References.h>
#include <Games/Skyrim/EquipManager.h>
#include <Games/Skyrim/Misc/ActorProcessManager.h>
#include <Games/Skyrim/Misc/MiddleProcess.h>
#include <Games/Skyrim/Misc/SkyrimVM.h>
#include <Games/Skyrim/DefaultObjectManager.h>
#include <Games/Skyrim/Forms/TESNPC.h>
#include <Games/Skyrim/Forms/TESFaction.h>
#include <Games/Skyrim/Components/TESActorBaseData.h>
#include <Games/Skyrim/ExtraData/ExtraFactionChanges.h>
#include <Games/Memory.h>
#include <Games/RTTI.h>

#include <World.h>
#include <Services/PapyrusService.h>

#ifdef SAVE_STUFF

#include <Games/Skyrim/SaveLoad.h>

void Actor::Save_Reversed(const uint32_t aChangeFlags, Buffer::Writer& aWriter)
{
    BGSSaveFormBuffer buffer;

    Save(&buffer);

    ActorProcessManager* pProcessManager = processManager;
    const int32_t handlerId = pProcessManager != nullptr ? pProcessManager->handlerId : -1;

    aWriter.WriteBytes((uint8_t*)&handlerId, 4); // TODO: is this needed ?
    aWriter.WriteBytes((uint8_t*)&flags1, 4);

    //     if (!handlerId
//         && (uint8_t)ActorProcessManager::GetBoolInSubStructure(pProcessManager))
//     {
//         Actor::SaveSkinFar(this);
//     }


    TESObjectREFR::Save_Reversed(aChangeFlags, aWriter);

    if (pProcessManager); // Skyrim saves the process manager state, but we don't give a shit so skip !

    aWriter.WriteBytes((uint8_t*)&unk194, 4);
    aWriter.WriteBytes((uint8_t*)&headTrackingUpdateDelay, 4);
    aWriter.WriteBytes((uint8_t*)&unk9C, 4);
	// We skip 0x180 as it's not something we care about, some timer related data
   


    aWriter.WriteBytes((uint8_t*)&unk98, 4);
	// skip A8 - related to timers 
	// skip AC - related to timers as well
    aWriter.WriteBytes((uint8_t*)&unkB0, 4);
	// skip E4 - never seen this used
	// skip E8 - same as E4
    aWriter.WriteBytes((uint8_t*)&unk84, 4);
    aWriter.WriteBytes((uint8_t*)&unkA4, 4);
	// skip baseForm->weight
	// skip 12C

	// Save actor state sub_6F0FB0
}

#endif

TP_THIS_FUNCTION(TCharacterConstructor, Actor*, Actor);
TP_THIS_FUNCTION(TCharacterConstructor2, Actor*, Actor, uint8_t aUnk);
TP_THIS_FUNCTION(TCharacterDestructor, Actor*, Actor);

TCharacterConstructor* RealCharacterConstructor;
TCharacterConstructor2* RealCharacterConstructor2;
TCharacterDestructor* RealCharacterDestructor;

Actor* TP_MAKE_THISCALL(HookCharacterConstructor, Actor)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    ThisCall(RealCharacterConstructor, apThis);

    return apThis;
}

Actor* TP_MAKE_THISCALL(HookCharacterConstructor2, Actor, uint8_t aUnk)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    ThisCall(RealCharacterConstructor2, apThis, aUnk);

    return apThis;
}

Actor* TP_MAKE_THISCALL(HookCharacterDestructor, Actor)
{
    TP_EMPTY_HOOK_PLACEHOLDER;

    auto pExtension = apThis->GetExtension();

    if(pExtension)
    {
        pExtension->~ActorExtension();
    }

    ThisCall(RealCharacterDestructor, apThis);

    return apThis;
}

GamePtr<Actor> Actor::New() noexcept
{
    const auto pActor = Memory::Allocate<Actor>();

    ThisCall(RealCharacterConstructor, pActor);

    return pActor;
}


TESForm* Actor::GetEquippedWeapon(uint32_t aSlotId) const noexcept
{
    if (processManager && processManager->middleProcess)
    {
        auto pMiddleProcess = processManager->middleProcess;

        if (aSlotId == 0 && pMiddleProcess->leftEquippedObject)
            return *(pMiddleProcess->leftEquippedObject);

        else if (aSlotId == 1 && pMiddleProcess->rightEquippedObject)
            return *(pMiddleProcess->rightEquippedObject);

    }

    return nullptr;
}

Inventory Actor::GetInventory() const noexcept
{
    auto& modSystem = World::Get().GetModSystem();

    Inventory inventory;
    inventory.Buffer = SerializeInventory();

    auto pMainHandWeapon = GetEquippedWeapon(0);
    uint32_t mainId = pMainHandWeapon ? pMainHandWeapon->formID : 0;
    modSystem.GetServerModId(mainId, inventory.LeftHandWeapon);

    auto pSecondaryHandWeapon = GetEquippedWeapon(1);
    uint32_t secondaryId = pSecondaryHandWeapon ? pSecondaryHandWeapon->formID : 0;
    modSystem.GetServerModId(secondaryId, inventory.RightHandWeapon);

    mainId = magicItems[0] ? magicItems[0]->formID : 0;
    modSystem.GetServerModId(mainId, inventory.LeftHandSpell);

    secondaryId = magicItems[1] ? magicItems[1]->formID : 0;
    modSystem.GetServerModId(secondaryId, inventory.RightHandSpell);

    uint32_t shoutId = equippedShout ? equippedShout->formID : 0;
    modSystem.GetServerModId(shoutId, inventory.Shout);

    return inventory;
}

Factions Actor::GetFactions() const noexcept
{
    Factions result;

    auto& modSystem = World::Get().GetModSystem();

    auto* pNpc = RTTI_CAST(baseForm, TESForm, TESNPC);
    if (pNpc)
    {
        auto& factions = pNpc->actorData.factions;

        for (auto i = 0; i < factions.length; ++i)
        {
            Faction faction;

            modSystem.GetServerModId(factions[i].faction->formID, faction.Id);
            faction.Rank = factions[i].rank;

            result.NpcFactions.push_back(faction);
        }
    }

    auto* pChanges = RTTI_CAST(extraData.GetByType(ExtraData::Faction), BSExtraData, ExtraFactionChanges);
    if (pChanges)
    {
        for (auto i = 0; i < pChanges->entries.length; ++i)
        {
            Faction faction;

            modSystem.GetServerModId(pChanges->entries[i].faction->formID, faction.Id);
            faction.Rank = pChanges->entries[i].rank;

            result.ExtraFactions.push_back(faction);
        }
    }

    return result;
}

void Actor::SetInventory(const Inventory& acInventory) noexcept
{
    UnEquipAll();

    auto* pEquipManager = EquipManager::Get();

    if (!acInventory.Buffer.empty())
        DeserializeInventory(acInventory.Buffer);

    auto& modSystem = World::Get().GetModSystem();

    uint32_t mainHandWeaponId = modSystem.GetGameId(acInventory.LeftHandWeapon);

    if (mainHandWeaponId)
        pEquipManager->Equip(this, TESForm::GetById(mainHandWeaponId), nullptr, 1, DefaultObjectManager::Get().leftEquipSlot, false, true, false, false);

    uint32_t secondaryHandWeaponId = modSystem.GetGameId(acInventory.RightHandWeapon);

    if (secondaryHandWeaponId)
        pEquipManager->Equip(this, TESForm::GetById(secondaryHandWeaponId), nullptr, 1, DefaultObjectManager::Get().rightEquipSlot, false, true, false, false);

    mainHandWeaponId = modSystem.GetGameId(acInventory.LeftHandSpell);

    if (mainHandWeaponId)
        pEquipManager->EquipSpell(this, TESForm::GetById(mainHandWeaponId), 0);

    secondaryHandWeaponId = modSystem.GetGameId(acInventory.RightHandSpell);

    if (secondaryHandWeaponId)
        pEquipManager->EquipSpell(this, TESForm::GetById(secondaryHandWeaponId), 1);

    uint32_t shoutId = modSystem.GetGameId(acInventory.Shout);

    if (shoutId)
        pEquipManager->EquipShout(this, TESForm::GetById(shoutId));
}

void Actor::SetFactions(const Factions& acFactions) noexcept
{
    RemoveFromAllFactions();

    auto& modSystem = World::Get().GetModSystem();

    for (auto& entry : acFactions.NpcFactions)
    {
        auto pForm = TESForm::GetById(modSystem.GetGameId(entry.Id));
        auto pFaction = RTTI_CAST(pForm, TESForm, TESFaction);
        if (pFaction)
        {
            SetFactionRank(pFaction, entry.Rank);
        }
    }

    for (auto& entry : acFactions.ExtraFactions)
    {
        auto pForm = TESForm::GetById(modSystem.GetGameId(entry.Id));
        auto pFaction = RTTI_CAST(pForm, TESForm, TESFaction);
        if (pFaction)
        {
            SetFactionRank(pFaction, entry.Rank);
        }
    }
}

void Actor::SetFactionRank(const TESFaction* apFaction, int8_t aRank) noexcept
{
    TP_THIS_FUNCTION(TSetFactionRankInternal, void, Actor, const TESFaction*, int8_t);

    POINTER_SKYRIMSE(TSetFactionRankInternal, s_setFactionRankInternal, 0x1405F7AB0 - 0x140000000);

    ThisCall(s_setFactionRankInternal, this, apFaction, aRank);
}

void Actor::UnEquipAll() noexcept
{
    // For each change 
    const auto pContainerChanges = GetContainerChanges()->entries;
    for (auto pChange : *pContainerChanges)
    {
        if (pChange && pChange->form && pChange->dataList)
        {
            // Parse all extra data lists
            const auto pDataLists = pChange->dataList;
            for (auto* pDataList : *pDataLists)
            {
                if (pDataList)
                {
                    BSScopedLock<BSRecursiveLock> _(pDataList->lock);

                    // Right slot
                    if (pDataList->Contains(ExtraData::Worn))
                    {
                        EquipManager::Get()->UnEquip(this, pChange->form, pDataList, 1, DefaultObjectManager::Get().rightEquipSlot, true, false, true, false, nullptr);
                    }

                    // Left slot
                    if (pDataList->Contains(ExtraData::WornLeft))
                    {
                        EquipManager::Get()->UnEquip(this, pChange->form, pDataList, 1, DefaultObjectManager::Get().leftEquipSlot, true, false, true, false, nullptr);
                    }
                }
            }
        }
    }

    RemoveAllItems();

    // Taken from skyrim's code shouts can be two form types apparently
    if (equippedShout && (equippedShout->formType - 41) <= 1)
    {
        EquipManager::Get()->UnEquipShout(this, equippedShout);
        equippedShout = nullptr;
    }
}

void Actor::RemoveFromAllFactions() noexcept
{
    PAPYRUS_FUNCTION(void, Actor, RemoveFromAllFactions);

    s_pRemoveFromAllFactions(this);
}


static TiltedPhoques::Initializer s_actorHooks([]()
    {
        POINTER_SKYRIMSE(TCharacterConstructor, s_characterCtor, 0x1406928C0 - 0x140000000);
        POINTER_SKYRIMSE(TCharacterConstructor2, s_characterCtor2, 0x1406929C0 - 0x140000000);
        POINTER_SKYRIMSE(TCharacterDestructor, s_characterDtor, 0x1405CDDA0 - 0x140000000);

        RealCharacterConstructor = s_characterCtor.Get();
        RealCharacterConstructor2 = s_characterCtor2.Get();

        TP_HOOK(&RealCharacterConstructor, HookCharacterConstructor);
        TP_HOOK(&RealCharacterConstructor2, HookCharacterConstructor2);
    });

#endif
