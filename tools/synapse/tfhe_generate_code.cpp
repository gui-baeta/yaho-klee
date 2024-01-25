#include <string>

#include "klee/util/Ref.h"
#include "klee/Expr.h"

// Code generation helper functions

std::string generate_tfhe_constant(const klee::ref<klee::Expr>& expr) {
    klee::ConstantExpr* const_expr = dyn_cast<klee::ConstantExpr>(expr);
    std::string constant;
    const_expr->toString(constant);
    return constant;
}

std::string generate_tfhe_read(const klee::ref<klee::Expr>& expr) {
    klee::ReadExpr* read_expr = dyn_cast<klee::ReadExpr>(expr);
    std::string array_name = read_expr->updates.root->name;
    klee::ConstantExpr* const_expr =
        dyn_cast<klee::ConstantExpr>(read_expr->index);

    std::string index_as_str;
    const_expr->toString(index_as_str);
    return std::string("val") + index_as_str + std::string(".clone()");
//    return array_name + std::string("[") + index + std::string("]");
}

std::string generate_tfhe_code(const klee::ref<klee::Expr>& expr) {
    std::string code = "()";

    switch (expr->getKind()) {
    case klee::Expr::Constant: {
        code = generate_tfhe_constant(expr);
        break;
    }
    case klee::Expr::Read: {
        code = generate_tfhe_read(expr);
        break;
    }
    case klee::Expr::Select: {
        // TODO
        break;
    }
    case klee::Expr::Concat: {
        code = generate_tfhe_code(expr->getKid(0)) + std::string(", ") + generate_tfhe_code(expr->getKid(1));
        break;
    }
    case klee::Expr::Extract: {
        // This is done since the Extract doesn't do anything in particular
        code = generate_tfhe_code(expr->getKid(0));
        break;
    }
    case klee::Expr::ZExt: {
        // FIXME This is a hack to avoid generating the zero extension for a
        //  casting
        //  Only the expression inside the Zero Extension is used:
        //    (ZExt w32
        //  >    (Read w8
        //  >         (w32 0)
        //  >         packet_chunks
        //  >    )
        //    )

        code = generate_tfhe_code(expr->getKid(0));
        break;
    }
    case klee::Expr::SExt: {
        // FIXME This is a hack to avoid generating the signed extension for a
        //  casting. The Signed Extension adds 1s to the left of the rightmost 1
        code = generate_tfhe_code(expr->getKid(0));
        break;
    }
    case klee::Expr::Add: {
        code = generate_tfhe_code(expr->getKid(0)) + std::string(" + ") + generate_tfhe_code(expr->getKid(1));
        break;
    }
    case klee::Expr::Sub: {
        // TODO
        break;
    }
    case klee::Expr::Mul: {
        // TODO
        break;
    }
    case klee::Expr::UDiv: {
        // TODO
        break;
    }
    case klee::Expr::SDiv: {
        // TODO
        break;
    }
    case klee::Expr::URem: {
        // TODO
        break;
    }
    case klee::Expr::SRem: {
        // TODO
        break;
    }
    case klee::Expr::Not: {
        // TODO
        break;
    }
    case klee::Expr::And: {
        // TODO
        break;
    }
    case klee::Expr::Or: {
        // TODO
        break;
    }
    case klee::Expr::Xor: {
        // TODO
        break;
    }
    case klee::Expr::Shl: {
        // TODO
        break;
    }
    case klee::Expr::LShr: {
        // TODO
        break;
    }
    case klee::Expr::AShr: {
        // TODO
        break;
    }
    }

    return code;
}

//std::string generate_tfhe_code(const ExecutionPlanNode_ptr &ep_node) {
//    return std::string("Unimplemented");
//}