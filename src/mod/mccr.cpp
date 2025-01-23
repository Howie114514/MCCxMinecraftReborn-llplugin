#include "mod/mccr.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "ll/api/command/Command.h"
#include "ll/api/command/ParamTraits.h"
#include "ll/api/command/SoftEnum.h"
#include "ll/api/command/runtime/RuntimeEnum.h"
#include "ll/api/event/Event.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/service/Bedrock.h"
#include "mc/_HeaderOutputPredefine.h"
#include "mc/common/ActorUniqueID.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/nbt/Tag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/packet/ChangeMobPropertyPacket.h"
#include "mc/network/packet/Packet.h"
#include "mc/server/commands/CommandBlockName.h"
#include "mc/server/commands/CommandBlockNameResult.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutputType.h"
#include "mc/server/commands/CommandParameterDataType.h"
#include "mc/server/commands/CommandParameterOption.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandVersion.h"
#include "mc/server/commands/CurrentCmdVersion.h"
#include "mc/server/commands/GenerateMessageResult.h"
#include "mc/server/commands/MinecraftCommands.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"


namespace mccr {

MCCRMod& MCCRMod::getInstance() {
    static MCCRMod instance;
    return instance;
}

bool MCCRMod::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

void syncBoolProperty(Player* player, const Actor* entity, std::string prop, bool value) {
    ChangeMobPropertyPacket packet;
    packet.mActorId          = entity->getOrCreateUniqueID();
    packet.mPropName         = prop;
    packet.mBoolComponentVal = value;
    MCCRMod::getInstance().getSelf().getLogger().info("{}", packet.isValid());
    player->sendNetworkPacket(packet);
}

bool MCCRMod::enable() {
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
        .text("syncprop")
        .required("propname", ll::command::ParamKind::String)
        .required("propvalue", ll::command::ParamKind::Bool)
        .optional("player", ll::command::ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& cmd) {
            if (origin.getOriginType() == CommandOriginType::Player) {
                output.error("玩家不能执行该指令。");
                return;
            }
            const std::string propname  = std::get<std::string>(cmd["propname"].value());
            const bool        propvalue = std::get<bool>(cmd["propvalue"].value());
            const auto*       entity    = origin.getEntity();
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
                        "在向玩家%s同步%s的属性数据%s=%t时出现了问题:%s",
                        p->getName(),
                        entity->getEntityLocNameString(),
                        propname,
                        propvalue,
                        err.what()
                    );
                }
            }
        });
    return true;
}

bool MCCRMod::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    return true;
}

} // namespace mccr

LL_REGISTER_MOD(mccr::MCCRMod, mccr::MCCRMod::getInstance());
