#pragma once

#include "usecases/project_session.hpp"
#include "usecases/plugin_manager.hpp"
#include "usecases/offline_renderer.hpp"
#include "../adapters/audio/audio_device_chain.hpp"
#include "../adapters/project/odp_repository.hpp"
#include "../adapters/config/config_store.hpp"
#include "../adapters/config/user_tree.hpp"

namespace digidaw::app {

class Engine {
public:
    Engine()
        : repo_(std::make_shared<adapters::project::OdpFileRepository>()),
          session_(std::make_unique<ProjectSession>(repo_)),
          plugin_mgr_(std::make_unique<PluginManager>()),
          audio_device_(std::make_unique<adapters::audio::AudioDeviceChain>()),
          transport_(std::make_unique<Transport>(session_->project())) {

        // Initialize user tree in standard path
        adapters::config::UserTreeManager::ensure_user_tree("DigiDawUserData");
    }

    [[nodiscard]] ProjectSession& session() noexcept { return *session_; }
    [[nodiscard]] PluginManager& plugin_manager() noexcept { return *plugin_mgr_; }
    [[nodiscard]] adapters::audio::AudioDeviceChain& audio_device() noexcept { return *audio_device_; }
    [[nodiscard]] Transport& transport() noexcept { return *transport_; }

    domain::Result<void> start_audio() {
        return audio_device_->open(44100.0, 512, [this](domain::AudioBufferView& out) {
            // Audio processing callback
            out.clear();
        });
    }

    void stop_audio() {
        audio_device_->stop();
        audio_device_->close();
    }

private:
    std::shared_ptr<IProjectRepository> repo_;
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<PluginManager> plugin_mgr_;
    std::unique_ptr<adapters::audio::AudioDeviceChain> audio_device_;
    std::unique_ptr<Transport> transport_;
};

} // namespace digidaw::app
