// Checks for the DelayLamaVoice stdin protocol parser (Windows/voice_protocol.h).
// No assert(): CTest runs Release builds, where NDEBUG removes it.

#include "voice_protocol.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace delay_lama::voice;

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
    if (!condition) {
        std::fprintf(stderr, "voice_protocol: FAIL %s\n", what.c_str());
        ++failures;
    }
}

Command parse_ok(const std::string &line) {
    Command command;
    std::string error;
    const ParseResult result = parse_line(line, command, error);
    check(result == ParseResult::Command, "'" + line + "' parses (" + error + ")");
    return command;
}

void parse_error(const std::string &line) {
    Command command;
    std::string error;
    const ParseResult result = parse_line(line, command, error);
    check(result == ParseResult::Error, "'" + line + "' is rejected");
    check(result != ParseResult::Error || !error.empty(), "'" + line + "' says why");
}

void parse_blank(const std::string &line) {
    Command command;
    std::string error;
    check(parse_line(line, command, error) == ParseResult::Blank, "'" + line + "' is blank");
}

} // namespace

int main() {
    check(kProtocolVersion == 1 && kOldestProtocolVersion == 0, "protocol versions 0 to 1");

    Command c = parse_ok("hello 0");
    check(c.type == CommandType::Hello && c.a == 0, "hello 0");
    c = parse_ok("on 60 100");
    check(c.type == CommandType::NoteOn && c.a == 60 && c.b == 100 && c.value == -1.0,
          "on 60 100 (no duration: -1)");
    c = parse_ok("on 60 100 250.5");
    check(c.type == CommandType::NoteOn && c.a == 60 && std::fabs(c.value - 250.5) < 1e-9,
          "on 60 100 250.5 (protocol 1 duration)");
    c = parse_ok("on 60 100 -1");
    check(c.type == CommandType::NoteOn && c.value == -1.0, "on with duration -1 (unknown)");
    c = parse_ok("next Onset.Vowel 0.7");
    check(c.type == CommandType::Next && c.key == "onset.vowel" &&
              std::fabs(c.value - 0.7) < 1e-9,
          "next onset.vowel 0.7");
    c = parse_ok("  ON\t60   100 \r\n");
    check(c.type == CommandType::NoteOn && c.a == 60 && c.b == 100,
          "case, tabs, spaces and CRLF are accepted");
    c = parse_ok("on 0 0");
    check(c.type == CommandType::NoteOn && c.a == 0 && c.b == 0, "on 0 0 (a note-off)");
    c = parse_ok("off 127");
    check(c.type == CommandType::NoteOff && c.a == 127, "off 127");
    c = parse_ok("bend 0");
    check(c.type == CommandType::Bend && c.a == 0, "bend 0");
    c = parse_ok("bend 16383");
    check(c.type == CommandType::Bend && c.a == 16383, "bend 16383");
    c = parse_ok("bendrange 2.5");
    check(c.type == CommandType::BendRange && std::fabs(c.value - 2.5) < 1e-9, "bendrange 2.5");
    c = parse_ok("cc 64 127");
    check(c.type == CommandType::Cc && c.a == 64 && c.b == 127, "cc 64 127");
    c = parse_ok("program 16383 127");
    check(c.type == CommandType::Program && c.a == 16383 && c.b == 127, "program 16383 127");
    c = parse_ok("allnotesoff");
    check(c.type == CommandType::AllNotesOff, "allnotesoff");
    c = parse_ok("set Glide 0.25");
    check(c.type == CommandType::Set && c.key == "glide" && std::fabs(c.value - 0.25) < 1e-9,
          "set Glide 0.25 (key lowercased)");
    c = parse_ok("wait 250.5");
    check(c.type == CommandType::Wait && std::fabs(c.value - 250.5) < 1e-9, "wait 250.5");
    c = parse_ok("quit");
    check(c.type == CommandType::Quit, "quit");

    parse_blank("");
    parse_blank("   \t\r\n");
    parse_blank("# a comment");
    parse_blank("#on 60 100");

    parse_error("bogus 1");
    parse_error("on");
    parse_error("on 60");
    parse_error("on 60 100 7 8");
    parse_error("on 60 100 x");
    parse_error("next onset.vowel");
    parse_error("on 128 100");
    parse_error("on -1 100");
    parse_error("on 60 128");
    parse_error("on 60.5 100");
    parse_error("on 6x 100");
    parse_error("on 0x3c 100");
    parse_error("off");
    parse_error("bend 16384");
    parse_error("bend -1");
    parse_error("bendrange -1");
    parse_error("bendrange 200");
    parse_error("bendrange nan");
    parse_error("bendrange inf");
    parse_error("cc 128 0");
    parse_error("cc 7 128");
    parse_error("program 0 128");
    parse_error("program 16384 0");
    parse_error("allnotesoff now");
    parse_error("set glide");
    parse_error("set glide abc");
    parse_error("wait -5");
    parse_error("hello");
    parse_error("quit now");

    Command kept = parse_ok("on 61 90");
    std::string error;
    parse_line("on 999 1", kept, error);
    check(kept.type == CommandType::NoteOn && kept.a == 61 && kept.b == 90,
          "a rejected line leaves the command untouched");
    check(error.find("usage: on") == 0, "a bad on says its usage");
    parse_line("bogus", kept, error);
    check(error == "unknown command bogus", "an unknown command is named");

    if (failures != 0) {
        std::fprintf(stderr, "voice_protocol: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("voice_protocol: PASS\n");
    return 0;
}
