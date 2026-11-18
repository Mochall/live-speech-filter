#include "utils.hpp"
#include "cpp-pinyin/Pinyin.h"
#include "cpp-pinyin/G2pglobal.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Pinyin::setDictionaryPath(argv[1]);
    Pinyin::Pinyin converter;
    std::ifstream input(argv[2]);
    std::string item;
    while (std::getline(input, item)) {
        auto result = converter.hanziToPinyin(item, Pinyin::ManTone::Style::NORMAL,
            Pinyin::Error::Default, false, false);
        std::cout << item << '\t';
        bool first = true;
        for (const auto& token : result) {
            if (token.pinyin.empty() || token.pinyin == " ") continue;
            if (!first) std::cout << ' ';
            first = false;
            std::cout << NormalizePinyin(token.pinyin);
        }
        std::cout << '\n';
    }
}
