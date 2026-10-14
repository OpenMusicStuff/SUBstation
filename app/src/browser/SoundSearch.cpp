#include "browser/SoundSearch.h"

#include <QStringList>

#include <cmath>
#include <optional>

namespace sub::app {

namespace tagging = intelligence::tagging;

namespace {

class TagMatcher final : public browser::SoundMatcher {
public:
    TagMatcher(std::shared_ptr<const tagging::SoundTagTable> tags, tagging::WordQuery words,
               std::optional<tagging::Meaning> filter)
        : tags_(std::move(tags)), words_(std::move(words)), filter_(std::move(filter)) {}

    bool hear(uint64_t fileHash, uint64_t& heard, float& score) const override {
        heard = 0;
        score = -1.f;
        const tagging::SoundTags* t = tags_->find(fileHash);
        if (!t) return !filter_;  // (not analysed yet: only its name can match, and no filter can keep it)
        if (filter_ && !filter_->heardIn(*t)) return false;
        if (words_.any()) heard = words_.hear(*t, score);
        else if (filter_) score = filter_->degree(*t);
        return true;
    }

private:
    std::shared_ptr<const tagging::SoundTagTable> tags_;
    tagging::WordQuery words_;
    std::optional<tagging::Meaning> filter_;
};

}  // namespace

SoundMatching soundMatching(std::shared_ptr<const tagging::SoundTagTable> tags, const QString& filter) {
    if (!tags) return {};
    std::optional<tagging::Meaning> meaning;
    if (!filter.isEmpty()) {
        meaning = tagging::meaningOf(filter.toStdString());
        if (meaning->type == tagging::Meaning::Type::None) meaning.reset();
    }
    return [tags, meaning](const std::vector<std::string>& terms) -> std::shared_ptr<const browser::SoundMatcher> {
        tagging::WordQuery words(terms);
        if (!words.any() && !meaning) return nullptr;
        return std::make_shared<TagMatcher>(tags, std::move(words), meaning);
    };
}

QString soundLabel(const tagging::SoundTagTable* tags, const std::string& path) {
    if (!tags) return {};
    const tagging::SoundTags* t = tags->find(path);
    return t ? QString::fromStdString(tagging::label(*t)) : QString();
}

QString soundToolTip(const tagging::SoundTagTable* tags, const std::string& path) {
    if (!tags) return {};
    const tagging::SoundTags* t = tags->find(path);
    if (!t) return {};
    QStringList lines;
    const auto percent = [](float p) { return QString::number(std::lround(p * 100.f)) + QStringLiteral("%"); };
    QStringList guesses;
    for (const auto& g : t->kinds)
        if (g.kind != tagging::Kind::None && g.probability >= 13)  // (5% or more)
            guesses << QStringLiteral("%1 %2").arg(QString::fromStdString(tagging::kindTitle(g.kind)),
                                                   percent(tagging::SoundTags::unit(g.probability)));
    if (!guesses.isEmpty()) lines << QStringLiteral("Sounds like: ") + guesses.join(QStringLiteral(", "));
    lines << (t->isLoop() ? QStringLiteral("A loop") : QStringLiteral("A one-shot"));
    if (std::isfinite(t->pitch)) lines << QStringLiteral("Note: ") + QString::fromStdString(tagging::noteName(t->pitch));
    const QString words = QString::fromStdString(tagging::label(*t, 6));
    if (!words.isEmpty()) lines << QStringLiteral("Heard as: ") + words;
    return lines.join(QLatin1Char('\n'));
}

std::vector<std::pair<QString, QString>> soundCategories() {
    return {{QStringLiteral("Kicks"), QStringLiteral("kick")},     {QStringLiteral("Snares"), QStringLiteral("snare")},
            {QStringLiteral("Claps"), QStringLiteral("clap")},     {QStringLiteral("Hats"), QStringLiteral("hat")},
            {QStringLiteral("Cymbals"), QStringLiteral("cymbal")}, {QStringLiteral("Toms"), QStringLiteral("tom")},
            {QStringLiteral("Percussion"), QStringLiteral("perc")}, {QStringLiteral("Bass"), QStringLiteral("bass")},
            {QStringLiteral("Synths"), QStringLiteral("synths")},  {QStringLiteral("Keys"), QStringLiteral("keys")},
            {QStringLiteral("Guitars"), QStringLiteral("guitar")},
            {QStringLiteral("Vocals"), QStringLiteral("vocal")},   {QStringLiteral("Loops"), QStringLiteral("loop")}};
}

}  // namespace sub::app
