/**
 * AlgoForge — src/algo_gen/dsl.cpp
 * S1 Phase 1 — Whitelist DSL expression parser (recursive descent).
 *
 * Grammar (mirrors Python dsl.py semantics):
 *   expr   = or_expr
 *   or_expr  = and_expr ('or'  and_expr)*
 *   and_expr = not_expr ('and' not_expr)*
 *   not_expr = 'not'? cmp_expr
 *   cmp_expr = add_expr (('<'|'<='|'>'|'>='|'=='|'!=') add_expr)?
 *   add_expr = mul_expr (('+' | '-') mul_expr)*
 *   mul_expr = unary (('*' | '/') unary)*
 *   unary    = '-'? primary
 *   primary  = NUMBER | NAME | 'pattern' '.' NAME | '(' expr ')'
 *
 * Allowed NAMEs:
 *   bar fields: close open high low volume spread
 *   declared indicator IDs
 *   special: pattern (prefix for pattern.X access)
 *
 * pattern.X is an attribute access producing a bool.
 */

#include "algo_gen_internal.hpp"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <cmath>

namespace algoforge::algo_gen::dsl {

/* =========================================================================
 * Known names
 * ========================================================================= */

static const std::set<std::string> BAR_FIELDS = {
    "close","open","high","low","volume","spread"
};

static const std::set<std::string> PATTERN_NAMES = {
    "doji","hammer","engulfing","marubozu","pin_bar",
    "morning_star","evening_star",
    "three_white_soldiers","three_black_crows",
    "double_top","double_bottom",
    "ascending_triangle","descending_triangle",
    "bullish_flag","bearish_flag",
    "head_and_shoulders","inverse_head_and_shoulders",
    "gartley_bull","gartley_bear",
    "bat_bull","butterfly_bull","crab_bull",
};

/* =========================================================================
 * Tokeniser
 * ========================================================================= */

struct Token {
    enum Kind {
        Num, Id, LParen, RParen, Dot,
        Plus, Minus, Star, Slash,
        Lt, LtEq, Gt, GtEq, EqEq, NEq,
        And, Or, Not,
        Eof, Unknown
    } kind;
    std::string text;
    double      num = 0.0;
};

struct Lexer {
    const char* src;
    size_t      len;
    size_t      pos = 0;

    explicit Lexer(const std::string& s) : src(s.data()), len(s.size()) {}

    void skip_ws() {
        while (pos < len && std::isspace((unsigned char)src[pos])) ++pos;
    }

    Token next() {
        skip_ws();
        if (pos >= len) return {Token::Eof, ""};
        char c = src[pos];

        // Numbers
        if (std::isdigit((unsigned char)c) || (c == '.' && pos+1 < len && std::isdigit((unsigned char)src[pos+1]))) {
            size_t start = pos;
            while (pos < len && (std::isdigit((unsigned char)src[pos]) || src[pos] == '.')) ++pos;
            if (pos < len && (src[pos] == 'e' || src[pos] == 'E')) {
                ++pos;
                if (pos < len && (src[pos] == '+' || src[pos] == '-')) ++pos;
                while (pos < len && std::isdigit((unsigned char)src[pos])) ++pos;
            }
            std::string s(src + start, pos - start);
            Token t;
            t.kind = Token::Num;
            t.text = s;
            t.num  = std::stod(s);
            return t;
        }

        // Identifiers / keywords
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t start = pos;
            while (pos < len && (std::isalnum((unsigned char)src[pos]) || src[pos] == '_')) ++pos;
            std::string s(src + start, pos - start);
            if (s == "and") return {Token::And, s};
            if (s == "or")  return {Token::Or,  s};
            if (s == "not") return {Token::Not, s};
            return {Token::Id, s};
        }

        ++pos;
        switch (c) {
            case '(': return {Token::LParen,  "("};
            case ')': return {Token::RParen,  ")"};
            case '.': return {Token::Dot,     "."};
            case '+': return {Token::Plus,    "+"};
            case '-': return {Token::Minus,   "-"};
            case '*': return {Token::Star,    "*"};
            case '/': return {Token::Slash,   "/"};
            case '<':
                if (pos < len && src[pos] == '=') { ++pos; return {Token::LtEq, "<="}; }
                return {Token::Lt, "<"};
            case '>':
                if (pos < len && src[pos] == '=') { ++pos; return {Token::GtEq, ">="}; }
                return {Token::Gt, ">"};
            case '=':
                if (pos < len && src[pos] == '=') { ++pos; return {Token::EqEq, "=="}; }
                return {Token::Unknown, "="};
            case '!':
                if (pos < len && src[pos] == '=') { ++pos; return {Token::NEq, "!="}; }
                return {Token::Unknown, "!"};
            default:
                return {Token::Unknown, std::string(1,c)};
        }
    }

