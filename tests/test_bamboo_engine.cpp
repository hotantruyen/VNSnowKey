/*
 * VNSnowKey - Unit Tests for BambooEngine & SessionProcessor
 * Verifies 100% behavioral parity with BambooEngine/bamboo-core
 * plus VNSnowKey extensions (shorthand, English auto-restore, macros, charsets).
 */

#include "../src/engine/bamboo_engine.hpp"
#include "../src/engine/bamboo_utils.hpp"
#include "../src/engine/charset_encoder.hpp"
#include "../src/engine/flattener.hpp"
#include "../src/engine/macro_table.hpp"
#include "../src/engine/rules_parser.hpp"
#include "../src/engine/session_processor.hpp"
#include "../src/engine/spelling.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace vnsnowkey::bamboo;

namespace {

int g_passed = 0;
int g_failed = 0;

void checkEq(const std::string& got, const std::string& expected, const char* expr, int line) {
    if (got == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << got
                  << "], expected [" << expected << "]\n";
    }
}

void checkBool(bool got, bool expected, const char* expr, int line) {
    if (got == expected) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] Line " << line << " (" << expr << "): got [" << (got ? "true" : "false")
                  << "], expected [" << (expected ? "true" : "false") << "]\n";
    }
}

#define EXPECT_STR_EQ(got, exp) checkEq((got), (exp), #got, __LINE__)
#define EXPECT_BOOL_EQ(got, exp) checkBool((got), (exp), #got, __LINE__)

BambooEngine newStdEngine(const std::string& imName = "Telex 2", uint32_t flags = EstdFlags) {
    auto im = parseInputMethod(getInputMethodDefinitions(), imName);
    return BambooEngine(im, flags);
}

// Helper to simulate typing a full string into SessionProcessor and maintaining a virtual screen buffer
std::string simulateSessionTyping(SessionProcessor& sp, std::string_view input) {
    std::u32string screen;
    std::u32string u32In = utf8ToU32(input);
    for (char32_t c : u32In) {
        if (c == U'\b') {
            KeyActionDiff diff = sp.processBackspace();
            if (diff.passThrough) {
                if (!screen.empty()) screen.pop_back();
            } else {
                for (int i = 0; i < diff.backspaces && !screen.empty(); ++i) {
                    screen.pop_back();
                }
                screen.append(diff.textToEmit);
            }
        } else {
            KeyActionDiff diff = sp.processPrintableKey(c);
            if (diff.passThrough) {
                screen.push_back(c);
            } else {
                for (int i = 0; i < diff.backspaces && !screen.empty(); ++i) {
                    screen.pop_back();
                }
                screen.append(diff.textToEmit);
            }
        }
    }
    return u32ToUtf8(screen);
}

void testUtilsAndRulesParser() {
    // utils_test.go parity
    EXPECT_BOOL_EQ(isVowel(U'a'), true);
    EXPECT_BOOL_EQ(isVowel(U'á'), true);
    EXPECT_BOOL_EQ(isVowel(U'b'), false);
    for (char32_t v : getVowels()) {
        EXPECT_BOOL_EQ(isVowel(v), true);
    }
    EXPECT_BOOL_EQ(findToneFromChar(U'e') == Tone::None, true);
    EXPECT_BOOL_EQ(findToneFromChar(U'è') == Tone::Grave, true);
    EXPECT_BOOL_EQ(findToneFromChar(U'é') == Tone::Acute, true);
    EXPECT_BOOL_EQ(findToneFromChar(U'ẽ') == Tone::Tilde, true);
    EXPECT_BOOL_EQ(findToneFromChar(U'ẻ') == Tone::Hook, true);
    EXPECT_BOOL_EQ(findToneFromChar(U'ạ') == Tone::Dot, true);
    EXPECT_BOOL_EQ(addToneToChar(U'a', static_cast<uint8_t>(Tone::Dot)) == U'ạ', true);
    EXPECT_BOOL_EQ(addToneToChar(U'y', 0) == U'y', true);
    EXPECT_BOOL_EQ(addMarkToChar(U'y', 0) == U'y', true);
    EXPECT_BOOL_EQ(addMarkToChar(U'ạ', static_cast<uint8_t>(Mark::Breve)) == U'ặ', true);

    // rules_parser_test.go parity
    auto rZ = parseRules(U'z', "XoaDauThanh");
    EXPECT_BOOL_EQ(rZ.size() == 1 && rZ[0].effectType == EffectType::ToneTransformation && rZ[0].getTone() == Tone::None, true);
    auto rX = parseRules(U'x', "DauNga");
    EXPECT_BOOL_EQ(rX.size() == 1 && rX[0].effectType == EffectType::ToneTransformation && rX[0].getTone() == Tone::Tilde, true);

    auto rD = parseTonelessRules(U'd', "D_Đ");
    EXPECT_BOOL_EQ(rD.size() == 2 && rD[0].effectType == EffectType::MarkTransformation &&
                   rD[0].getMark() == Mark::Dash && rD[0].effectOn == U'd', true);

    auto rBrace = parseTonelessRules(U'{', "_Ư");
    EXPECT_BOOL_EQ(rBrace.size() == 1 && rBrace[0].effectType == EffectType::Appending && rBrace[0].effectOn == U'Ư', true);

    auto rW1 = parseTonelessRules(U'w', "UOA_ƯƠĂ");
    EXPECT_BOOL_EQ(rW1.size() == 33, true);
    EXPECT_BOOL_EQ(rW1[0].effectType == EffectType::MarkTransformation && rW1[0].getMark() == Mark::Horn && rW1[0].effectOn == U'u', true);
    EXPECT_BOOL_EQ(rW1[7].effectType == EffectType::MarkTransformation && rW1[7].getMark() == Mark::Horn && rW1[7].effectOn == U'o', true);
    EXPECT_BOOL_EQ(rW1[20].effectType == EffectType::MarkTransformation && rW1[20].getMark() == Mark::Breve && rW1[20].effectOn == U'a', true);

    auto rW2 = parseTonelessRules(U'w', "UOA_ƯƠĂ__Ư");
    EXPECT_BOOL_EQ(rW2.size() == 34, true);
    EXPECT_BOOL_EQ(rW2[33].effectType == EffectType::Appending && rW2[33].effectOn == U'ư', true);

    auto rApp1 = parseTonelessRules(U'[', "__ươ");
    EXPECT_BOOL_EQ(rApp1.size() == 1 && rApp1[0].appendedRules.size() == 1 && rApp1[0].appendedRules[0].effectOn == U'ơ', true);
    auto rApp2 = parseTonelessRules(U'{', "__ƯƠ");
    EXPECT_BOOL_EQ(rApp2.size() == 1 && rApp2[0].appendedRules.size() == 1 && rApp2[0].appendedRules[0].effectOn == U'Ơ', true);
}

