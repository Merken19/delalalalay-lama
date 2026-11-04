#pragma once

#include <string>
#include <string_view>

namespace delay_lama::voice {

// The stdin protocol of DelayLamaVoice.exe: one text command per line, as
// described in the Delay Lama Everything docs (ARCHITECTURE.md, "Pipe
// protocol"). Change the version whenever the commands change.
constexpr int kProtocolVersion = 1;
// Protocol 1 only adds to protocol 0, so `hello 0` is accepted too.
constexpr int kOldestProtocolVersion = 0;

enum class CommandType {
    Hello,       // hello <version>
    NoteOn,      // on <note> <velocity> [<duration ms>]; velocity 0 is a note-off
    NoteOff,     // off <note>
    Bend,        // bend <0..16383>, centre 8192
    BendRange,   // bendrange <semitones>
    Cc,          // cc <number> <value>
    Program,     // program <bank> <program>
    AllNotesOff, // allnotesoff
    Set,         // set <key> <value>
    Next,        // next <key> <value>: profile override for the next note (protocol 1)
    Wait,        // wait <milliseconds>: scripts and --render only
    Quit,        // quit
};

struct Command {
    CommandType type = CommandType::Quit;
    // hello: version; on: note, velocity; off: note; bend: value;
    // cc: number, value; program: bank, program.
    int a = 0;
    int b = 0;
    // bendrange: semitones; wait: milliseconds; set, next: value;
    // on: duration in ms, or -1 when not given.
    double value = 0.0;
    // set, next: key.
    std::string key;
};

enum class ParseResult { Command, Blank, Error };

// Parses one line (without or with its trailing "\r\n"). Blank lines and
// lines starting with '#' give Blank. Command words are case-insensitive.
// On Error, `error` says what is wrong in a few words.
ParseResult parse_line(std::string_view line, Command &command, std::string &error);

} // namespace delay_lama::voice
