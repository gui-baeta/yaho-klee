#include "ast.h"
#include "bdd/bdd.h"
#include "bdd/nodes/node.h"

using std::string;

constexpr char AST::CHUNK_LAYER_2[];
constexpr char AST::CHUNK_LAYER_3[];
constexpr char AST::CHUNK_LAYER_4[];

std::string get_symbol_label(const std::string &wanted,
                             const BDD::symbols_t &symbols) {
  for (auto symbol : symbols) {
    if (symbol.label_base == wanted) {
      return symbol.label;
    }
  }

  return "";
}

Expr_ptr fix_time_32_bits(Expr_ptr now) {
  if (now->get_kind() != Node::NodeKind::READ) {
    return now;
  }

  auto read = static_cast<Read *>(now.get());

  if (read->get_expr()->get_kind() != Node::NodeKind::VARIABLE) {
    return now;
  }

  auto variable = static_cast<Variable *>(read->get_expr().get());

  if (variable->get_symbol() != "now" || read->get_type()->get_size() != 32) {
    return now;
  }

  return variable->clone();
}

Expr_ptr fix_time_expiration(Expr_ptr now) {
  if (now->get_kind() != Node::NodeKind::ADD) {
    return now;
  }

  auto add = static_cast<Add *>(now.get());

  if (add->get_rhs()->get_kind() != Node::NodeKind::VARIABLE ||
      add->get_lhs()->get_kind() != Node::NodeKind::CONSTANT) {
    return now;
  }

  auto variable = static_cast<Variable *>(add->get_rhs().get());
  auto constant = static_cast<Constant *>(add->get_lhs().get());

  if (variable->get_symbol() != "now" ||
      constant->get_type()->get_size() != 64) {
    return now;
  }

  auto constant_value = constant->get_value();
  auto new_now = Sub::build(
      variable->clone(), Constant::build(PrimitiveType::PrimitiveKind::UINT64_T,
                                         ~constant_value + 1));
  return new_now;
}

// this is just to fix a bug in the bridge
klee::ref<klee::Expr> fix_key_klee_expr(klee::ref<klee::Expr> key) {
  if (key->getWidth() != 8) {
    return key;
  }

  if (key->getKind() != klee::Expr::Kind::Read) {
    return key;
  }

  auto read = static_cast<klee::ReadExpr *>(key.get());

  auto ul = read->updates;
  auto root = ul.root;

  if (root->name != "packet_chunks") {
    return key;
  }

  auto current_index = kutil::solver_toolbox.value_from_expr(read->index);

  klee::ref<klee::Expr> concat;

  for (auto i = 0; i < 6; i++) {
    auto index = kutil::solver_toolbox.exprBuilder->Constant(current_index + i,
                                                             klee::Expr::Int32);
    auto offset = kutil::solver_toolbox.exprBuilder->Read(ul, index);

    if (!concat.isNull()) {
      concat = kutil::solver_toolbox.exprBuilder->Concat(offset, concat);
    } else {
      concat = offset;
    }
  }

  return concat;
}

Variable_ptr AST::generate_new_symbol(klee::ref<klee::Expr> expr) {
  Type_ptr type = type_from_size(expr->getWidth());

  kutil::RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size() == 1);

  std::string symbol = from_cp_symbol(*symbols.begin());

  auto state_partial_name_finder = [&](Variable_ptr v) -> bool {
    std::string local_symbol = v->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto local_partial_name_finder = [&](local_variable_t v) -> bool {
    std::string local_symbol = v.first->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto state_it =
      std::find_if(state.begin(), state.end(), state_partial_name_finder);
  assert(state_it == state.end());

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto local_it =
        std::find_if(stack.begin(), stack.end(), local_partial_name_finder);
    assert(local_it == stack.end());
  }

  return Variable::build(symbol, type);
}

Variable_ptr AST::generate_new_symbol(std::string symbol, Type_ptr type,
                                      unsigned int ptr_lvl,
                                      unsigned int counter_begins) {

  symbol = from_cp_symbol(symbol);

  auto local_partial_name_finder = [&](local_variable_t v) -> bool {
    std::string local_symbol = v.first->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  unsigned int counter = 0;
  unsigned int last_id = 0;

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto local_it =
        std::find_if(stack.begin(), stack.end(), local_partial_name_finder);

    while (local_it != stack.end()) {
      Variable_ptr var = local_it->first;
      std::string saved_symbol = var->get_symbol();

      auto delim = saved_symbol.find(symbol);
      assert(delim != std::string::npos);

      std::string suffix = saved_symbol.substr(delim + symbol.size());
      if (suffix.size() > 1 && isdigit(suffix[1])) {
        assert(suffix[0] == '_');
        suffix = suffix.substr(1);
        unsigned int id = std::stoi(suffix, nullptr);
        if (last_id < id) {
          last_id = id;
        }
      }

      counter++;
      local_it =
          std::find_if(++local_it, stack.end(), local_partial_name_finder);
    }
  }

  std::string new_symbol = symbol;

  if (counter == 0 && counter_begins > 0) {
    new_symbol += "_" + std::to_string(counter_begins);
  } else if (counter > 0) {
    new_symbol += "_" + std::to_string(last_id + 1);
  }

  while (ptr_lvl != 0) {
    type = Pointer::build(type);
    ptr_lvl--;
  }

  return Variable::build(new_symbol, type);
}

Variable_ptr AST::generate_new_symbol(const std::string &symbol,
                                      Type_ptr type) {
  return generate_new_symbol(symbol, type, 0, 0);
}

Variable_ptr AST::get_from_state(const std::string &symbol) {
  auto translated = from_cp_symbol(symbol);

  auto finder = [&](Variable_ptr v) -> bool {
    return translated == v->get_symbol();
  };

  auto it = std::find_if(state.begin(), state.end(), finder);

  if (it == state.end()) {
    return nullptr;
  }

  return *it;
}

std::string AST::from_cp_symbol(std::string name) {
  if (callpath_var_translation.find(name) == callpath_var_translation.end()) {
    return name;
  }

  return callpath_var_translation[name];
}

AST::chunk_t AST::get_chunk_from_local(unsigned int idx) {
  chunk_t result;

  result.var = nullptr;
  result.start_index = 0;

  auto finder = [&](local_variable_t v) -> bool {
    Variable_ptr var = v.first;
    klee::ref<klee::Expr> expr = v.second;

    std::string symbol = var->get_symbol();

    if (symbol != CHUNK_LAYER_2 && symbol != CHUNK_LAYER_3 &&
        symbol != CHUNK_LAYER_4) {
      return false;
    }

    if (expr->getKind() != klee::Expr::Kind::Concat) {
      return false;
    }

    auto start_idx = get_first_concat_idx(expr);
    auto end_idx = get_last_concat_idx(expr);

    return start_idx <= idx && idx <= end_idx;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), finder);
    if (it != stack.end()) {
      result.var = it->first;
      result.start_index = get_first_concat_idx(it->second);
      break;
    }
  }

  if (result.var == nullptr) {
    dump_stack();
  }

  return result;
}

Variable_ptr AST::get_from_local(const std::string &symbol, bool partial) {
  auto translated = from_cp_symbol(symbol);

  auto finder = [&](local_variable_t v) -> bool {
    if (!partial) {
      return v.first->get_symbol() == translated;
    } else {
      std::string local_symbol = v.first->get_symbol();
      return local_symbol.find(translated) != std::string::npos;
    }
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;

    auto it = std::find_if(stack.begin(), stack.end(), finder);
    if (it != stack.end()) {
      return it->first;
    }
  }

  return nullptr;
}

