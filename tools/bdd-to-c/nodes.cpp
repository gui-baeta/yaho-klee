#include "nodes.h"
#include "ast.h"

Expr_ptr SignedExpression::simplify(AST *ast) const {
  Expr_ptr signed_expr_simplified = expr->simplify(ast);
  return SignedExpression::build(signed_expr_simplified);
}

Expr_ptr Cast::simplify(AST *ast) const {
  Expr_ptr expr_simplified = expr->simplify(ast);
  return Cast::build(expr_simplified, type);
}

Expr_ptr NotEquals::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return NotEquals::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Equals::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);

  if (lhs_simplified->get_kind() == CONSTANT &&
      rhs_simplified->get_kind() == EQUALS) {
    Constant *c = static_cast<Constant *>(lhs_simplified.get());
    Equals *e = static_cast<Equals *>(rhs_simplified.get());

    if (c->get_value() == 0) {
      NotEquals_ptr ne = NotEquals::build(e->get_lhs(), e->get_rhs());
      return ne->simplify(ast);
    }
  }

  if (lhs_simplified->get_kind() == EQUALS &&
      rhs_simplified->get_kind() == CONSTANT) {
    Equals *e = static_cast<Equals *>(lhs_simplified.get());
    Constant *c = static_cast<Constant *>(rhs_simplified.get());

    if (c->get_value() == 0) {
      NotEquals_ptr ne = NotEquals::build(e->get_lhs(), e->get_rhs());
      return ne->simplify(ast);
    }
  }

  return Equals::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Not::simplify(AST *ast) const {
  Expr_ptr expr_simplified = expr->simplify(ast);

  switch (expr_simplified->get_kind()) {
  case NOT: {
    Not *n = static_cast<Not *>(expr_simplified.get());
    return n->get_expr()->simplify(ast);
  }
  case EQUALS: {
    Equals *eq = static_cast<Equals *>(expr_simplified.get());
    Expr_ptr constant;
    Expr_ptr expression;

    if (eq->get_lhs()->get_kind() == CONSTANT &&
        eq->get_rhs()->get_kind() != CONSTANT) {
      constant = eq->get_lhs();
      expression = eq->get_rhs();
    } else if (eq->get_lhs()->get_kind() != CONSTANT &&
               eq->get_rhs()->get_kind() == CONSTANT) {
      constant = eq->get_rhs();
      expression = eq->get_lhs();
    } else if (eq->get_lhs()->get_kind() != CONSTANT &&
               eq->get_rhs()->get_kind() != CONSTANT) {
      NotEquals_ptr ne = NotEquals::build(eq->get_lhs(), eq->get_rhs());
      return ne->simplify(ast);
    } else {
      break;
    }

    Constant *c = static_cast<Constant *>(constant.get());

    if (c->get_value() != 0) {
      break;
    }

    return expression->simplify(ast);
  }

  case NOT_EQUALS: {
    NotEquals *ne = static_cast<NotEquals *>(expr_simplified.get());
    Equals_ptr eq = Equals::build(ne->get_lhs(), ne->get_rhs());
    return eq->simplify(ast);
  };
  default:
    break;
  };

  return Not::build(expr_simplified);
}

Expr_ptr FunctionCall::simplify(AST *ast) const {
  std::vector<ExpressionType_ptr> args_simplified;

  for (auto arg : args) {
    if (arg->get_expression_type_kind() == EXPRESSION_KIND) {
      Expression *expr = static_cast<Expression *>(arg.get());
      args_simplified.push_back(expr->simplify(ast));
    } else {
      args_simplified.push_back(arg);
    }
  }

  return FunctionCall::build(name, args, type);
}

Expr_ptr Greater::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Greater::build(lhs_simplified, rhs_simplified);
}

Expr_ptr GreaterEq::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return GreaterEq::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Less::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Less::build(lhs_simplified, rhs_simplified);
}

Expr_ptr LessEq::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return LessEq::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Add::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Add::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Sub::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Sub::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Mul::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Mul::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Div::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Div::build(lhs_simplified, rhs_simplified);
}

Expr_ptr And::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return And::build(lhs_simplified, rhs_simplified);
}

Expr_ptr LogicalAnd::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return LogicalAnd::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Or::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Or::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Xor::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Xor::build(lhs_simplified, rhs_simplified);
}

Expr_ptr Mod::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return Mod::build(lhs_simplified, rhs_simplified);
}

Expr_ptr ShiftLeft::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return ShiftLeft::build(lhs_simplified, rhs_simplified);
}

Expr_ptr ShiftRight::simplify(AST *ast) const {
  Expr_ptr lhs_simplified = lhs->simplify(ast);
  Expr_ptr rhs_simplified = rhs->simplify(ast);
  return ShiftRight::build(lhs_simplified, rhs_simplified);
}

Expr_ptr AddressOf::simplify(AST *ast) const {
  Expr_ptr expr_simplified = expr->simplify(ast);
  return AddressOf::build(expr_simplified);
}

Expr_ptr Read::simplify(AST *ast) const {
  Expr_ptr idx_simplified = idx->simplify(ast);
  Expr_ptr expr_simplified = expr->simplify(ast);

  Type_ptr expr_type = expr_simplified->get_type();
  while (expr_type->get_type_kind() == Type::TypeKind::POINTER) {
    expr_type = static_cast<Pointer *>(expr_type.get())->get_type();
  }

  if (idx_simplified->get_kind() == Node::NodeKind::CONSTANT) {
    Constant *idx_constant = static_cast<Constant *>(idx_simplified.get());
    Type_ptr expr_type = expr_simplified->get_type();

    auto size = type->get_size();
    auto idx_value = idx_constant->get_value();
    auto expr_size = expr_type->get_size();

    if (expr_simplified->get_kind() != Node::NodeKind::CAST && idx_value == 0 &&
        size == expr_size && type->get_type_kind() != Type::TypeKind::POINTER &&
        type->get_type_kind() != Type::TypeKind::ARRAY) {
      return expr_simplified;
    }
  }

  return Read::build(expr_simplified, type, idx_simplified);
}