void testBambooCoreParity() {
    // 100% of bamboo_test.go test cases
    {
        auto ng = newStdEngine();
        ng.processString("aw", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "ă");

        ng.reset();
        ng.processString("uw", VietnameseMode);
        ng.processString("o", VietnameseMode);
        ng.processString("w", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "ươ");

        ng.reset();
        ng.processString("chuaarn", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "chuẩn");

        ng.reset();
        ng.processString("giamaf", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "giầm");
    }
    {
        auto ng = newStdEngine();
        ng.processString("dd", VietnameseMode);
        EXPECT_BOOL_EQ(ng.isValid(false), true);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "đ");

        ng.reset();
        ng.processString("ddafi", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "đài");

        ng.reset();
        ng.processString("SD", VietnameseMode);
        ng.processString("D", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "SĐ");
    }
    {
        auto ng = newStdEngine();
        ng.processString("Muoiwq", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "Muoiwq");

        ng.reset();
        ng.processString("mootj", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "một");
    }
    {
        auto ng = newStdEngine();
        ng.processString("Thuow", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "Thuơ");
        ng.removeLastChar(true);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "Thu");
    }
    {
        auto ng = newStdEngine();
        ng.removeLastChar(true);
        ng.processString(" ", EnglishMode);
        ng.removeLastChar(true);
        ng.processString("loanj", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "loạn");
        ng.removeLastChar(true);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "lọa");
        ng.processString(":", EnglishMode);
        ng.removeLastChar(true);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "lọa");
    }
    {
        auto ng = newStdEngine();
        ng.processString("VIEETJ", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "VIỆT");
        ng.removeLastChar(false);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "VIỆ");
        ng.processKey(U'Q', VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "VIEEJQ");
        ng.reset();
        ng.processString("IB", EnglishMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "IB");
    }
    {
        auto ng = newStdEngine();
        ng.processString("noww", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "noww");
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "now");

        ng.reset();
        ng.processString("sawss", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "sawss");
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "săs");
    }
    {
        auto ng = newStdEngine();
        ng.processString("t ]", EnglishMode);
        ng.processString("a", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "]a");

        ng.reset();
        ng.processString("]]a", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "]a");

        ng.reset();
        ng.processString("[", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "ơ");

        ng.reset();
        ng.processString("{", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "Ơ");
    }
    {
        auto ng = newStdEngine();
        ng.processString("wowfi", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "ười");

        ng.reset();
        ng.processString("hanhj", VietnameseMode);
        ng.removeLastChar(true);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "hạn");

        ng.reset();
        ng.processString("catr", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "catr");

        ng.reset();
        ng.processString("toowi", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tơi");

        ng.reset();
        ng.processString("aloo", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "alô");

        ng.reset();
        ng.processString("giw", VietnameseMode);
        EXPECT_BOOL_EQ(ng.isValid(false), true);

        ng.reset();
        ng.processString("[[", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "[");

        ng.reset();
        ng.processString("tooss", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tôs");

        ng.reset();
        ng.processString("tosos", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tôs");

        ng.reset();
        ng.processString("ww", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "w");
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "w");

        ng.reset();
        ng.processString("wiw", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "uiw");
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "wiw");

        ng.reset();
        ng.processString("duwoi", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "dươi");

        ng.reset();
        ng.processString("reff", VietnameseMode);
        ng.processString("resh", EnglishMode);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "reffresh");
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "refresh");

        ng.reset();
        ng.processString("reff", VietnameseMode);
        ng.removeLastChar(true);
        ng.processKey(U'f', VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "rè");

        ng.reset();
        ng.processString("oddp", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "ođp");

        ng.reset();
        ng.processString("gis", VietnameseMode);
        ng.processString("a", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "giá");

        ng.reset();
        ng.processString("kimso", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "kímo");

        ng.reset();
        ng.processString("to", VietnameseMode);
        EXPECT_BOOL_EQ(ng.isValid(true), true);

        ng.reset();
        ng.processString("toorr", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tôr");

        ng.reset();
        ng.processString("tnoss", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tnos");

        ng.reset();
        ng.processString("ddawks", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "đắk");

        ng.reset();
        ng.processString("tooi oo HIEEUR", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "HIỂU");

        ng.reset();
        ng.processString("NGUOIW", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "NGƯƠI");

        ng.reset();
        ng.processString("{s", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "Ớ");
    }
    {
        auto ng = newStdEngine("VNI", EstdFlags);
        ng.processString("o55", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "o5");
    }
    {
        auto ng = newStdEngine();
        ng.processString("duwongwj", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "duongwj");
    }
    {
        // Old vs Modern tone style test (choas -> choá vs chóa)
        auto ngModern = newStdEngine("Telex 2", EstdFlags & ~EstdToneStyle);
        ngModern.processString("choas", VietnameseMode);
        EXPECT_STR_EQ(ngModern.getProcessedString(VietnameseMode), "choá");
        ngModern.reset();
        ngModern.processString("bieecs", VietnameseMode);
        EXPECT_STR_EQ(ngModern.getProcessedString(VietnameseMode), "biếc");
        ngModern.reset();
        ngModern.processString("uese", VietnameseMode);
        EXPECT_STR_EQ(ngModern.getProcessedString(VietnameseMode), "uế");

        auto ngOld = newStdEngine("Telex 2", EstdFlags);
        ngOld.processString("hoaf", VietnameseMode);
        EXPECT_STR_EQ(ngOld.getProcessedString(VietnameseMode), "hòa");
        ngOld.reset();
        ngOld.processString("thuyr", VietnameseMode);
        EXPECT_STR_EQ(ngOld.getProcessedString(VietnameseMode), "thủy");

        ngModern.reset();
        ngModern.processString("hoaf", VietnameseMode);
        EXPECT_STR_EQ(ngModern.getProcessedString(VietnameseMode), "hoà");
        ngModern.reset();
        ngModern.processString("thuyr", VietnameseMode);
        EXPECT_STR_EQ(ngModern.getProcessedString(VietnameseMode), "thuỷ");
    }
    {
        auto ng = newStdEngine();
        ng.processString("duwongj tooi", VietnameseMode);
        ng.restoreLastWord(false);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tooi");
    }
    {
        auto ng = newStdEngine("Microsoft layout", EstdFlags);
        ng.processString("112", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "1â");
        ng.restoreLastWord(false);
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "12");
        ng.reset();
        ng.processString("d[]ng9 t4i", VietnameseMode);
        ng.restoreLastWord(false);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "t4i");
    }
    {
        auto ng = newStdEngine();
        ng.processString("loz", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "loz");
        ng.reset();
        ng.processString("losz", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "lo");
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "losz");

        ng.reset();
        ng.processString("tôifs", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tối");
        EXPECT_STR_EQ(ng.getProcessedString(EnglishMode), "tôifs");

        ng.reset();
        ng.processString("tốif", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tồi");

        ng.reset();
        ng.processString("tốiz", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tôi");

        ng.reset();
        ng.processString("linuxx", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "linux");

        ng.reset();
        ng.processString("buwoo", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "buô");

        ng.reset();
        ng.processString("buowco", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "buôc");

        ng.reset();
        ng.processString("cuoiwo", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "cuôi");

        ng.reset();
        ng.processString("acha", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "acha");

        ng.reset();
        ng.processString("nhuw", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "như");
        EXPECT_BOOL_EQ(ng.isValid(true), true);

        ng.reset();
        ng.processString("arch", VietnameseMode);
        EXPECT_BOOL_EQ(ng.isValid(false), false);

        ng.reset();
        ng.processString("[[oo", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "[ô");

        ng.reset();
        ng.processString("oo]", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "ôư");

        ng.reset();
        ng.processString("chury", VietnameseMode);
        EXPECT_BOOL_EQ(ng.isValid(true), true);

        ng.reset();
        ng.processString("turyn", VietnameseMode);
        ng.removeLastChar(true);
        ng.removeLastChar(true);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "tủ");

        ng.reset();
        ng.processString("chuyểnz", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "chuyên");

        ng.reset();
        ng.processString("nhueej", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "nhuệ");

        ng.reset();
        ng.processString("cuongw", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "cương");

        ng.reset();
        ng.processString("quawcj", VietnameseMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "quặc");

        ng.reset();
        ng.processString("tôi）t", EnglishMode);
        EXPECT_STR_EQ(ng.getProcessedString(VietnameseMode), "t");
    }
}

