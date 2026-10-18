#include "Lim/Compiler/Compiler.hpp"
#include "Lim/Parser/Parser.hpp"
#include "Lim/Tooling/Tooling.hpp"
#include "Lim/VM/Vm.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cctype>

using namespace lim;
using namespace lim::tooling;

namespace {

bool has(const std::vector<Completion>& list, const std::string& label) {
    return std::any_of(list.begin(), list.end(), [&](const Completion& c) { return c.label == label; });
}

std::uint32_t at(const std::string& source, const std::string& marker, std::size_t occurrence = 0) {
    std::size_t position = source.find(marker);
    while (occurrence-- > 0) position = source.find(marker, position + 1);
    return static_cast<std::uint32_t>(position);
}

}

TEST_CASE("Evidenziazione della sintassi Lim", "[lim][tooling]") {
    std::string source = "let x = Vec3(1, 2, 3) // nota\nprint(\"ciao \\(x.y)\")";
    auto spans = highlight(source);
    auto style = [&](const std::string& word) {
        auto offset = source.find(word);
        for (const auto& s : spans)
            if (s.offset == offset) return s.style;
        return TokenStyle::invalid;
    };
    CHECK(style("let") == TokenStyle::keyword);
    CHECK(style("Vec3") == TokenStyle::type);
    CHECK(style("1") == TokenStyle::number);
    CHECK(style("// nota") == TokenStyle::comment);
    CHECK(style("print") == TokenStyle::function);
    CHECK(style("\"ciao") == TokenStyle::text);
}

TEST_CASE("Formattazione Lim sicura", "[lim][tooling]") {
    auto result = format("fn  somma(a:Number,b :Number)->Number{\nreturn a+b\n}\n\n\n\nvar  t=  -1\nfor i in 1...3 {\nt+=somma(a:i,b:2)\n}\nprint(\"totale \\( t )\", [1,2][0], -t)\n");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.changed);
    CHECK(result.text == "fn somma(a: Number, b: Number) -> Number {\n    return a + b\n}\n\nvar t = -1\nfor i in 1...3 {\n    t += somma(a: i, b: 2)\n}\nprint(\"totale \\(t)\", [1, 2][0], -t)\n");
    CHECK_FALSE(format(result.text).changed);
    auto broken = format("let = ");
    CHECK_FALSE(broken.diagnostics.empty());
    CHECK(broken.text == "let = ");
}

TEST_CASE("Lim: completamento e rinomina", "[lim][tooling]") {
    TypeRegistry registry = standardRegistry();
    Context context{registry, {}};
    std::string source = "let velocita = 16\nfn salta(altezza: Number) -> Number {\n    return altezza * 2\n}\nlet v = Vec3(1, 2, 3)\nprint(velocita, salta(altezza: 1), v.)";
    auto members = complete(source, at(source, "v.)") + 2, context);
    CHECK(has(members, "x"));
    CHECK(has(members, "length"));
    auto scope = complete(source, at(source, "print"), context);
    CHECK(has(scope, "print"));
    auto prefixed = complete("let velocita = 1\nvel", 21, context);
    REQUIRE_FALSE(prefixed.empty());
    CHECK(prefixed.front().label == "velocita");
    auto list = complete("let nomi = [\"a\"]\nnomi.", 23, context);
    CHECK(has(list, "append"));
    CHECK(has(list, "count"));
    auto info = hover(source, at(source, "velocita", 1), context);
    REQUIRE(info);
    CHECK(info->title == "let velocita: Integer");
    auto function = hover(source, at(source, "salta", 1), context);
    REQUIRE(function);
    CHECK(function->title.find("fn salta") == 0);
    auto target = definition(source, at(source, "salta", 1), context);
    REQUIRE(target);
    CHECK(target->start.offset == at(source, "salta"));
    auto uses = references(source, at(source, "altezza"), context);
    CHECK(uses.size() == 2);
    auto renamed = rename(source, at(source, "velocita"), "rapidita", context);
    REQUIRE(renamed.error.empty());
    std::string changed = applyEdits(source, renamed.edits);
    CHECK(changed.find("velocita") == std::string::npos);
    CHECK(changed.find("print(rapidita") != std::string::npos);
    CHECK_FALSE(rename(source, at(source, "velocita"), "fn", context).error.empty());
    CHECK_FALSE(rename(source, at(source, "velocita"), "salta", context).error.empty());
    CHECK_FALSE(rename(source, at(source, "print"), "stampa", context).error.empty());
    CHECK(snippets().size() >= 10);
}

