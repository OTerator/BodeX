#include "model/Bidi.h"

#include <algorithm>
#include <climits>

namespace gt {

namespace {

// --- BiDi character classes we care about (subset of UAX #9) ----------------
enum Bc {
    L, R, AL,               // strong
    EN, ES, ET, AN, CS, NSM,// weak
    BN, B, S, WS, ON        // neutrals / boundary
};

// Classify a codepoint. Only the ranges relevant to Hebrew/Latin/number notes are
// enumerated; everything unlisted falls back to L (strong LTR) or ON (punctuation).
Bc bidiClass(char32_t c)
{
    switch (c) {
        case U'\n': return B;
        case U'\t': return S;
        case U' ':  return WS;
        case 0x00A0: return CS;   // no-break space
        case 0x200E: return L;    // LRM
        case 0x200F: return R;    // RLM
        case 0x200C: case 0x200D: return BN; // ZWNJ / ZWJ
        default: break;
    }
    if (c >= U'0' && c <= U'9')  return EN;
    if (c == U'+' || c == U'-')  return ES;
    if (c == U'#' || c == U'$' || c == U'%') return ET;
    if (c == U',' || c == U'.' || c == U':' || c == U'/') return CS;
    if ((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z')) return L;

    // Hebrew block U+0590..U+05FF: combining points/accents are NSM, the rest R.
    if (c >= 0x0591 && c <= 0x05BD) return NSM;
    if (c == 0x05BF || c == 0x05C1 || c == 0x05C2 ||
        c == 0x05C4 || c == 0x05C5 || c == 0x05C7) return NSM;
    if (c >= 0x0590 && c <= 0x05FF) return R;
    if (c == 0x05BE || c == 0x05C0 || c == 0x05C3 || c == 0x05C6) return R; // punct, still R

    // Arabic (basic, no shaping): digits are AN, letters AL.
    if (c >= 0x0660 && c <= 0x0669) return AN;
    if (c >= 0x0600 && c <= 0x06FF) return AL;
    if (c >= 0x0750 && c <= 0x077F) return AL; // Arabic Supplement

    // ASCII punctuation / symbols -> neutral.
    if (c < 0x80 && !((c >= U'0' && c <= U'9') ||
                      (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z')))
        return ON;

    return L; // default: treat unknown scripts as strong LTR
}

bool isStrongClass(Bc t) { return t == L || t == R || t == AL; }
bool isNeutral(Bc t)     { return t == B || t == S || t == WS || t == ON || t == BN; }

// Direction a resolved type contributes for neutral resolution (N-rules):
// numbers count as R. Returns L or R.
Bc strongDir(Bc t) { return (t == L) ? L : R; } // t in {L,R,EN,AN} here -> R for the rest

// Reorder one line [lo,hi) (no newlines) into visual order; returns global logical
// indices (lo-based) in left-to-right drawing order. `outRtl` is appended in lockstep
// with the return value: 1 where the resolved level of that visual glyph is odd (RTL),
// used by callers for mirroring and caret-side geometry.
std::vector<int> reorderLine(const std::u32string& logical, int lo, int hi, bool baseRtl,
                             std::vector<char>& outRtl)
{
    const int len = hi - lo;
    std::vector<int> out;
    if (len <= 0) return out;

    const int paraLevel = baseRtl ? 1 : 0;
    const Bc  sorType   = (paraLevel & 1) ? R : L; // start/end-of-run direction

    std::vector<Bc> t(len), orig(len);
    for (int k = 0; k < len; ++k)
        orig[k] = t[k] = bidiClass(logical[lo + k]);

    // ---- W1: NSM takes the type of the previous char (sor at run start) --------
    for (int k = 0; k < len; ++k)
        if (t[k] == NSM)
            t[k] = (k == 0) ? sorType : t[k - 1];

    // ---- W2: EN -> AN when the last strong type is AL --------------------------
    {
        Bc lastStrong = sorType;
        for (int k = 0; k < len; ++k) {
            if (isStrongClass(t[k])) lastStrong = t[k];
            else if (t[k] == EN && lastStrong == AL) t[k] = AN;
        }
    }
    // ---- W3: AL -> R -----------------------------------------------------------
    for (int k = 0; k < len; ++k)
        if (t[k] == AL) t[k] = R;

    // ---- W4: a single separator between two like numbers joins them ------------
    for (int k = 1; k + 1 < len; ++k) {
        if (t[k] == ES && t[k - 1] == EN && t[k + 1] == EN) t[k] = EN;
        else if (t[k] == CS && t[k - 1] == EN && t[k + 1] == EN) t[k] = EN;
        else if (t[k] == CS && t[k - 1] == AN && t[k + 1] == AN) t[k] = AN;
    }
    // ---- W5: a run of ET adjacent to EN becomes EN -----------------------------
    for (int k = 0; k < len; ) {
        if (t[k] != ET) { ++k; continue; }
        int a = k; while (k < len && t[k] == ET) ++k; // run [a,k)
        const bool touchEN = (a > 0 && t[a - 1] == EN) || (k < len && t[k] == EN);
        if (touchEN) for (int m = a; m < k; ++m) t[m] = EN;
    }
    // ---- W6: remaining ES/ET/CS -> ON ------------------------------------------
    for (int k = 0; k < len; ++k)
        if (t[k] == ES || t[k] == ET || t[k] == CS) t[k] = ON;
    // ---- W7: EN -> L when the last strong type is L ----------------------------
    {
        Bc lastStrong = sorType;
        for (int k = 0; k < len; ++k) {
            if (t[k] == L || t[k] == R) lastStrong = t[k];
            else if (t[k] == EN && lastStrong == L) t[k] = L;
        }
    }

    // ---- N1/N2: resolve runs of neutrals ---------------------------------------
    for (int k = 0; k < len; ) {
        if (!isNeutral(t[k])) { ++k; continue; }
        int a = k; while (k < len && isNeutral(t[k])) ++k; // NI run [a,k)
        const Bc left  = (a == 0)   ? sorType : strongDir(t[a - 1]);
        const Bc right = (k == len) ? sorType : strongDir(t[k]);
        const Bc resolved = (left == right) ? left               // N1: matching sides
                                            : ((paraLevel & 1) ? R : L); // N2: embedding dir
        for (int m = a; m < k; ++m) t[m] = resolved;
    }

    // ---- I1/I2: implicit levels ------------------------------------------------
    std::vector<int> level(len, paraLevel);
    for (int k = 0; k < len; ++k) {
        if ((paraLevel & 1) == 0) {            // even (LTR) base
            if (t[k] == R)                 level[k] = paraLevel + 1;
            else if (t[k] == EN || t[k] == AN) level[k] = paraLevel + 2;
        } else {                                // odd (RTL) base
            if (t[k] == L || t[k] == EN || t[k] == AN) level[k] = paraLevel + 1;
        }
    }

    // ---- L1: reset separators and trailing/pre-separator whitespace ------------
    for (int k = len - 1; k >= 0; --k) {       // trailing whitespace at end of line
        if (orig[k] == WS || orig[k] == BN || orig[k] == S || orig[k] == B)
            level[k] = paraLevel;
        else break;
    }
    for (int k = 0; k < len; ++k)              // whitespace before a segment/para sep
        if (orig[k] == S || orig[k] == B) {
            level[k] = paraLevel;
            for (int m = k - 1; m >= 0 && (orig[m] == WS || orig[m] == BN); --m)
                level[m] = paraLevel;
        }

    // ---- L2: reverse contiguous runs, highest level down to lowest odd ---------
    std::vector<int> vis(len);
    for (int k = 0; k < len; ++k) vis[k] = k;
    int maxLevel = 0, minOdd = INT_MAX;
    for (int k = 0; k < len; ++k) {
        maxLevel = std::max(maxLevel, level[k]);
        if (level[k] & 1) minOdd = std::min(minOdd, level[k]);
    }
    for (int lv = maxLevel; lv >= minOdd; --lv) {
        int p = 0;
        while (p < len) {
            if (level[p] >= lv) {
                int start = p;
                while (p < len && level[p] >= lv) ++p;
                std::reverse(vis.begin() + start, vis.begin() + p);
            } else ++p;
        }
    }

    out.reserve(len);
    for (int k = 0; k < len; ++k) {
        out.push_back(lo + vis[k]);
        outRtl.push_back((level[vis[k]] & 1) ? 1 : 0);
    }
    return out;
}

} // namespace

std::u32string utf8ToCodepoints(const std::string& s)
{
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp; int extra;
        if (c < 0x80)      { cp = c;        extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E){ cp = c & 0x07; extra = 3; }
        else { out.push_back(0xFFFD); ++i; continue; } // invalid lead byte
        if (i + extra >= n) { out.push_back(0xFFFD); break; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

std::string codepointsToUtf8(const std::u32string& cps)
{
    std::string out;
    out.reserve(cps.size());
    for (char32_t cp : cps) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

char32_t bidiMirror(char32_t c)
{
    switch (c) {
        case U'(': return U')';   case U')': return U'(';
        case U'[': return U']';   case U']': return U'[';
        case U'{': return U'}';   case U'}': return U'{';
        case U'<': return U'>';   case U'>': return U'<';
        case 0x00AB: return 0x00BB; case 0x00BB: return 0x00AB; // guillemets « »
        default: return c;
    }
}

// Base direction over the whole text: first strong char (P2/P3), unless forced.
static bool computeBaseRtl(const std::u32string& logical, BaseDir base)
{
    if (base == BaseDir::LTR) return false;
    if (base == BaseDir::RTL) return true;
    for (char32_t c : logical) {
        Bc t = bidiClass(c);
        if (t == L)             return false;
        if (t == R || t == AL)  return true;
    }
    return false; // no strong char -> LTR
}

BidiResult bidiReorder(const std::u32string& logical, BaseDir base)
{
    BidiResult res;
    const int n = static_cast<int>(logical.size());
    res.baseRtl = computeBaseRtl(logical, base);
    res.logicalToVisual.assign(n, 0);

    std::vector<int>  order;      // visual -> logical index
    std::vector<char> orderRtl;   // visual -> 1 if RTL level (lockstep with `order`)
    order.reserve(n);
    orderRtl.reserve(n);
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && logical[j] != U'\n') ++j;
        std::vector<int> seg = reorderLine(logical, i, j, res.baseRtl, orderRtl);
        for (int idx : seg) order.push_back(idx);
        if (j < n) { order.push_back(j); orderRtl.push_back(res.baseRtl ? 1 : 0); } // newline keeps its place
        i = (j < n) ? j + 1 : j;
    }

    res.visualToLogical = order;
    res.visual.resize(order.size());
    res.visualRtl.assign(order.size(), 0);
    for (int v = 0; v < static_cast<int>(order.size()); ++v) {
        const int      li  = order[v];
        const bool     rtl = orderRtl[v] != 0;
        const char32_t cp  = logical[li];
        res.visual[v]    = rtl ? bidiMirror(cp) : cp; // mirror paired glyphs in RTL runs
        res.visualRtl[v] = rtl ? 1 : 0;
        res.logicalToVisual[li] = v;
    }
    return res;
}

std::string bidiVisualUtf8(const std::string& logicalUtf8, BaseDir base)
{
    const std::u32string cps = utf8ToCodepoints(logicalUtf8);
    return codepointsToUtf8(bidiReorder(cps, base).visual);
}

bool bidiBaseIsRtl(const std::string& logicalUtf8, BaseDir base)
{
    return computeBaseRtl(utf8ToCodepoints(logicalUtf8), base);
}

} // namespace gt
