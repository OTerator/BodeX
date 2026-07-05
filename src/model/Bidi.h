#pragma once

// Reduced Unicode Bidirectional Algorithm (UAX #9), enough for Hebrew notes.
//
// Dear ImGui draws and edits glyphs strictly left-to-right in memory order and has
// no BiDi engine, so to display Hebrew (or mixed Hebrew/Latin/numbers) correctly we
// reorder the logical (as-typed) codepoints into visual (as-drawn) order ourselves.
//
// Scope: strong L/R (+ basic AL), European/Arabic numbers, neutrals. No explicit
// embedding controls (LRE/RLE/…) and no Arabic joining/shaping — Hebrew needs
// neither. Deliberately GUI-free and header-light so it lives in the model layer
// and is unit-tested (see tests/test_core.cpp). All string I/O is UTF-8.

#include <string>
#include <vector>
#include <cstdint>

namespace gt {

// Requested base paragraph direction. Auto derives it from the first strong
// character (UAX #9 rules P2/P3); LTR/RTL force it (e.g. the Ctrl+Shift toggle).
enum class BaseDir { Auto, LTR, RTL };

struct BidiResult {
    std::u32string   visual;          // codepoints in visual (drawing) order
    std::vector<int> visualToLogical; // size == visual.size(); logical index of each
    std::vector<int> logicalToVisual; // size == logical.size(); visual index of each
    bool             baseRtl = false; // resolved base direction (for alignment)
};

// Reorder logical -> visual. Newlines ('\n') are hard separators: each line is
// reordered independently and the newline keeps its logical position.
BidiResult bidiReorder(const std::u32string& logical, BaseDir base);

// Convenience: UTF-8 logical text -> UTF-8 visual (drawing-order) text.
std::string bidiVisualUtf8(const std::string& logicalUtf8, BaseDir base);

// Resolved base direction for the text (true = RTL). Drives right-alignment.
bool bidiBaseIsRtl(const std::string& logicalUtf8, BaseDir base);

// Portable UTF-8 <-> UTF-32 (codepoints). Lenient: malformed bytes decode to the
// replacement character U+FFFD. No Win32 dependency (unlike util/utf.h).
std::u32string utf8ToCodepoints(const std::string& s);
std::string    codepointsToUtf8(const std::u32string& cps);

} // namespace gt
