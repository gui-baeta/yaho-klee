#pragma once

#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include "klee-util.h"
#include "load-call-paths.h"

enum TargetOption { SEQUENTIAL, SHARED_NOTHING, LOCKS, TM, BDD_NODE_HIT_RATE };

class AST;

class Node {
public:
  enum NodeKind {
    COMMENT,
    SIGNED_EXPRESSION,
    TYPE,
    EXPRESSION_TYPE,
    CAST,
    IMPORT,
    BLOCK,
    BRANCH,
    RETURN,
    FUNCTION_CALL,
    FUNCTION_ARG_DECL,
    VARIABLE_DECL,
    VARIABLE,
    FUNCTION,
    ASSIGNMENT,
    SELECT,
    ADDRESSOF,
    NOT,
    EQUALS,
    NOT_EQUALS,
    LESS,
    LESS_EQ,
    GREATER,
    GREATER_EQ,
    ADD,
    SUB,
    MUL,
    DIV,
    AND,
    LOGICAL_AND,
    OR,
    XOR,
    MOD,
    SHIFT_LEFT,
    SHIFT_RIGHT,
    READ,
    CONCAT,
    CONSTANT,
    MACRO_CALL
  };

protected:
  NodeKind kind;

  Node(NodeKind _kind) : kind(_kind) {}

  void indent(std::ostream &ofs, unsigned int lvl = 0) const {
    while (lvl != 0) {
      ofs << " ";
      lvl--;
    }
  }

public:
  NodeKind get_kind() const { return kind; }

  virtual void synthesize(std::ostream &ofs, unsigned int lvl = 0) const = 0;
  virtual void debug(std::ostream &ofs, unsigned int lvl = 0) const = 0;
};

typedef std::shared_ptr<Node> Node_ptr;

class ExpressionType {
public:
  enum ExpressionTypeKind { EXPRESSION_KIND, TYPE_KIND };

private:
  ExpressionTypeKind expression_type_kind;

protected:
  ExpressionType(ExpressionTypeKind _kind) : expression_type_kind(_kind) {}

public:
  ExpressionTypeKind get_expression_type_kind() const {
    return expression_type_kind;
  }
};

typedef std::shared_ptr<ExpressionType> ExpressionType_ptr;

class Type : public Node, public ExpressionType {
public:
  enum TypeKind { PRIMITIVE, STRUCT, ARRAY, POINTER };

protected:
  TypeKind type_kind;
  unsigned int size;

  Type(TypeKind _type_kind, unsigned int _size)
      : Node(TYPE), ExpressionType(TYPE_KIND), type_kind(_type_kind),
        size(_size) {}

public:
  unsigned int get_size() const { return size; }
  TypeKind get_type_kind() const { return type_kind; }

  virtual const std::string &get_name() const = 0;
  virtual std::shared_ptr<Type> clone() const = 0;
};

typedef std::shared_ptr<Type> Type_ptr;

class Comment : public Node {
private:
  std::string comment;

  Comment(const std::string &_comment) : Node(COMMENT), comment(_comment) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "// " << comment;
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<!-- " << comment << " -->"
        << "\n";
  }

  static std::shared_ptr<Comment> build(const std::string &_comment) {
    Comment *c = new Comment(_comment);
    return std::shared_ptr<Comment>(c);
  }
};

typedef std::shared_ptr<Comment> Comment_ptr;

class Expression : public Node, public ExpressionType {
protected:
  bool terminate_line;
  bool wrap;
  Type_ptr type;

  Expression(NodeKind kind, std::shared_ptr<Expression> _expr1,
             std::shared_ptr<Expression> _expr2)
      : Node(kind), ExpressionType(EXPRESSION_KIND), terminate_line(false),
        wrap(false) {
    Type_ptr type1 = _expr1->get_type();
    Type_ptr type2 = _expr2->get_type();

    if (type1->get_size() >= type2->get_size()) {
      type = type1->clone();
    } else {
      type = type2->clone();
    }
  }

  Expression(NodeKind kind, Type_ptr _type)
      : Node(kind), ExpressionType(EXPRESSION_KIND), terminate_line(false),
        wrap(false), type(_type->clone()) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    if (wrap) {
      ofs << "(";
    }

    synthesize_expr(ofs, lvl);

    if (wrap) {
      ofs << ")";
    }

    if (terminate_line) {
      ofs << ";";
    }
  }

  Type_ptr get_type() const { return type; }

  virtual void synthesize_expr(std::ostream &ofs,
                               unsigned int lvl = 0) const = 0;
  virtual std::shared_ptr<Expression> simplify(AST *ast) const = 0;
  virtual std::shared_ptr<Expression> clone() const = 0;

  void set_terminate_line(bool terminate) { terminate_line = terminate; }

  virtual void set_wrap(bool _wrap) { wrap = _wrap; }
};

typedef std::shared_ptr<Expression> Expr_ptr;

class SignedExpression : public Expression {
private:
  Expr_ptr expr;

  SignedExpression(Expr_ptr _expr)
      : Expression(SIGNED_EXPRESSION, _expr->get_type()), expr(_expr) {}

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    expr->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    ofs << "<signed-expression>"
        << "\n";

    expr->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</cast>"
        << "\n";
  }

  Expr_ptr get_expression() const { return expr; }

  Expr_ptr simplify(AST *ast) const override;

  void set_wrap(bool _wrap) override { expr->set_wrap(_wrap); }

  Expr_ptr clone() const override {
    Expression *signed_expr = new SignedExpression(expr);
    return Expr_ptr(signed_expr);
  }

  static std::shared_ptr<SignedExpression> build(Expr_ptr _expr) {
    SignedExpression *signed_expr = new SignedExpression(_expr);
    return std::shared_ptr<SignedExpression>(signed_expr);
  }
};

typedef std::shared_ptr<SignedExpression> SignedExpr_ptr;

class PrimitiveType : public Type {
public:
  enum PrimitiveKind {
    VOID,
    BOOL,
    UINT8_T,
    INT8_T,
    UINT16_T,
    INT16_T,
    INT,
    UINT32_T,
    INT32_T,
    UINT64_T,
    INT64_T
  };

private:
  PrimitiveKind primitive_kind;
  std::string name;

  PrimitiveType(PrimitiveKind _primitive_kind, unsigned int _size)
      : Type(PRIMITIVE, _size), primitive_kind(_primitive_kind) {

    switch (primitive_kind) {
    case VOID:
      name = "void";
      break;
    case BOOL:
      name = "bool";
      break;
    case UINT8_T:
      name = "uint8_t";
      break;
    case INT8_T:
      name = "int8_t";
      break;
    case UINT16_T:
      name = "uint16_t";
      break;
    case INT16_T:
      name = "int16_t";
      break;
    case INT:
      name = "int";
      break;
    case UINT32_T:
      name = "uint32_t";
      break;
    case INT32_T:
      name = "int32_t";
      break;
    case UINT64_T:
      name = "uint64_t";
      break;
    case INT64_T:
      name = "int64_t";
      break;
    }
  }

public:
  PrimitiveKind get_primitive_kind() const { return primitive_kind; }

  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << name;
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << name;
  }

  const std::string &get_name() const override { return name; }

  void sign() {
    switch (primitive_kind) {
    case UINT8_T:
      primitive_kind = INT8_T;
      break;
    case UINT16_T:
      primitive_kind = INT16_T;
      break;
    case UINT32_T:
      primitive_kind = INT32_T;
      break;
    case UINT64_T:
      primitive_kind = INT64_T;
      break;
    default:
      break;
    }
  }

  std::shared_ptr<Type> clone() const override {
    Type *nt = new PrimitiveType(primitive_kind, size);
    return std::shared_ptr<Type>(nt);
  }

  static std::shared_ptr<PrimitiveType> build(PrimitiveKind _kind) {
    PrimitiveType *nt = nullptr;

    switch (_kind) {
    case VOID:
      nt = new PrimitiveType(_kind, 0);
      break;
    case BOOL:
      nt = new PrimitiveType(_kind, 1);
      break;
    case UINT8_T:
    case INT8_T:
      nt = new PrimitiveType(_kind, 8);
      break;
    case UINT16_T:
    case INT16_T:
      nt = new PrimitiveType(_kind, 16);
      break;
    case INT:
    case UINT32_T:
    case INT32_T:
      nt = new PrimitiveType(_kind, 32);
      break;
    case UINT64_T:
    case INT64_T:
      nt = new PrimitiveType(_kind, 64);
      break;
    }

    return std::shared_ptr<PrimitiveType>(nt);
  }
};

