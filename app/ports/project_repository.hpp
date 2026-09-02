#pragma once

#include "../../domain/project/project.hpp"
#include "../../domain/common/result.hpp"
#include <string>

namespace digidaw::app {

class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;

    virtual domain::Result<void> save(const domain::Project& project, const std::string& filepath) = 0;
    virtual domain::Result<domain::Project> load(const std::string& filepath) = 0;
};

} // namespace digidaw::app