    // Peek ahead without consuming
    Token peek() {
        size_t saved = pos;
        Token t = next();
        pos = saved;
        return t;
    }
};

/* =========================================================================
 * Recursive-descent parser
 * ========================================================================= */

struct Parser {
    Lexer lexer;
    Token cur;
    const std::vector<std::string>& ind_ids;
    std::set<std::string> allowed_names;

    Parser(const std::string& src, const std::vector<std::string>& ids)
        : lexer(src), ind_ids(ids)
    {
        for (const auto& id : ids) allowed_names.insert(id);
        for (const auto& bf : BAR_FIELDS) allowed_names.insert(bf);
        allowed_names.insert("pattern");
        cur = lexer.next();
    }

    Token consume() {
        Token t = cur;
        cur = lexer.next();
        return t;
    }

    bool at(Token::Kind k) const { return cur.kind == k; }

    Expr make_binop(const std::string& op, Expr l, Expr r) {
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::BinOp;
        n->name = op;
        n->children = {l, r};
        return n;
    }
    Expr make_cmpop(const std::string& op, Expr l, Expr r) {
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::CmpOp;
        n->name = op;
        n->children = {l, r};
        return n;
    }
    Expr make_bool(NodeKind k, Expr l, Expr r) {
        auto n = std::make_shared<Node>();
        n->kind = k;
        n->children = {l, r};
        return n;
    }

    // Parse full expression
    Expr parse_expr() {
        return parse_or();
    }

    Expr parse_or() {
        Expr left = parse_and();
        while (at(Token::Or)) {
            consume();
            Expr right = parse_and();
            left = make_bool(NodeKind::BoolOr, left, right);
        }
        return left;
    }

    Expr parse_and() {
        Expr left = parse_not();
        while (at(Token::And)) {
            consume();
            Expr right = parse_not();
            left = make_bool(NodeKind::BoolAnd, left, right);
        }
        return left;
    }

    Expr parse_not() {
        if (at(Token::Not)) {
            consume();
            Expr operand = parse_cmp();
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::Not;
            n->children = {operand};
            return n;
        }
        return parse_cmp();
    }

    Expr parse_cmp() {
        Expr left = parse_add();
        if (at(Token::Lt) || at(Token::LtEq) || at(Token::Gt) || at(Token::GtEq) ||
            at(Token::EqEq) || at(Token::NEq)) {
            std::string op = cur.text;
            consume();
            Expr right = parse_add();
            return make_cmpop(op, left, right);
        }
        return left;
    }

    Expr parse_add() {
        Expr left = parse_mul();
        while (at(Token::Plus) || at(Token::Minus)) {
            std::string op = cur.text;
            consume();
            Expr right = parse_mul();
            left = make_binop(op, left, right);
        }
        return left;
    }

    Expr parse_mul() {
        Expr left = parse_unary();
        while (at(Token::Star) || at(Token::Slash)) {
            std::string op = cur.text;
            consume();
            Expr right = parse_unary();
            left = make_binop(op, left, right);
        }
        return left;
    }

    Expr parse_unary() {
        if (at(Token::Minus)) {
            consume();
            Expr operand = parse_primary();
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::Neg;
            n->children = {operand};
            return n;
        }
        return parse_primary();
    }

