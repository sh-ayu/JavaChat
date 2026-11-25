-- JavaChat (LeviLamina client native mod) + standalone core tests.
--
-- Targets:
--  * jchat_core:  pure C++20 logic (config, formatter/wrapper, history/sync/hit-test).
--                 No LeviLamina dependency.
--  * jchat_tests: standalone unit test suite (`xmake run jchat_tests`).
--  * JavaChat:    LeviLamina 26.60.8 client mod DLL + bundled JavaChatUI resource pack.
set_xmakever("3.0.0")
add_rules("mode.release", "mode.debug")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("with_mod")
    set_default(true)
    set_showmenu(true)
    set_description("Build the LeviLamina client mod DLL")
option_end()

if has_config("with_mod") then
    add_requires("levilamina 26.60.8", {configs = {target_type = "client"}})
    add_requires("levibuildscript")
end

-- Core: NO LeviLamina dependency (unit-testable standalone).
target("jchat_core")
    set_kind("static")
    set_languages("c++20")
    add_files("src/core/*.cpp")
    add_includedirs("src", {public = true})
    if is_plat("windows") then
        set_runtimes("MD")
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        add_cxflags("/utf-8")
    end

-- Standalone unit tests (no LeviLamina, no game).
target("jchat_tests")
    set_kind("binary")
    set_languages("c++20")
    set_default(true)
    add_files("tests/test_javachat.cpp")
    add_deps("jchat_core")
    if is_plat("windows") then
        set_runtimes("MD")
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        add_cxflags("/utf-8")
    end

-- LeviLamina client mod DLL.
if has_config("with_mod") then
target("JavaChat")
    set_kind("shared")
    set_languages("c++20")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {
        modName = "JavaChat",
        modFile = "JavaChat.dll",
        modVersion = "1.1.0",
        modPlatform = "client"
    })
    add_files("src/core/*.cpp", "src/mod/*.cpp")
    add_includedirs("src")
    add_packages("levilamina")
    set_symbols("debug")
    if is_plat("windows") then
        set_toolchains("clang-cl")
        set_runtimes("MD")
        add_syslinks("user32", "shell32")
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1", "LL_PLAT_C")
        add_cxflags("/utf-8")
        add_shflags("/PDBALTPATH:%_PDB%", {force = true})
    end
    after_build(function (target)
        -- Bundle the companion lightweight JSON-UI resource pack alongside the mod output.
        -- LeviLamina's InitPacksHook automatically registers <modDir>/resource_packs/.
        local src = path.join(os.projectdir(), "resource_packs", "JavaChatUI")
        local dst = path.join(
            os.projectdir(), "bin", "JavaChat", "resource_packs", "JavaChatUI"
        )
        if os.isdir(src) then
            os.rm(dst)
            os.cp(src, dst)
            print("JavaChat: bundled UI pack -> " .. dst)
        else
            print("JavaChat: UI pack source missing, skipping bundle (" .. src .. ")")
        end
    end)
end