klee::ref<klee::Expr> AST::get_expr_from_local_by_addr(unsigned int addr) {
  klee::ref<klee::Expr> found;

  assert(addr != 0);
  auto addr_finder = [&](local_variable_t v) -> bool {
    unsigned int local_addr = v.first->get_addr();
    return local_addr == addr;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), addr_finder);
    if (it != stack.end()) {
      found = it->second;
      break;
    }
  }

  return found;
}

Variable_ptr AST::get_from_local_by_addr(const std::string &symbol,
                                         unsigned int addr) {
  assert(addr != 0);
  auto translated = from_cp_symbol(symbol);

  auto partial_name_finder = [&](local_variable_t v) -> bool {
    std::string local_symbol = v.first->get_symbol();
    return local_symbol.find(translated) != std::string::npos;
  };

  auto addr_finder = [&](local_variable_t v) -> bool {
    unsigned int local_addr = v.first->get_addr();
    return local_addr == addr;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), addr_finder);
    if (it != stack.end()) {
      return it->first;
    }
  }

  // allocating)
  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), partial_name_finder);

    if (it == stack.end()) {
      continue;
    }

    Variable_ptr var = it->first;

    if (var->get_addr() != 0) {
      continue;
    }

    var->set_addr(addr);
    return var;
  }

  std::cerr << "Error: all pointers are allocated, or symbol not found.\n";
  std::cerr << "Symbol: " << symbol << "\n";
  dump_stack();
  assert(false);
  exit(1);

  Variable_ptr ptr;
  return ptr;
}

Variable_ptr AST::get_from_state(unsigned int addr) {
  assert(addr != 0);

  auto addr_finder = [&](Variable_ptr v) -> bool {
    return v->get_addr() == addr;
  };

  auto addr_finder_it = std::find_if(state.begin(), state.end(), addr_finder);
  if (addr_finder_it != state.end()) {
    return *addr_finder_it;
  }

  dump_stack();
  std::cerr << "Address requested: " << addr << "\n";
  assert(false && "No variable allocated in this address");
}

Expr_ptr AST::get_from_local(klee::ref<klee::Expr> expr) {
  assert(!expr.isNull());

  auto find_matching_offset = [](klee::ref<klee::Expr> saved,
                                 klee::ref<klee::Expr> wanted) -> int {
    auto saved_sz = saved->getWidth();
    auto wanted_sz = wanted->getWidth();

    kutil::RetrieveSymbols retriever;
    retriever.visit(saved);
    if (retriever.get_retrieved_strings().size() == 0) {
      return -1;
    }

    if (wanted_sz > saved_sz) {
      return -1;
    }

    for (unsigned int offset = 0; offset <= saved_sz - wanted_sz; offset += 8) {
      auto saved_chunk =
          kutil::solver_toolbox.exprBuilder->Extract(saved, offset, wanted_sz);

      if (kutil::solver_toolbox.are_exprs_always_equal(saved_chunk, wanted)) {
        return offset;
      }
    }

    return -1;
  };

  auto finder = [&](local_variable_t v) -> bool {
    if (v.second.isNull()) {
      return false;
    }

    return find_matching_offset(v.second, expr) >= 0;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;

    auto it = std::find_if(stack.begin(), stack.end(), finder);
    if (it != stack.end()) {
      auto offset = find_matching_offset(it->second, expr);
      assert(offset % 8 == 0 &&
             "Found the local variable, but offset is not multiple of byte");

      if (offset == 0 && it->second->getWidth() == expr->getWidth()) {
        return it->first;
      }

      Constant_ptr idx =
          Constant::build(PrimitiveType::PrimitiveKind::UINT64_T, offset / 8);

      Read_ptr extracted =
          Read::build(it->first, type_from_size(expr->getWidth()), idx);

      return extracted;
    }
  }

  return nullptr;
}

void AST::associate_expr_to_local(const std::string &symbol,
                                  klee::ref<klee::Expr> expr) {
  auto translated = from_cp_symbol(symbol);

  auto name_finder = [&](local_variable_t v) -> bool {
    return v.first->get_symbol() == translated;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;

    auto name_it = std::find_if(stack.begin(), stack.end(), name_finder);
    if (name_it != stack.end()) {
      auto association = std::make_pair(name_it->first, expr);
      std::replace_if(stack.begin(), stack.end(), name_finder, association);
      return;
    }
  }

  assert(false && "Variable not found");
}

bool AST::is_in_state(const std::string &symbol) const {
  for (const auto &var : state) {
    if (var->get_symbol() == symbol) {
      return true;
    }
  }

  return false;
}

void AST::push_to_state(Variable_ptr var) {
  if (!is_in_state(var->get_symbol())) {
    state.push_back(var);
  }
}

void AST::push_to_local(Variable_ptr var) {
  assert(get_from_local(var->get_symbol()) == nullptr);
  assert(local_variables.size() > 0);
  local_variables.back().push_back(std::make_pair(var, nullptr));
}

void AST::push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr) {
  assert(get_from_local(var->get_symbol()) == nullptr);
  assert(local_variables.size() > 0);
  local_variables.back().push_back(std::make_pair(var, expr));
}

FunctionCall_ptr spread_capacity_among_cores(Expr_ptr capacity) {
  auto args = std::vector<ExpressionType_ptr>{capacity};
  auto ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
  auto fcall = FunctionCall::build("spread_data_among_cores", args, ret_type);
  return fcall;
}

