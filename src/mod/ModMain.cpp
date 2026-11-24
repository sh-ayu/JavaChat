#include "mod/JavaChatMod.h"

#include <ll/api/mod/RegisterHelper.h>

namespace javachat {

struct ModEntry {
    bool load() { return JavaChatMod::instance().load(); }
    bool enable() { return JavaChatMod::instance().enable(); }
    bool disable() { return JavaChatMod::instance().disable(); }
};

ModEntry entry;

} // namespace javachat

LL_REGISTER_MOD(javachat::ModEntry, javachat::entry)
