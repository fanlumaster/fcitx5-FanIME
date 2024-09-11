#include "dict.h"

std::vector<std::string> DictionaryUlPb::alpha_list = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};

DictionaryUlPb::DictionaryUlPb() {
    const char *homeDir = getenv("HOME");
    if (!homeDir) {
        // std::cerr << "cannot get home directory." << std::endl;
    }
    std::string filePath = std::string(homeDir) + "/.local/share/fcitx5-fanyime/word.txt";
    inputFile.open(filePath);
    if (!inputFile.is_open()) {
        // std::cerr << "why?" << std::endl;
    }
    std::string line;
    while (std::getline(inputFile, line)) {
        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            std::string key = line.substr(0, tabPos);
            std::string value = line.substr(tabPos + 1);
            dict_map[key].push_back(value);
        } else {
            // std::cerr << "error: cannot find tab char: " << line << std::endl;
        }
    }
}

std::vector<std::string> DictionaryUlPb::generate(const std::string code) {
    std::vector<std::string> candidateList;
    std::vector<std::string> code_list;
    if (code.size() == 1) {
        for (auto letter : alpha_list) {
            code_list.push_back(code + letter);
            candidateList.insert(candidateList.end(), dict_map[code + letter].begin(), dict_map[code + letter].end());
        }
    } else {
        candidateList = dict_map[code];
    }
    return candidateList;
}

DictionaryUlPb::~DictionaryUlPb() {
    if (inputFile.is_open()) {
        inputFile.close();
    }
}