Node_ptr AST::init_state_node_from_call(const BDD::Call *bdd_call,
                                        TargetOption target) {
  auto call = bdd_call->get_call();
  auto symbols = bdd_call->get_local_generated_symbols();

  auto fname = call.function_name;
  std::vector<ExpressionType_ptr> args;

  PrimitiveType_ptr ret_type;
  klee::ref<klee::Expr> ret_expr;
  std::string ret_symbol;

  if (fname == "rte_lcore_count") {
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
    ret_symbol = get_symbol_label("lcores", symbols);
    ret_expr = call.ret;
  } else if (fname == "map_allocate") {
    Expr_ptr map_out_expr = transpile(this, call.args["map_out"].out);
    assert(map_out_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr =
        (static_cast<Constant *>(map_out_expr.get()))->get_value();

    assert(call.args["keq"].fn_ptr_name.first);
    assert(call.args["khash"].fn_ptr_name.first);

    Type_ptr void_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);

    Expr_ptr keq =
        Variable::build(call.args["keq"].fn_ptr_name.second, void_type);
    assert(keq);

    auto keq_ret_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL);
    std::vector<FunctionArgDecl_ptr> keq_args{
        FunctionArgDecl::build("a", Pointer::build(void_type)),
        FunctionArgDecl::build("b", Pointer::build(void_type)),
    };

    auto keq_decl = Function::build(call.args["keq"].fn_ptr_name.second,
                                    keq_args, keq_ret_type);
    keq_decl->set_terminate_line(true);

    if (!function_in_global_code(call.args["keq"].fn_ptr_name.second)) {
      push_global_code(keq_decl);
    }

    Expr_ptr khash =
        Variable::build(call.args["khash"].fn_ptr_name.second, void_type);
    assert(khash);

    auto khash_ret_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
    std::vector<FunctionArgDecl_ptr> khash_args{
        FunctionArgDecl::build("obj", Pointer::build(void_type)),
    };
    auto kash_decl = Function::build(call.args["khash"].fn_ptr_name.second,
                                     khash_args, khash_ret_type);
    kash_decl->set_terminate_line(true);

    if (!function_in_global_code(call.args["khash"].fn_ptr_name.second)) {
      push_global_code(kash_decl);
    }

    Expr_ptr capacity = transpile(this, call.args["capacity"].expr);
    assert(capacity);

    if (target == TargetOption::SHARED_NOTHING) {
      capacity = spread_capacity_among_cores(capacity);
    }

    Type_ptr map_type = Struct::build(translate_struct("Map", target));
    Variable_ptr new_map = generate_new_symbol("map", map_type, 1, 0);
    new_map->set_addr(map_addr);

    push_to_state(new_map);
    push_to_local(new_map);

    // hack
    if (target == TargetOption::SHARED_NOTHING) {
      new_map = generate_new_symbol("(*" + new_map->get_symbol() + "_ptr)",
                                    map_type, 1, 0);
    }

    args = std::vector<ExpressionType_ptr>{keq, khash, capacity,
                                           AddressOf::build(new_map)};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("map_allocation_succeeded", symbols);
  } else if (fname == "vector_allocate") {
    Expr_ptr vector_out_expr = transpile(this, call.args["vector_out"].out);
    assert(vector_out_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_out_expr.get()))->get_value();

    assert(call.args["init_elem"].fn_ptr_name.first);
    Type_ptr void_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);

    Expr_ptr elem_size = transpile(this, call.args["elem_size"].expr);
    assert(elem_size);
    Expr_ptr capacity = transpile(this, call.args["capacity"].expr);
    assert(capacity);

    Expr_ptr init_elem =
        Variable::build(call.args["init_elem"].fn_ptr_name.second, void_type);
    assert(init_elem);

    auto init_elem_ret_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
    std::vector<FunctionArgDecl_ptr> init_elem_args{
        FunctionArgDecl::build("obj", Pointer::build(void_type)),
    };
    auto init_elem_decl =
        Function::build(call.args["init_elem"].fn_ptr_name.second,
                        init_elem_args, init_elem_ret_type);
    init_elem_decl->set_terminate_line(true);
    if (!function_in_global_code(call.args["init_elem"].fn_ptr_name.second)) {
      push_global_code(init_elem_decl);
    }

    Type_ptr vector_type = Struct::build(translate_struct("Vector", target));
    Variable_ptr new_vector = generate_new_symbol("vector", vector_type, 1, 0);
    new_vector->set_addr(vector_addr);

    push_to_state(new_vector);
    push_to_local(new_vector);

    // hack
    if (target == TargetOption::SHARED_NOTHING) {
      new_vector = generate_new_symbol(
          "(*" + new_vector->get_symbol() + "_ptr)", vector_type, 1, 0);
    }

    if (target == TargetOption::SHARED_NOTHING) {
      capacity = spread_capacity_among_cores(capacity);
    }

    args = std::vector<ExpressionType_ptr>{elem_size, capacity, init_elem,
                                           AddressOf::build(new_vector)};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("vector_alloc_success", symbols);
  } else if (fname == "dchain_allocate") {
    Expr_ptr chain_out_expr = transpile(this, call.args["chain_out"].out);
    assert(chain_out_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t dchain_addr =
        (static_cast<Constant *>(chain_out_expr.get()))->get_value();

    Expr_ptr index_range = transpile(this, call.args["index_range"].expr);
    assert(index_range);

    Type_ptr dchain_type =
        Struct::build(translate_struct("DoubleChain", target));
    Variable_ptr new_dchain = generate_new_symbol("dchain", dchain_type, 1, 0);
    new_dchain->set_addr(dchain_addr);

    push_to_state(new_dchain);
    push_to_local(new_dchain);

    // hack
    if (target == TargetOption::SHARED_NOTHING) {
      new_dchain = generate_new_symbol(
          "(*" + new_dchain->get_symbol() + "_ptr)", dchain_type, 1, 0);
    }

    if (target == TargetOption::SHARED_NOTHING) {
      index_range = spread_capacity_among_cores(index_range);
    }

    args = std::vector<ExpressionType_ptr>{index_range,
                                           AddressOf::build(new_dchain)};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("is_dchain_allocated", symbols);
  } else if (fname == "sketch_allocate") {
    assert(call.args["khash"].fn_ptr_name.first);
    Type_ptr void_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
    Expr_ptr khash =
        Variable::build(call.args["khash"].fn_ptr_name.second, void_type);
    assert(khash);

    auto khash_ret_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
    std::vector<FunctionArgDecl_ptr> khash_args{
        FunctionArgDecl::build("obj", Pointer::build(void_type)),
    };
    auto kash_decl = Function::build(call.args["khash"].fn_ptr_name.second,
                                     khash_args, khash_ret_type);
    kash_decl->set_terminate_line(true);

    if (!function_in_global_code(call.args["khash"].fn_ptr_name.second)) {
      push_global_code(kash_decl);
    }

    Expr_ptr capacity = transpile(this, call.args["capacity"].expr);
    assert(capacity);

    Expr_ptr threshold = transpile(this, call.args["threshold"].expr);
    assert(threshold);

    Expr_ptr sketch_out_expr = transpile(this, call.args["sketch_out"].out);
    assert(sketch_out_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t sketch_addr =
        (static_cast<Constant *>(sketch_out_expr.get()))->get_value();

    Type_ptr sketch_type = Struct::build(translate_struct("Sketch", target));
    Variable_ptr new_sketch = generate_new_symbol("sketch", sketch_type, 1, 0);
    new_sketch->set_addr(sketch_addr);

    push_to_state(new_sketch);
    push_to_local(new_sketch);

    // hack
    if (target == TargetOption::SHARED_NOTHING) {
      new_sketch = generate_new_symbol(
          "(*" + new_sketch->get_symbol() + "_ptr)", sketch_type, 1, 0);
    }

    if (target == TargetOption::SHARED_NOTHING) {
      capacity = spread_capacity_among_cores(capacity);
    }

    args = std::vector<ExpressionType_ptr>{khash, capacity, threshold,
                                           AddressOf::build(new_sketch)};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("sketch_allocation_succeeded", symbols);
  } else if (fname == "cht_fill_cht") {
    Expr_ptr vector_expr = transpile(this, call.args["cht"].expr);
    assert(vector_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_expr.get()))->get_value();

    Expr_ptr vector = get_from_state(vector_addr);
    Expr_ptr cht_height = transpile(this, call.args["cht_height"].expr);
    assert(cht_height);
    Expr_ptr backend_capacity =
        transpile(this, call.args["backend_capacity"].expr);
    assert(backend_capacity);

    args =
        std::vector<ExpressionType_ptr>{vector, cht_height, backend_capacity};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("cht_fill_cht_successful", symbols);
  } else {
    std::cerr << call.function_name << "\n";

    for (const auto &arg : call.args) {
      std::cerr << arg.first << " : " << kutil::expr_to_string(arg.second.expr)
                << "\n";
      if (!arg.second.in.isNull()) {
        std::cerr << "  in:  " << kutil::expr_to_string(arg.second.in) << "\n";
      }
      if (!arg.second.out.isNull()) {
        std::cerr << "  out: " << kutil::expr_to_string(arg.second.out) << "\n";
      }
    }

    for (const auto &ev : call.extra_vars) {
      std::cerr << ev.first << " : " << kutil::expr_to_string(ev.second.first)
                << " | " << kutil::expr_to_string(ev.second.second) << "\n";
    }

    std::cerr << "Not implemented:\n";
    std::cerr << kutil::expr_to_string(call.ret) << "\n";

    assert(false && "Not implemented");
    exit(1);
  }

  fname = translate_fname(fname, target);
  assert(args.size() == call.args.size());

  FunctionCall_ptr fcall = FunctionCall::build(fname, args, ret_type);

  if (ret_type->get_primitive_kind() != PrimitiveType::PrimitiveKind::VOID) {
    assert(ret_symbol.size());

    Variable_ptr ret_var = generate_new_symbol(ret_symbol, ret_type);
    ret_var->set_wrap(false);

    if (!ret_expr.isNull()) {
      push_to_local(ret_var, ret_expr);
    } else {
      push_to_local(ret_var);
    }

    VariableDecl_ptr ret = VariableDecl::build(ret_var);
    Assignment_ptr assignment = Assignment::build(ret, fcall);
    assignment->set_terminate_line(true);

    return assignment;
  }

  return fcall;
}

