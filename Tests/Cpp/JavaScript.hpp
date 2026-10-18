#pragma once

#include <memory>
#include <string>

namespace ld {

class JavaScriptContext {
public:
    JavaScriptContext();
    ~JavaScriptContext();
    JavaScriptContext(const JavaScriptContext&) = delete;
    JavaScriptContext& operator=(const JavaScriptContext&) = delete;
    std::string evaluate(const std::string& source, const std::string& url = "");
    double number(const std::string& source);
    bool evaluateScript(const std::string& source, const std::string& url, std::string& failure);
    void installRandom(const std::string& name);

private:
    struct State;
    std::unique_ptr<State> state;
};

std::string javaScriptLiteral(const std::string& value);

}