void testBacktrackAndInputMethods() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    // Requirement 2: a + s -> á, + f -> à, + z -> a; dd -> đ, ddd -> dd; uow -> ươ; w alone -> ư
    EXPECT_STR_EQ(simulateSessionTyping(sp, "as"), "á");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "asf"), "à");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "asfz"), "a");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "dd"), "đ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ddd"), "dd");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "uow"), "ươ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "w"), "ư");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ww"), "w");
    sp.reset();

    // Full sentence in Telex
    EXPECT_STR_EQ(
        simulateSessionTyping(sp, "Tieengs Vieetj treen Arch Linux vaf KDE Plasma Wayland!"),
        "Tiếng Việt trên Arch Linux và KDE Plasma Wayland!");
    sp.reset();

    // Simple Telex: standalone w stays w
    opts.inputMethodName = "Simple Telex";
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "w"), "w");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "nhuwng"), "nhưng");
    sp.reset();

    // VNI input method
    opts.inputMethodName = "VNI";
    sp.setOptions(opts);
    EXPECT_STR_EQ(
        simulateSessionTyping(sp, "Cha2o mu72ng d9e61n vo71i Vie65t Nam!"),
        "Chào mừng đến với Việt Nam!");
    sp.reset();
    // VNI backtrack: a + 1 -> á, + 2 -> à, + 0 -> a, d99 -> d9
    EXPECT_STR_EQ(simulateSessionTyping(sp, "a1"), "á");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "a12"), "à");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "a120"), "a");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "d99"), "d9");
    sp.reset();

    // VIQR input method
    opts.inputMethodName = "VIQR";
    sp.setOptions(opts);
    EXPECT_STR_EQ(
        simulateSessionTyping(sp, "Tie^'ng Vie^.t"),
        "Tiếng Việt");
    sp.reset();
}

