#include "voice_protocol.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace delay_lama::voice {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string_view> split_words(std::string_view line) {
    std::vector<std::string_view> words;
    size_t index = 0;
    while (index < line.size()) {
        while (index < line.size() && is_space(line[index]))
            ++index;
        const size_t start = index;
        while (index < line.size() && !is_space(line[index]))
            ++index;
        if (index > start)
            words.push_back(line.substr(start, index - start));
    }
    return words;
}

std::string lower(std::string_view word) {
    std::string result(word);
    for (char &c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

// Whole word as a decimal integer in [low, high].
bool parse_int(std::string_view word, int low, int high, int &value) {
    const std::string text(word);
    if (text.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end != text.c_str() + text.size() || parsed < low || parsed > high)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

// Whole word as a finite number in [low, high].
bool parse_number(std::string_view word, double low, double high, double &value) {
    const std::string text(word);
    if (text.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end != text.c_str() + text.size() || !std::isfinite(parsed) ||
        parsed < low || parsed > high)
        return false;
    value = parsed;
    return true;
}

struct Spec {
    const char *word;
    CommandType type;
    int arguments;
    const char *usage;
};

constexpr Spec kSpecs[] = {
    {"hello", CommandType::Hello, 1, "hello <version>"},
    {"on", CommandType::NoteOn, 2, "on <note 0-127> <velocity 0-127>"},
    {"off", CommandType::NoteOff, 1, "off <note 0-127>"},
    {"bend", CommandType::Bend, 1, "bend <0-16383>"},
    {"bendrange", CommandType::BendRange, 1, "bendrange <semitones 0-127>"},
    {"cc", CommandType::Cc, 2, "cc <number 0-127> <value 0-127>"},
    {"program", CommandType::Program, 2, "program <bank 0-16383> <program 0-127>"},
    {"allnotesoff", CommandType::AllNotesOff, 0, "allnotesoff"},
    {"set", CommandType::Set, 2, "set <key> <value>"},
    {"wait", CommandType::Wait, 1, "wait <milliseconds 0-3600000>"},
    {"quit", CommandType::Quit, 0, "quit"},
};

} // namespace

ParseResult parse_line(std::string_view line, Command &command, std::string &error) {
    const std::vector<std::string_view> words = split_words(line);
    if (words.empty() || words[0].front() == '#')
        return ParseResult::Blank;

    const std::string word = lower(words[0]);
    const Spec *spec = nullptr;
    for (const Spec &candidate : kSpecs) {
        if (word == candidate.word) {
            spec = &candidate;
            break;
        }
    }
    if (!spec) {
        error = "unknown command " + std::string(words[0]);
        return ParseResult::Error;
    }
    if (static_cast<int>(words.size()) - 1 != spec->arguments) {
        error = std::string("usage: ") + spec->usage;
        return ParseResult::Error;
    }

    Command parsed;
    parsed.type = spec->type;
    bool ok = true;
    switch (spec->type) {
    case CommandType::Hello:
        ok = parse_int(words[1], 0, 1000000, parsed.a);
        break;
    case CommandType::NoteOn:
        ok = parse_int(words[1], 0, 127, parsed.a) && parse_int(words[2], 0, 127, parsed.b);
        break;
    case CommandType::NoteOff:
        ok = parse_int(words[1], 0, 127, parsed.a);
        break;
    case CommandType::Bend:
        ok = parse_int(words[1], 0, 16383, parsed.a);
        break;
    case CommandType::BendRange:
        ok = parse_number(words[1], 0.0, 127.0, parsed.value);
        break;
    case CommandType::Cc:
        ok = parse_int(words[1], 0, 127, parsed.a) && parse_int(words[2], 0, 127, parsed.b);
        break;
    case CommandType::Program:
        ok = parse_int(words[1], 0, 16383, parsed.a) && parse_int(words[2], 0, 127, parsed.b);
        break;
    case CommandType::Set:
        parsed.key = lower(words[1]);
        ok = parse_number(words[2], -1.0e6, 1.0e6, parsed.value);
        break;
    case CommandType::Wait:
        ok = parse_number(words[1], 0.0, 3600000.0, parsed.value);
        break;
    case CommandType::AllNotesOff:
    case CommandType::Quit:
        break;
    }
    if (!ok) {
        error = std::string("usage: ") + spec->usage;
        return ParseResult::Error;
    }
    command = std::move(parsed);
    return ParseResult::Command;
}

} // namespace delay_lama::voice
