#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <algorithm>

namespace digidaw::domain {

// Sandboxed mathematical expression evaluator for automation curves (DAW-FR-703)
// Evaluates formulas such as "x", "x^2", "sin(x * 3.14159)", "1.0 - x", "abs(x)"
class FormulaEvaluator {
public:
    static double evaluate(std::string_view expression, double x) {
        if (expression.empty()) {
            return x;
        }

        FormulaEvaluator parser(expression, x);
        try {
            double result = parser.parse_expression();
            if (parser.peek() != '\0') {
                throw std::runtime_error("Trailing unparsed characters");
            }
            if (std::isnan(result) || std::isinf(result)) {
                return std::clamp(x, 0.0, 1.0);
            }
            return std::clamp(result, 0.0, 1.0);
        } catch (...) {
            return x; // Fail-safe: fallback to raw x
        }
    }

private:
    explicit FormulaEvaluator(std::string_view expr, double x_val)
        : expr_(expr), pos_(0), x_(x_val) {}

    char peek() const noexcept {
        skip_whitespace();
        if (pos_ < expr_.size()) {
            return expr_[pos_];
        }
        return '\0';
    }

    char get() noexcept {
        skip_whitespace();
        if (pos_ < expr_.size()) {
            return expr_[pos_++];
        }
        return '\0';
    }

    void skip_whitespace() const noexcept {
        while (pos_ < expr_.size() && std::isspace(static_cast<unsigned char>(expr_[pos_]))) {
            ++pos_;
        }
    }

    double parse_expression() {
        double val = parse_term();
        while (true) {
            char op = peek();
            if (op == '+' || op == '-') {
                get();
                double rhs = parse_term();
                if (op == '+') val += rhs;
                else val -= rhs;
            } else {
                break;
            }
        }
        return val;
    }

    double parse_term() {
        double val = parse_power();
        while (true) {
            char op = peek();
            if (op == '*' || op == '/') {
                get();
                double rhs = parse_power();
                if (op == '*') {
                    val *= rhs;
                } else {
                    val = (std::abs(rhs) > 1e-9) ? (val / rhs) : 0.0;
                }
            } else {
                break;
            }
        }
        return val;
    }

    double parse_power() {
        double val = parse_factor();
        if (peek() == '^') {
            get();
            double rhs = parse_power();
            val = std::pow(val, rhs);
        }
        return val;
    }

    double parse_factor() {
        char c = peek();
        if (c == '-') {
            get();
            return -parse_factor();
        }
        if (c == '+') {
            get();
            return parse_factor();
        }

        if (c == '(') {
            get(); // Consume '('
            double val = parse_expression();
            if (peek() == ')') get(); // Consume ')'
            return val;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            return parse_number();
        }

        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string ident;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
                ident += get();
            }

            std::transform(ident.begin(), ident.end(), ident.begin(), ::tolower);

            if (ident == "x") {
                return x_;
            } else if (ident == "pi") {
                return 3.14159265358979323846;
            }

            // Function call
            if (peek() == '(') {
                get();
                double arg = parse_expression();
                if (peek() == ')') get();

                if (ident == "sin") return std::sin(arg);
                if (ident == "cos") return std::cos(arg);
                if (ident == "tan") return std::tan(arg);
                if (ident == "abs") return std::abs(arg);
                if (ident == "sqrt") return (arg >= 0.0) ? std::sqrt(arg) : 0.0;
                if (ident == "exp") return std::exp(arg);
                if (ident == "log") return (arg > 1e-9) ? std::log(arg) : 0.0;
                return arg;
            }
        }

        throw std::runtime_error("Unexpected token in factor");
    }

    double parse_number() {
        size_t start = pos_;
        while (pos_ < expr_.size() && (std::isdigit(static_cast<unsigned char>(expr_[pos_])) || expr_[pos_] == '.')) {
            ++pos_;
        }
        std::string num_str(expr_.substr(start, pos_ - start));
        try {
            return std::stod(num_str);
        } catch (...) {
            return 0.0;
        }
    }

    std::string_view expr_;
    mutable size_t pos_{0};
    double x_{0.0};
};

} // namespace digidaw::domain