void testEnglishAutoRestoreAndShorthandAndMacros() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = true;
    opts.shorthandInitialConsonant = true;
    opts.shorthandFinalConsonant = true;
    opts.macroEnabled = true;
    opts.macroAutoCase = true;
    sp.setOptions(opts);

    // English words must NOT be distorted when typing in Vietnamese mode!
    EXPECT_STR_EQ(simulateSessionTyping(sp, "windows "), "windows ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "search "), "search ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "world "), "world ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "github "), "github ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "firefox "), "firefox ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "wayland "), "wayland ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "plasma "), "plasma ");

    // Initial consonant shorthand: cc->ch, gg->gi, kk->kh, nn->ng, qq->qu, pp->ph, tt->th
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ccao "), "chao ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ggiaf "), "già ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "kkoong "), "không ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "nnguwowif "), "người ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "qquas "), "quá ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ppowr "), "phở ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ttuowng "), "thương ");

    // Final consonant shorthand: g->ng, h->nh, k->ch
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ag "), "ang ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ah "), "anh ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ak "), "ach ");

    // Macros with case adaptation
    sp.macroTable().addMacro("vn", "Việt Nam");
    sp.macroTable().addMacro("tphcm", "Thành phố Hồ Chí Minh");
    sp.macroTable().addMacro("ko", "không");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vn "), "Việt Nam ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "VN "), "VIỆT NAM ");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "tphcm."), "Thành phố Hồ Chí Minh.");
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ko "), "không ");

    // Charset conversion roundtrip tests
    std::string sample = "Cộng hòa Xã hội Chủ nghĩa Việt Nam - Độc lập, Tự do, Hạnh phúc";
    for (const char* cs : {CHARSET_TCVN3, CHARSET_VNI_WINDOWS, CHARSET_UNICODE_COMPOSITE, CHARSET_VIQR, CHARSET_CP1258}) {
        std::string encoded = encodeCharset(cs, sample);
        EXPECT_BOOL_EQ(encoded != sample, true);
        if (std::string(cs) != CHARSET_TCVN3) {
            // TCVN3 shares some lowercase/uppercase vowel codepoints by standard design;
            // VNI Windows, Unicode Composite, VIQR, CP1258 are 100% lossless round-trip!
            std::string decoded = decodeCharset(cs, encoded);
            EXPECT_STR_EQ(decoded, sample);
        }
    }
    EXPECT_STR_EQ(removeVietnameseDiacritics("Tiếng Việt Đậm Đà"), "Tieng Viet Dam Da");
}

void testBackspaceAndEditingParity() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.modernToneStyle = true; // hoà, thuỷ, toà (default)
    opts.freeToneMarking = true;
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    // Backspace character-by-character: Backspacing a word (e.g. "toans" -> "toán") deletes the last character ("toá").
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans\b"), "toá");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans\bf"), "toà");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans\bfn"), "toàn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieets\b"), "viế");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieets\bj"), "việ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieets\bjt"), "việt");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "yeens\bf"), "yề");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "yeens\bfn"), "yền");
    sp.reset();

    // Verify tracked displayedWord().size() always matches virtual screen length after every key & Backspace
    std::u32string screen;
    for (char32_t c : utf8ToU32("toans\bf\br ")) {
        KeyActionDiff diff = (c == U'\b') ? sp.processBackspace() : sp.processPrintableKey(c);
        if (diff.passThrough) {
            if (c == U'\b') {
                if (!screen.empty()) screen.pop_back();
            } else {
                screen.push_back(c);
            }
        } else {
            for (int i = 0; i < diff.backspaces && !screen.empty(); ++i) screen.pop_back();
            screen.append(diff.textToEmit);
        }
        if (c != U' ') {
            EXPECT_BOOL_EQ(sp.currentDisplayedWord().size() == screen.size(), true);
        }
    }
    EXPECT_STR_EQ(u32ToUtf8(screen), "tỏ ");
    sp.reset();

    // Consecutive Backspaces
    EXPECT_STR_EQ(simulateSessionTyping(sp, "tieengs\b\b\bnhj "), "tịnh ");
    sp.reset();

    // Backspace all the way to empty buffer, extra Backspace, then type a brand-new word
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans\b\b\b\b\bmowis "), "mới ");
    sp.reset();

    // Backspace with uppercase / mixed case
    EXPECT_STR_EQ(simulateSessionTyping(sp, "TOANS\bF"), "TOÀ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "TOANS\bFN"), "TOÀN");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "Vieets\bJ"), "Việ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "Vieets\bJt"), "Việt");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "DDas\byj "), "Đỵ ");
    sp.reset();

    // Old tone style explicitly sets EstdToneStyle (hòa, thủy, tòa)
    opts.modernToneStyle = false;
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "hoaf "), "hòa ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans\bf"), "tòa");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans\bfn"), "toàn");
    sp.reset();

    // Auto-restored English words must delete strictly 1 char per Backspace and never morph back into Vietnamese
    opts.modernToneStyle = true;
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "windows\b"), "window");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "windows\b\b\b"), "wind");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "windows\b\b\bows "), "windows ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "windows\b\b\b\b\b\b\btoans "), "toán ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "autocomplete\b"), "autocomplet");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "firefox\b\b"), "firef");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "world\b\b"), "wor");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "linux\b"), "linu");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "telex\b"), "tele");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "google\b\b\b"), "goo");
    sp.reset();

    // Verify 1-char-per-Backspace screen invariant on auto-restored words
    {
        std::u32string engScreen;
        for (char32_t c : utf8ToU32("windows")) {
            KeyActionDiff diff = sp.processPrintableKey(c);
            if (diff.passThrough) engScreen.push_back(c);
            else {
                for (int i = 0; i < diff.backspaces && !engScreen.empty(); ++i) engScreen.pop_back();
                engScreen.append(diff.textToEmit);
            }
            EXPECT_BOOL_EQ(sp.currentDisplayedWord().size() == engScreen.size(), true);
        }
        EXPECT_STR_EQ(u32ToUtf8(engScreen), "windows");
        for (int b = 1; b <= 7; ++b) {
            size_t prevLen = engScreen.size();
            KeyActionDiff diff = sp.processBackspace();
            if (diff.passThrough) {
                if (!engScreen.empty()) engScreen.pop_back();
            } else {
                for (int i = 0; i < diff.backspaces && !engScreen.empty(); ++i) engScreen.pop_back();
                engScreen.append(diff.textToEmit);
            }
            EXPECT_BOOL_EQ(engScreen.size() == prevLen - 1, true);
            EXPECT_BOOL_EQ(sp.currentDisplayedWord().size() == engScreen.size(), true);
        }
        EXPECT_STR_EQ(u32ToUtf8(engScreen), "");
        sp.reset();
    }

    // Backspacing back across Space / punctuation / multiple words to fix tone marks ("xoá lùi về sửa dấu")
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans \bf "), "toàn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans \b\bf "), "toà ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans \b\bfn "), "toàn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieets \bj "), "việt ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieets \b\bj "), "việ ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieets \b\bjt "), "việt ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans hocj\b\b\b\bf "), "toàn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans hocj\b\b\b\b\bf "), "toà ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaof banj\b\b\b\bs "), "cháo ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans, \b\bf "), "toàn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans, \b\b\bf "), "toà ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans  \b\bf "), "toàn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans  \b\b\bf "), "toà ");
    sp.reset();
    // Backspacing all the way across Space through first word into empty, then new word
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toans a\b\b\b\b\b\b\bmowis "), "mới ");
    sp.reset();
}

