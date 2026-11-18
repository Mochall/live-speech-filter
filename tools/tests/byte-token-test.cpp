// MergeByteTokens: byte-fallback tokens from the streaming model become characters.
// Token strings below are taken from a 2026-11-07 live-mic ASR debug log ("卧槽" was
// emitted as six byte tokens and therefore never matched the lexicon).
#include "utils.hpp"
#include <cassert>
int main() {
    std::vector<std::string> t;
    std::vector<float> ts;
    const char *a[] = {"你", "<0xE5>", "<0x8D>", "<0xA7>", "<0xE6>", "<0xA7>", "<0xBD>", "了"};
    float at[] = {0.0f, 0.1f, 0.12f, 0.14f, 0.3f, 0.32f, 0.34f, 0.5f};
    MergeByteTokens(a, at, 8, t, ts);
    assert(t.size() == 4 && t[1] == "\xE5\x8D\xA7" && t[2] == "\xE6\xA7\xBD" && t[3] == "了");
    assert(ts[1] == 0.1f && ts[2] == 0.3f && ts[3] == 0.5f);
    std::string joined; for (auto &s : t) joined += s;
    assert(joined == "你卧槽了");
    // Incomplete sequence at the end of a partial result: keep the raw tokens.
    const char *b[] = {"臭", "<0xE8>", "<0x87>"};
    float bt[] = {0, 1, 2};
    MergeByteTokens(b, bt, 3, t, ts);
    assert(t.size() == 3 && t[1] == "<0xE8>" && t[2] == "<0x87>");
    // Invalid continuation byte: kept as-is.
    const char *c[] = {"<0xE8>", "<0x41>", "<0xAD>"};
    MergeByteTokens(c, bt, 3, t, ts);
    assert(t.size() == 3 && t[0] == "<0xE8>");
    // Not byte tokens.
    const char *d[] = {"<0xE8", "<unk>", "ab"};
    MergeByteTokens(d, bt, 3, t, ts);
    assert(t.size() == 3 && t[0] == "<0xE8" && t[1] == "<unk>");
    MergeByteTokens(nullptr, nullptr, 0, t, ts);
    assert(t.empty() && ts.empty());
}
