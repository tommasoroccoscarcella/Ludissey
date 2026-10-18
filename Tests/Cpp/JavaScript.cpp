#include "JavaScript.hpp"

#include "Engine/Core/Async.hpp"
#include "Engine/Core/Json.hpp"

#include <JavaScriptCore/JavaScriptCore.h>

#include <stdlib.h>

namespace ld {

namespace {

std::string text(JSContextRef context, JSValueRef value) {
    if (!value) return "";
    JSStringRef s = JSValueToStringCopy(context, value, nullptr);
    if (!s) return "";
    std::size_t size = JSStringGetMaximumUTF8CStringSize(s);
    std::string out(size, '\0');
    std::size_t written = JSStringGetUTF8CString(s, out.data(), size);
    JSStringRelease(s);
    out.resize(written > 0 ? written - 1 : 0);
    return out;
}

JSValueRef randomHex(JSContextRef context, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*) {
    unsigned char bytes[32];
    arc4random_buf(bytes, sizeof bytes);
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (unsigned char b : bytes) {
        hex += digits[b >> 4];
        hex += digits[b & 15];
    }
    JSStringRef s = JSStringCreateWithUTF8CString(hex.c_str());
    JSValueRef v = JSValueMakeString(context, s);
    JSStringRelease(s);
    return v;
}

}

struct JavaScriptContext::State {
    JSGlobalContextRef context = nullptr;
    JSValueRef run(const std::string& source, const std::string& url, std::string& failure) {
        failure.clear();
        JSStringRef script = JSStringCreateWithUTF8CString(source.c_str());
        JSStringRef location = url.empty() ? nullptr : JSStringCreateWithUTF8CString(url.c_str());
        JSValueRef exception = nullptr;
        JSValueRef result = JSEvaluateScript(context, script, nullptr, location, 1, &exception);
        JSStringRelease(script);
        if (location) JSStringRelease(location);
        if (exception) failure = text(context, exception);
        return result;
    }
};

JavaScriptContext::JavaScriptContext() : state(std::make_unique<State>()) {
    state->context = JSGlobalContextCreate(nullptr);
    if (!state->context) throw ServiceError("[LocalServerUnavailable] JavaScriptCore non disponibile.");
}

JavaScriptContext::~JavaScriptContext() {
    if (state->context) JSGlobalContextRelease(state->context);
}

bool JavaScriptContext::evaluateScript(const std::string& source, const std::string& url, std::string& failure) {
    state->run(source, url, failure);
    return failure.empty();
}

std::string JavaScriptContext::evaluate(const std::string& source, const std::string& url) {
    std::string failure;
    JSValueRef value = state->run(source, url, failure);
    if (!failure.empty()) throw ServiceError(failure);
    return text(state->context, value);
}

double JavaScriptContext::number(const std::string& source) {
    std::string failure;
    JSValueRef value = state->run(source, "", failure);
    if (!failure.empty()) throw ServiceError(failure);
    return JSValueToNumber(state->context, value, nullptr);
}

void JavaScriptContext::installRandom(const std::string& name) {
    JSStringRef fn = JSStringCreateWithUTF8CString(name.c_str());
    JSObjectRef function = JSObjectMakeFunctionWithCallback(state->context, fn, randomHex);
    JSObjectSetProperty(state->context, JSContextGetGlobalObject(state->context), fn, function, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(fn);
}

std::string javaScriptLiteral(const std::string& value) { return escapeJson(value); }

}