std::string simulateCursorSession(SessionProcessor& sp, std::string_view ops) {
    std::u32string screen;
    size_t cursor = 0;
    std::u32string u32Ops = utf8ToU32(ops);
    for (char32_t c : u32Ops) {
        if (c == U'<') {
            sp.moveCursorLeft();
            if (cursor > 0) --cursor;
        } else if (c == U'>') {
            sp.moveCursorRight();
            if (cursor < screen.size()) ++cursor;
        } else if (c == U'\x7f') {
            sp.processDelete();
            if (cursor < screen.size()) screen.erase(cursor, 1);
        } else if (c == U'\b') {
            KeyActionDiff diff = sp.processBackspace();
            if (diff.passThrough) {
                if (cursor > 0) {
                    screen.erase(cursor - 1, 1);
                    --cursor;
                }
            } else {
                int bs = std::min<int>(diff.backspaces, static_cast<int>(cursor));
                screen.erase(cursor - bs, bs);
                cursor -= bs;
                screen.insert(cursor, diff.textToEmit);
                cursor += diff.textToEmit.size();
            }
        } else {
            KeyActionDiff diff = sp.processPrintableKey(c);
            if (diff.passThrough) {
                screen.insert(cursor, 1, c);
                ++cursor;
            } else {
                int bs = std::min<int>(diff.backspaces, static_cast<int>(cursor));
                screen.erase(cursor - bs, bs);
                cursor -= bs;
                screen.insert(cursor, diff.textToEmit);
                cursor += diff.textToEmit.size();
            }
        }
    }
    return u32ToUtf8(screen);
}

void testArrowKeyCursorNavigation() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.modernToneStyle = true;
    opts.freeToneMarking = true;
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    // Move Left past Space into "toán", Backspace deletes 'n' -> "toá", type 'f' -> "toà", Space stays intact
    EXPECT_STR_EQ(simulateCursorSession(sp, "toans <\bf"), "toà ");
    sp.reset();
    EXPECT_STR_EQ(simulateCursorSession(sp, "toans <\bfn"), "toàn ");
    sp.reset();

    // Move Left back past an entire second word ("toán học" -> left 4 chars -> "toán| học") and edit tone
    EXPECT_STR_EQ(simulateCursorSession(sp, "toans hocj<<<<\bf>>>>!"), "toà học!");
    sp.reset();
    EXPECT_STR_EQ(simulateCursorSession(sp, "toans hocj<<<<\bfn>>>>!"), "toàn học!");
    sp.reset();

    // Back inside mid-word ("toán" -> Left before 'n' -> Delete 'n' -> type 'n' -> "toán ")
    EXPECT_STR_EQ(simulateCursorSession(sp, "toans<\x7fn "), "toán ");
    sp.reset();

    // Use Delete key to remove trailing char after moving Left ("toáns" -> Left -> Delete 'n' -> type 'f' -> "toà ")
    EXPECT_STR_EQ(simulateCursorSession(sp, "toans<\x7f" "f "), "toà ");
    sp.reset();

    // Move Left across Space into "việt", Backspace 't', retype 't', move Right across Space, type "nam " -> "việt nam "
    EXPECT_STR_EQ(simulateCursorSession(sp, "vieejt <\bt>nam "), "việt nam ");
    sp.reset();

    // Stepping past left boundary resets history safely
    sp.reset();
    for (char32_t c : utf8ToU32("toans")) sp.processPrintableKey(c);
    for (int i = 0; i < 6; ++i) sp.moveCursorLeft(); // Past start of word ("toán" has 4 chars)
    EXPECT_BOOL_EQ(sp.hasUndoableHistory(), false);
    EXPECT_BOOL_EQ(sp.currentRawKeys().empty(), true);
    sp.reset();
}

