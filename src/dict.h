#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>

class DictionaryUlPb {
  public:
    std::vector<std::string> generate(const std::string code);

    DictionaryUlPb();
    ~DictionaryUlPb();

  private:
    std::ifstream inputFile;
    std::unordered_map<std::string, std::vector<std::string>> dict_map;
    static std::vector<std::string> alpha_list;
};
