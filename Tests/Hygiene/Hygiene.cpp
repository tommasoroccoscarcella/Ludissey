#include "Hygiene.hpp"

#include "Engine/Core/Text.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace hygiene {

namespace fs = std::filesystem;

namespace {

const std::set<std::string> cFamily{".cpp", ".hpp", ".h", ".mm", ".metal", ".lim"};
const std::set<std::string> script{".js"};
const std::set<std::string> markup{".html", ".css"};
const std::set<std::string> binary{".png", ".icns", ".jpg", ".jpeg", ".gif", ".ico", ".ttf", ".otf", ".woff", ".woff2", ".pdf", ".zip", ".gz", ".character", ".af", ".blend"};
const std::vector<std::string> markers{"TODO", "FIXME", "XXX", "HACK"};
const std::set<std::string> topLevel{"Apps", "CloudScript", "Design", "Engine", "HistoricalAPI", "Interface", "Lim", "Localization", "Platform", "Player", "Resources", "Studio", "Tests", "ThirdParty", "Tools", "Web"};
const std::set<std::string> product{"Engine", "Lim", "Localization", "Interface", "Platform", "Player", "Studio", "CloudScript", "Web"};
const std::set<std::string> residueFolders{"__MACOSX", "DerivedData", "build", "CMakeFiles", ".build", "__pycache__", "Documentation"};
const std::set<std::string> residueFiles{".DS_Store", "CMakeCache.txt", "Thumbs.db"};
const std::map<std::string, std::set<std::string>> allowed{
    {"Engine", {".cpp", ".hpp", ".h", ".metal"}},
    {"Lim", {".cpp", ".hpp", ".h", ".lim"}},
    {"Localization", {".cpp", ".hpp"}},
    {"Interface", {".cpp", ".hpp", ".h"}},
    {"Platform", {".cpp", ".hpp", ".h", ".mm", ".metal"}},
    {"Player", {".cpp", ".hpp", ".h"}},
    {"Studio", {".cpp", ".hpp", ".h"}},
    {"Web", {".html", ".css", ".js"}},
    {"CloudScript", {".js"}},
    {"Tools", {".cpp", ".hpp"}},
    {"Tests", {".cpp", ".hpp", ".mm", ".js", ".json", ".png"}},
    {"Resources", {".json", ".png", ".icns", ".character", ".jpg", ".svg"}},
    {"HistoricalAPI", {".json"}},
    {"Design", {".af", ".blend", ".svg"}},
};
const std::regex release(R"(/Users/|/tmp/|DerivedData|Ludissey Player\.app|console\.(log|debug|trace)|debugPrint|NSLog\(|std::cout|std::cerr|\bdebugger;)");
const std::regex printing(R"((^|[^A-Za-z_])f?printf\()");
const std::regex privileged(R"(Security/Security\.h|\bSecItem[A-Z]\w*|\bSecKeychain\w*|\bSecAccess\w*|\bAuthorization(Create|ExecuteWithPrivileges|CopyRights)\b|\bSMJobBless\b|\bsudo\b|with administrator privileges)");
const std::regex legacy(R"(SceneKit|\bSCN[A-Z]\w*|import\s+SwiftUI|\bSwiftUI\b|lua_[A-Za-z]+\(|luaL_[A-Za-z]+\(|lua\.h|LuaBridge|JavaScriptCore|WKWebView|\bElectron\b)");
const std::regex crash(R"((\.ips|\.crash|\.dmp|\.diag|\.log|\.trace|\.core)$|^core(\.\d+)?$)");
const std::regex temporary(R"((\.tmp|\.bak|\.old|\.orig|\.swp|\.swo|~)$)");
const std::regex technical("^[A-Z][A-Za-z0-9]*$");
const std::string regexPrefix = "(,=:[!&|?{};+-*%<>~^\n";

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::string> parts(const fs::path& path, const fs::path& root) {
    std::vector<std::string> out;
    for (const auto& part : fs::relative(path, root)) out.push_back(part.string());
    return out;
}

std::string joined(const std::vector<std::string>& p) {
    std::string out;
    for (const auto& item : p) out += (out.empty() ? "" : "/") + item;
    return out;
}

bool bundled(const std::vector<std::string>& p) {
    return std::any_of(p.begin(), p.end(), [](const std::string& s) { return s.size() > 4 && s.compare(s.size() - 4, 4, ".app") == 0; });
}

bool licensed(const std::vector<std::string>& p) { return !p.empty() && p[0] == "ThirdParty"; }

bool ascii(const std::string& text) { return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 128; }); }