typedef std::shared_ptr<PrimitiveType> PrimitiveType_ptr;

class Array : public Type {
private:
  unsigned int n_elems;
  Type_ptr type;

  Array(Type_ptr _type, unsigned int _n_elems)
      : Type(ARRAY, _type->get_size() * _n_elems), n_elems(_n_elems),
        type(_type) {}

public:
  const std::string &get_name() const override { return type->get_name(); }
  Type_ptr get_elem_type() const { return type; }
  unsigned int get_n_elems() const { return n_elems; }

  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << type->get_name();
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << type->get_name();
    ofs << "*";
    ofs << n_elems;
  }

  std::shared_ptr<Type> clone() const override {
    Type *t = new Array(type, n_elems);
    return std::shared_ptr<Type>(t);
  }

  static std::shared_ptr<Array> build(Type_ptr _type, unsigned int _n_elems) {
    Array *a = new Array(_type, _n_elems);
    return std::shared_ptr<Array>(a);
  }
};

typedef std::shared_ptr<Array> Array_ptr;

class Pointer : public Type {
private:
  Type_ptr type;

  Pointer(Type_ptr _type)
      : Type(POINTER, _type->get_size()), type(_type->clone()) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    type->synthesize(ofs, lvl);
    ofs << "*";
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    type->debug(ofs, lvl);
    ofs << "*";
  }

  Type_ptr get_type() const { return type; }

  const std::string &get_name() const override { return type->get_name(); }

  std::shared_ptr<Type> clone() const override {
    Type *ptr = new Pointer(type);
    return std::shared_ptr<Type>(ptr);
  }

  static std::shared_ptr<Pointer> build(Type_ptr _type) {
    Pointer *ptr = new Pointer(_type);
    return std::shared_ptr<Pointer>(ptr);
  }
};

typedef std::shared_ptr<Pointer> Pointer_ptr;

class Cast : public Expression {
private:
  Expr_ptr expr;

  Cast(Expr_ptr _expr, Type_ptr _type) : Expression(CAST, _type), expr(_expr) {}

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    if (type->get_type_kind() == Type::TypeKind::POINTER) {
      ofs << "(";
      type->synthesize(ofs, 0);
      ofs << ")";

      ofs << "(";
    }

    expr->synthesize(ofs, lvl);

    if (type->get_type_kind() == Type::TypeKind::POINTER) {
      ofs << ")";
    }
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    ofs << "<cast";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    expr->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</cast>"
        << "\n";
  }

  Expr_ptr get_expression() const { return expr; }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *cast = new Cast(expr, type);
    return Expr_ptr(cast);
  }

  static std::shared_ptr<Cast> build(Expr_ptr _expr, Type_ptr _type) {
    Cast *cast = new Cast(_expr, _type);
    return std::shared_ptr<Cast>(cast);
  }

  static std::shared_ptr<Cast> build(Expr_ptr _expr, bool _signed) {
    Type_ptr _type = _expr->get_type();
    Type_ptr _new_type;

    switch (_type->get_type_kind()) {
    case Type::TypeKind::PRIMITIVE: {
      PrimitiveType *primitive = static_cast<PrimitiveType *>(_type.get());

      switch (primitive->get_primitive_kind()) {
      case PrimitiveType::PrimitiveKind::VOID:
        assert(false && "Flipping sign of a void type");
        break;
      case PrimitiveType::PrimitiveKind::BOOL:
        assert(false && "Flipping sign of a bool type");
        break;
      case PrimitiveType::PrimitiveKind::UINT8_T:
        _new_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT8_T);
        break;
      case PrimitiveType::PrimitiveKind::UINT16_T:
        _new_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT16_T);
        break;
      case PrimitiveType::PrimitiveKind::UINT32_T:
        _new_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT32_T);
        break;
      case PrimitiveType::PrimitiveKind::UINT64_T:
        _new_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT64_T);
        break;
      case PrimitiveType::PrimitiveKind::INT:
      case PrimitiveType::PrimitiveKind::INT8_T:
      case PrimitiveType::PrimitiveKind::INT16_T:
      case PrimitiveType::PrimitiveKind::INT32_T:
      case PrimitiveType::PrimitiveKind::INT64_T:
        _new_type = _type;
        break;
      }
    } break;
    case Type::TypeKind::STRUCT:
    case Type::TypeKind::POINTER:
    case Type::TypeKind::ARRAY:
      _expr->debug(std::cerr);
      std::cerr << "\n";
      assert(false && "Not implemented");
      break;

    default:
      assert(false && "Not a type kind");
      break;
    }

    Cast *cast = new Cast(_expr, _new_type);
    return std::shared_ptr<Cast>(cast);
  }
};

typedef std::shared_ptr<Cast> Cast_ptr;

class Import : public Node {
private:
  std::string path;
  bool relative;

  Import(const std::string &_path, bool _relative)
      : Node(IMPORT), path(_path), relative(_relative) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << "#include ";

    ofs << (relative ? "\"" : "<");
    ofs << path;
    ofs << (relative ? "\"" : ">");
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << "<include";
    ofs << " relative=" << relative;
    ofs << " path=" << path;
    ofs << " />"
        << "\n";
  }

  static std::shared_ptr<Import> build(const std::string &_path,
                                       bool _relative) {
    Import *import = new Import(_path, _relative);
    return std::shared_ptr<Import>(import);
  }
};

typedef std::shared_ptr<Import> Import_ptr;

class Block : public Node {
private:
  std::vector<Node_ptr> nodes;
  bool enclose;

  Block(const std::vector<Node_ptr> &_nodes, bool _enclose)
      : Node(BLOCK), enclose(_enclose) {
    for (auto node : _nodes) {
      nodes.push_back(node);
    }
  }

  Block(const std::vector<Expr_ptr> &_exprs, bool _enclose)
      : Node(BLOCK), enclose(_enclose) {

    for (auto expr : _exprs) {
      nodes.push_back(expr);
    }
  }

  Block(Block *block)
      : Node(BLOCK), nodes(block->nodes), enclose(block->enclose) {}

  Block(Block *block, bool _enclose)
      : Node(BLOCK), nodes(block->nodes), enclose(_enclose) {}

public:
  void preppend(Node_ptr node) { nodes.insert(nodes.begin(), node); }

  void set_enclose(bool _enclose) { enclose = _enclose; }

  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    if (enclose) {
      ofs << "{";
      ofs << "\n";
      for (const auto &node : nodes) {
        node->synthesize(ofs, lvl + 2);
        ofs << "\n";
      }

      indent(ofs, lvl);
      ofs << "}";
    } else {
      for (unsigned int i = 0; i < nodes.size(); i++) {
        nodes[i]->synthesize(ofs, lvl);

        if (i < nodes.size() - 1)
          ofs << "\n";
      }
    }
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<block>"
        << "\n";

