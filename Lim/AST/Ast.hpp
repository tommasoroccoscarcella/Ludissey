#pragma once

#include "Lim/Lexer/Lexer.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lim {

struct TypeExpr {
    enum class Kind { named, list, map, function, optional };
    Kind kind = Kind::named;
    std::string name;
    std::vector<std::shared_ptr<TypeExpr>> arguments;
    std::shared_ptr<TypeExpr> result;
    Range range;
};

using TypeRef = std::shared_ptr<TypeExpr>;

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Block {
    std::vector<StmtPtr> statements;
    Range range;
};

struct Parameter {
    std::string name;
    TypeRef type;
    ExprPtr fallback;
    Range range;
};

struct Argument {
    std::string label;
    ExprPtr value;
    Range range;
};

enum class ExprKind {
    none,
    boolean,
    integer,
    number,
    text,
    interpolation,
    identifier,
    self,
    shorthand,
    list,
    map,
    unary,
    binary,
    call,
    member,
    index,
    lambda,
    await,
    is,
    cast,
    invalid
};

struct Expr {
    ExprKind kind;
    Range range;
    bool flag = false;
    long long integer = 0;
    double number = 0;
    std::string text;
    std::string op;
    std::vector<std::string> parts;
    std::vector<ExprPtr> children;
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;
    std::vector<Argument> arguments;
    std::vector<Parameter> parameters;
    TypeRef type;
    TypeRef returns;
    std::shared_ptr<Block> body;
    bool optionalChain = false;
    bool async = false;
    int slot = -1;
    int depth = -1;

    explicit Expr(ExprKind kind, Range range) : kind(kind), range(range) {}
    Expr* child(std::size_t i) const { return i < children.size() ? children[i].get() : nullptr; }
};

enum class StmtKind { declaration, function, classDecl, enumDecl, import, ifStmt, whileStmt, forStmt, matchStmt, returnStmt, breakStmt, continueStmt, expression, assign, on, block };

struct MatchCase {
    std::vector<ExprPtr> patterns;
    bool otherwise = false;
    std::shared_ptr<Block> body;
    Range range;
};

struct FunctionDecl {
    std::string name;
    std::vector<Parameter> parameters;
    TypeRef returns;
    std::shared_ptr<Block> body;
    bool async = false;
    bool isInit = false;
    Range nameRange;
};

struct Field {
    std::string name;
    bool mutable_ = true;
    TypeRef type;
    ExprPtr initializer;
    Range range;
};

struct Stmt {
    StmtKind kind;
    Range range;
    std::string name;
    Range nameRange;
    bool mutable_ = false;
    bool exported = false;
    TypeRef type;
    ExprPtr value;
    ExprPtr target;
    std::string op;
    std::vector<std::string> names;
    std::vector<Range> nameRanges;
    std::shared_ptr<Block> body;
    std::shared_ptr<Block> otherwise;
    std::vector<MatchCase> cases;
    std::shared_ptr<FunctionDecl> function;
    std::vector<Field> fields;
    std::vector<std::shared_ptr<FunctionDecl>> methods;
    std::vector<std::pair<std::string, Range>> enumCases;
    std::vector<Parameter> parameters;
    int slot = -1;

    explicit Stmt(StmtKind kind, Range range) : kind(kind), range(range) {}
};

struct Program {
    std::vector<StmtPtr> statements;
    std::string file;
};

}
