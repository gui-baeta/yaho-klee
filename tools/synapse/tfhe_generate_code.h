#pragma once

#include <string>
#include "klee/util/Ref.h"
#include "klee/Expr.h"

// Code generation helper functions

std::string generate_tfhe_constant(const klee::ref<klee::Expr>& expr);

std::string generate_tfhe_read(const klee::ref<klee::Expr>& expr, bool needs_cloning = true);

std::string generate_tfhe_code(const klee::ref<klee::Expr>& expr, bool needs_cloning = true);

std::vector<int> get_dependent_values(const klee::ref<klee::Expr> &expr, const klee::ref<klee::Expr> &parent_expr = nullptr, bool check_if_modified = false);