void testMistypedUndoAndSpellCheckEnglish() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.modernToneStyle = true;
    opts.freeToneMarking = true;
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    // 1. Undoing tone/mark via double-key ("chaanss" -> "châns", "ddd" -> "dd", etc.) and pressing Backspace
    //    must keep the tone/mark undone ("chân", "d") instead of snapping back to "chấn" / "đ".
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanss"), "châns");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanss\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanss\bf "), "chần ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanss\bs "), "chấn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanss \b\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanff\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanrr\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanxx\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanjj\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ddd\b"), "d");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ddd\ba "), "da ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ddd\bd "), "đ ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "aaa\b"), "a");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "aaa\bn "), "an ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "tooss\b"), "tô");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toorr\b"), "tô");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "tuww\b"), "tu");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "uoww\b"), "uo");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasns"), "tâns");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasns\b"), "tân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasns\bf "), "tần ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taanss\b"), "tân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasnss\b\b"), "tân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasnk\b"), "tấn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasns \b\b"), "tấn");
    sp.reset();

    // Common English tech words stay intact with Auto-Restore ON
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moder "), "moder ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "shader "), "shader ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "shift "), "shift ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "folder docker render browser "), "folder docker render browser ");
    sp.reset();
    // Make sure English word Backspace still never mutates back ("work\b" stays "wor", "windows\b\b\bows " stays "windows ")
    EXPECT_STR_EQ(simulateSessionTyping(sp, "work\b "), "wor ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "windows\b\b\bows "), "windows ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "meeting\b\b\b"), "meet");
    sp.reset();

    // 1b. Plain-ASCII prefix unlock after spellcheck lock (must unlock even across >= 2 backspaces, without morphing)
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moters\b\b\b\b\bof"), "mò");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chakas\b\b\b\b\bas"), "cá");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moderss\b\b\b\b\botj "), "một ");
    sp.reset();

    // 1c. Vowel-initial mistyped Vietnamese words unlock cleanly on Backspace
    EXPECT_STR_EQ(simulateSessionTyping(sp, "uoongsk\b"), "uống");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "anhsk\b"), "ánh");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "oongsk\b"), "ống");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "yeesnk\b"), "yến");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "oosk\b"), "ố");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "uowsk\b"), "ướ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "awjd\b"), "ặ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "wk\b"), "ư");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "uwk\boif"), "ười");
    sp.reset();

    // 1d. Multi-char typo (>= 2 extra keys) unlocks when backspaced to a valid >=4-char Vietnamese word with tone or đ
    EXPECT_STR_EQ(simulateSessionTyping(sp, "tieengskkk\b\b\b"), "tiếng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toanskkk\b\b\b"), "toán");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanskkk\b\b\b"), "chấn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ddaxkkk\b\b\b"), "đã");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieejtaaa\b\b\b"), "việt");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "uoongskkk\b\b\b"), "uống");
    sp.reset();

    // 1e. Standalone "oă" syllables can be typed mid-word with spellcheck ON, while incomplete "oă" at word end restores
    EXPECT_STR_EQ(simulateSessionTyping(sp, "oawts "), "oắt ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "oawms "), "oắm ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "oawng "), "oăng ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "oaw "), "oaw ");
    sp.reset();

    // 2. With SpellCheck ON and Auto-Restore OFF: "moder", "shift", "shader" must NOT become "modẻ", "shìt", "shadẻ"
    opts.autoRestoreEnglish = false;
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moder "), "moder ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "shift "), "shift ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "shader "), "shader ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "folder docker render browser street "), "folder docker render browser street ");
    sp.reset();
    // Compound non-Vietnamese words with an earlier valid/undone fragment must still suppress invalid diacritics (e.g. u+x->ũ)
    EXPECT_STR_EQ(simulateSessionTyping(sp, "linux "), "linux ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "arrchlinux "), "archlinux ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "archlinux "), "ảchlinux ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "archlinux\b "), "ảchlinu ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "tooss taasns "), "tôs tâns ");
    sp.reset();
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "archlinux ArchLinux "), "archlinux ArchLinux ");
    sp.reset();
    opts.autoRestoreEnglish = false;
    sp.setOptions(opts);
    // Backspacing out of undone "taasns" / "chaanss" or "moder" back into valid Vietnamese prefix works cleanly
    EXPECT_STR_EQ(simulateSessionTyping(sp, "taasns\b"), "tân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaanss\b"), "chân");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moder\b\b\btj "), "mọt ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moder\b\b\botj "), "một ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "moderss\b\b\b\b\botj "), "một ");
    sp.reset();

    // Alias "SimpleTelex" must resolve to "Simple Telex" (standalone w stays w!)
    SessionOptions simpleOpts;
    simpleOpts.vietnameseEnabled = true;
    simpleOpts.inputMethodName = "SimpleTelex";
    sp.setOptions(simpleOpts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "w wa uow "), "w wa ươ ");
    sp.reset();
    simpleOpts.inputMethodName = "Simple Telex";
    sp.setOptions(simpleOpts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "w uow "), "w ươ ");

    // Alias "TCVN3" must encode identically to "TCVN3 (ABC)"
    EXPECT_STR_EQ(encodeCharset("TCVN3", "Tiếng Việt"), encodeCharset(CHARSET_TCVN3, "Tiếng Việt"));

    // Fast-typing permutations of "truyền" across Simple Telex & Telex with Auto-Restore OFF & ON
    for (bool ar : {false, true}) {
        SessionOptions truyenOpts;
        truyenOpts.vietnameseEnabled = true;
        truyenOpts.inputMethodName = "Simple Telex";
        truyenOpts.spellCheckEnabled = true;
        truyenOpts.autoRestoreEnglish = ar;
        truyenOpts.modernToneStyle = true;
        sp.setOptions(truyenOpts);

        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeenf "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeefn "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyefen "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyefne "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyenef "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "Hoof Taasn Truyeenf "), "Hồ Tấn Truyền ");

        truyenOpts.inputMethodName = "Telex";
        sp.setOptions(truyenOpts);
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeenf "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeefn "), "truyền ");
        sp.reset();
        EXPECT_STR_EQ(simulateSessionTyping(sp, "truyefen "), "truyền ");
    }
}

