#include "builder.hpp"
#include "../pch.hpp"
#include "../util/logger.hpp"

#include "bdd/nodes/node.h"
#include "bdd/nodes/return_init.h"
#include "bdd/nodes/symbol.h"
#include "klee/Constraints.h"
#include "retrieve_symbols.h"
#include "solver_toolbox.h"

using namespace BDD;

namespace Clone {

	/* Constructors and destructors */

	Builder::Builder(unique_ptr<BDD::BDD> bdd): bdd(move(bdd)) {
	}

	Builder::~Builder() = default;

	/* Private methods */

	Tails Builder::clone_node(BDD::BDDNode_ptr root, unsigned input_port) {
		assert(root);

		stack<BDDNode_ptr> s;

		Tails tails;
		s.push(root);

		while(!s.empty()) {
			auto curr = s.top();
			s.pop();

			//curr->update_id(counter++);

			switch(curr->get_type()) {
				case Node::NodeType::BRANCH: {
					auto branch { static_cast<Branch*>(curr.get()) };
					assert(branch->get_on_true());
					assert(branch->get_on_false());

					auto prev = branch->get_prev();
					auto next_true = branch->get_on_true()->clone();
					auto next_false = branch->get_on_false()->clone();
					branch->disconnect();
					branch->add_prev(prev);

					const auto &condition_symbols { kutil::get_symbols(branch->get_condition()) };

					if (condition_symbols.size() == 1 && *condition_symbols.begin() == "VIGOR_DEVICE") {
						auto vigor_device { kutil::solver_toolbox.create_new_symbol("VIGOR_DEVICE", 32) };
						auto port { kutil::solver_toolbox.exprBuilder->Constant(input_port, vigor_device->getWidth()) };
						auto eq { kutil::solver_toolbox.exprBuilder->Eq(vigor_device, port) };
						auto cm = branch->get_constraints();
						cm.addConstraint(eq);
						branch->set_constraints(cm);
					}

					auto condition = branch->get_condition();

					bool maybe_true = kutil::solver_toolbox.is_expr_maybe_true(branch->get_constraints(), branch->get_condition()) ;
					bool maybe_false = kutil::solver_toolbox.is_expr_maybe_false(branch->get_constraints(), branch->get_condition()) ;
					
					if(!maybe_true) {
						//debug("Trimming branch ", curr->get_id(), " to false branch");
						trim_node(curr, next_false);
						s.push(next_false);
					}
					else if(!maybe_false) {
						//info("Trimming branch ", curr->get_id(), " to true branch");
						trim_node(curr, next_true);
						s.push(next_true);
					}
					else {
						branch->add_on_true(next_true);
						next_true->replace_prev(curr);
						s.push(next_true);

						branch->add_on_false(next_false);
						next_false->replace_prev(curr);
						s.push(next_false);
					}

					break;
				}
				case Node::NodeType::CALL: {
					auto call { static_cast<Call*>(curr.get()) };
					assert(call->get_next());

					auto next = call->get_next()->clone();
					call->replace_next(next);
					next->replace_prev(curr);
					s.push(next);

					break;
				}
				case Node::NodeType::RETURN_INIT: {
					auto ret { static_cast<ReturnInit*>(curr.get()) };

					if(ret->get_return_value() == ReturnInit::ReturnType::SUCCESS) {
						init_tail = curr;
					}
					break;
				}
				case Node::NodeType::RETURN_PROCESS: {
					auto ret = static_cast<ReturnProcess*>(curr.get()) ;

					if(ret->get_return_operation() == ReturnProcess::Operation::FWD) {
						tails.push_back(curr);
					}

					break;
				}
				case Node::NodeType::RETURN_RAW: {
					auto ret { static_cast<ReturnRaw*>(curr.get()) };
					
					break;
				}
			}
		}

		return tails;
	}

