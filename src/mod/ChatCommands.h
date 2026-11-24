#pragma once

#include <memory>

namespace ll::mod {
class Mod;
}

namespace javachat {

void registerChatCommands(std::weak_ptr<ll::mod::Mod> weakMod);

} // namespace javachat