    for (const auto &node : nodes) {
      node->debug(ofs, lvl + 2);
    }

    indent(ofs, lvl);
    ofs << "</block>"
        << "\n";
  }

  static std::shared_ptr<Block> build(const std::vector<Node_ptr> _nodes) {
    Block *block = new Block(_nodes, true);
    return std::shared_ptr<Block>(block);
  }

  static std::shared_ptr<Block> build(const std::vector<Node_ptr> _nodes,
                                      bool _enclose) {
    Block *block = new Block(_nodes, _enclose);
    return std::shared_ptr<Block>(block);
  }

  static std::shared_ptr<Block> build(const std::vector<Expr_ptr> _exprs,
                                      bool _enclose) {
    Block *block = new Block(_exprs, _enclose);
    return std::shared_ptr<Block>(block);
  }

  static std::shared_ptr<Block> build(Node_ptr block_node) {
    return build(block_node, false);
  }

  static std::shared_ptr<Block> build(Node_ptr block_node, bool _enclose) {
    assert(block_node->get_kind() == Node::NodeKind::BLOCK);
    Block *_block = static_cast<Block *>(block_node.get());
    Block *block = new Block(_block, _enclose);
    return std::shared_ptr<Block>(block);
  }
};

typedef std::shared_ptr<Block> Block_ptr;

class Constant : public Expression {
private:
  std::vector<uint64_t> values;
  bool hex;

  Constant(PrimitiveType::PrimitiveKind _kind, uint64_t _value, bool _hex)
      : Expression(CONSTANT, PrimitiveType::build(_kind)), hex(_hex) {
    values.push_back(_value);
  }

  Constant(Type_ptr type) : Expression(CONSTANT, type), hex(false) {
    switch (type->get_type_kind()) {
    case Type::TypeKind::ARRAY: {
      Array *arr = static_cast<Array *>(type.get());
      values = std::vector<uint64_t>(arr->get_n_elems());
      break;
    }
    case Type::TypeKind::POINTER:
    case Type::TypeKind::PRIMITIVE:
      values = std::vector<uint64_t>(1);
      break;
    case Type::TypeKind::STRUCT:
      assert(false && "Not implemented");
    };
  }

  Constant(Type_ptr type, std::vector<uint64_t> _values, bool _hex)
      : Expression(CONSTANT, type), values(_values), hex(_hex) {}

  void parse_value(std::ostream &ofs) const {
    assert(values.size());

    if (hex) {
      ofs << std::hex;
    }

    switch (type->get_type_kind()) {
    case Type::TypeKind::ARRAY: {
      ofs << "{ ";
      for (unsigned int idx = 0; idx < values.size(); idx++) {
        auto value = values[idx];

        if (hex)
          ofs << "0x";
        ofs << value;

        if (idx < values.size() - 1) {
          ofs << ", ";
        }
      }
      ofs << " }";
      break;
    }
    case Type::TypeKind::POINTER:
      assert(false && "Not implemented");
    case Type::TypeKind::PRIMITIVE: {
      PrimitiveType *primitive = static_cast<PrimitiveType *>(type.get());

      if (hex && primitive->get_primitive_kind() !=
                     PrimitiveType::PrimitiveKind::BOOL) {
        ofs << "0x";
      }

      switch (primitive->get_primitive_kind()) {
      case PrimitiveType::PrimitiveKind::VOID:
        assert(false);
      case PrimitiveType::PrimitiveKind::BOOL:
        if (values[0] == 0)
          ofs << "false";
        else
          ofs << "true";
        break;
      case PrimitiveType::PrimitiveKind::INT8_T:
        ofs << ((int8_t)values[0]);
        break;
      case PrimitiveType::PrimitiveKind::UINT8_T:
        ofs << (/* (uint8_t) */ values[0]);
        if (!hex)
          ofs << "u";
        break;
      case PrimitiveType::PrimitiveKind::INT16_T:
        ofs << ((int16_t)values[0]);
        break;
      case PrimitiveType::PrimitiveKind::UINT16_T:
        ofs << ((uint16_t)values[0]);
        if (!hex)
          ofs << "u";
        break;
      case PrimitiveType::PrimitiveKind::INT32_T:
      case PrimitiveType::PrimitiveKind::INT:
        ofs << ((int)values[0]);
        break;
      case PrimitiveType::PrimitiveKind::UINT32_T:
        ofs << ((uint32_t)values[0]);
        if (!hex)
          ofs << "u";
        break;
      case PrimitiveType::PrimitiveKind::INT64_T:
        ofs << ((int64_t)values[0]);
        break;
      case PrimitiveType::PrimitiveKind::UINT64_T:
        ofs << ((uint64_t)values[0]);
        if (!hex)
          ofs << "ul";
        break;
      }
      break;
    }
    case Type::TypeKind::STRUCT:
      assert(false && "Not implemented");
    };

    ofs << std::dec;
  }

public:
  uint64_t get_value() const {
    uint64_t value = 0;

    assert(values.size());
    switch (type->get_type_kind()) {
    case Type::TypeKind::ARRAY: {
      Array *array = static_cast<Array *>(type.get());
      auto elem_sz = array->get_elem_type()->get_size();
      assert(array->get_n_elems() == values.size());

      unsigned int shift = 0;
      for (const auto &stored : values) {
        value |= (stored << shift);
        shift += elem_sz;
      }
      break;
    }
    case Type::TypeKind::PRIMITIVE: {
      value = values[0];
      break;
    }
    case Type::TypeKind::POINTER:
    case Type::TypeKind::STRUCT:
      assert(false && "Not implemented");
    };

    return value;
  }

  uint64_t get_value(unsigned int idx) const {
    assert(values.size() > idx);
    return values[idx];
  }

  void set_value(uint64_t value) {
    assert(values.size());

    switch (type->get_type_kind()) {
    case Type::TypeKind::ARRAY: {
      Array *array = static_cast<Array *>(type.get());
      auto elem_sz = array->get_elem_type()->get_size();
      auto n_elems = array->get_n_elems();

      uint64_t mask = 0;
      for (unsigned int i = 0; i < elem_sz; i++) {
        mask = mask << 1;
        mask |= 1;
      }

      for (unsigned int i = 0; i < n_elems; i++) {
        values[i] = value & mask;
        value = value >> elem_sz;
      }

      break;
    }
    case Type::TypeKind::PRIMITIVE: {
      values[0] = value;
      break;
    }
    case Type::TypeKind::POINTER:
    case Type::TypeKind::STRUCT:
      assert(false && "Not implemented");
    };
  }

  void set_value(uint64_t value, unsigned int idx) {
    assert(values.size() > idx);
    values[idx] = value;
  }

  void sign() {
    if (type->get_type_kind() != Type::TypeKind::PRIMITIVE) {
      return;
    }

    PrimitiveType *primitive = static_cast<PrimitiveType *>(type.get());
    primitive->sign();
  }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    parse_value(ofs);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<literal";
    ofs << " type=";
    type->debug(ofs);
    ofs << " value=";
    parse_value(ofs);
    ofs << " />"
        << "\n";
  }

  void set_wrap(bool _wrap) override {
    // ignore
  }

  Expr_ptr simplify(AST *ast) const override { return clone(); }

  Expr_ptr clone() const override {
    Expression *e = new Constant(type, values, hex);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Constant> build(Type_ptr _type) {
    Constant *literal = new Constant(_type);
    return std::shared_ptr<Constant>(literal);
  }

  static std::shared_ptr<Constant> build(PrimitiveType::PrimitiveKind _kind,
                                         uint64_t _value, bool _hex) {
    Constant *literal = new Constant(_kind, _value, _hex);
    return std::shared_ptr<Constant>(literal);
  }

  static std::shared_ptr<Constant> build(PrimitiveType::PrimitiveKind _kind,
                                         uint64_t _value) {
    Constant *literal = new Constant(_kind, _value, false);
    return std::shared_ptr<Constant>(literal);
  }
};