std::string kind(const fs::path& path) { return path.filename() == "CMakeLists.txt" ? ".cmake" : path.extension().string(); }

int lineAt(const std::string& text, std::size_t position) { return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<long>(std::min(position, text.size())), '\n')); }

struct Stripped {
    std::string code;
    std::vector<int> comments;
};

Stripped stripCFamily(const std::string& text, bool js, int offset = 0) {
    Stripped out;
    std::size_t i = 0, n = text.size();
    int line = 1 + offset;
    auto newlines = [&](std::size_t from, std::size_t to) { return static_cast<int>(std::count(text.begin() + static_cast<long>(from), text.begin() + static_cast<long>(to), '\n')); };
    while (i < n) {
        char c = text[i];
        if (c == '\n') {
            out.code += c;
            ++line;
            ++i;
            continue;
        }
        if (text.compare(i, 2, "//") == 0) {
            out.comments.push_back(line);
            auto end = text.find('\n', i);
            i = end == std::string::npos ? n : end;
            continue;
        }
        if (text.compare(i, 2, "/*") == 0) {
            out.comments.push_back(line);
            auto end = text.find("*/", i + 2);
            end = end == std::string::npos ? n : end + 2;
            int count = newlines(i, end);
            out.code += std::string(static_cast<std::size_t>(count), '\n');
            line += count;
            i = end;
            continue;
        }
        if (!js && c == 'R' && text.compare(i, 2, "R\"") == 0 && (i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_'))) {
            auto open = text.find('(', i + 2);
            if (open != std::string::npos && open - i - 2 <= 16) {
                std::string delimiter = text.substr(i + 2, open - i - 2);
                std::string closing = ")" + delimiter + "\"";
                auto end = text.find(closing, open + 1);
                std::size_t stop = end == std::string::npos ? n : end + closing.size();
                if (delimiter == "METAL") {
                    auto inner = stripCFamily(text.substr(open + 1, (end == std::string::npos ? n : end) - open - 1), false, line - 1);
                    out.comments.insert(out.comments.end(), inner.comments.begin(), inner.comments.end());
                }
                int count = newlines(i, stop);
                out.code += "\"\"" + std::string(static_cast<std::size_t>(count), '\n');
                line += count;
                i = stop;
                continue;
            }
        }
        if (c == '"' || c == '\'' || (js && c == '`')) {
            if (c == '\'' && !js && i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1]))) {
                out.code += c;
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            while (j < n && text[j] != c) {
                if (text[j] == '\\') ++j;
                else if (text[j] == '\n' && c != '`') break;
                ++j;
            }
            std::size_t end = std::min(n, j + 1);
            int count = newlines(i, end);
            out.code += std::string(2, c) + std::string(static_cast<std::size_t>(count), '\n');
            line += count;
            i = end;
            continue;
        }
        if (js && c == '/') {
            long k = static_cast<long>(i) - 1;
            while (k >= 0 && (text[static_cast<std::size_t>(k)] == ' ' || text[static_cast<std::size_t>(k)] == '\t')) --k;
            bool afterReturn = k >= 5 && text.compare(static_cast<std::size_t>(k) - 5, 6, "return") == 0;
            if (k < 0 || regexPrefix.find(text[static_cast<std::size_t>(k)]) != std::string::npos || afterReturn) {
                std::size_t j = i + 1;
                bool group = false;
                while (j < n && text[j] != '\n') {
                    if (text[j] == '\\') {
                        j += 2;
                        continue;
                    }
                    if (text[j] == '[') group = true;
                    else if (text[j] == ']') group = false;
                    else if (text[j] == '/' && !group) break;
                    ++j;
                }
                out.code += "//";
                i = std::min(n, j + 1);
                continue;
            }
        }
        out.code += c;
        ++i;
    }
    return out;
}

