#pragma once

#include "../../domain/common/result.hpp"
#include <string>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace digidaw::adapters::desktop {

class FileAssociation {
public:
    static constexpr const char* ProgId = "DigiDAW.Project.26";
    static constexpr const char* FileExtension = ".odp";
    static constexpr const char* FileDescription = "DigiDAW Project File";

    // Registers .odp file association in HKCU (no admin privileges required - DESKTOP-FR-001/002)
    static domain::Result<void> register_association(const std::string& executable_path) {
#ifdef _WIN32
        const std::string open_cmd = "\"" + executable_path + "\" \"%1\"";

        // 1. HKCU\Software\Classes\.odp -> DigiDAW.Project.26
        HKEY hKeyExt = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\.odp", 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyExt, nullptr) != ERROR_SUCCESS) {
            return domain::Result<void>(domain::ErrorCode::RegistryDenied);
        }
        RegSetValueExA(hKeyExt, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(ProgId),
                       static_cast<DWORD>(strlen(ProgId) + 1));
        RegCloseKey(hKeyExt);

        // 2. HKCU\Software\Classes\DigiDAW.Project.26
        HKEY hKeyProg = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, ("Software\\Classes\\" + std::string(ProgId)).c_str(),
                            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyProg, nullptr) != ERROR_SUCCESS) {
            return domain::Result<void>(domain::ErrorCode::RegistryDenied);
        }
        RegSetValueExA(hKeyProg, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(FileDescription),
                       static_cast<DWORD>(strlen(FileDescription) + 1));

        // 3. shell\open\command
        HKEY hKeyCmd = nullptr;
        std::string cmd_subkey = "Software\\Classes\\" + std::string(ProgId) + "\\shell\\open\\command";
        if (RegCreateKeyExA(HKEY_CURRENT_USER, cmd_subkey.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyCmd, nullptr) != ERROR_SUCCESS) {
            RegCloseKey(hKeyProg);
            return domain::Result<void>(domain::ErrorCode::RegistryDenied);
        }
        RegSetValueExA(hKeyCmd, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(open_cmd.c_str()),
                       static_cast<DWORD>(open_cmd.length() + 1));

        RegCloseKey(hKeyCmd);
        RegCloseKey(hKeyProg);

        // Notify Windows Explorer of association change
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return domain::Result<void>::ok();
#else
        (void)executable_path;
        return domain::Result<void>::ok();
#endif
    }

    // Cleanly removes registry association on uninstallation (INT-10)
    static domain::Result<void> unregister_association() {
#ifdef _WIN32
        RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.odp");
        RegDeleteTreeA(HKEY_CURRENT_USER, ("Software\\Classes\\" + std::string(ProgId)).c_str());
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return domain::Result<void>::ok();
#else
        return domain::Result<void>::ok();
#endif
    }

    [[nodiscard]] static bool is_registered() {
#ifdef _WIN32
        HKEY hKey = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\.odp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char val[128]{0};
            DWORD size = sizeof(val);
            LONG ret = RegQueryValueExA(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(val), &size);
            RegCloseKey(hKey);
            return (ret == ERROR_SUCCESS && std::string(val) == ProgId);
        }
        return false;
#else
        return true;
#endif
    }
};

} // namespace digidaw::adapters::desktop