typedef std::shared_ptr<Constant> Constant_ptr;

class NotEquals : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  NotEquals(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(NOT_EQUALS,
                   PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL)),
        lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " != ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<not-equals>"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</not-equals>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new NotEquals(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<NotEquals> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    NotEquals *nequals = new NotEquals(_lhs, _rhs);
    return std::shared_ptr<NotEquals>(nequals);
  }
};

typedef std::shared_ptr<NotEquals> NotEquals_ptr;

class Equals : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Equals(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(EQUALS,
                   PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL)),
        lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " == ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<equals>"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</equals>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Equals(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Equals> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Equals *equals = new Equals(_lhs, _rhs);
    return std::shared_ptr<Equals>(equals);
  }
};

typedef std::shared_ptr<Equals> Equals_ptr;

class Not : public Expression {
private:
  Expr_ptr expr;

  Not(Expr_ptr _expr)
      : Expression(NOT, _expr->get_type()), expr(_expr->clone()) {
    expr->set_wrap(true);
  }

public:
  Expr_ptr get_expr() const { return expr; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << "!";
    expr->synthesize(ofs);
    ofs << "";
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<not";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    expr->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</not>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Not(expr);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Not> build(Expr_ptr _expr) {
    Not *n = new Not(_expr);
    return std::shared_ptr<Not>(n);
  }
};

typedef std::shared_ptr<Not> Not_ptr;

class Branch : public Node {
private:
  Expr_ptr condition;
  Node_ptr on_true;
  Node_ptr on_false;

  std::vector<Comment_ptr> on_true_cps;
  std::vector<Comment_ptr> on_false_cps;

  Comment_ptr on_false_comment;

  Branch(Expr_ptr _condition, Node_ptr _on_true, Node_ptr _on_false)
      : Node(BRANCH), condition(_condition), on_true(_on_true),
        on_false(_on_false) {
    condition->set_terminate_line(false);
    condition->set_wrap(false);

    Expr_ptr not_condition = Not::build(condition);
    not_condition->set_wrap(false);

    std::stringstream msg_stream;
    not_condition->synthesize(msg_stream);
    on_false_comment = Comment::build(msg_stream.str());
  }

  Branch(Expr_ptr _condition, Node_ptr _on_true)
      : Node(BRANCH), condition(_condition), on_true(_on_true) {
    condition->set_terminate_line(false);
    condition->set_wrap(false);
  }

  Branch(Expr_ptr _condition, Node_ptr _on_true, Node_ptr _on_false,
         const std::vector<uint64_t> &_on_true_term_ids,
         const std::vector<uint64_t> &_on_false_term_ids)
      : Branch(_condition, _on_true, _on_false) {

    for (auto term_id : _on_true_term_ids) {
      on_true_cps.push_back(Comment::build(std::to_string(term_id)));
    }

    for (auto term_id : _on_false_term_ids) {
      on_false_cps.push_back(Comment::build(std::to_string(term_id)));
    }
  }

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    for (auto c : on_true_cps) {
      ofs << "\n";
      indent(ofs, lvl);
      c->synthesize(ofs);
    }

    ofs << "\n";

    indent(ofs, lvl);

    ofs << "if (";
    condition->synthesize(ofs);
    ofs << ") ";

    if (on_true->get_kind() == Node::NodeKind::BLOCK) {
      on_true->synthesize(ofs, lvl);
    } else {
      ofs << "{"
          << "\n";
      on_true->synthesize(ofs, lvl + 2);
      ofs << "\n";
      indent(ofs, lvl);
      ofs << "}";
    }

    ofs << "\n";

    if (on_false.get() == nullptr) {
      return;
    }

    ofs << "\n";

    for (auto c : on_false_cps) {
      indent(ofs, lvl);
      c->synthesize(ofs);
      ofs << "\n";
    }

    indent(ofs, lvl);
    ofs << "else ";

    if (on_false->get_kind() == Node::NodeKind::BLOCK) {
      on_false->synthesize(ofs, lvl);
    } else {
      ofs << "{"
          << "\n";
      on_false->synthesize(ofs, lvl + 2);
      ofs << "\n";
      indent(ofs, lvl);
      ofs << "}";
    }

    ofs << " ";
    on_false_comment->synthesize(ofs);
    ofs << "\n";
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<if>"
        << "\n";

    condition->debug(ofs, lvl + 2);
    on_true->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</if>"
        << "\n";

    if (on_false.get() == nullptr) {
      return;
    }

    indent(ofs, lvl);
    ofs << "<else>"
        << "\n";

    on_false->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</else>"
        << "\n";
  }

  static std::shared_ptr<Branch> build(Expr_ptr _condition, Node_ptr _on_true,
                                       Node_ptr _on_false) {
    Branch *branch = new Branch(_condition, _on_true, _on_false);
    return std::shared_ptr<Branch>(branch);
  }

  static std::shared_ptr<Branch> build(Expr_ptr _condition, Node_ptr _on_true) {
    Branch *branch = new Branch(_condition, _on_true);
    return std::shared_ptr<Branch>(branch);
  }

  static std::shared_ptr<Branch>
  build(Expr_ptr _condition, Node_ptr _on_true, Node_ptr _on_false,
        const std::vector<uint64_t> &_on_true_term_ids,
        const std::vector<uint64_t> &_on_false_term_ids) {
    Branch *branch = new Branch(_condition, _on_true, _on_false,
                                _on_true_term_ids, _on_false_term_ids);
    return std::shared_ptr<Branch>(branch);
  }
};

typedef std::shared_ptr<Branch> Branch_ptr;

class Return : public Node {
private:
  Expr_ptr value;

  Return(Expr_ptr _value) : Node(RETURN), value(_value) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    ofs << "return ";
    value->synthesize(ofs);
    ofs << ";";
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<return>"
        << "\n";
    value->debug(ofs, lvl + 2);
    indent(ofs, lvl);
    ofs << "</return>"
        << "\n";
  }

  static std::shared_ptr<Return> build(Expr_ptr _value) {
    Return *_return = new Return(_value);
    return std::shared_ptr<Return>(_return);
  }
};

typedef std::shared_ptr<Return> Return_ptr;

class FunctionCall : public Expression {
private:
  std::string name;
  std::vector<ExpressionType_ptr> args;

  FunctionCall(const std::string &_name,
               const std::vector<ExpressionType_ptr> &_args, Type_ptr _ret)
      : Expression(FUNCTION_CALL, _ret), name(_name), args(_args) {}

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << name;
    ofs << "(";

    for (unsigned int i = 0; i < args.size(); i++) {
      const auto &arg = args[i];

      if (arg->get_expression_type_kind() == EXPRESSION_KIND) {
        Expression *expr = static_cast<Expression *>(arg.get());
        expr->synthesize(ofs);
      } else {
        Type *type = static_cast<Type *>(arg.get());
        type->synthesize(ofs);
      }

      if (i < args.size() - 1) {
        ofs << ", ";
      }
    }