const BDD::Call *find_vector_return_with_obj(const BDD::Node *root,
                                             klee::ref<klee::Expr> obj) {
  assert(root && "Root is null");
  std::vector<const BDD::Node *> nodes{root};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    if (node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto node_branch = static_cast<const BDD::Branch *>(node);

      nodes.push_back(node_branch->get_on_true().get());
      nodes.push_back(node_branch->get_on_false().get());

      continue;
    }

    if (node->get_type() != BDD::Node::NodeType::CALL) {
      continue;
    }

    nodes.push_back(node->get_next().get());

    auto node_call = static_cast<const BDD::Call *>(node);
    auto call = node_call->get_call();

    if (call.function_name != "vector_return") {
      continue;
    }

    auto this_obj = call.args["vector"].expr;
    auto extracted = kutil::solver_toolbox.exprBuilder->Extract(
        this_obj, 0, obj->getWidth());
    auto eq = kutil::solver_toolbox.are_exprs_always_equal(obj, extracted);

    if (eq) {
      return node_call;
    }
  }

  return nullptr;
}

const BDD::Call *find_vector_return_with_value(const BDD::Node *root,
                                               klee::ref<klee::Expr> value) {
  assert(root && "Root is null");
  std::vector<const BDD::Node *> nodes{root};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    if (node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto node_branch = static_cast<const BDD::Branch *>(node);

      nodes.push_back(node_branch->get_on_true().get());
      nodes.push_back(node_branch->get_on_false().get());

      continue;
    }

    if (node->get_type() != BDD::Node::NodeType::CALL) {
      continue;
    }

    nodes.push_back(node->get_next().get());

    auto node_call = static_cast<const BDD::Call *>(node);
    auto call = node_call->get_call();

    if (call.function_name != "vector_return") {
      continue;
    }

    auto this_vector_value = call.args["value"].in;

    if (this_vector_value->getWidth() < value->getWidth()) {
      continue;
    }

    auto extracted = kutil::solver_toolbox.exprBuilder->Extract(
        this_vector_value, 0, value->getWidth());
    auto eq = kutil::solver_toolbox.are_exprs_always_equal(value, extracted);

    if (eq) {
      return node_call;
    }
  }

  return nullptr;
}

std::pair<bool, Expr_ptr> AST::inc_pkt_offset(Expr_ptr offset) {
  if (pkt_buffer_offset.top().size() == 0) {
    pkt_buffer_offset.top().push(offset);
    Expr_ptr zero = Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, 0);
    return std::pair<bool, Expr_ptr>(false, zero);
  }

  auto last_offset = pkt_buffer_offset.top().top();
  auto add = Add::build(last_offset, offset);

  pkt_buffer_offset.top().push(add);

  return std::pair<bool, Expr_ptr>(true, last_offset);
}

void AST::dec_pkt_offset() {
  if (pkt_buffer_offset.size() == 0 || pkt_buffer_offset.top().size() == 0) {
    std::cerr << "Error: decrementing empty pkt_buffer_offset\n";
    assert(false);
    exit(1);
  }

  pkt_buffer_offset.top().pop();
}

std::vector<BDD::Node_ptr>
get_prev_functions(BDD::Node_ptr start, std::string function_name,
                   std::unordered_set<std::string> stop_nodes) {
  std::vector<BDD::Node_ptr> return_nodes;

  bool loop = true;
  BDD::Node_ptr node = start;

  auto is_stop_node = [&stop_nodes](BDD::Node_ptr node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      return false;
    }

    auto call = static_cast<BDD::Call *>(node.get())->get_call();

    return stop_nodes.find(call.function_name) != stop_nodes.end();
  };

  auto is_target_node = [&function_name](BDD::Node_ptr node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      return false;
    }

    auto call = static_cast<BDD::Call *>(node.get())->get_call();
    return call.function_name == function_name;
  };

  while (loop && node != nullptr) {
    loop = !is_stop_node(node);

    if (is_target_node(node)) {
      return_nodes.push_back(node);
    }

    node = node->get_prev();
  }

  return return_nodes;
}

