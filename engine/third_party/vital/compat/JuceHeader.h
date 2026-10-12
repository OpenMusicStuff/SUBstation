#pragma once
// SUBstation: what Vital's synthesis code takes from JUCE, without JUCE.
//
// Vital (src/synthesis, and the parts of src/common vendored with it) includes
// "JuceHeader.h" for a handful of things: leak detector and non-copyable
// macros, juce::String in a few helpers, Base64 for wave data in presets, and
// switching denormals off. This header gives those, in standard C++, so the
// vendored files compile unchanged where they can (see ../README.md for the
// files that were changed instead).

// What JuceHeader.h brings in from the standard library, which Vital's files
// rely on without including (and fourier_transform.h includes KISS FFT inside
// namespace vital, so its standard headers must already be in).
#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

#define JUCE_LEAK_DETECTOR(className)
#define JUCE_DECLARE_NON_COPYABLE(className) \
    className(const className&) = delete;    \
    className& operator=(const className&) = delete;
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(className) JUCE_DECLARE_NON_COPYABLE(className)

namespace juce {

// The few members of juce::String Vital's engine files use, over a std::string.
class String {
public:
    String() = default;
    String(const char* text) : text_(text != nullptr ? text : "") {}
    String(const std::string& text) : text_(text) {}
    explicit String(int value) : text_(std::to_string(value)) {}

    const std::string& toStdString() const { return text_; }
    bool isEmpty() const { return text_.empty(); }
    bool isNotEmpty() const { return !text_.empty(); }
    int length() const { return static_cast<int>(text_.size()); }

    String trim() const {
        size_t begin = 0;
        size_t end = text_.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(text_[begin]))) ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(text_[end - 1]))) --end;
        return text_.substr(begin, end - begin);
    }
    String toLowerCase() const {
        std::string lowered = text_;
        for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lowered;
    }
    int getIntValue() const { return std::atoi(text_.c_str()); }
    float getFloatValue() const { return static_cast<float>(std::atof(text_.c_str())); }
    bool contains(const String& sub) const { return text_.find(sub.text_) != std::string::npos; }
    bool containsOnly(const String& chars) const {
        return std::all_of(text_.begin(), text_.end(), [&](char c) { return chars.text_.find(c) != std::string::npos; });
    }
    String upToFirstOccurrenceOf(const String& sub, bool include, bool /*ignoreCase*/) const {
        const size_t at = text_.find(sub.text_);
        if (at == std::string::npos) return *this;
        return text_.substr(0, include ? at + sub.text_.size() : at);
    }
    String upToLastOccurrenceOf(const String& sub, bool include, bool /*ignoreCase*/) const {
        const size_t at = text_.rfind(sub.text_);
        if (at == std::string::npos) return *this;
        return text_.substr(0, include ? at + sub.text_.size() : at);
    }
    String fromFirstOccurrenceOf(const String& sub, bool include, bool /*ignoreCase*/) const {
        const size_t at = text_.find(sub.text_);
        if (at == std::string::npos) return {};
        return text_.substr(include ? at : at + sub.text_.size());
    }
    String fromLastOccurrenceOf(const String& sub, bool include, bool /*ignoreCase*/) const {
        const size_t at = text_.rfind(sub.text_);
        if (at == std::string::npos) return *this;
        return text_.substr(include ? at : at + sub.text_.size());
    }

    String substring(int start) const {
        return start >= length() ? String() : String(text_.substr(static_cast<size_t>(std::max(0, start))));
    }
    String substring(int start, int end) const {
        start = std::max(0, start);
        end = std::min(end, length());
        return end <= start ? String() : String(text_.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
    }
    String removeCharacters(const String& chars) const {
        std::string kept;
        for (const char c : text_) {
            if (chars.text_.find(c) == std::string::npos) kept += c;
        }
        return kept;
    }
    // A character, or 0 past the end (as JUCE's).
    char operator[](int index) const {
        return index >= 0 && index < length() ? text_[static_cast<size_t>(index)] : '\0';
    }

    String operator+(const String& other) const { return text_ + other.text_; }
    String& operator+=(const String& other) {
        text_ += other.text_;
        return *this;
    }
    bool operator==(const String& other) const { return text_ == other.text_; }
    bool operator!=(const String& other) const { return text_ != other.text_; }
    operator std::string() const { return text_; }

private:
    std::string text_;
};

inline String operator+(const char* a, const String& b) { return String(a) + b; }
inline String operator+(const std::string& a, const String& b) { return String(a) + b; }