    Expr parse_primary() {
        if (at(Token::Num)) {
            auto n = std::make_shared<Node>();
            n->kind    = NodeKind::Lit;
            n->lit_val = cur.num;
            consume();
            return n;
        }

        if (at(Token::LParen)) {
            consume();
            Expr inner = parse_expr();
            if (!at(Token::RParen))
                throw std::invalid_argument("DSL: expected ')' in expression");
            consume();
            return inner;
        }

        if (at(Token::Id)) {
            std::string id = cur.text;
            consume();

            // pattern.X ?
            if (id == "pattern" && at(Token::Dot)) {
                consume();
                if (!at(Token::Id))
                    throw std::invalid_argument("DSL: expected pattern name after 'pattern.'");
                std::string pname = cur.text;
                consume();
                if (PATTERN_NAMES.find(pname) == PATTERN_NAMES.end())
                    throw std::invalid_argument(
                        "DSL: unknown pattern name '" + pname + "'");
                auto n = std::make_shared<Node>();
                n->kind = NodeKind::Pattern;
                n->name = pname;
                return n;
            }

            // Must be an allowed name
            if (allowed_names.find(id) == allowed_names.end()) {
                throw std::invalid_argument(
                    "DSL: name '" + id + "' is not declared");
            }
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::Name;
            n->name = id;
            return n;
        }

        throw std::invalid_argument(
            "DSL: unexpected token '" + cur.text + "' in expression");
    }
};

/* =========================================================================
 * Public API
 * ========================================================================= */

Expr compile_expr(const std::string& src, const std::vector<std::string>& indicator_ids) {
    if (src.empty())
        throw std::invalid_argument("DSL: expression must be non-empty");
    Parser p(src, indicator_ids);
    Expr expr = p.parse_expr();
    if (p.cur.kind != Token::Eof)
        throw std::invalid_argument("DSL: unexpected token at end: '" + p.cur.text + "'");
    return expr;
}

/* =========================================================================
 * Evaluator
 * ========================================================================= */

static double eval_double(const Expr& node, const Env& env);

static double eval_double(const Expr& node, const Env& env) {
    switch (node->kind) {
        case NodeKind::Lit:
            return node->lit_val;

        case NodeKind::Name: {
            auto it = env.find(node->name);
            if (it == env.end()) return std::numeric_limits<double>::quiet_NaN();
            return it->second;
        }

        case NodeKind::Pattern: {
            // pattern values stored as 0.0/1.0 in env with prefix "pattern."
            auto it = env.find("pattern." + node->name);
            if (it != env.end()) return it->second;
            return 0.0;
        }

        case NodeKind::Neg:
            return -eval_double(node->children[0], env);

        case NodeKind::BinOp: {
            double l = eval_double(node->children[0], env);
            double r = eval_double(node->children[1], env);
            if (std::isnan(l) || std::isnan(r)) return std::numeric_limits<double>::quiet_NaN();
            if (node->name == "+") return l + r;
            if (node->name == "-") return l - r;
            if (node->name == "*") return l * r;
            if (node->name == "/") return (std::abs(r) > 1e-15) ? l / r : std::numeric_limits<double>::quiet_NaN();
            return std::numeric_limits<double>::quiet_NaN();
        }

        case NodeKind::CmpOp: {
            double l = eval_double(node->children[0], env);
            double r = eval_double(node->children[1], env);
            if (std::isnan(l) || std::isnan(r)) return 0.0;
            if (node->name == "<")  return l < r  ? 1.0 : 0.0;
            if (node->name == "<=") return l <= r ? 1.0 : 0.0;
            if (node->name == ">")  return l > r  ? 1.0 : 0.0;
            if (node->name == ">=") return l >= r ? 1.0 : 0.0;
            if (node->name == "==") return l == r ? 1.0 : 0.0;
            if (node->name == "!=") return l != r ? 1.0 : 0.0;
            return 0.0;
        }

        case NodeKind::BoolAnd: {
            double l = eval_double(node->children[0], env);
            if (std::isnan(l) || l == 0.0) return 0.0;
            double r = eval_double(node->children[1], env);
            return (std::isnan(r) || r == 0.0) ? 0.0 : 1.0;
        }

        case NodeKind::BoolOr: {
            double l = eval_double(node->children[0], env);
            if (!std::isnan(l) && l != 0.0) return 1.0;
            double r = eval_double(node->children[1], env);
            return (!std::isnan(r) && r != 0.0) ? 1.0 : 0.0;
        }

        case NodeKind::Not: {
            double v = eval_double(node->children[0], env);
            if (std::isnan(v)) return 1.0; // not NaN → treat as false → not false = true? No — treat as false.
            return v == 0.0 ? 1.0 : 0.0;
        }

        default:
            return std::numeric_limits<double>::quiet_NaN();
    }
}

bool eval_expr(const Expr& expr, const Env& env) {
    try {
        double v = eval_double(expr, env);
        if (std::isnan(v)) return false;
        return v != 0.0;
    } catch (...) {
        return false;
    }
}

} /* namespace algoforge::algo_gen::dsl */