    ofs << ")";
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<call";
    ofs << " name=" << name;
    ofs << " return=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    for (const auto &arg : args) {
      if (arg->get_expression_type_kind() == EXPRESSION_KIND) {
        Expression *expr = static_cast<Expression *>(arg.get());
        expr->debug(ofs, lvl + 2);
      } else {
        Type *type = static_cast<Type *>(arg.get());
        type->debug(ofs, lvl + 2);
      }
    }

    indent(ofs, lvl);
    ofs << "</call>"
        << "\n";
  }

  void set_wrap(bool _wrap) override {
    // ignore
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new FunctionCall(name, args, type);
    return Expr_ptr(e);
  }

  static std::shared_ptr<FunctionCall>
  build(const std::string &_name, const std::vector<ExpressionType_ptr> &_args,
        Type_ptr _ret) {
    FunctionCall *function_call = new FunctionCall(_name, _args, _ret);
    return std::shared_ptr<FunctionCall>(function_call);
  }
};

typedef std::shared_ptr<FunctionCall> FunctionCall_ptr;

class Greater : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Greater(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(GREATER,
                   PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL)),
        lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " > ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<greater-than>"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</greater-than>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Greater(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Greater> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Greater *g = new Greater(_lhs, _rhs);
    return std::shared_ptr<Greater>(g);
  }
};

typedef std::shared_ptr<Greater> Greater_ptr;

class GreaterEq : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  GreaterEq(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(GREATER_EQ,
                   PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL)),
        lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " >= ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<greater-eq>"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</greater-eq>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new GreaterEq(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<GreaterEq> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    GreaterEq *ge = new GreaterEq(_lhs, _rhs);
    return std::shared_ptr<GreaterEq>(ge);
  }
};

typedef std::shared_ptr<GreaterEq> GreaterEq_ptr;

class Less : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Less(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(LESS,
                   PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL)),
        lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " < ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<less>"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</less>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Less(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Less> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Less *l = new Less(_lhs, _rhs);
    return std::shared_ptr<Less>(l);
  }
};

typedef std::shared_ptr<Less> Less_ptr;

class LessEq : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  LessEq(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(LESS_EQ,
                   PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL)),
        lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " <= ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<less-eq>"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</less-eq>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new LessEq(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<LessEq> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    LessEq *le = new LessEq(_lhs, _rhs);
    return std::shared_ptr<LessEq>(le);
  }
};

typedef std::shared_ptr<LessEq> LessEq_ptr;

class Add : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Add(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(ADD, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);

    if (rhs->get_kind() == NodeKind::SIGNED_EXPRESSION &&
        lhs->get_kind() == NodeKind::CONSTANT) {
      Constant *lhs_const = static_cast<Constant *>(lhs.get());
      SignedExpression *rhs_signed_expr =
          static_cast<SignedExpression *>(rhs.get());

      lhs_const->sign();
      rhs = rhs_signed_expr->get_expression();
    }
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " + ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<add";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</add>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Add(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Add> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Add *add = new Add(_lhs, _rhs);
    return std::shared_ptr<Add>(add);
  }
};

typedef std::shared_ptr<Add> Add_ptr;

