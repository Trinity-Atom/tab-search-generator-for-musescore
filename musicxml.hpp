#pragma once

#include "model.hpp"

#include <filesystem>
#include <iosfwd>

namespace tabs {
// Invalid chord symbols throw ChordParseError before writing or truncating output.
bool writeMusicXml(const TabVersion& version, std::ostream& output);
bool exportToMusicXml(const TabVersion& version, const std::filesystem::path& path);
}