TEST_CASE("Lim: debugger", "[lim][debugger]") {
    TypeRegistry registry = standardRegistry();
    AnalyzeOptions options;
    options.file = "Debug.lim";
    auto result = compileSource("fn doppio(n: Integer) -> Integer {\n    let r = n * 2\n    return r\n}\nvar totale = 0\ntotale = doppio(n: 4)\nprint(totale)\n", "Debug", registry, options);
    REQUIRE(result.module);
    Vm vm;
    std::vector<std::string> printed;
    vm.setOutput([&](const OutputMessage& m) { printed.push_back(m.text); });
    std::vector<DebugStop> stops;
    DebugSettings settings;
    settings.breakpoints["Debug.lim"] = {3};
    settings.stopped = [&](const DebugStop& s) { stops.push_back(s); };
    vm.setDebugger(settings);
    vm.run(std::make_shared<const Module>(std::move(*result.module)), vm.newEnvironment("Debug"), "Debug.lim");
    REQUIRE(stops.size() == 1);
    CHECK(vm.paused());
    CHECK(printed.empty());
    REQUIRE(stops[0].frames.size() == 2);
    CHECK(stops[0].frames[0].function == "doppio");
    CHECK(stops[0].frames[0].line == 3);
    auto locals = stops[0].frames[0].locals;
    auto value = [&](const std::string& name) {
        for (const auto& l : locals)
            if (l.name == name) return l.value;
        return std::string("?");
    };
    CHECK(value("n") == "4");
    CHECK(value("r") == "8");
    vm.resume(StepMode::out);
    REQUIRE(stops.size() == 2);
    CHECK(stops[1].frames.size() == 1);
    CHECK(stops[1].frames[0].line == 6);
    vm.resume(StepMode::over);
    REQUIRE(stops.size() == 3);
    CHECK(stops[2].frames[0].line == 7);
    vm.clearDebugger();
    CHECK_FALSE(vm.paused());
    CHECK(printed == std::vector<std::string>{"8"});
}

namespace {

const char* const editorCorpus = R"LIM(import Combattimento
let limite = 16
var contatore: Integer = 0
var punti = [1, 2, 3]
var nomi = ["a": 1, "b": 2]
enum Stato { fermo, corsa, salto }
class Arma {
    var danno: Number = 25
    let nome = "spada"
    fn colpisci(bersaglio: Number, forza: Number = 1) -> Number {
        self.danno += forza
        return bersaglio - danno
    }
}
fn somma(a: Number, b: Number = 0) -> Number { return a + b }
async fn attesa(secondi: Number) {
    await Task.wait(secondi)
}
let arma = Arma()
contatore = contatore + 1
contatore += limite
punti[0] = somma(1, b: 2)
nomi["c"] = punti.count
arma.danno *= 2
if contatore > 3 && limite != 0 {
    print("alto \(contatore)")
} else if not (contatore == 1) {
    print(nomi["a"] ?? 0)
} else {
    contatore -= 1
}
while contatore < 10 { contatore += 1; if contatore == 5 { break } else { continue } }
for i in 0..<3 { print(i) }
for chiave, valore in nomi { print(chiave, valore) }
let doppio = fn(x) { return x * 2 }
var stato: Stato = .fermo
match stato {
    .fermo { print("fermo") }
    .corsa, .salto { print("mosso") }
    else { print("altro") }
}
let forse: Arma? = none
print(forse?.danno ?? 0, punti.map(doppio), stato is Stato, -contatore, !true)
on Players.joined(player) {
    print(player.name)
}
)LIM";

void exercise(const std::string& source, const Context& context) {
    auto parsed = parse(source, "prova");
    analyze(parsed.program, context.registry, context.options);
    highlight(source);
    format(source);
    std::uint32_t length = static_cast<std::uint32_t>(source.size());
    for (std::uint32_t offset : {std::uint32_t(0), length / 3, length / 2, length > 0 ? length - 1 : 0, length}) {
        complete(source, offset, context);
        hover(source, offset, context);
        definition(source, offset, context);
        references(source, offset, context);
        rename(source, offset, "nuovo", context);
    }
}

}

TEST_CASE("Lim: assegnazione incompleta nell'editor", "[lim][tooling][crash]") {
    TypeRegistry registry = standardRegistry();
    Context context{registry, {}};
    for (const std::string source : {"var x = 1\nx = ", "var x = 1\nx += ", "var x = [1]\nx[0] = ", "var x = 1\nx = \nprint(x)", "x.y = ", "= 1", "var a = 1\na = (", "var a = 1\na = fn("}) {
        INFO(source);
        auto parsed = parse(source, "prova");
        CHECK_FALSE(parsed.diagnostics.empty());
        auto analysis = analyze(parsed.program, context.registry, context.options);
        for (std::uint32_t offset = 0; offset <= source.size(); ++offset) {
            hover(source, offset, context);
            complete(source, offset, context);
            definition(source, offset, context);
            references(source, offset, context);
        }
    }
    SUCCEED();
}

TEST_CASE("Lim: analisi di ogni prefisso", "[lim][tooling][crash][fuzz]") {
    TypeRegistry registry = standardRegistry();
    Context context{registry, {}};
    std::string corpus = editorCorpus;
    REQUIRE(parse(corpus, "prova").diagnostics.empty());
    int analysed = 0;
    for (std::size_t length = 0; length <= corpus.size(); ++length) {
        exercise(corpus.substr(0, length), context);
        ++analysed;
    }
    std::vector<std::string> lines;
    for (std::size_t start = 0; start < corpus.size();) {
        std::size_t end = corpus.find('\n', start);
        if (end == std::string::npos) end = corpus.size();
        lines.push_back(corpus.substr(start, end - start));
        start = end + 1;
    }
    for (std::size_t skip = 0; skip < lines.size(); ++skip) {
        std::string source;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (i != skip) source += lines[i] + "\n";
        exercise(source, context);
        ++analysed;
    }
    for (std::size_t position = 0; position < corpus.size(); ++position) {
        if (std::isalnum(static_cast<unsigned char>(corpus[position])) || corpus[position] == ' ' || corpus[position] == '\n') continue;
        std::string source = corpus;
        source.erase(position, 1);
        exercise(source, context);
        source = corpus;
        source.insert(position, 1, corpus[position]);
        exercise(source, context);
        analysed += 2;
    }
    CHECK(analysed > 1500);
}
