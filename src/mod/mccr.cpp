#include "mccr.h"
#include "fmt/core.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/event/Event.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/service/Bedrock.h"
#include "mc/_HeaderOutputPredefine.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/utility/MCRESULT.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/packet/AddActorPacket.h"
#include "mc/network/packet/Packet.h"
#include "mc/network/packet/SetActorDataPacket.h"
#include "mc/network/packet/SetTimePacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SyncActorPropertyPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandBlockName.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/CommandMessage.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/server/commands/CommandVersion.h"
#include "mc/server/commands/GenerateMessageResult.h"
#include "mc/server/commands/MinecraftCommands.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/Minecraft.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/RenderParams.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/animation/AnimationComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/world/actor/state/PropertyGroup.h"
#include "mc/world/actor/state/PropertyGroupManager.h"
#include "mc/world/actor/state/PropertyValues.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/VanillaDimensions.h"
#include "nlohmann/detail/exceptions.hpp"
#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"
#include <climits>
#include <exception>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
#include <unordered_map>


auto Logger = ll::io::LoggerRegistry::getInstance().getOrCreate("MCCxMinecraftReborn");
using json  = nlohmann::json;
namespace mccr {

MCCRMod& MCCRMod::getInstance() {
    static MCCRMod instance;
    return instance;
}

bool MCCRMod::load() {
    Logger->info("");
    Logger->info("\x1b[33mMCCxMinecraft \x1b[36mReborn\x1b[0m v{}", VERSION);
    Logger->info("一个致力于打造全还原MCCxMinecraft15周年服务器地图的项目");
    Logger->info("地图下载: https://howie114514.github.io/MCCxMinecraftReborn");
    Logger->info("Github: https://github.com/Howie114514/MCCxMinecraftReborn");
    Logger->info("");
    return true;
}

enum BPMsgTypes { connect = 0x01, set_time = 0x02, set_property = 0x03, spawn_particle = 0x04 };
enum ResponseMsgTypes {
    success = 0x01,
    error   = 0x02,
};
enum EventTypes { set_actor_data_packet_sent = 0x01 };

void syncBoolProperty(Player* player, Actor* entity, std::string prop, bool value) {
    HashedString      name = entity->getTypeName().c_str();
    PropertyComponent component(
        ll::service::getLevel()
            ->getActorPropertyGroup()
            .mUnk64dcbf.as<std::unordered_map<HashedString, std::shared_ptr<PropertyGroup>>>()
            .at(name),
        entity->getAnimationComponent().mRenderParams
    );
    HashedString hs = prop.c_str();
    CompoundTag  nbt;
    entity->save(nbt);
    if (!nbt["properties"].is_object()) {
        Logger->info("实体不拥有properties标签");
        return;
    }
    CompoundTag properties = nbt["properties"].get<CompoundTag>();
    CompoundTag data       = *properties.clone();
    data[prop]             = value;
    component.mPropertyContainer->readLoadedProperties(data);
    SynchedActorDataEntityWrapper adw = entity->mEntityData;
    SetActorDataPacket
        packet(entity->getRuntimeID(), adw, &component, ll::service::getLevel()->getCurrentTick().tickID, true);
    player->sendNetworkPacket(packet);
}

void showParticle(Player* player, Vec3 pos, std::string effect) {
    SpawnParticleEffectPacket packet(pos, effect, VanillaDimensions::Overworld().id, std::nullopt);
    player->sendNetworkPacket(packet);
}

// 执行scriptevent指令与脚本通信
void send(json data) {
    CommandContext ctx = CommandContext(
        fmt::format("scriptevent mccr.plugin:communicate {}", data.dump()),
        std::make_unique<ServerCommandOrigin>(
            "Server",
            ll::service::getLevel()->asServer(),
            CommandPermissionLevel::Internal,
            0
        ),
        CommandVersion::CurrentVersion()
    );
    ll::service::getMinecraft()->mCommands->executeCommand(ctx, false);
}

void runCommand(std::string c) {
    CommandContext ctx = CommandContext(
        c,
        std::make_unique<ServerCommandOrigin>(
            "Server",
            ll::service::getLevel()->asServer(),
            CommandPermissionLevel::Internal,
            0
        ),
        CommandVersion::CurrentVersion()
    );
    ll::service::getMinecraft()->mCommands->executeCommand(ctx, false);
}

void sendError(std::string e, std::string id) {
    send({
        {"type",   ResponseMsgTypes::error},
        {"reason", e                      },
        {"id",     id                     }
    });
}

void sendSuccess(std::string id, std::string r = "") {
    send({
        {"type",   ResponseMsgTypes::success},
        {"reason", r                        },
        {"id",     id                       }
    });
}

Player* getPlayerByJSON(json j) { return ll::service::getLevel()->getPlayer(j.get<std::string>()); }


Vec3 vec3FromJSON(json j) {
    Vec3 v;
    v.x = j["x"].get<float>();
    v.y = j["y"].get<float>();
    v.z = j["z"].get<float>();
    return v;
}

enum TimeOfDay {
    /**
     * @remarks
     * Sets the time to the start of the day, which is time of the
     * day 1,000 (or the equivalent of 7am) in Minecraft.
     *
     */
    Day = 1000,
    /**
     * @remarks
     * Sets the time to noon, which is time of the day 6,000 in
     * Minecraft.
     *
     */
    Noon = 6000,
    /**
     * @remarks
     * Sets the time to sunset, which is time of the day 12,000 (or
     * the equivalent of 6pm) in Minecraft.
     *
     */
    Sunset = 12000,
    /**
     * @remarks
     * Sets the time to night, which is time of the day 13,000 (or
     * the equivalent of 7:00pm) in Minecraft.
     *
     */
    Night = 13000,
    /**
     * @remarks
     * Sets the time to midnight, which is time of the day 18,000
     * (or the equivalent of 12:00am) in Minecraft.
     *
     */
    Midnight = 18000,
    /**
     * @remarks
     * Sets the time to sunrise, which is time of the day 23,000
     * (or the equivalent of 5am) in Minecraft.
     *
     */
    Sunrise = 23000,
};

bool MCCRMod::enable() {
    auto commandRegistry = ll::service::getCommandRegistry();
    if (!commandRegistry) {
        throw std::runtime_error("failed to get command registry");
    }
    auto& mccr_command = ll::command::CommandRegistrar::getInstance()
                             .getOrCreateCommand("mccr", "MCCxMinecraft Reborn", CommandPermissionLevel::Any);
    mccr_command.runtimeOverload()
        .text("getinfo")
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
                output.success(
                    "{} UniqueID:{} RuntimeId:{}",
                    nbt.toSnbt(),
                    a->getOrCreateUniqueID().rawID,
                    a->getRuntimeID().rawID
                );
            }
        });
    mccr_command.runtimeOverload().text("detect").execute(
        [](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& cmd) {
            output.success("OK");
        }
    );

    auto& mccr_communicate = ll::command::CommandRegistrar::getInstance().getOrCreateCommand(
        "mccrc",
        "MCCxMinecraftReborn脚本拓展API通信命令",
        CommandPermissionLevel::Any
    );
    mccr_communicate.runtimeOverload()
        .required("data", ll::command::ParamKind::Message)
        .execute([](CommandOrigin const& origin, CommandOutput& output, const ll::command::RuntimeCommand& command) {
            std::string rid = "";
            if (origin.getOriginType() != CommandOriginType::Scripting
                && origin.getOriginType() != CommandOriginType::DedicatedServer) {
                output.error("只有脚本和控制台能执行该命令。");
                return;
            }
            try {
                json data = json::parse(
                    std::get<CommandMessage>(command["data"].value()).generateMessage(origin, INT_MAX).mMessage->c_str()
                );
                std::string id = data["id"].get<std::string>();
                rid            = id;
                switch (data["type"].get<BPMsgTypes>()) {
                case BPMsgTypes::connect: {
                    send({
                        {"id",      id                       },
                        {"type",    ResponseMsgTypes::success},
                        {"version", VERSION                  }
                    });
                    break;
                }
                case BPMsgTypes::set_property: {
                    auto UID  = ActorUniqueID();
                    UID.rawID = std::stoll(data["actor"].get<std::string>());
                    syncBoolProperty(
                        ll::service::getLevel()->getPlayer(data["player"].get<std::string>()),
                        ll::service::getLevel()->fetchEntity(UID, false),
                        data["prop"].get<std::string>(),
                        data["value"].get<bool>()
                    );
                    sendSuccess(id);
                    break;
                }
                case BPMsgTypes::set_time: {
                    SetTimePacket packet;
                    packet.mTime = data["time"].get<int>();
                    Logger->info("Set time");
                    Player* player = getPlayerByJSON(data["player"]);
                    packet.sendTo(*player);
                    sendSuccess(id);
                    break;
                }
                case BPMsgTypes::spawn_particle: {
                    showParticle(
                        getPlayerByJSON(data["player"]),
                        vec3FromJSON(data["pos"]),
                        data["particle"].get<std::string>()
                    );
                    sendSuccess(id);
                    break;
                }
                }
            } catch (nlohmann::detail::exception& e) {
                output.error("解析JSON失败:{}", e.what());
                return;
            } catch (std::exception& e) {
                sendError(e.what(), rid);
            }
        });

    return true;
}

bool MCCRMod::disable() {
    getSelf().getLogger().debug("Disabling...");
    return true;
}
} // namespace mccr


LL_REGISTER_MOD(mccr::MCCRMod, mccr::MCCRMod::getInstance());