Stripped stripHash(const std::string& text) {
    Stripped out;
    std::istringstream stream(text);
    int number = 0;
    std::regex quoted(R"("(?:[^"\\]|\\.)*"|'[^']*')");
    for (std::string line; std::getline(stream, line);) {
        ++number;
        std::string body = std::regex_replace(line, quoted, "\"\"");
        auto position = body.find('#');
        while (position != std::string::npos && position > 0 && std::string("$\\{").find(body[position - 1]) != std::string::npos) position = body.find('#', position + 1);
        if (position != std::string::npos) {
            out.comments.push_back(number);
            body = body.substr(0, position);
        }
        out.code += body + "\n";
    }
    return out;
}

Stripped stripMarkup(const std::string& text, const std::string& extension) {
    Stripped out;
    std::regex pattern(extension == ".html" ? std::string("<!--[\\s\\S]*?-->") : std::string("/\\*[\\s\\S]*?\\*/"));
    std::string code;
    std::size_t last = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        out.comments.push_back(lineAt(text, static_cast<std::size_t>(it->position())));
        std::string matched = it->str();
        code += text.substr(last, static_cast<std::size_t>(it->position()) - last) + std::string(static_cast<std::size_t>(std::count(matched.begin(), matched.end(), '\n')), '\n');
        last = static_cast<std::size_t>(it->position() + it->length());
    }
    code += text.substr(last);
    if (extension == ".html") {
        std::regex block(R"(<(script|style)\b[^>]*>([\s\S]*?)</(script|style)>)");
        for (std::sregex_iterator it(code.begin(), code.end(), block), end; it != end; ++it) {
            auto inner = stripCFamily((*it)[2].str(), (*it)[1].str() == "script", lineAt(code, static_cast<std::size_t>(it->position(2))) - 1);
            out.comments.insert(out.comments.end(), inner.comments.begin(), inner.comments.end());
        }
    }
    out.code = code;
    return out;
}

Stripped analyse(const std::string& text, const std::string& extension) {
    if (cFamily.count(extension)) return stripCFamily(text, false);
    if (script.count(extension)) return stripCFamily(text, true);
    if (extension == ".cmake") return stripHash(text);
    return stripMarkup(text, extension);
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        auto end = text.find('\n', start);
        out.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

bool utf8(const std::string& data) {
    std::size_t i = 0;
    while (i < data.size()) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + static_cast<std::size_t>(extra) >= data.size() + (extra == 0 ? 1 : 0)) return false;
        for (int k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(data[i + static_cast<std::size_t>(k)]) >> 6) != 2) return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

void whitespace(const std::string& data, const std::string& name, std::vector<Finding>& out) {
    if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) out.push_back({"IGIENE", name + ": BOM inatteso"});
    if (data.find('\r') != std::string::npos) out.push_back({"IGIENE", name + ": fine riga non LF"});
    if (!utf8(data)) {
        out.push_back({"IGIENE", name + ": testo non UTF-8"});
        return;
    }
    if (data.empty()) return;
    auto all = lines(data);
    if (all.front().find_first_not_of(" \t") == std::string::npos) out.push_back({"IGIENE", name + ": riga vuota iniziale"});
    if (data.back() != '\n') out.push_back({"IGIENE", name + ": manca la newline finale"});
    if (data.size() >= 2 && data.compare(data.size() - 2, 2, "\n\n") == 0) out.push_back({"IGIENE", name + ": newline finali multiple"});
    if (!all.empty() && all.back().empty()) all.pop_back();
    int blank = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto& line = all[i];
        if (!line.empty() && (line.back() == ' ' || line.back() == '\t')) out.push_back({"IGIENE", name + ":" + std::to_string(i + 1) + ": spazi finali"});
        if (line.find_first_not_of(" \t") == std::string::npos) {
            if (++blank > 1) out.push_back({"IGIENE", name + ":" + std::to_string(i + 1) + ": righe vuote consecutive"});
        } else blank = 0;
    }
}

