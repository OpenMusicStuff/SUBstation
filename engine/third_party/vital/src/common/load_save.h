/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

// SUBstation: Vital's load_save.h trimmed to what load_save.cpp keeps (see there).

#pragma once

#include "JuceHeader.h"
#include "json/json.h"

#include <map>
#include <set>
#include <string>

using json = nlohmann::json;

class LoadSave {
  public:
    enum PresetStyle {
      kBass,
      kLead,
      kKeys,
      kPad,
      kPercussion,
      kSequence,
      kExperimental,
      kSfx,
      kTemplate,
      kNumPresetStyles
    };

    static const int kMaxCommentLength = 500;
    static void convertBufferToPcm(json& data, const std::string& field);
    static void convertPcmToFloatBuffer(json& data, const std::string& field);
    static json updateFromOldVersion(json state);
    static std::string getAuthor(json file);
    static std::string getLicense(json state);
    static int compareFeatureVersionStrings(String a, String b);
    static int compareVersionStrings(String a, String b);

  private:
    LoadSave() { }
};
