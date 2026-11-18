#include "utils.hpp"
#include <cassert>
int main() {
 std::vector<std::vector<std::string>> p; std::string e;
 assert(ParsePinyinWords("ji ba, wo cao, zheng chang",p,e));
 assert(p.size()==3 && p[0]==std::vector<std::string>({"ji","ba"}));
 assert(p[2]==std::vector<std::string>({"zen","can"}));
 assert(!ParsePinyinWords("ji ba, wo3 cao",p,e) && p.empty() && !e.empty());
 assert(ParsePinyinWords(" \n, ",p,e) && p.empty() && e.empty());
 assert(ParsePinyinWords("\tji   fa\n",p,e) && p[0]==std::vector<std::string>({"ji","fa"}));
}