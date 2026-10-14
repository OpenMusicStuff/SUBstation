#pragma once
// The browser's side of the sound tagger (intelligence/src/tagging): searches
// that find files by what they sound like as well as by their names ("hard
// trap snare" finds the hard, trappy snares however they are named), the
// sidebar's sounds (Samples › Kicks), and what a file is called in its row
// ("Snare · Punchy · Bright").
//
// The tags are the sound similarity's (SoundSimilarity::tags(): every analysed
// file of the browser's index, from its fingerprint), looked up by the hash of
// a file's path, which the backend makes from its folder's and its name
// (browser::fileHash: the same as the table's pathHash of the path joined).

#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Model.h"
#include "tagging/SoundTagTable.h"
#include "tagging/SoundWords.h"

namespace sub::app {

// What the browser's search asks of sounds (browser::Query::sounds), from
// these tags: the search's terms the tagger reads ("kick", "hard", "dubstep",
// "c2"...), and `filter` (a sidebar's sounds: a word every file listed must
// sound like, "kick"; empty for none). Null when there is nothing to hear: no
// tags, or neither a filter nor a word the tagger reads.
using SoundMatching = std::function<std::shared_ptr<const browser::SoundMatcher>(const std::vector<std::string>&)>;
SoundMatching soundMatching(std::shared_ptr<const intelligence::tagging::SoundTagTable> tags, const QString& filter = {});

// A file's label (its path in the backend's form): "Snare · Punchy · Bright";
// "" if the tagger knows nothing of it.
QString soundLabel(const intelligence::tagging::SoundTagTable* tags, const std::string& path);
// Its tooltip's lines: what it is (and how sure), its note, how it sounds, what
// people would call it.
QString soundToolTip(const intelligence::tagging::SoundTagTable* tags, const std::string& path);

// The sidebar's sounds: (title, word) in order: ("Kicks", "kick")...
std::vector<std::pair<QString, QString>> soundCategories();

}  // namespace sub::app