Node_ptr AST::process_state_node_from_call(const BDD::Call *bdd_call,
                                           TargetOption target) {
  auto call = bdd_call->get_call();
  auto symbols = bdd_call->get_local_generated_symbols();

  auto fname = call.function_name;

  std::vector<Expr_ptr> exprs;
  std::vector<Expr_ptr> after_call_exprs;
  std::vector<ExpressionType_ptr> args;

  Type_ptr ret_type;
  std::string ret_symbol;
  klee::ref<klee::Expr> ret_expr;
  std::pair<bool, uint64_t> ret_addr;

  int counter_begins = 0;
  bool ignore = false;

  bool check_write_attempt = false;
  bool write_attempt = false;

  if (fname == "current_time") {
    associate_expr_to_local("now", call.ret);
    ignore = true;
  } else if (fname == "packet_borrow_next_chunk") {
    ignore = true;
    Expr_ptr chunk_expr = transpile(this, call.args["chunk"].out);
    assert(chunk_expr->get_kind() == Node::NodeKind::CONSTANT);
    auto hdr_addr = static_cast<Constant *>(chunk_expr.get())->get_value();

    Variable_ptr p = get_from_local("p");
    assert(p);
    Expr_ptr pkt_len = transpile(this, call.args["length"].expr);
    auto inc_pkt_offset_res = inc_pkt_offset(pkt_len);

    Type_ptr hdr_type;
    std::string hdr_symbol;
    klee::ref<klee::Expr> hdr_expr;

    auto prev_functions = get_prev_functions(
        bdd_call->get_prev(), "packet_borrow_next_chunk", {"current_time"});

    switch (prev_functions.size()) {
    case 0: {
      Array_ptr _uint8_t_6 = Array::build(
          PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T), 6);

      std::vector<Variable_ptr> ether_addr_fields{
          Variable::build("addr_bytes", _uint8_t_6)};

      Struct_ptr ether_addr = Struct::build("ether_addr", ether_addr_fields);

      std::vector<Variable_ptr> ether_hdr_fields{
          Variable::build("d_addr", ether_addr),
          Variable::build("s_addr", ether_addr),
          Variable::build(
              "ether_type",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T))};

      Struct_ptr ether_hdr = Struct::build("rte_ether_hdr", ether_hdr_fields);

      hdr_type = Pointer::build(ether_hdr);
      hdr_symbol = CHUNK_LAYER_2;

      layer.back()++;
      break;
    }
    case 1: {
      std::vector<Variable_ptr> ipv4_hdr_fields{
          Variable::build(
              "version_ihl",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T)),
          Variable::build(
              "type_of_service",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T)),
          Variable::build(
              "total_length",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
          Variable::build(
              "packet_id",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
          Variable::build(
              "fragment_offset",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
          Variable::build(
              "time_to_live",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T)),
          Variable::build(
              "next_proto_id",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T)),
          Variable::build(
              "hdr_checksum",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
          Variable::build(
              "src_addr",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T)),
          Variable::build(
              "dst_addr",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T))};

      Struct_ptr ipv4_hdr = Struct::build("rte_ipv4_hdr", ipv4_hdr_fields);

      hdr_type = Pointer::build(ipv4_hdr);
      hdr_symbol = CHUNK_LAYER_3;

      layer.back()++;
      break;
    }
    case 2:
    case 3: {
      if (pkt_len->get_kind() != Node::NodeKind::CONSTANT) {
        hdr_type = Pointer::build(
            PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T));
        hdr_symbol = "ip_options";
        break;
      }

      std::vector<Variable_ptr> tcpudp_hdr_fields{
          Variable::build(
              "src_port",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
          Variable::build(
              "dst_port",
              PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T))};

      Struct_ptr tcpudp_hdr = Struct::build("tcpudp_hdr", tcpudp_hdr_fields);

      hdr_type = Pointer::build(tcpudp_hdr);
      hdr_symbol = CHUNK_LAYER_4;

      layer.back()++;
      break;
    }
    default: {
      assert(false && "Missing layers implementation");
    }
    }

    hdr_expr = call.extra_vars["the_chunk"].second;

    auto nf_count =
        get_prev_functions(bdd_call->get_prev(), "current_time", {});

    Variable_ptr hdr_var = Variable::build(
        hdr_symbol + "_" + std::to_string(nf_count.size()), hdr_type);
    hdr_var->set_addr(hdr_addr);

    VariableDecl_ptr hdr_decl = VariableDecl::build(hdr_var);
    Expr_ptr p_offseted;

    if (!inc_pkt_offset_res.first) {
      p_offseted = p;
    } else {
      p_offseted = Add::build(p, inc_pkt_offset_res.second);
    }

    auto p_casted = Cast::build(p_offseted, hdr_type);
    auto p_offseted_assignment = Assignment::build(hdr_decl, p_casted);
    exprs.push_back(p_offseted_assignment);
    push_to_local(hdr_var, hdr_expr);
  } else if (fname == "packet_get_unread_length") {
    Variable_ptr p = get_from_local("p");
    assert(p);

    args = std::vector<ExpressionType_ptr>{p};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T);
    ret_symbol = get_symbol_label("unread_len", symbols);
    ret_expr = call.ret;
  } else if (fname == "expire_items_single_map") {
    check_write_attempt = true;

    Expr_ptr chain_expr = transpile(this, call.args["chain"].expr);
    assert(chain_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chain_addr =
        (static_cast<Constant *>(chain_expr.get()))->get_value();

    Expr_ptr vector_expr = transpile(this, call.args["vector"].expr);
    assert(vector_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_expr.get()))->get_value();

    Expr_ptr map_expr = transpile(this, call.args["map"].expr);
    assert(map_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr = (static_cast<Constant *>(map_expr.get()))->get_value();

    Variable_ptr chain = get_from_state(chain_addr);
    Variable_ptr vector = get_from_state(vector_addr);
    Variable_ptr map = get_from_state(map_addr);
    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);

    now = fix_time_expiration(now);

    args = std::vector<ExpressionType_ptr>{chain, vector, map, now};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("number_of_freed_flows", symbols);
    ret_expr = call.ret;
  } else if (fname == "expire_items_single_map_offseted") {
    check_write_attempt = true;

    Expr_ptr chain_expr = transpile(this, call.args["chain"].expr);
    assert(chain_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chain_addr =
        (static_cast<Constant *>(chain_expr.get()))->get_value();

    Expr_ptr vector_expr = transpile(this, call.args["vector"].expr);
    assert(vector_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_expr.get()))->get_value();

    Expr_ptr map_expr = transpile(this, call.args["map"].expr);
    assert(map_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr = (static_cast<Constant *>(map_expr.get()))->get_value();

    Variable_ptr chain = get_from_state(chain_addr);
    Variable_ptr vector = get_from_state(vector_addr);
    Variable_ptr map = get_from_state(map_addr);
    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);
    Expr_ptr offset = transpile(this, call.args["offset"].expr);
    assert(offset);

    now = fix_time_expiration(now);

    args = std::vector<ExpressionType_ptr>{chain, vector, map, now, offset};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("number_of_freed_flows", symbols);
    ret_expr = call.ret;
  } else if (fname == "expire_items_single_map_iteratively") {
    check_write_attempt = true;

    Expr_ptr vector_expr = transpile(this, call.args["vector"].expr);
    assert(vector_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_expr.get()))->get_value();

    Expr_ptr map_expr = transpile(this, call.args["map"].expr);
    assert(map_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr = (static_cast<Constant *>(map_expr.get()))->get_value();

    Variable_ptr vector = get_from_state(vector_addr);
    Variable_ptr map = get_from_state(map_addr);
    Expr_ptr start = transpile(this, call.args["start"].expr, true);
    assert(start);
    Expr_ptr n_elems = transpile(this, call.args["n_elems"].expr, true);
    assert(n_elems);

    args = std::vector<ExpressionType_ptr>{vector, map, start, n_elems};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("number_of_freed_flows", symbols);
    ret_expr = call.ret;
  } else if (fname == "sketch_compute_hashes") {
    Expr_ptr sketch_expr = transpile(this, call.args["sketch"].expr);
    assert(sketch_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t sketch_addr =
        (static_cast<Constant *>(sketch_expr.get()))->get_value();

    Expr_ptr sketch = get_from_state(sketch_addr);

    auto key_klee_expr = call.args["key"].in;
    key_klee_expr = fix_key_klee_expr(key_klee_expr);

    Type_ptr key_type = type_from_klee_expr(key_klee_expr, true);
    Variable_ptr key = generate_new_symbol("sketch_key", key_type);
    push_to_local(key);

    VariableDecl_ptr key_decl = VariableDecl::build(key);
    exprs.push_back(key_decl);

    auto statements = build_and_fill_byte_array(this, key, key_klee_expr);
    assert(statements.size());
    exprs.insert(exprs.end(), statements.begin(), statements.end());

    args = std::vector<ExpressionType_ptr>{sketch, AddressOf::build(key)};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "sketch_refresh") {
    Expr_ptr sketch_expr = transpile(this, call.args["sketch"].expr);
    assert(sketch_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t sketch_addr =
        (static_cast<Constant *>(sketch_expr.get()))->get_value();

    Expr_ptr sketch = get_from_state(sketch_addr);

    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);

    now = fix_time_32_bits(now);

    args = std::vector<ExpressionType_ptr>{sketch, now};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "sketch_fetch") {
    Expr_ptr sketch_expr = transpile(this, call.args["sketch"].expr);
    assert(sketch_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t sketch_addr =
        (static_cast<Constant *>(sketch_expr.get()))->get_value();

    Expr_ptr sketch = get_from_state(sketch_addr);

    args = std::vector<ExpressionType_ptr>{sketch};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("overflow", symbols);
    ret_expr = call.ret;
  } else if (fname == "sketch_touch_buckets") {
    check_write_attempt = true;

    Expr_ptr sketch_expr = transpile(this, call.args["sketch"].expr);
    assert(sketch_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t sketch_addr =
        (static_cast<Constant *>(sketch_expr.get()))->get_value();

    Expr_ptr sketch = get_from_state(sketch_addr);

    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);
    now = fix_time_32_bits(now);

    args = std::vector<ExpressionType_ptr>{sketch, now};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("success", symbols);
    ret_expr = call.ret;
  } else if (fname == "sketch_expire") {
    check_write_attempt = true;

    Expr_ptr sketch_expr = transpile(this, call.args["sketch"].expr);
    assert(sketch_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t sketch_addr =
        (static_cast<Constant *>(sketch_expr.get()))->get_value();

    Expr_ptr sketch = get_from_state(sketch_addr);

    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);

    now = fix_time_expiration(now);

    args = std::vector<ExpressionType_ptr>{sketch, now};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "map_get") {
    Expr_ptr map_expr = transpile(this, call.args["map"].expr);
    assert(map_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr = (static_cast<Constant *>(map_expr.get()))->get_value();

    auto key_klee_expr = call.args["key"].in;
    key_klee_expr = fix_key_klee_expr(key_klee_expr);

    Type_ptr key_type = type_from_klee_expr(key_klee_expr, true);
    Variable_ptr key = generate_new_symbol("map_key", key_type);
    push_to_local(key);

    VariableDecl_ptr key_decl = VariableDecl::build(key);
    exprs.push_back(key_decl);

    auto statements = build_and_fill_byte_array(this, key, key_klee_expr);
    assert(statements.size());
    exprs.insert(exprs.end(), statements.begin(), statements.end());

    Expr_ptr map = get_from_state(map_addr);

    Type_ptr value_out_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    Variable_ptr value_out = generate_new_symbol("value_out", value_out_type);

    assert(!call.args["value_out"].out.isNull());
    push_to_local(value_out, call.args["value_out"].out);

    VariableDecl_ptr value_out_decl = VariableDecl::build(value_out);
    exprs.push_back(value_out_decl);

    args =
        std::vector<ExpressionType_ptr>{map, key, AddressOf::build(value_out)};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("map_has_this_key", symbols);
    ret_expr = call.ret;
  } else if (fname == "dchain_allocate_new_index") {
    check_write_attempt = true;

    Expr_ptr chain_expr = transpile(this, call.args["chain"].expr);
    assert(chain_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chain_addr =
        (static_cast<Constant *>(chain_expr.get()))->get_value();

    Expr_ptr chain = get_from_state(chain_addr);
    assert(chain);

    Variable_ptr index_out = generate_new_symbol(call.args["index_out"].out);
    assert(index_out);
    push_to_local(index_out, call.args["index_out"].out);

    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);

    now = fix_time_32_bits(now);

    VariableDecl_ptr index_out_decl = VariableDecl::build(index_out);
    exprs.push_back(index_out_decl);

    args = std::vector<ExpressionType_ptr>{chain, AddressOf::build(index_out),
                                           now};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("out_of_space", symbols);

    ret_expr = call.ret;
    counter_begins = -1;
  } else if (fname == "vector_borrow") {
    assert(!call.args["val_out"].out.isNull());

    Expr_ptr vector_expr = transpile(this, call.args["vector"].expr);
    assert(vector_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_expr.get()))->get_value();

    Expr_ptr val_out_expr = transpile(this, call.args["val_out"].out);
    assert(val_out_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t val_out_addr =
        (static_cast<Constant *>(val_out_expr.get()))->get_value();

    Expr_ptr vector = get_from_state(vector_addr);
    Expr_ptr index = transpile(this, call.args["index"].expr, true);
    assert(index);

    Expr_ptr index_arg = index;

    if (index->get_type()->get_type_kind() == Type::TypeKind::POINTER) {
      Type_ptr int_type =
          PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
      Type_ptr index_type = Pointer::build(int_type);
      Cast_ptr index_cast = Cast::build(index, index_type);
      Expr_ptr zero =
          Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, 0);
      index_arg = Read::build(index_cast, int_type, zero);
    }

    Type_ptr val_out_type =
        PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
    Variable_ptr val_out = generate_new_symbol("val_out", val_out_type, 1, 0);
    val_out->set_addr(val_out_addr);

    assert(!call.extra_vars["borrowed_cell"].second.isNull());
    push_to_local(val_out, call.extra_vars["borrowed_cell"].second);

    VariableDecl_ptr val_out_decl = VariableDecl::build(val_out);
    Expr_ptr zero = Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, 0);
    exprs.push_back(Assignment::build(val_out_decl, zero));

    Type_ptr val_out_type_arg = Pointer::build(Pointer::build(
        PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID)));
    Expr_ptr val_out_arg = AddressOf::build(val_out);
    Cast_ptr val_out_cast = Cast::build(val_out_arg, val_out_type_arg);

    args = std::vector<ExpressionType_ptr>{vector, index_arg, val_out_cast};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);

    // preemptive write

    auto vector_return =
        find_vector_return_with_obj(bdd_call, call.args["vector"].expr);
    assert(vector_return && "vector_return not found after vector_borrow");

    auto vector_return_call = vector_return->get_call();
    auto before_value = call.extra_vars["borrowed_cell"].second;
    auto after_value = vector_return_call.args["value"].in;

    auto changes = apply_changes(this, val_out, before_value, after_value);

    write_attempt = changes.size();
    after_call_exprs.insert(after_call_exprs.end(), changes.begin(),
                            changes.end());
  } else if (fname == "map_put") {
    Expr_ptr map_expr = transpile(this, call.args["map"].expr);
    assert(map_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr = (static_cast<Constant *>(map_expr.get()))->get_value();

    Expr_ptr map = get_from_state(map_addr);

    auto vector_return =
        find_vector_return_with_value(bdd_call, call.args["key"].in);
    assert(vector_return &&
           "couldn't find vector_return with a key to this map_put");

    auto vector_return_call = vector_return->get_call();
    check_write_attempt = true;
    Expr_ptr vector_return_value_expr =
        transpile(this, vector_return_call.args["value"].expr);
    assert(vector_return_value_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_return_value_addr =
        (static_cast<Constant *>(vector_return_value_expr.get()))->get_value();
    Expr_ptr vector_return_value =
        get_from_local_by_addr("val_out", vector_return_value_addr);
    assert(vector_return_value);

    Expr_ptr value = transpile(this, call.args["value"].expr, true);
    assert(value);

    args = std::vector<ExpressionType_ptr>{map, vector_return_value, value};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "vector_return") {
    Expr_ptr vector_expr = transpile(this, call.args["vector"].expr);
    assert(vector_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t vector_addr =
        (static_cast<Constant *>(vector_expr.get()))->get_value();

    Expr_ptr value_expr = transpile(this, call.args["value"].expr);
    assert(value_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t value_addr =
        (static_cast<Constant *>(value_expr.get()))->get_value();

    Expr_ptr vector = get_from_state(vector_addr);
    Expr_ptr index = transpile(this, call.args["index"].expr, true);
    assert(index);
    Expr_ptr value = get_from_local_by_addr("val_out", value_addr);
    assert(value);

    Expr_ptr index_arg = index;

    if (index->get_type()->get_type_kind() == Type::TypeKind::POINTER) {
      Type_ptr int_type =
          PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
      Type_ptr index_type = Pointer::build(int_type);
      Cast_ptr index_cast = Cast::build(index, index_type);
      Expr_ptr zero =
          Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, 0);
      index_arg = Read::build(index_cast, int_type, zero);
    }

    args = std::vector<ExpressionType_ptr>{vector, index_arg, value};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "dchain_rejuvenate_index") {
    Expr_ptr chain_expr = transpile(this, call.args["chain"].expr);
    assert(chain_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chain_addr =
        (static_cast<Constant *>(chain_expr.get()))->get_value();

    Expr_ptr chain = get_from_state(chain_addr);
    assert(chain);
    Expr_ptr index = transpile(this, call.args["index"].expr);
    assert(index);
    Expr_ptr now = transpile(this, call.args["time"].expr);
    assert(now);

    now = fix_time_32_bits(now);

    args = std::vector<ExpressionType_ptr>{chain, index, now};

    // actually this is an int, but we never use it in any call path...
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "packet_return_chunk") {
    ignore = true;
    dec_pkt_offset();

    Expr_ptr chunk_expr = transpile(this, call.args["the_chunk"].expr);
    assert(chunk_expr->get_kind() == Node::NodeKind::CONSTANT);

    uint64_t chunk_addr =
        (static_cast<Constant *>(chunk_expr.get()))->get_value();

    klee::ref<klee::Expr> prev_chunk = get_expr_from_local_by_addr(chunk_addr);
    assert(!prev_chunk.isNull());

    auto eq = kutil::solver_toolbox.are_exprs_always_equal(
        prev_chunk, call.args["the_chunk"].in);

    // changes to the chunk
    if (!eq) {
      std::vector<Expr_ptr> changes =
          apply_changes_to_match(this, prev_chunk, call.args["the_chunk"].in);
      exprs.insert(exprs.end(), changes.begin(), changes.end());
    }
  } else if (fname == "rte_ether_addr_hash") {
    assert(kutil::solver_toolbox.are_exprs_always_equal(call.args["obj"].in,
                                                        call.args["obj"].out));
    Expr_ptr obj = transpile(this, call.args["obj"].in);
    assert(obj);

    args = std::vector<ExpressionType_ptr>{AddressOf::build(obj)};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = "hash";
    ret_expr = call.ret;
  } else if (fname == "dchain_is_index_allocated") {
    Expr_ptr chain_expr = transpile(this, call.args["chain"].expr);
    assert(chain_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chain_addr =
        (static_cast<Constant *>(chain_expr.get()))->get_value();

    Expr_ptr chain = get_from_state(chain_addr);
    assert(chain);
    Expr_ptr index = transpile(this, call.args["index"].expr);
    assert(index);

    Expr_ptr index_arg = index;

    if (index->get_type()->get_type_kind() == Type::TypeKind::POINTER) {
      Type_ptr int_type =
          PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
      Type_ptr index_type = Pointer::build(int_type);
      Cast_ptr index_cast = Cast::build(index, index_type);
      Expr_ptr zero =
          Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, 0);
      index_arg = Read::build(index_cast, int_type, zero);
    }

    args = std::vector<ExpressionType_ptr>{chain, index_arg};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT32_T);
    ret_symbol = get_symbol_label("dchain_is_index_allocated", symbols);
    ret_expr = call.ret;
  } else if (fname == "LoadBalancedFlow_hash") {
    auto obj = call.args["obj"].in;
    assert(!obj.isNull());

    Type_ptr obj_type = type_from_klee_expr(obj, true);
    Variable_ptr hashed_obj = generate_new_symbol("hashed_obj", obj_type);
    push_to_local(hashed_obj);

    VariableDecl_ptr hashed_obj_decl = VariableDecl::build(hashed_obj);
    exprs.push_back(hashed_obj_decl);

    auto statements = build_and_fill_byte_array(this, hashed_obj, obj);
    assert(statements.size());
    exprs.insert(exprs.end(), statements.begin(), statements.end());

    args = std::vector<ExpressionType_ptr>{hashed_obj};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);
    ret_symbol = get_symbol_label("LoadBalancedFlow_hash", symbols);
    ret_expr = call.ret;
  } else if (fname == "cht_find_preferred_available_backend") {
    Expr_ptr hash = transpile(this, call.args["hash"].expr);
    assert(hash);

    Expr_ptr cht_expr = transpile(this, call.args["cht"].expr);
    assert(cht_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t cht_addr = (static_cast<Constant *>(cht_expr.get()))->get_value();

    Expr_ptr cht = get_from_state(cht_addr);
    assert(cht);

    Expr_ptr active_backends_expr =
        transpile(this, call.args["active_backends"].expr);
    assert(active_backends_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t active_backends_addr =
        (static_cast<Constant *>(active_backends_expr.get()))->get_value();

    Expr_ptr active_backends = get_from_state(active_backends_addr);
    assert(active_backends);

    Expr_ptr cht_height = transpile(this, call.args["cht_height"].expr);
    assert(cht_height);

    Expr_ptr backend_capacity =
        transpile(this, call.args["backend_capacity"].expr);
    assert(backend_capacity);

    Variable_ptr chosen_backend =
        generate_new_symbol(call.args["chosen_backend"].out);

    Expr_ptr chosen_backend_expr =
        transpile(this, call.args["chosen_backend"].expr);
    assert(chosen_backend_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chosen_backend_addr =
        (static_cast<Constant *>(chosen_backend_expr.get()))->get_value();
    chosen_backend->set_addr(chosen_backend_addr);
    push_to_local(chosen_backend, call.args["chosen_backend"].out);

    VariableDecl_ptr chosen_backend_decl = VariableDecl::build(chosen_backend);
    Expr_ptr zero = Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, 0);
    exprs.push_back(Assignment::build(chosen_backend_decl, zero));

    args = std::vector<ExpressionType_ptr>{hash,
                                           cht,
                                           active_backends,
                                           cht_height,
                                           backend_capacity,
                                           AddressOf::build(chosen_backend)};

    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT32_T);
    ret_symbol = get_symbol_label("prefered_backend_found", symbols);
    ret_expr = call.ret;
  } else if (fname == "nf_set_rte_ipv4_udptcp_checksum") {
    Expr_ptr ip_header_expr = transpile(this, call.args["ip_header"].expr);
    assert(ip_header_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t ip_header_addr =
        (static_cast<Constant *>(ip_header_expr.get()))->get_value();

    Expr_ptr l4_header_expr = transpile(this, call.args["l4_header"].expr);
    assert(l4_header_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t l4_header_addr =
        (static_cast<Constant *>(l4_header_expr.get()))->get_value();

    Expr_ptr ip_header = get_from_local_by_addr("rte_ipv4_hdr", ip_header_addr);
    assert(ip_header);
    Expr_ptr l4_header = get_from_local_by_addr("tcpudp_hdr", l4_header_addr);
    assert(l4_header);
    Expr_ptr packet = get_from_local("p");
    assert(packet);

    fname = "rte_ipv4_udptcp_cksum";
    args = std::vector<ExpressionType_ptr>{ip_header, l4_header};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    ret_symbol = get_symbol_label("checksum", symbols);
  } else if (fname == "map_erase") {
    check_write_attempt = true;

    Expr_ptr map_expr = transpile(this, call.args["map"].expr);
    assert(map_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t map_addr = (static_cast<Constant *>(map_expr.get()))->get_value();

    Expr_ptr map = get_from_state(map_addr);

    Type_ptr key_type = type_from_klee_expr(call.args["key"].in, true);
    Variable_ptr key = generate_new_symbol("map_key", key_type);
    push_to_local(key);

    VariableDecl_ptr key_decl = VariableDecl::build(key);
    exprs.push_back(key_decl);

    auto statements = build_and_fill_byte_array(this, key, call.args["key"].in);
    assert(statements.size());
    exprs.insert(exprs.end(), statements.begin(), statements.end());

    Variable_ptr trash = generate_new_symbol("trash", key_type);
    push_to_local(trash);

    VariableDecl_ptr trash_decl = VariableDecl::build(trash);
    exprs.push_back(trash_decl);

    Type_ptr trash_type_arg = Pointer::build(Pointer::build(
        PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID)));
    Expr_ptr trash_arg = AddressOf::build(trash);
    Cast_ptr trash_cast = Cast::build(trash_arg, trash_type_arg);

    args =
        std::vector<ExpressionType_ptr>{map, AddressOf::build(key), trash_cast};
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else if (fname == "dchain_free_index") {
    check_write_attempt = true;

    Expr_ptr chain_expr = transpile(this, call.args["chain"].expr);
    assert(chain_expr->get_kind() == Node::NodeKind::CONSTANT);
    uint64_t chain_addr =
        (static_cast<Constant *>(chain_expr.get()))->get_value();

    Expr_ptr chain = get_from_state(chain_addr);
    assert(chain);
    Expr_ptr index = transpile(this, call.args["index"].expr);
    assert(index);

    args = std::vector<ExpressionType_ptr>{chain, index};

    // actually this is an int, but we never use it in any call path...
    ret_type = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
  } else {
    std::cerr << call.function_name << "\n";

    for (const auto &arg : call.args) {
      std::cerr << arg.first << " : " << kutil::expr_to_string(arg.second.expr)
                << "\n";
      if (!arg.second.in.isNull()) {
        std::cerr << "  in:  " << kutil::expr_to_string(arg.second.in) << "\n";
      }
      if (!arg.second.out.isNull()) {
        std::cerr << "  out: " << kutil::expr_to_string(arg.second.out) << "\n";
      }
    }

    for (const auto &ev : call.extra_vars) {
      std::cerr << ev.first << " : " << kutil::expr_to_string(ev.second.first)
                << " | " << kutil::expr_to_string(ev.second.second) << "\n";
    }

    std::cerr << kutil::expr_to_string(call.ret) << "\n";

    assert(false && "Not implemented");
  }

  fname = translate_fname(fname, target);

  if (!ignore) {
    assert(call.function_name != fname || args.size() == call.args.size());
    FunctionCall_ptr fcall = FunctionCall::build(fname, args, ret_type);

    bool is_void = false;

    if (ret_type->get_type_kind() == Type::TypeKind::PRIMITIVE) {
      PrimitiveType *primitive = static_cast<PrimitiveType *>(ret_type.get());
      is_void =
          primitive->get_primitive_kind() == PrimitiveType::PrimitiveKind::VOID;
    }

    if (!is_void && ret_symbol.size()) {
      Variable_ptr ret_var;

      if (counter_begins >= 0) {
        ret_var = generate_new_symbol(ret_symbol, ret_type, 0, counter_begins);
      } else {
        ret_var = Variable::build(ret_symbol, ret_type);
      }

      if (!ret_expr.isNull()) {
        push_to_local(ret_var, ret_expr);
      } else {
        push_to_local(ret_var);
      }

      if (ret_addr.first) {
        ret_var->set_addr(ret_addr.second);
      }

      VariableDecl_ptr ret = VariableDecl::build(ret_var);
      Assignment_ptr assignment;

      // hack!
      if (ret_symbol.find("out_of_space") != std::string::npos) {
        assignment = Assignment::build(ret, Not::build(fcall));
      } else {
        assignment = Assignment::build(ret, fcall);
      }

      exprs.push_back(assignment);
    } else {
      exprs.push_back(fcall);
    }
  }

  exprs.insert(exprs.end(), after_call_exprs.begin(), after_call_exprs.end());

  for (auto expr : exprs) {
    expr->set_terminate_line(true);
    expr->set_wrap(false);
  }

  if (exprs.size() == 0) {
    return nullptr;
  }

  std::vector<Node_ptr> nodes;

  if (target == LOCKS && write_attempt) {
    nodes.push_back(AST::write_attempt());
  }

  nodes.insert(nodes.end(), exprs.begin(), exprs.end());

  if (target == LOCKS && check_write_attempt) {
    nodes.push_back(AST::check_write_attempt());
  }

  return Block::build(nodes, false);
}

void AST::push() {
  local_variables.emplace_back();
  layer.push_back(layer.back());

  if (pkt_buffer_offset.size()) {
    pkt_buffer_offset.push(pkt_buffer_offset.top());
  } else {
    pkt_buffer_offset.emplace();
  }
}

void AST::pop() {
  assert(local_variables.size() > 0);
  local_variables.pop_back();

  assert(layer.size() > 1);
  layer.pop_back();

  assert(pkt_buffer_offset.size() > 0);
  pkt_buffer_offset.pop();
}

Node_ptr AST::node_from_call(const BDD::Call *bdd_call, TargetOption target) {
  switch (context) {
  case INIT:
    return init_state_node_from_call(bdd_call, target);
  case PROCESS:
    return process_state_node_from_call(bdd_call, target);
  case DONE:
    assert(false);
  }

  return nullptr;
}

void AST::context_switch(Context ctx) {
  context = ctx;

  switch (context) {
  case INIT:
    push();
    break;

  case PROCESS: {
    pop();
    push();

    std::vector<VariableDecl_ptr> args{
        VariableDecl::build(
            from_cp_symbol("src_devices"),
            PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
        VariableDecl::build(from_cp_symbol("p"),
                            Pointer::build(PrimitiveType::build(
                                PrimitiveType::PrimitiveKind::UINT8_T))),
        VariableDecl::build(
            from_cp_symbol("pkt_len"),
            PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
        VariableDecl::build(
            from_cp_symbol("now"),
            PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T))};

    for (const auto &arg : args) {
      push_to_local(Variable::build(arg->get_symbol(), arg->get_type()));
    }

    break;
  }

  case DONE:
    pop();
    break;
  }
}

void AST::commit(Node_ptr body) {
  Block_ptr _body = Block::build(body);

  switch (context) {
  case INIT: {
    std::vector<FunctionArgDecl_ptr> _args;
    Type_ptr _return = PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL);

    nf_init = Function::build("nf_init", _args, _body, _return);

    context_switch(PROCESS);
    break;
  }

  case PROCESS: {
    std::vector<FunctionArgDecl_ptr> _args{
        FunctionArgDecl::build(
            from_cp_symbol("src_devices"),
            PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
        FunctionArgDecl::build(from_cp_symbol("p"),
                               Pointer::build(PrimitiveType::build(
                                   PrimitiveType::PrimitiveKind::UINT8_T))),
        FunctionArgDecl::build(
            from_cp_symbol("pkt_len"),
            PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT16_T)),
        FunctionArgDecl::build(
            from_cp_symbol("now"),
            PrimitiveType::build(PrimitiveType::PrimitiveKind::INT64_T)),
    };

    Type_ptr _return = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);

    nf_process = Function::build("nf_process", _args, _body, _return);

    context_switch(DONE);
    break;
  }

  case DONE:
    assert(false);
  }
}