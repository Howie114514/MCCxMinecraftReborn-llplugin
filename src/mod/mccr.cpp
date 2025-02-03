#include "mccr.h"

#include <exception>
#include <memory>
#include <stdint.h>
#include <string>
#include <unordered_map>

#include "fmt/core.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/service/Bedrock.h"
#include "mc/_HeaderOutputPredefine.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/network/packet/AddActorPacket.h"
#include "mc/network/packet/Packet.h"
#include "mc/network/packet/SetActorDataPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SyncActorPropertyPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandBlockName.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/server/commands/CommandVersion.h"
#include "mc/server/commands/MinecraftCommands.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/Minecraft.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/RenderParams.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyGroup.h"
#include "mc/world/actor/state/PropertyGroupManager.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/VanillaDimensions.h"


auto Logger = ll::io::LoggerRegistry::getInstance().getOrCreate("mccr");


namespace mccr {

const PropertyGroupManager* pPropertyGroupManager;

MCCRMod& MCCRMod::getInstance() {
    static MCCRMod instance;
    return instance;
}

bool MCCRMod::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

void syncBoolProperty(Player* player, Actor* entity, std::string prop, bool value) {
    HashedString      name = entity->getTypeName().c_str();
    PropertyComponent component(pPropertyGroupManager->getAllPropertyGroups().at(name), entity->getRenderParams());
    HashedString      hs = prop.c_str();
    CompoundTag       nbt;
    entity->save(nbt);
    if (!nbt["properties"].is_object()) {
        Logger->info("实体不拥有properties标签");
        return;
    }
    CompoundTag properties = nbt["properties"].get<CompoundTag>();
    CompoundTag data       = *properties.clone();
    data[prop]             = value;
    component.readLoadedProperties(data);
    SynchedActorDataEntityWrapper adw = entity->getEntityData();
    SetActorDataPacket
        packet(entity->getRuntimeID(), adw, &component, ll::service::getLevel()->getCurrentTick().tickID, true);
    player->sendNetworkPacket(packet);
}

void showParticle(Player* player, Vec3 pos, std::string effect) {
    MolangVariableMap         map;
    SpawnParticleEffectPacket packet(pos, effect, VanillaDimensions::Overworld().id, map);
    player->sendNetworkPacket(packet);
}

bool MCCRMod::enable() {
    Logger->info("MCCxMinecraft Reborn配套插件加载成功！");
    auto commandRegistry = ll::service::getCommandRegistry();
    if (!commandRegistry) {
        throw std::runtime_error("failed to get command registry");
    }
    auto& mccr_command = ll::command::CommandRegistrar::getInstance()
                             .getOrCreateCommand("mccr", "MCCxMinecraft Reborn专用命令", CommandPermissionLevel::Any);
    mccr_command.overload().text("detect").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getPermissionsLevel() < CommandPermissionLevel::Admin) {
            output.error("你的权限不足。");
            return;
        }
        output.mSuccessCount = 1;
        output.success("MCCxMinecraftReborn插件正常运行中");
    });
    mccr_command.runtimeOverload()
        .text("getnbt")
        .required("actor", ll::command::ParamKind::Actor)
        .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& cmd) {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::Admin) {
                output.error("你的权限不足。");
                return;
            }
            auto actors = std::get<CommandSelector<Actor>>(cmd["actor"].value()).results(origin);
            for (auto a : actors) {
                CompoundTag nbt;
                a->save(nbt);
                output.success(nbt.toSnbt());
            }
        });
    mccr_command.runtimeOverload()
        .text("syncprop")
        .required("propname", ll::command::ParamKind::String)
        .required("propvalue", ll::command::ParamKind::Bool)
        .required("player", ll::command::ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& cmd) {
            if (origin.getOriginType() == CommandOriginType::Player) {
                output.error("玩家不能执行该指令。");
                return;
            }
            const std::string propname  = std::get<std::string>(cmd["propname"].value());
            const bool        propvalue = std::get<bool>(cmd["propvalue"].value());
            auto*             entity    = origin.getEntity();
            if (entity == nullptr) {
                output.error("执行者不是一个有效的实体");
                return;
            }
            for (auto p : std::get<CommandSelector<Player>>(cmd["player"].value()).results(origin)) {
                try {
                    syncBoolProperty(p, entity, propname, propvalue);
                    output.mSuccessCount += 1;
                    output.success(fmt::format("OK: {}->{}={}", entity->getFormattedNameTag(), propname, propvalue));
                } catch (const std::exception& err) {
                    output.error(
                        "在向玩家{}同步{}的属性数据{}={}时出现了问题:{}",
                        p->getName(),
                        entity->getEntityLocNameString(),
                        propname,
                        propvalue,
                        err.what()
                    );
                }
            }
        });
    mccr_command.runtimeOverload()
        .text("particle")
        .required("name", ll::command::ParamKind::String)
        .required("pos", ll::command::ParamKind::Vec3)
        .required("player", ll::command::ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& cmd) {
            if (origin.getOriginType() == CommandOriginType::Player
                && origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
                output.error("你的权限不足。");
                return;
            }
            auto name = std::get<std::string>(cmd["name"].value());
            auto pos  = std::get<CommandPositionFloat>(cmd["pos"].value())
                           .getPosition(CommandVersion::CurrentVersion(), origin, {0.F, 0.F, 0.F});
            for (auto p : std::get<CommandSelector<Player>>(cmd["player"].value()).results(origin)) {
                showParticle(p, pos, name);
                output.success("成功向{}在{}处播放粒子{}", p->getName(), pos.toString(), name);
            }
        });
    return true;
}

bool MCCRMod::disable() {
    getSelf().getLogger().debug("Disabling...");
    return true;
}
} // namespace mccr


LL_AUTO_TYPE_INSTANCE_HOOK(PropGroupMgrCtor, ll::memory::HookPriority::Normal, PropertyGroupManager, &PropertyGroupManager::$ctor, void*) {
    Logger->debug("PropertyGroupManager被构造了");
    void* o                     = origin();
    mccr::pPropertyGroupManager = reinterpret_cast<PropertyGroupManager*>(o);
    return o;
};


LL_REGISTER_MOD(mccr::MCCRMod, mccr::MCCRMod::getInstance());