// Tokens of a string (tuning files' lines).
class StringArray {
public:
    // Splits at whitespace (`preserveQuoted`: a "quoted" token stays whole).
    void addTokens(const String& text, bool preserveQuoted) { addTokens(text, " \t\n\r", preserveQuoted ? "\"" : ""); }
    // As JUCE's: every break ends a token, so two in a row make an empty one;
    // nothing is added for an empty text.
    void addTokens(const String& text, const String& breakChars, const String& quoteChars) {
        const std::string& in = text.toStdString();
        if (in.empty()) return;
        const std::string& breaks = breakChars.toStdString();
        const std::string& quotes = quoteChars.toStdString();
        std::string token;
        char quote = 0;
        for (const char c : in) {
            if (quote != 0) {
                if (c == quote) quote = 0;
                token += c;
            } else if (breaks.find(c) != std::string::npos) {
                items_.push_back(token);
                token.clear();
            } else {
                if (quotes.find(c) != std::string::npos) quote = c;
                token += c;
            }
        }
        items_.push_back(token);
    }
    void add(const String& item) { items_.push_back(item); }
    int size() const { return static_cast<int>(items_.size()); }
    // An item, or "" past the end (as JUCE's).
    String operator[](int index) const { return index >= 0 && index < size() ? items_[static_cast<size_t>(index)] : String(); }
    std::vector<String>::const_iterator begin() const { return items_.begin(); }
    std::vector<String>::const_iterator end() const { return items_.end(); }

private:
    std::vector<String> items_;
};

// A file by its path: what tuning files are read through.
class File {
public:
    File() = default;
    explicit File(const String& path) : path_(path.toStdString()) {}

    const std::string& getFullPathName() const { return path_; }
    // ".scl" (with the dot), or "".
    String getFileExtension() const {
        const size_t slash = path_.find_last_of("/\\");
        const size_t dot = path_.rfind('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
        return path_.substr(dot);
    }
    String getFileNameWithoutExtension() const {
        const size_t slash = path_.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path_ : path_.substr(slash + 1);
        const size_t dot = name.rfind('.');
        return dot == std::string::npos ? name : name.substr(0, dot);
    }
    // Its lines (none if it can't be read).
    void readLines(StringArray& lines) const;

private:
    std::string path_;
};

// A growing block of bytes, as Base64 decodes into.
class MemoryOutputStream {
public:
    MemoryOutputStream() = default;
    explicit MemoryOutputStream(size_t initialSize) { data_.reserve(initialSize); }
    const void* getData() const { return data_.data(); }
    size_t getDataSize() const { return data_.size(); }
    void write(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        data_.insert(data_.end(), bytes, bytes + size);
    }

private:
    std::vector<uint8_t> data_;
};

// Standard base64 (RFC 4648, padded), as juce::Base64 reads and writes it.
struct Base64 {
    static String toBase64(const void* data, size_t size) {
        static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const auto* bytes = static_cast<const uint8_t*>(data);
        std::string out;
        out.reserve((size + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < size; i += 3) {
            const uint32_t n = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
            out += kAlphabet[(n >> 18) & 63];
            out += kAlphabet[(n >> 12) & 63];
            out += kAlphabet[(n >> 6) & 63];
            out += kAlphabet[n & 63];
        }
        if (i < size) {
            uint32_t n = uint32_t(bytes[i]) << 16;
            if (i + 1 < size) n |= uint32_t(bytes[i + 1]) << 8;
            out += kAlphabet[(n >> 18) & 63];
            out += kAlphabet[(n >> 12) & 63];
            out += i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=';
            out += '=';
        }
        return out;
    }

    // Decodes `text` into `out`; false if it isn't base64 (what came before the fault is kept).
    static bool convertFromBase64(MemoryOutputStream& out, const String& text) {
        const std::string& in = text.toStdString();
        uint32_t bits = 0;
        int count = 0;
        std::vector<uint8_t> bytes;
        bytes.reserve(in.size() / 4 * 3);
        for (const char c : in) {
            int value;
            if (c >= 'A' && c <= 'Z') value = c - 'A';
            else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
            else if (c >= '0' && c <= '9') value = c - '0' + 52;
            else if (c == '+') value = 62;
            else if (c == '/') value = 63;
            else if (c == '=') break;
            else if (std::isspace(static_cast<unsigned char>(c))) continue;
            else {
                out.write(bytes.data(), bytes.size());
                return false;
            }
            bits = (bits << 6) | static_cast<uint32_t>(value);
            if (++count == 4) {
                bytes.push_back(static_cast<uint8_t>(bits >> 16));
                bytes.push_back(static_cast<uint8_t>(bits >> 8));
                bytes.push_back(static_cast<uint8_t>(bits));
                bits = 0;
                count = 0;
            }
        }
        if (count == 2) {
            bytes.push_back(static_cast<uint8_t>(bits >> 4));
        } else if (count == 3) {
            bytes.push_back(static_cast<uint8_t>(bits >> 10));
            bytes.push_back(static_cast<uint8_t>(bits >> 2));
        }
        out.write(bytes.data(), bytes.size());
        return true;
    }
};

struct FloatVectorOperations {
    // Vital calls this at the start of every block. SUBstation's renderer
    // already flushes denormals where it renders (ScopedNoDenormals), so this
    // only makes sure of it, as JUCE does.
    static void disableDenormalisedNumberSupport() noexcept {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
        _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ | DAZ
#endif
    }
};

inline void File::readLines(StringArray& lines) const {
    std::ifstream in(std::filesystem::path(std::u8string(path_.begin(), path_.end())), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.add(line);
    }
}

}  // namespace juce

using namespace juce;

// The version presets are written with (and older ones upgraded from): Vital's
// own at the vendored commit.
namespace ProjectInfo {
inline const char* const projectName = "Vital";
inline const char* const versionString = "1.0.6";
inline const int versionNumber = 0x10006;
}  // namespace ProjectInfo