	void Builder::trim_node(BDD::BDDNode_ptr curr, BDD::BDDNode_ptr next) {
		auto prev = curr->get_prev();

		if(prev->get_type() == Node::NodeType::BRANCH) {
			auto branch { static_cast<Branch*>(prev.get()) };
			auto on_true = branch->get_on_true();
			auto on_false = branch->get_on_false();

			if (on_true->get_id() == curr->get_id()) {
				branch->replace_on_true(next);
				next->replace_prev(prev);
				assert(branch->get_on_true()->get_prev()->get_id() == prev->get_id());
			}
			else if (on_false->get_id() == curr->get_id()) {
				branch->replace_on_false(next);
				next->replace_prev(prev);
				assert(branch->get_on_false()->get_prev()->get_id() == prev->get_id());
			}
			else {
				danger("Could not trim branch ", prev->get_id(), " to leaf ", curr->get_id());
			}
		}
		else {
			prev->replace_next(next);
			next->replace_prev(prev);
		}
	}

	/* Static methods */

	unique_ptr<Builder> Builder::create() {
		auto builder { new Builder(unique_ptr<BDD::BDD>(new BDD::BDD())) };
		return unique_ptr<Builder>(move(builder));
	}

	/* Public methods */
	bool Builder::is_init_empty() const {
		return bdd->get_init() == nullptr;
	}

	bool Builder::is_process_empty() const {
		return bdd->get_process() == nullptr;
	}

	void Builder::add_process_branch(unsigned input_port) {
		klee::ConstraintManager cm {};
		auto vigor_device = kutil::solver_toolbox.create_new_symbol("VIGOR_DEVICE", 32);
		auto port = kutil::solver_toolbox.exprBuilder->Constant(input_port, vigor_device->getWidth()) ;
		auto eq = kutil::solver_toolbox.exprBuilder->Eq(vigor_device, port) ;
		auto node = BDD::BDDNode_ptr(new BDD::Branch(counter++, cm, eq));
		auto return_drop = BDD::BDDNode_ptr(new BDD::ReturnProcess(counter++, node, {}, 0, BDD::ReturnProcess::Operation::DROP));
		auto return_fwd = BDD::BDDNode_ptr(new BDD::ReturnProcess(counter++, node, {}, 0, BDD::ReturnProcess::Operation::FWD));
		auto branch { static_cast<Branch*>(node.get()) };

		branch->add_on_false(return_drop);
		branch->add_on_true(return_fwd);

		if(process_root != nullptr) {
			assert(process_root->get_type() == Node::NodeType::BRANCH);
			auto root_branch { static_cast<Branch*>(process_root.get()) };
			root_branch->replace_on_false(node);
		}

		process_root = branch->get_on_true();
		assert(process_root != nullptr);

		if(is_process_empty()) {
			bdd->set_process(node);
			assert(bdd->get_process() != nullptr);
		}
	}

	void Builder::join_init(const shared_ptr<NF> &nf) {
		if(merged_inits.find(nf->get_id()) != merged_inits.end()) {
			return;
		}

		auto init_new = nf->get_bdd()->get_init()->clone(true);
		init_new->recursive_update_ids(counter);

		if(is_init_empty()) {
			bdd->set_init(init_new);
		}
		else {
			assert(init_tail != nullptr);

			trim_node(init_tail, init_new);
		}

 		clone_node(init_new, 0); //TODO: check if this is correct		
		merged_inits.insert(nf->get_id());
	}

	Tails Builder::join_process(const shared_ptr<NF> &nf, unsigned port, const BDD::BDDNode_ptr &tail) {
		assert(process_root != nullptr);

		auto root = nf->get_bdd()->get_process()->clone(true);
		root->recursive_update_ids(counter);
		trim_node(tail, root);

		return clone_node(root, port);
	}

	const std::unique_ptr<BDD::BDD>& Builder::get_bdd() const {
		return this->bdd;
	}

	BDD::BDDNode_ptr Builder::get_process_root() const {
		return this->process_root;
	}

	void Builder::dump(std::string path) {
		debug("Dumping BDD to file");
		this->bdd->serialize(path);
	}
}