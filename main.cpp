#include <iostream>
#include <fstream>

#include "cxxprops.h"

/* Test driver */
int main(int argc, char** args)
{
    std::cout << "C++ STD: " << __cplusplus << std::endl;

    cxxprops::Properties props;

    std::ifstream prop(args[1]);
    props.parse(prop);

    std::cout << "Keys:" << std::endl;
    for (const auto& key : props.keys())
        std::cout << key << ",";
    std::cout << std::endl;

    std::cout << "Values:" << std::endl;
    for (const auto& val : props.values())
        std::cout << val << ",";
    std::cout << std::endl;

    std::string findMe = u8"嗨";
    for (char& c : findMe)
        std::cout << std::bitset<8>(c) << ",";
    std::cout << std::endl;

    std::cout << "Default value: " << props.get("not.there", "default!") << std::endl;

    props.remove("removeme");
    props.put("bind", "127.0.0.0");
    props.put("str.with.leading.ws", "   \t127.0.0.0");

    // Add an empty line, a comment and a property at the end of the file
    props.putEmptyLine();
    props.putComment("A new comment!");
    props.put("new-multiline", "this takes \nmultiple \nlines");

    std::cout << "Alternative server log level: '" << props.get("server.alternative.log.level", "not found") << "'" << std::endl;
    std::cout << "Nested grouping 1: " << props.get("server.alternative.log.inner.value", "not found") << std::endl;
    std::cout << "Nested grouping 2: " << props.get("server.alternative.log.inner2.value", "not found") << std::endl;

    std::cout << "text() pretty printed:" << std::endl;
    std::cout << "-----------------------------------------------------------------" << std::endl;
    std::cout << props.text(true) << std::endl;

    std::cout << std::endl;

    std::cout << "text() original formatting:" << std::endl;
    std::cout << "-----------------------------------------------------------------" << std::endl;
    std::cout << props.text(false) << std::endl;


    return 0;
}