class Sub : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Sub(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(SUB, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " - ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<sub";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</sub>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Sub(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Sub> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Sub *sub = new Sub(_lhs, _rhs);
    return std::shared_ptr<Sub>(sub);
  }
};

typedef std::shared_ptr<Sub> Sub_ptr;

class Mul : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Mul(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(MUL, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " * ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<mul";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</mul>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Mul(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Mul> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Mul *mul = new Mul(_lhs, _rhs);
    return std::shared_ptr<Mul>(mul);
  }
};

typedef std::shared_ptr<Mul> Mul_ptr;

class Div : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Div(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(DIV, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " / ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<div";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</div>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Div(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Div> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Div *div = new Div(_lhs, _rhs);
    return std::shared_ptr<Div>(div);
  }
};

typedef std::shared_ptr<Div> Div_ptr;

class And : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  And(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(AND, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " & ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<bitwise-and";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</bitwise-and>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new And(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<And> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    And *_and = new And(_lhs, _rhs);
    return std::shared_ptr<And>(_and);
  }
};

typedef std::shared_ptr<And> And_ptr;

class LogicalAnd : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  LogicalAnd(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(LOGICAL_AND, _lhs, _rhs), lhs(_lhs->clone()),
        rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " && ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<logical-and";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</logical-and>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new LogicalAnd(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<LogicalAnd> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    LogicalAnd *_and = new LogicalAnd(_lhs, _rhs);
    return std::shared_ptr<LogicalAnd>(_and);
  }
};

typedef std::shared_ptr<LogicalAnd> LogicalAnd_ptr;

class Or : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Or(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(OR, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " | ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<bitwise-or";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</bitwise-or>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Or(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Or> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Or *_or = new Or(_lhs, _rhs);
    return std::shared_ptr<Or>(_or);
  }
};

typedef std::shared_ptr<Or> Or_ptr;

class Xor : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Xor(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(XOR, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " ^ ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<xor";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</xor>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Xor(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Xor> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Xor *_xor = new Xor(_lhs, _rhs);
    return std::shared_ptr<Xor>(_xor);
  }
};

typedef std::shared_ptr<Xor> Xor_ptr;

class Mod : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Mod(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(MOD, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " % ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<mod";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</mod>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Mod(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Mod> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Mod *mod = new Mod(_lhs, _rhs);
    return std::shared_ptr<Mod>(mod);
  }
};

typedef std::shared_ptr<Mod> Mod_ptr;

class ShiftLeft : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  ShiftLeft(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(SHIFT_LEFT, _lhs->get_type()), lhs(_lhs->clone()),
        rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " << ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<shift-left";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</shift-left>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new ShiftLeft(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<ShiftLeft> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    ShiftLeft *sl = new ShiftLeft(_lhs, _rhs);
    return std::shared_ptr<ShiftLeft>(sl);
  }
};

typedef std::shared_ptr<ShiftLeft> ShiftLeft_ptr;

class ShiftRight : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  ShiftRight(Expr_ptr _lhs, Expr_ptr _rhs)
      : Expression(SHIFT_RIGHT, _lhs->get_type()), lhs(_lhs->clone()),
        rhs(_rhs->clone()) {
    lhs->set_wrap(true);
    rhs->set_wrap(true);
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " >> ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<shift-right";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    lhs->debug(ofs, lvl + 2);
    rhs->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</shift-right>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new ShiftRight(lhs, rhs);
    return Expr_ptr(e);
  }

  static std::shared_ptr<ShiftRight> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    ShiftRight *sr = new ShiftRight(_lhs, _rhs);
    return std::shared_ptr<ShiftRight>(sr);
  }
};

typedef std::shared_ptr<ShiftRight> ShiftRight_ptr;

class Variable : public Expression {
private:
  std::string symbol;
  unsigned int addr;

  Variable(std::string _symbol, Type_ptr _type)
      : Expression(VARIABLE, _type), symbol(_symbol), addr(0) {}

  Variable(std::string _symbol, Type_ptr _type, unsigned int _addr)
      : Expression(VARIABLE, _type), symbol(_symbol), addr(_addr) {}

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << symbol;
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    ofs << "<var";
    ofs << " symbol=";
    ofs << symbol;
    ofs << " type=";
    type->debug(ofs);
    ofs << " addr=";
    ofs << addr;
    ofs << " />"
        << "\n";
  }

  const std::string &get_symbol() const { return symbol; }
  unsigned int get_addr() const { return addr; }

  void set_addr(unsigned int _addr) {
    assert(addr == 0 && "Double allocation");
    addr = _addr;
  }

  void change_symbol(const std::string &new_symbol) { symbol = new_symbol; }

  Expr_ptr simplify(AST *ast) const override { return clone(); }

  void set_wrap(bool _wrap) override {
    // ignore
  }

  Expr_ptr clone() const override {
    Expression *e = new Variable(symbol, type, addr);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Variable> build(const std::string &_symbol,
                                         Type_ptr _type) {
    Variable *variable = new Variable(_symbol, _type);
    return std::shared_ptr<Variable>(variable);
  }
};

typedef std::shared_ptr<Variable> Variable_ptr;

class Struct : public Type {
public:
private:
  std::string name;
  std::vector<Variable_ptr> fields;

  Struct(const std::string &_name, const std::vector<Variable_ptr> &_fields)
      : Type(STRUCT, std::accumulate(_fields.begin(), _fields.end(), 0,
                                     [&](int sum, Variable_ptr curr) {
                                       return sum +
                                              curr->get_type()->get_size();
                                     })),
        name(_name), fields(_fields) {}

  Struct(const std::string &_name) : Type(STRUCT, 0), name(_name) {}

public:
  const std::string &get_name() const override { return name; }
  const std::vector<Variable_ptr> &query_fields() const { return fields; }

  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << "struct ";
    ofs << get_name();
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << "struct ";
    ofs << get_name();
    ofs << " (";
    ofs << get_size();
    ofs << " bits)";
  }

  std::shared_ptr<Type> clone() const override {
    Type *t;

    if (fields.size()) {
      t = new Struct(name, fields);
    } else {
      t = new Struct(name);
    }

    return std::shared_ptr<Type>(t);
  }

  static std::shared_ptr<Struct>
  build(const std::string &_name, const std::vector<Variable_ptr> &_fields) {
    Struct *s = new Struct(_name, _fields);
    return std::shared_ptr<Struct>(s);
  }

  static std::shared_ptr<Struct> build(const std::string &_name) {
    Struct *s = new Struct(_name);
    return std::shared_ptr<Struct>(s);
  }
};

typedef std::shared_ptr<Struct> Struct_ptr;

class AddressOf : public Expression {
private:
  Expr_ptr expr;

  AddressOf(Expr_ptr _expr)
      : Expression(ADDRESSOF, PrimitiveType::build(
                                  PrimitiveType::PrimitiveKind::UINT32_T)) {
    expr = _expr->clone();
  }

public:
  Expr_ptr get_expr() const { return expr; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    ofs << "&";
    expr->synthesize(ofs, lvl);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<address_of>"
        << "\n";

    expr->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</address_of>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  void set_wrap(bool _wrap) override {
    // ignore
  }

  Expr_ptr clone() const override {
    Expression *e = new AddressOf(expr);
    return Expr_ptr(e);
  }

  static std::shared_ptr<AddressOf> build(Expr_ptr _expr) {
    AddressOf *address_of = new AddressOf(_expr);
    return std::shared_ptr<AddressOf>(address_of);
  }
};

typedef std::shared_ptr<AddressOf> AddressOf_ptr;

class Read : public Expression {
private:
  Expr_ptr expr;
  Expr_ptr idx;

  Read(Expr_ptr _expr, Type_ptr _type, Expr_ptr _idx)
      : Expression(READ, _type), expr(_expr->clone()), idx(_idx) {}

  void synthesize_pointer(std::ostream &ofs, unsigned int lvl) const {
    Variable *var;
    Type_ptr t;

    if (expr->get_kind() == Node::NodeKind::CAST) {
      Cast *cast = static_cast<Cast *>(expr.get());
      assert(cast->get_expression()->get_kind() == Node::NodeKind::VARIABLE);

      var = static_cast<Variable *>(cast->get_expression().get());
      t = cast->get_type();
    } else {
      assert(expr->get_kind() == Node::NodeKind::VARIABLE);
      var = static_cast<Variable *>(expr.get());
      t = var->get_type();
    }

    assert(t->get_type_kind() == Type::TypeKind::POINTER);

    if (expr->get_kind() == Node::NodeKind::CAST) {
      expr->set_wrap(true);
    }

    expr->synthesize(ofs);
    ofs << "[";
    idx->synthesize(ofs);
    ofs << "]";
  }

  void synthesize_array(std::ostream &ofs, unsigned int lvl) const {
    Variable *var;
    Type_ptr t;

    if (expr->get_kind() == Node::NodeKind::CAST) {
      Cast *cast = static_cast<Cast *>(expr.get());
      assert(cast->get_expression()->get_kind() == Node::NodeKind::VARIABLE);

      var = static_cast<Variable *>(cast->get_expression().get());
      t = cast->get_type();
    } else {
      assert(expr->get_kind() == Node::NodeKind::VARIABLE);
      var = static_cast<Variable *>(expr.get());
      t = var->get_type();
    }

    assert(t->get_type_kind() == Type::TypeKind::ARRAY);

    if (expr->get_kind() == Node::NodeKind::CAST) {
      expr->set_wrap(true);
    }

    expr->synthesize(ofs);
    ofs << "[";
    idx->synthesize(ofs);
    ofs << "]";
  }

  void synthesize_struct(std::ostream &ofs, unsigned int lvl,
                         bool open_parenthesis) const {
    assert(expr->get_kind() != Node::NodeKind::CAST && "Not implemented");

    Variable *var = static_cast<Variable *>(expr.get());
    Type_ptr t = var->get_type();
    bool is_ptr = false;

    if (t->get_type_kind() == Type::TypeKind::POINTER) {
      is_ptr = true;
      Pointer *ptr = static_cast<Pointer *>(var->get_type().get());
      t = ptr->get_type();
    }

    assert(t->get_type_kind() == Type::TypeKind::STRUCT);
    assert(idx->get_kind() == CONSTANT);

    Constant *idx_const = static_cast<Constant *>(idx.get());
    Struct *s = static_cast<Struct *>(t.get());

    unsigned int idx_val = idx_const->get_value();

    for (Variable_ptr field : s->query_fields()) {
      unsigned int field_size = field->get_type()->get_size();

      if (idx_val >= field_size / 8) {
        idx_val -= field_size / 8;
        continue;
      }

      PrimitiveType *idx_primitive_type =
          static_cast<PrimitiveType *>(idx_const->get_type().get());
      Expr_ptr new_idx_expr =
          Constant::build(idx_primitive_type->get_primitive_kind(), idx_val);

      std::shared_ptr<Read> field_read = Read::build(field, type, new_idx_expr);
      field_read->set_wrap(false);

      if (idx_val != 0 && open_parenthesis &&
          field->get_type()->get_type_kind() == Type::TypeKind::PRIMITIVE) {
        ofs << "(";
      }

      ofs << var->get_symbol();
      ofs << (is_ptr ? "->" : ".");

      field_read->synthesize_helper(ofs, 0, false);
      return;
    }

    assert(false);
  }

  void synthesize_helper(std::ostream &ofs, unsigned int lvl,
                         bool open_parenthesis) const {
    Variable *var;
    Type_ptr t;

    if (expr->get_kind() == Node::NodeKind::CAST) {
      Cast *cast = static_cast<Cast *>(expr.get());
      assert(cast->get_expression()->get_kind() == Node::NodeKind::VARIABLE);

      var = static_cast<Variable *>(cast->get_expression().get());
      t = cast->get_type();
    } else {
      assert(expr->get_kind() == Node::NodeKind::VARIABLE);
      var = static_cast<Variable *>(expr.get());
      t = var->get_type();
    }

    if (t->get_type_kind() == Type::TypeKind::POINTER) {
      Pointer *ptr = static_cast<Pointer *>(var->get_type().get());

      if (ptr->get_type()->get_type_kind() == Type::TypeKind::PRIMITIVE) {
        synthesize_pointer(ofs, lvl);
        return;
      } else {
        t = ptr->get_type();
      }
    }

    if (t->get_type_kind() == Type::TypeKind::ARRAY) {
      synthesize_array(ofs, lvl);
      return;
    }

    if (t->get_type_kind() == Type::TypeKind::STRUCT) {
      synthesize_struct(ofs, lvl, open_parenthesis);
      return;
    }

    unsigned int size = type->get_size();

    if (idx->get_kind() == CONSTANT) {
      Constant *idx_const = static_cast<Constant *>(idx.get());
      unsigned int idx_val = idx_const->get_value();

      if (idx_val == 0 && size == expr->get_type()->get_size()) {
        expr->synthesize(ofs);
        return;
      }

      if (idx_val != 0 && open_parenthesis) {
        ofs << "(";
      }

      expr->synthesize(ofs);

      if (idx_val != 0) {
        ofs << " >> ";
        ofs << (idx_val * size);
        ofs << ")";
      }
    } else {
      Expr_ptr offset = Mul::build(
          idx, Constant::build(PrimitiveType::PrimitiveKind::INT, size));

      ofs << "(";
      expr->synthesize(ofs);
      ofs << " >> ";
      offset->synthesize(ofs);
      ofs << ")";
    }

    if (size != expr->get_type()->get_size()) {
      ofs << " & 0x";
      assert(size > 0);
      assert(size % 8 == 0);
      ofs << std::string(size / 4, 'f');
    }
  }

public:
  Expr_ptr get_expr() const { return expr; }
  Expr_ptr get_idx() const { return idx; }

  std::string get_symbol() const {
    std::string symbol;

    switch (expr->get_kind()) {
    case VARIABLE: {
      Variable *var = static_cast<Variable *>(expr.get());
      symbol = var->get_symbol();
      break;
    }
    default: {
      assert(false && "Not implemented");
    }
    }

    return symbol;
  }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    synthesize_helper(ofs, lvl, true);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    unsigned int size = type->get_size();

    indent(ofs, lvl);
    ofs << "<read";
    ofs << " size=" << size;
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    idx->debug(ofs, lvl + 2);
    expr->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</read>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  void set_wrap(bool _wrap) override {
    // ignore
  }

  Expr_ptr clone() const override {
    Expression *e = new Read(expr, type, idx);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Read> build(Expr_ptr _expr, Type_ptr _type,
                                     Expr_ptr _idx) {
    Read *read = new Read(_expr, _type, _idx);
    return std::shared_ptr<Read>(read);
  }

  static std::shared_ptr<Read> build(Expr_ptr _expr) {
    auto zero = Constant::build(PrimitiveType::PrimitiveKind::INT, 0);
    Read *read = new Read(_expr, _expr->get_type(), zero);
    return std::shared_ptr<Read>(read);
  }
};

typedef std::shared_ptr<Read> Read_ptr;

class Concat : public Expression {
private:
  Expr_ptr left;
  Expr_ptr right;

  Concat(Expr_ptr _left, Expr_ptr _right, Type_ptr _type)
      : Expression(CONCAT, _type), left(_left->clone()),
        right(_right->clone()) {
    Type_ptr lt = left->get_type();
    Type_ptr rt = right->get_type();

    assert(type->get_size() >= (lt->get_size() + rt->get_size()));
    left->set_wrap(true);
    right->set_wrap(true);
  }

  Concat(Expr_ptr _left, Expr_ptr _right)
      : Concat(_left, _right, type_from_concat_exprs(_left, _right)) {}

  static Type_ptr type_from_concat_exprs(Expr_ptr _left, Expr_ptr _right) {
    Type_ptr lt = _left->get_type();
    Type_ptr rt = _right->get_type();

    uint64_t sz = lt->get_size() + rt->get_size();
    Type_ptr result;

    switch (sz) {
    case 1:
      result = PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL);
      break;
    case 8:
      result = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
      break;
    case 16:
      result = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T);
      break;
    case 32:
      result = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
      break;
    case 64:
      result = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T);
      break;
    default:
      if (sz % 8 != 0) {
        assert(false && "Width not a byte multiple");
      }

      Type_ptr byte =
          PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
      result = Array::build(byte, sz / 8);
    }

    return result;
  }

public:
  Expr_ptr get_left() const { return left; }
  Expr_ptr get_right() const { return right; }

  unsigned int get_last_idx() const {
    assert(is_concat_of_reads_and_concats());

    Read *left_read = static_cast<Read *>(left.get());
    Expr_ptr left_idx = left_read->get_idx();

    if (left_idx->get_kind() != CONSTANT) {
      return false;
    }

    Constant *left_idx_const = static_cast<Constant *>(left_idx.get());
    return left_idx_const->get_value();
  }

  bool is_sequential() const {
    if (!is_concat_of_reads_and_concats()) {
      return false;
    }

    assert(left->get_kind() == READ);

    Read *left_read = static_cast<Read *>(left.get());
    Expr_ptr left_idx = left_read->get_idx();

    if (left_idx->get_kind() != CONSTANT) {
      return false;
    }

    Constant *left_idx_const = static_cast<Constant *>(left_idx.get());

    if (right->get_kind() == READ) {
      Read *right_read = static_cast<Read *>(right.get());
      Expr_ptr right_idx = right_read->get_idx();
      auto right_read_size = right_read->get_type()->get_size();

      if (right_idx->get_kind() != CONSTANT) {
        return false;
      }

      Constant *right_idx_const = static_cast<Constant *>(right_idx.get());

      return left_idx_const->get_value() ==
             right_idx_const->get_value() + right_read_size / 8;
    }

    Concat *right_concat = static_cast<Concat *>(right.get());
    auto right_concat_size = right_concat->get_type()->get_size();

    if (!right_concat->is_sequential()) {
      return false;
    }

    return left_idx_const->get_value() ==
           right_concat->get_last_idx() + right_concat_size / 8;
  }

  bool is_concat_of_reads_and_concats() const {
    if (left->get_kind() != READ) {
      return false;
    }

    Read *left_read = static_cast<Read *>(left.get());
    std::string symbol = left_read->get_symbol();

    return is_concat_of_reads_and_concats(symbol);
  }

  bool is_concat_of_reads_and_concats(const std::string &symbol) const {
    if (left->get_kind() != READ) {
      return false;
    }

    Read *left_read = static_cast<Read *>(left.get());

    if (left_read->get_symbol() != symbol) {
      return false;
    }

    if (right->get_kind() == READ) {
      Read *right_read = static_cast<Read *>(right.get());
      return right_read->get_symbol() == symbol;
    } else if (right->get_kind() != CONCAT) {
      return false;
    }

    Concat *right_concat = static_cast<Concat *>(right.get());
    return right_concat->is_concat_of_reads_and_concats(symbol);
  }

  Expr_ptr get_var() const {
    assert(is_concat_of_reads_and_concats());
    Expr_ptr var;

    if (left->get_kind() == READ) {
      Read *left_read = static_cast<Read *>(left.get());
      var = left_read->get_expr();
    } else if (right->get_kind() == READ) {
      Read *right_read = static_cast<Read *>(right.get());
      var = right_read->get_expr();
    } else {
      Concat *right_concat = static_cast<Concat *>(right.get());
      var = right_concat->get_var();
    }

    return var;
  }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    unsigned int offset = right->get_type()->get_size();

    ofs << "(";
    left->synthesize(ofs);
    ofs << " << ";
    ofs << offset;
    ofs << ") | ";
    right->synthesize(ofs);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<concat";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    left->debug(ofs, lvl + 2);
    right->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</concat>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Concat(left, right, type);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Concat> build(Expr_ptr _left, Expr_ptr _right,
                                       Type_ptr _type) {
    Concat *concat = new Concat(_left, _right, _type);
    return std::shared_ptr<Concat>(concat);
  }
};

typedef std::shared_ptr<Concat> Concat_ptr;

class VariableDecl : public Expression {
private:
  std::string symbol;

  VariableDecl(const std::string &_symbol, Type_ptr _type)
      : Expression(VARIABLE_DECL, _type), symbol(_symbol) {}

public:
  const std::string &get_symbol() const { return symbol; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;

    if (type->get_type_kind() == Type::TypeKind::ARRAY) {
      Array *array = static_cast<Array *>(type.get());
      ofs << "[";
      ofs << array->get_n_elems();
      ofs << "]";
    }
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    ofs << "<varDecl";
    ofs << " symbol=" << symbol;
    ofs << " type=";
    type->debug(ofs);
    ofs << " />"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override { return clone(); }

  Expr_ptr clone() const override {
    Expression *e = new VariableDecl(symbol, type);
    return Expr_ptr(e);
  }

  void set_wrap(bool _wrap) override {
    // ignore
  }

  static std::shared_ptr<VariableDecl> build(const std::string &_symbol,
                                             Type_ptr _type) {
    VariableDecl *variable_decl = new VariableDecl(_symbol, _type);
    return std::shared_ptr<VariableDecl>(variable_decl);
  }

  static std::shared_ptr<VariableDecl> build(Variable_ptr variable) {
    VariableDecl *variable_decl =
        new VariableDecl(variable->get_symbol(), variable->get_type());
    return std::shared_ptr<VariableDecl>(variable_decl);
  }
};

typedef std::shared_ptr<VariableDecl> VariableDecl_ptr;

class FunctionArgDecl : public Node {
private:
  std::string symbol;
  Type_ptr type;

  FunctionArgDecl(const std::string &_symbol, Type_ptr _type)
      : Node(FUNCTION_ARG_DECL), symbol(_symbol), type(_type) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<functionArgDecl";
    ofs << " symbol=" << symbol;
    ofs << " type=";
    type->debug(ofs, lvl);
    ofs << " />";
    ofs << "\n";
  }

  static std::shared_ptr<FunctionArgDecl> build(const std::string &_symbol,
                                                Type_ptr _type) {
    FunctionArgDecl *function_arg_decl = new FunctionArgDecl(_symbol, _type);
    return std::shared_ptr<FunctionArgDecl>(function_arg_decl);
  }
};

typedef std::shared_ptr<FunctionArgDecl> FunctionArgDecl_ptr;

class Function : public Expression {
private:
  std::string name;

  std::vector<FunctionArgDecl_ptr> args;
  Block_ptr body;

  Type_ptr return_type;

  Function(const std::string &_name,
           const std::vector<FunctionArgDecl_ptr> &_args, Block_ptr _body,
           Type_ptr _return_type)
      : Expression(FUNCTION, _return_type), name(_name), args(_args),
        body(Block::build(_body, true)), return_type(_return_type) {}

  Function(const std::string &_name,
           const std::vector<FunctionArgDecl_ptr> &_args, Type_ptr _return_type)
      : Expression(FUNCTION, _return_type), name(_name), args(_args),
        return_type(_return_type) {}

public:
  std::string get_name() const { return name; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    return_type->synthesize(ofs, lvl);
    ofs << " ";

    ofs << name;
    ofs << "(";

    for (unsigned int i = 0; i < args.size(); i++) {
      const auto &arg = args[i];
      arg->synthesize(ofs, lvl);

      if (i < args.size() - 1) {
        ofs << ", ";
      }
    }

    ofs << ") ";

    if (body) {
      body->synthesize(ofs, lvl);
    }
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);

    ofs << "<function";
    ofs << " name=" << name;

    ofs << " return=";
    return_type->debug(ofs, lvl);

    ofs << ">"
        << "\n";

    for (const auto &arg : args) {
      arg->debug(ofs, lvl + 2);
    }

    if (body) {
      body->debug(ofs, lvl + 2);
    }

    indent(ofs, lvl);
    ofs << "</function>";
  }

  Expr_ptr simplify(AST *ast) const override { return clone(); }

  Expr_ptr clone() const override {
    Expression *e;

    if (body) {
      e = new Function(name, args, body, return_type);
    } else {
      e = new Function(name, args, return_type);
    }

    return Expr_ptr(e);
  }

  static std::shared_ptr<Function>
  build(const std::string &_name, const std::vector<FunctionArgDecl_ptr> &_args,
        Block_ptr _body, Type_ptr _return_type) {
    Function *function = new Function(_name, _args, _body, _return_type);
    return std::shared_ptr<Function>(function);
  }

  static std::shared_ptr<Function>
  build(const std::string &_name, const std::vector<FunctionArgDecl_ptr> &_args,
        Type_ptr _return_type) {
    Function *function = new Function(_name, _args, _return_type);
    return std::shared_ptr<Function>(function);
  }
};

typedef std::shared_ptr<Function> Function_ptr;

class Select : public Expression {
private:
  Expr_ptr cond;
  Expr_ptr first;
  Expr_ptr second;

  Select(Expr_ptr _cond, Expr_ptr _first, Expr_ptr _second)
      : Expression(SELECT,
                   _first->get_type() /* should be careful with this... */),
        cond(_cond->clone()), first(_first->clone()), second(_second->clone()) {
  }

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    cond->synthesize(ofs);
    ofs << " ? ";
    first->synthesize(ofs);
    ofs << " : ";
    second->synthesize(ofs);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<select";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    cond->debug(ofs, lvl + 2);
    first->debug(ofs, lvl + 2);
    second->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</select>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Select(cond, first, second);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Select> build(Expr_ptr _cond, Expr_ptr _first,
                                       Expr_ptr _second) {
    Select *select = new Select(_cond, _first, _second);
    return std::shared_ptr<Select>(select);
  }
};

typedef std::shared_ptr<Select> Select_ptr;

class Assignment : public Expression {
private:
  Expr_ptr variable;
  Expr_ptr value;

  Assignment(Expr_ptr _variable, Expr_ptr _value)
      : Expression(ASSIGNMENT, _variable->get_type()),
        variable(_variable->clone()), value(_value->clone()) {
    value->set_wrap(false);
  }

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl = 0) const override {
    variable->synthesize(ofs);
    ofs << " = ";
    value->synthesize(ofs);
  }

  void debug(std::ostream &ofs, unsigned int lvl = 0) const override {
    indent(ofs, lvl);
    ofs << "<assignment";
    ofs << " type=";
    type->debug(ofs);
    ofs << ">"
        << "\n";

    variable->debug(ofs, lvl + 2);
    value->debug(ofs, lvl + 2);

    indent(ofs, lvl);
    ofs << "</assignment>"
        << "\n";
  }

  Expr_ptr simplify(AST *ast) const override;

  Expr_ptr clone() const override {
    Expression *e = new Assignment(variable, value);
    return Expr_ptr(e);
  }

  static std::shared_ptr<Assignment> build(Expr_ptr _expr, Expr_ptr _value) {
    Assignment *assignment = new Assignment(_expr, _value);
    return std::shared_ptr<Assignment>(assignment);
  }
};

typedef std::shared_ptr<Assignment> Assignment_ptr;

Type_ptr type_from_size(uint64_t size, bool is_signed = false);
Type_ptr type_from_size(uint64_t size, bool is_signed, bool force_byte_array);
