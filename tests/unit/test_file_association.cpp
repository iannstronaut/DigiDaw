#include "../test_framework.hpp"
#include "../../adapters/desktop/file_association.hpp"

using namespace digidaw::adapters::desktop;

TEST_CASE(UnitDesktop, FileAssociationProgIdFormat) {
    ASSERT_EQ(std::string(FileAssociation::ProgId), "DigiDAW.Project.26");
    ASSERT_EQ(std::string(FileAssociation::FileExtension), ".odp");
    ASSERT_EQ(std::string(FileAssociation::FileDescription), "DigiDAW Project File");
}
