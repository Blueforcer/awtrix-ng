#pragma once

#include <string>

namespace awtrix {
namespace sound {

constexpr size_t kMaxMp3Name = 32;
constexpr const char* kDir = "/MP3/";
constexpr const char* kExt = ".mp3";
// A script's own sounds live in a folder named after it, next to its source.
constexpr const char* kScriptsDir = "/SCRIPTS/";
constexpr const char* kMelodiesDir = "/MELODIES/";

// An MP3 to fetch rather than a stored name; no name can contain ':' or '/'.
inline bool isUrl(const std::string& value) {
  return value.compare(0, 7, "http://") == 0 || value.compare(0, 8, "https://") == 0;
}

// The one rule for a name that becomes part of a sound's path: 1-32 of A-Z, a-z, 0-9, _ and -.
// It is the app name rule too, so every script's install name passes it. The alphabet has no '/'
// and no '.', so a path built from such names cannot leave the folder it is joined to.
bool validName(const std::string& name);

// "/MP3/<name>.mp3" and "/MELODIES/<name>.txt"; "" for a name that breaks the rule.
std::string mp3PathFor(const std::string& name);
std::string melodyPathFor(const std::string& name);

// "/SCRIPTS/<script>", the folder a script's sounds live in; "" for a name no folder can have.
std::string scriptSoundDir(const std::string& script);

// "/SCRIPTS/<script>/<name>.mp3"; "" unless both names follow the rule.
std::string scriptMp3PathFor(const std::string& script, const std::string& name);

// "<name>.mp3" back to "<name>" for the file a sound is stored in; "" for any other file name.
std::string mp3NameOfFile(const std::string& file);

// A plain name means one sound, so an MP3 in /MP3 and a melody in /MELODIES never share a name.
// The file the other kind would keep under path's name; "" for any other path, a script's own
// folder included.
std::string namesakePath(const std::string& path);

// Whether path is exactly one a script's sound can have: "/SCRIPTS/<script>/<name>.mp3".
bool isScriptMp3Path(const std::string& path);

// Whether path is place itself or lies inside the folder place: what deleting place takes along.
bool within(const std::string& path, const std::string& place);

}
}