void includes(const std::string& text, const std::string& name, std::vector<Finding>& out) {
    std::set<std::string> seen;
    std::vector<std::string> group;
    auto all = lines(text);
    auto close = [&](std::size_t number) {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        auto sorted = group;
        std::stable_sort(sorted.begin(), sorted.end(), [&](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        if (sorted != group) out.push_back({"IGIENE", name + ":" + std::to_string(number) + ": include non ordinati"});
        std::set<std::string> kinds;
        for (const auto& line : group) {
            auto space = line.find(' ');
            kinds.insert(line.substr(0, space) + (space != std::string::npos && space + 1 < line.size() ? line.substr(space + 1, 1) : ""));
        }
        if (kinds.size() > 1) out.push_back({"IGIENE", name + ":" + std::to_string(number) + ": include di tipo diverso nello stesso gruppo"});
        group.clear();
    };
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto& line = all[i];
        if (line.rfind("#include", 0) == 0 || line.rfind("#import", 0) == 0) {
            if (!seen.insert(line).second) out.push_back({"IGIENE", name + ":" + std::to_string(i + 1) + ": include duplicato"});
            group.push_back(line);
        } else if (!group.empty()) close(i);
    }
    if (!group.empty()) close(all.size());
}

std::vector<std::pair<fs::path, std::vector<std::string>>> files(const fs::path& root) {
    std::vector<std::pair<fs::path, std::vector<std::string>>> out;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        auto p = parts(it->path(), root);
        if (it->is_directory() && (it->path().filename() == ".git" || it->path().extension() == ".app")) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file()) out.push_back({it->path(), p});
    }
    std::sort(out.begin(), out.end());
    return out;
}

}

std::vector<int> comments(const std::string& text, const std::string& extension) { return analyse(text, extension).comments; }

std::vector<Finding> verify(const fs::path& root) {
    std::vector<Finding> out;
    std::map<std::string, std::string> digests;
    for (const auto& [path, p] : files(root)) {
        std::string name = joined(p), filename = path.filename().string(), extension = kind(path);
        if (residueFiles.count(filename)) {
            out.push_back({"RESIDUI", "file residuo non ammesso: " + name});
            continue;
        }
        if (licensed(p)) continue;
        if (!ascii(name)) out.push_back({"STRUTTURA", "nome non ASCII: " + name});
        if (p.size() == 1 && filename != "CMakeLists.txt" && filename != ".gitattributes" && filename != ".gitignore") out.push_back({"STRUTTURA", "file sciolto nella radice: " + name});
        if (std::regex_search(filename, temporary)) out.push_back({"RESIDUI", "file temporaneo non ammesso: " + name});
        if (std::regex_search(filename, crash)) out.push_back({"RESIDUI", "crash dump o log non ammesso: " + name});
        std::string lowerName = filename;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerName.rfind("readme", 0) == 0 || path.extension() == ".md" || path.extension() == ".markdown") out.push_back({"DOCUMENTI", "documento non ammesso nella baseline: " + name});
        if (p.size() > 1) {
            auto rule = allowed.find(p[0]);
            if (rule != allowed.end() && !rule->second.count(path.extension().string())) out.push_back({"LINGUAGGI", "estensione non ammessa in " + p[0] + ": " + name});
            if (path.extension() == ".mm" && p[0] != "Tests" && !(p[0] == "Platform" && p.size() > 2 && p[1] == "Mac")) out.push_back({"LINGUAGGI", "Objective-C++ fuori da Platform/Mac: " + name});
        }
        std::string data = read(path);
        if (!data.empty()) {
            std::string digest = ld::sha256Hex(data);
            auto [it, inserted] = digests.emplace(digest, name);
            if (!inserted) out.push_back({"DUPLICATI", "file duplicato: " + name + " uguale a " + it->second});
        }
        if (binary.count(path.extension().string()) || p[0] == "Design") continue;
        whitespace(data, name, out);
        bool source = cFamily.count(extension) || script.count(extension) || markup.count(extension) || extension == ".cmake";
        if (!source) continue;
        const auto& text = data;
        auto all = lines(text);
        for (std::size_t i = 0; i < all.size(); ++i) {
            const auto& line = all[i];
            auto lead = line.substr(0, line.find_first_not_of(" \t") == std::string::npos ? line.size() : line.find_first_not_of(" \t"));
            if (lead.find('\t') != std::string::npos) out.push_back({"IGIENE", name + ":" + std::to_string(i + 1) + ": tabulazione nell'indentazione"});
        }
        if (cFamily.count(extension)) includes(text, name, out);
        auto stripped = analyse(text, extension);
        for (int line : stripped.comments) out.push_back({"IGIENE", name + ":" + std::to_string(line) + ": commento non ammesso"});
        auto code = lines(stripped.code);
        for (std::size_t i = 0; i < code.size(); ++i)
            for (const auto& word : markers)
                if (std::regex_search(code[i], std::regex("\\b" + word + "\\b"))) out.push_back({"IGIENE", name + ":" + std::to_string(i + 1) + ": marcatore vietato " + word});
        if (product.count(p[0])) {
            for (std::size_t i = 0; i < all.size(); ++i) {
                std::smatch found;
                if (std::regex_search(all[i], found, release) || std::regex_search(all[i], found, printing)) out.push_back({"IGIENE", name + ":" + std::to_string(i + 1) + ": riferimento non ammesso in un prodotto " + found.str()});
            }
            if (std::regex_search(text, legacy)) out.push_back({"GATE", "tecnologia non ammessa nel prodotto: " + name});
            if (std::regex_search(text, privileged)) out.push_back({"GATE", "Portachiavi o operazione privilegiata nel prodotto: " + name});
        }
    }
    for (const auto& entry : fs::directory_iterator(root)) {
        std::string name = entry.path().filename().string();
        if (entry.is_directory() && name[0] != '.' && !topLevel.count(name)) out.push_back({"STRUTTURA", "cartella di primo livello non prevista: " + name});
    }
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_directory()) continue;
        auto p = parts(it->path(), root);
        std::string name = it->path().filename().string();
        if (name == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        if (!bundled(p) && !licensed(p) && !ascii(joined(p))) out.push_back({"STRUTTURA", "nome non ASCII: " + joined(p)});
        if (residueFolders.count(name) || name.rfind("Build-", 0) == 0 || name == "Ludissey Player.app") out.push_back({"RESIDUI", "residuo di build non ammesso: " + joined(p)});
        if (bundled(p) || licensed(p)) {
            if (bundled(p)) it.disable_recursion_pending();
            continue;
        }
        if (fs::is_empty(it->path())) out.push_back({"STRUTTURA", "cartella vuota: " + joined(p)});
        if (!residueFolders.count(name) && !std::regex_match(name, technical)) out.push_back({"STRUTTURA", "cartella tecnica non in inglese PascalCase: " + joined(p)});
    }
    if (fs::exists(root / "Apps"))
        for (const auto& entry : fs::directory_iterator(root / "Apps")) {
            std::string name = entry.path().filename().string();
            if (name != "Ludissey.app" && name != "Ludissey Studio.app") out.push_back({"BUNDLE", "applicazione non prevista: Apps/" + name});
            else {
                auto found = bundle(entry.path());
                out.insert(out.end(), found.begin(), found.end());
            }
        }
    return out;
}