Expr_ptr Concat::simplify(AST *ast) const {
  assert(ast);

  Expr_ptr left_simplified = left->simplify(ast);
  Expr_ptr right_simplified = right->simplify(ast);

  auto concat_size = type->get_size();

  if (left_simplified->get_kind() == CONSTANT &&
      right_simplified->get_kind() == CONCAT) {
    Concat *right_concat = static_cast<Concat *>(right_simplified.get());
    Expr_ptr right_concat_left = right_concat->get_left();
    Expr_ptr right_concat_right = right_concat->get_right();

    if (right_concat_left->get_kind() == CONSTANT) {
      Constant *left_const = static_cast<Constant *>(left_simplified.get());
      Constant *right_left_const =
          static_cast<Constant *>(right_concat_left.get());

      Type_ptr left_const_type = left_const->get_type();
      Type_ptr right_left_const_type = right_left_const->get_type();

      Type_ptr type = type_from_size(left_const_type->get_size() +
                                     right_left_const_type->get_size());

      auto left_const_value = left_const->get_value();
      auto right_left_const_value = right_left_const->get_value();

      Constant_ptr new_const = Constant::build(type);
      new_const->set_value(left_const_value
                               << right_left_const_type->get_size() |
                           right_left_const_value);

      Expr_ptr left_concat_simplified = new_const->simplify(ast);
      Concat *final_concat =
          new Concat(left_concat_simplified, right_concat_right);

      return final_concat->simplify(ast);
    }
  }

  if (left_simplified->get_kind() == READ &&
      right_simplified->get_kind() == READ) {
    Read *lread = static_cast<Read *>(left_simplified.get());
    Read *rread = static_cast<Read *>(right_simplified.get());

    if (lread->get_symbol() == rread->get_symbol()) {
      if (rread->get_expr()->get_kind() == VARIABLE) {
        Variable *var = static_cast<Variable *>(rread->get_expr().get());

        auto var_size = var->get_type()->get_size();

        if (var_size == concat_size) {
          return var->clone();
        }
      }

      Read_ptr r = Read::build(rread->get_expr(), type, rread->get_idx());
      return r->simplify(ast);
    }
  }

  if (left_simplified->get_kind() == READ &&
      right_simplified->get_kind() == CONCAT) {
    Concat *right_concat = static_cast<Concat *>(right_simplified.get());
    Expr_ptr right_concat_left = right_concat->get_left();
    Expr_ptr right_concat_right = right_concat->get_right();

    if (right_concat_left->get_kind() == READ) {
      Read *left_read = static_cast<Read *>(left_simplified.get());
      Read *right_left_read = static_cast<Read *>(right_concat_left.get());

      if (left_read->get_symbol() == right_left_read->get_symbol()) {
        Concat *left_concat = new Concat(left_simplified, right_concat_left);
        Expr_ptr left_concat_simplified = left_concat->simplify(ast);
        Concat *final_concat =
            new Concat(left_concat_simplified, right_concat_right);

        return final_concat->simplify(ast);
      }
    }
  }

  return Concat::build(left_simplified, right_simplified, type);
}

Expr_ptr Select::simplify(AST *ast) const {
  Expr_ptr cond_simplified = cond->simplify(ast);
  Expr_ptr first_simplified = first->simplify(ast);
  Expr_ptr second_simplified = second->simplify(ast);
  return Select::build(cond_simplified, first_simplified, second_simplified);
}

Expr_ptr Assignment::simplify(AST *ast) const {
  Expr_ptr variable_simplified = variable->simplify(ast);
  Expr_ptr value_simplified = value->simplify(ast);

  if (value_simplified->get_kind() == CAST) {
    Cast *cast = static_cast<Cast *>(value_simplified.get());
    value_simplified = cast->get_expression()->clone();
  }

  return Assignment::build(variable_simplified, value_simplified);
}

Type_ptr type_from_size(uint64_t size, bool is_signed) {
  return type_from_size(size, is_signed, false);
}

Type_ptr type_from_size(uint64_t size, bool is_signed, bool force_byte_array) {
  Type_ptr type;

  if (force_byte_array) {
    if (size % 8 != 0) {
      assert(false && "Size not a byte multiple");
    }

    Type_ptr byte = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
    type = Array::build(byte, size / 8);

    return type;
  }

  switch (size) {
  case 1:
    type = PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL);
    break;
  case 8:
    type = is_signed
               ? PrimitiveType::build(PrimitiveType::PrimitiveKind::INT8_T)
               : PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
    break;
  case 16:
    type = is_signed
               ? PrimitiveType::build(PrimitiveType::PrimitiveKind::INT16_T)
               : PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T);
    break;
  case 32:
    type = is_signed
               ? PrimitiveType::build(PrimitiveType::PrimitiveKind::INT32_T)
               : PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
    break;
  case 64:
    type = is_signed
               ? PrimitiveType::build(PrimitiveType::PrimitiveKind::INT64_T)
               : PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T);
    break;
  default:
    if (size % 8 != 0) {
      assert(false && "Size not a byte multiple");
    }

    Type_ptr byte = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
    type = Array::build(byte, size / 8);
  }

  return type;
}
