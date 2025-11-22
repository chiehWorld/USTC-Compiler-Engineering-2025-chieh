#include "DeadCode.hpp"
#include "Instruction.hpp"
#include "logging.hpp"
#include <memory>
#include <vector>

// 处理流程：两趟处理，mark 标记有用变量，sweep 删除无用指令
void DeadCode::run() {
    bool changed{};
    func_info->run();
    do {
        changed = false;
        for (auto &F : m_->get_functions()) {
            auto func = &F;
            changed |= clear_basic_blocks(func);
            mark(func);

            // 删除指令后，可能会导致其他指令的操作数变为无用，因此需要再次遍历函数的基本块
            // 需要多次sweep，直到没有指令被删除为止
            changed |= sweep(func);
        }
    } while (changed);
    LOG_INFO << "dead code pass erased " << ins_count << " instructions";
}

bool DeadCode::clear_basic_blocks(Function *func) {
    bool changed = 0;
    std::vector<BasicBlock *> to_erase;
    for (auto &bb1 : func->get_basic_blocks()) {
        auto bb = &bb1;
        if(bb->get_pre_basic_blocks().empty() && bb != func->get_entry_block()) {
            to_erase.push_back(bb);
            changed = 1;
        }
    }
    for (auto &bb : to_erase) {
        bb->erase_from_parent();
        delete bb;
    }
    return changed;
}

void DeadCode::mark(Function *func) {
    work_list.clear();
    marked.clear();

    for (auto &bb : func->get_basic_blocks()) {
        for (auto &ins : bb.get_instructions()) {
            if (is_critical(&ins)) {
                marked[&ins] = true;
                work_list.push_back(&ins);
            }
        }
    }

    while (work_list.empty() == false) {
        auto now = work_list.front();
        work_list.pop_front();
        mark(now);
    }
}

void DeadCode::mark(Instruction *ins) {
    for (auto op : ins->get_operands()) {
        auto def = dynamic_cast<Instruction *>(op);
        if (def == nullptr) continue;
        if (marked[def]) continue;
        if (def->get_function() != ins->get_function()) continue;
        marked[def] = true;
        work_list.push_back(def);
    }
}

bool DeadCode::sweep(Function *func) {
    // 删除无用指令
    std::unordered_set<Instruction *> wait_del{};

    for (auto &bb : func->get_basic_blocks()) {
        for (auto it = bb.get_instructions().begin(); it != bb.get_instructions().end();) {
            // 删除所有标记为true的指令，如果指令是有用的，则跳过
            if (marked[&*it]) {
                ++it;
                continue;
            } else {
                // 这里不能直接删除指令，因为正在遍历的指令可能会被删除，将其加入待删除列表
                auto tmp = &*it;
                wait_del.insert(tmp);
                it++;
            }
        }
    }

    // 执行删除
    for (auto inst : wait_del) {
        // 先删除操作数的引用
        inst->remove_all_operands();
    }
    for (auto inst : wait_del) {
        // 然后再删除指令本身
        inst->get_parent()->get_instructions().erase(inst);
    }
    ins_count += wait_del.size();
  
    // 如果删除了指令，返回true，否则返回false
    return not wait_del.empty(); // changed
}

bool DeadCode::is_critical(Instruction *ins) {
    // 判断指令是否是无用指令
    if (ins->is_call()) {
        auto call_inst = dynamic_cast<CallInst *>(ins);
        auto callee = dynamic_cast<Function *>(call_inst->get_operand(0));
        if (func_info->is_pure_function(callee))
            // 如果是函数调用，且函数是纯函数，则无用
            return false;
        return true;
    }
    if (ins->is_br() || ins->is_ret())
        // 如果是无用的分支指令，则无用
        // 如果是无用的返回指令，则无用
        return true;
    if (ins->is_store())
        // 如果是无用的存储指令，则无用
        return true;
    return false;
    
}

void DeadCode::sweep_globally() {
    std::vector<Function *> unused_funcs;
    std::vector<GlobalVariable *> unused_globals;
    for (auto &f_r : m_->get_functions()) {
        if (f_r.get_use_list().size() == 0 and f_r.get_name() != "main")
            unused_funcs.push_back(&f_r);
    }
    for (auto &glob_var_r : m_->get_global_variable()) {
        if (glob_var_r.get_use_list().size() == 0)
            unused_globals.push_back(&glob_var_r);
    }
    // changed |= unused_funcs.size() or unused_globals.size();
    for (auto func : unused_funcs)
        m_->get_functions().erase(func);
    for (auto glob : unused_globals)
        m_->get_global_variable().erase(glob);
}