std::vector<Finding> bundle(const fs::path& application) {
    std::vector<Finding> out;
    std::string app = application.filename().string();
    const std::set<std::string> resources{".icns", ".character", ".png", ".json", ".html", ".css", ".js", ".jpg"};
    for (const auto& entry : fs::recursive_directory_iterator(application)) {
        if (!entry.is_regular_file()) continue;
        auto p = parts(entry.path(), application);
        std::string name = app + "/" + joined(p), extension = entry.path().extension().string();
        bool fine = (p.size() == 2 && p[0] == "Contents" && (p[1] == "Info.plist" || p[1] == "PkgInfo")) || (p.size() == 3 && p[1] == "MacOS" && p[2] == "Ludissey") ||
                    (p.size() >= 3 && p[1] == "_CodeSignature") || (p.size() >= 3 && p[1] == "Resources" && resources.count(extension)) ||
                    (p.size() == 4 && p[1] == "Resources" && p[2] == "Licenses" && extension == ".txt");
        if (p.size() >= 4 && p[1] == "Resources" && p[2] == "Web" && !(extension == ".html" || extension == ".css" || extension == ".js")) fine = false;
        if (!fine) out.push_back({"BUNDLE", "file non previsto nel bundle: " + name});
    }
    return out;
}

std::vector<std::string> sweep(const fs::path& root) {
    std::vector<std::string> removed;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it)
        if (it->is_regular_file() && it->path().filename() == ".DS_Store") removed.push_back(it->path().string());
    for (const auto& path : removed) fs::remove(path);
    return removed;
}

}