void testPostClickWithoutReset() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.vietnameseEnabled = true;
    opts.inputMethodName = "Telex";
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    auto applyToScreen = [](std::u32string& scr, const KeyActionDiff& d, char32_t raw) {
        if (d.passThrough) {
            if (raw == U'\b') {
                if (!scr.empty()) scr.pop_back();
            } else if (raw != 0) {
                scr.push_back(raw);
            }
            return;
        }
        for (int i = 0; i < d.backspaces && !scr.empty(); ++i) scr.pop_back();
        scr += d.textToEmit;
    };
    auto typeOn = [&](std::u32string& scr, std::string_view input) {
        std::u32string u32 = utf8ToU32(input);
        for (char32_t c : u32) {
            if (c == U'\b') {
                applyToScreen(scr, sp.processBackspace(), U'\b');
            } else {
                applyToScreen(scr, sp.processPrintableKey(c), c);
            }
        }
    };

    // 1. In-place continuation when cursor stays on active word: "hoa" -> click -> "s" => "hoá", "vie" -> click -> "ejt" => "việt"
    std::u32string box1;
    typeOn(box1, "hoa");
    sp.onMouseClickWithoutReset();
    typeOn(box1, "s ");
    EXPECT_STR_EQ(u32ToUtf8(box1), "hoá ");

    sp.reset();
    box1.clear();
    typeOn(box1, "vie");
    sp.onMouseClickWithoutReset();
    typeOn(box1, "ejt ");
    EXPECT_STR_EQ(u32ToUtf8(box1), "việt ");

    // 2. Typing English word before click (wordLockedRaw_) must NOT disable Vietnamese after clicking elsewhere!
    sp.reset();
    box1.clear();
    typeOn(box1, "fedora");
    sp.onMouseClickWithoutReset();
    std::u32string box2;
    typeOn(box2, "tieengs vieejt ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "tiếng việt ");

    // 3. Typing Vietnamese word before click then clicking elsewhere and typing a new word must NOT jump/corrupt ("eejtnam")!
    sp.reset();
    box1.clear();
    typeOn(box1, "vieejt");
    sp.onMouseClickWithoutReset();
    box2 = utf8ToU32("Xin ");
    typeOn(box2, "nam ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "Xin nam ");

    sp.reset();
    box1.clear();
    typeOn(box1, "hoa");
    sp.onMouseClickWithoutReset();
    box2 = utf8ToU32("Về ");
    typeOn(box2, "nhaf ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "Về nhà ");

    sp.reset();
    box1.clear();
    typeOn(box1, "hoa");
    sp.onMouseClickWithoutReset();
    box2 = utf8ToU32("Rất ");
    typeOn(box2, "toots ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "Rất tốt ");

    sp.reset();
    box1.clear();
    typeOn(box1, "taans");
    sp.onMouseClickWithoutReset();
    box2 = utf8ToU32("Làm ");
    typeOn(box2, "sao ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "Làm sao ");

    // 4. Committed leftTokens_ history before click must NOT resurrect via Backspace after click
    sp.reset();
    box1.clear();
    typeOn(box1, "chao ");
    sp.onMouseClickWithoutReset();
    box2 = utf8ToU32("abc ");
    typeOn(box2, "\b\bvieejt ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "abviệt ");

    // 5. Immediate Backspace on another textbox after click must NOT emit multi-backspace rewrite ("taasns" -> \b -> 5 BS + "ấn")
    sp.reset();
    box1.clear();
    typeOn(box1, "taasns");
    sp.onMouseClickWithoutReset();
    box2 = utf8ToU32("hello");
    typeOn(box2, "\b tieengs ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "hell tiếng ");

    // 6. Post-click Backspace after typing valid post-click prefix keeps screen consistent
    sp.reset();
    box1.clear();
    typeOn(box1, "ch");
    sp.onMouseClickWithoutReset();
    box2.clear();
    typeOn(box2, "aw\bxuowng ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "xương ");

    // 7. Double click resets active buffer safely
    sp.reset();
    box1.clear();
    typeOn(box1, "hoa");
    sp.onMouseClickWithoutReset();
    sp.onMouseClickWithoutReset();
    box2.clear();
    typeOn(box2, "sao ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "sao ");

    // 8. Also check Auto-Restore OFF mode does not corrupt across click
    opts.autoRestoreEnglish = false;
    sp.setOptions(opts);
    sp.reset();
    box1.clear();
    typeOn(box1, "vieejt");
    sp.onMouseClickWithoutReset();
    box2.clear();
    typeOn(box2, "nam ");
    EXPECT_STR_EQ(u32ToUtf8(box2), "nam ");
}

void testFastRolloverAndAutoRestoreOffBackspaceUnlock() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Simple Telex";
    opts.freeToneMarking = true;
    opts.modernToneStyle = true;
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = false;
    sp.setOptions(opts);

    // Fast-typing out-of-order Telex key sequences in Simple Telex
    auto runSimple = [&](const std::string& keys) {
        sp.reset();
        return simulateSessionTyping(sp, keys);
    };
    EXPECT_STR_EQ(runSimple("truongfw "), "trường ");
    EXPECT_STR_EQ(runSimple("tiengse "), "tiếng ");
    EXPECT_STR_EQ(runSimple("chuyense "), "chuyến ");
    EXPECT_STR_EQ(runSimple("nguoifw "), "người ");
    EXPECT_STR_EQ(runSimple("dduocjw "), "được ");
    EXPECT_STR_EQ(runSimple("tuyeejt "), "tuyệt ");
    EXPECT_STR_EQ(runSimple("Tieengs Vieetj ddoongf booj "), "Tiếng Việt đồng bộ ");

    // If wordLockedRaw_ ever activates via an invalid ASCII-prefix suffix then user fixes via Backspace,
    // Backspace must still unlock Vietnamese even with autoRestoreEnglish = false!
    sp.reset();
    std::u32string screen;
    auto typeSeq = [&](const std::string& seq) {
        for (char c : seq) {
            if (c == '\b') {
                auto d = sp.processBackspace();
                if (d.passThrough) {
                    if (!screen.empty()) screen.pop_back();
                } else {
                    for (int i = 0; i < d.backspaces && !screen.empty(); ++i) screen.pop_back();
                    screen += d.textToEmit;
                }
            } else {
                auto d = sp.processPrintableKey(static_cast<char32_t>(static_cast<unsigned char>(c)));
                if (d.passThrough) {
                    screen.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
                } else {
                    for (int i = 0; i < d.backspaces && !screen.empty(); ++i) screen.pop_back();
                    screen += d.textToEmit;
                }
            }
        }
    };
    typeSeq("taasna"); // mistyped extra 'a' after 'tấn' -> autoRestoreEnglish=false suppresses only or locks
    typeSeq("\bf ");
    // Also test typo-lock on 'toaan' -> backspacing back to 'toa' unlocks and restores tone placement
    sp.reset();
    screen.clear();
    typeSeq("toaan");
    typeSeq("\b\bns ");
    EXPECT_STR_EQ(u32ToUtf8(screen), "toán ");
}

void testComprehensiveToneCorrectionAndFastTyping() {
    SessionProcessor sp;
    SessionOptions opts;
    opts.inputMethodName = "Telex";
    opts.modernToneStyle = true;
    opts.freeToneMarking = true;
    opts.spellCheckEnabled = true;
    opts.autoRestoreEnglish = true;
    sp.setOptions(opts);

    // 1. All complex diphthongs, triphthongs, and final consonants tone-editing parity
    EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeens\bf"), "truyề");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeens\bfn"), "truyền");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeenf\bs"), "truyế");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeenf\bsn"), "truyến");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "nghieengs\bj"), "nghiện");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "nghieengs\bjg"), "nghiệng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "nghieengr\bs"), "nghiến");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "nghieengr\bsg"), "nghiếng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "khoangr\bf"), "khoàn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "khoangr\bfg"), "khoàng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "khoangr\bs"), "khoán");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "khoangr\bsg"), "khoáng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "phuowngj\bs"), "phướn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "phuowngj\bsg"), "phướng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "thuowngf\bsg"), "thướng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chuois\bji"), "chuọi");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "dduowngf\bsg"), "đướng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "quynhr\bf"), "quỳn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "quynhr\bfh"), "quỳnh");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "khuyas\bf"), "khuỳ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "khuyas\bfa"), "khuỳa");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ngoanwf\bs"), "ngoắ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ngoanwf\bsn"), "ngoắn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ngheechs\bj"), "nghệc");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "ngheechs\bjh"), "nghệch");
    sp.reset();

    // 2. Rapid multiple tone cycles on the same word ("sửa dấu liên hoàn")
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toansfrxj "), "toạn ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaansfrxj "), "chận ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "truyeensfrx "), "truyễn ");
    sp.reset();

    // 3. VNI input method tone editing
    opts.inputMethodName = "VNI";
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "toan12"), "toàn");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "truyen612"), "truyền");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "viet615"), "việt");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "d9uong712"), "đường");
    sp.reset();

    // 4. Mixed-case and CapsLock tone editing
    opts.inputMethodName = "Telex";
    sp.setOptions(opts);
    EXPECT_STR_EQ(simulateSessionTyping(sp, "TRUYEENS\bFN"), "TRUYỀN");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "Nghieengs\bJg"), "Nghiệng");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "KHOANGR\bSG"), "KHOÁNG");
    sp.reset();

    // 5. Cross-word delimiter tone editing
    EXPECT_STR_EQ(simulateSessionTyping(sp, "chaof banj \bs "), "chào bán ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "hocj sinh, \b\bs "), "học sính ");
    sp.reset();
    EXPECT_STR_EQ(simulateSessionTyping(sp, "vieetj nam! \b\bs "), "việt nám ");
    sp.reset();
}

} // namespace

int main() {
    testUtilsAndRulesParser();
    testBambooCoreParity();
    testBacktrackAndInputMethods();
    testEnglishAutoRestoreAndShorthandAndMacros();
    testBackspaceAndEditingParity();
    testArrowKeyCursorNavigation();
    testMistypedUndoAndSpellCheckEnglish();
    testPostClickWithoutReset();
    testFastRolloverAndAutoRestoreOffBackspaceUnlock();
    testComprehensiveToneCorrectionAndFastTyping();

    std::cout << "Engine Tests Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
