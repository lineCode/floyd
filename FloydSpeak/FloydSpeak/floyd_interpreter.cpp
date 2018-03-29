//
//  parser_evaluator.cpp
//  FloydSpeak
//
//  Created by Marcus Zetterquist on 26/07/16.
//  Copyright © 2016 Marcus Zetterquist. All rights reserved.
//

#include "floyd_interpreter.h"

#include "parser_primitives.h"
#include "floyd_parser.h"
#include "ast_value.h"
#include "pass2.h"
#include "pass3.h"
#include "bytecode_gen.h"
#include "json_support.h"
#include "json_parser.h"

#include <array>
#include <cmath>
#include <sys/time.h>

#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <fstream>
#include "text_parser.h"
#include "host_functions.hpp"

namespace floyd {

using std::vector;
using std::string;
using std::pair;
using std::shared_ptr;
using std::make_shared;




BC_INLINE const typeid_t& get_type(const interpreter_t& vm, const bc_typeid_t& type){
	return vm._imm->_program._types[type];
}
BC_INLINE const base_type get_basetype(const interpreter_t& vm, const bc_typeid_t& type){
	return vm._imm->_program._types[type].get_base_type();
}

statement_result_t execute_body(interpreter_t& vm, const bc_body_optimized_t& body, const bc_pod_value_t* init_values, int init_value_count);


//	Will NOT bump RCs of init_values.
//	Returns new frame-pos, same as vm._current_stack_frame.
int open_stack_frame2_nobump(interpreter_t& vm, const bc_body_optimized_t& body, const bc_pod_value_t* init_values, int init_value_count){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(body.check_invariant());

	const auto new_frame_start = static_cast<int>(vm._value_stack.size());
	const auto prev_frame_pos = vm._current_stack_frame;
	vm._value_stack.push_intq(prev_frame_pos);
	const auto new_frame_pos = new_frame_start + 1;
	vm._current_stack_frame = new_frame_pos;

	if(init_value_count > 0){
		vm._value_stack.push_values_no_rc_bump(init_values, init_value_count);
	}

	//??? Make precomputed stuff in bc_body_t. Use memcpy() + a GC-fixup loop.
	for(vector<bc_value_t>::size_type i = init_value_count ; i < body._body._symbols.size() ; i++){
		const auto& symbol = body._body._symbols[i];
		bool is_ext = body._exts[i];

		//	Variable slot.
		//	This is just a variable slot without constant. We need to put something there, but that don't confuse RC.
		//	Problem is that IF this is an RC_object, it WILL be decremented when written to using replace_value_same_type_SLOW().
		//	Use a placeholder object. Type won't match symbol but that's OK.
		if(symbol.second._const_value.get_basetype() == base_type::k_internal_undefined){
			if(is_ext){
				vm._value_stack.push_value(vm._internal_placeholder_object, true);
			}
			else{
				vm._value_stack.push_intq(0xdeadbeef);
			}
		}

		//	Constant.
		else{
			vm._value_stack.push_value(value_to_bc(symbol.second._const_value), is_ext);
		}
	}
	return new_frame_pos;
}

//	Pops entire stack frame -- all locals etc.
//	Restores previous stack frame pos.
//	Returns resulting stack frame pos.
//	Decrements all stack frame object RCs.
int close_stack_frame(interpreter_t& vm, const bc_body_optimized_t& body){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(body.check_invariant());

	const auto current_pos = vm._current_stack_frame;
	const auto prev_frame_pos = vm._value_stack.load_intq(current_pos - 1);
	const auto prev_frame_end_pos = current_pos - 1;

	//	Using symbol table to figure out which stack-frame values needs RC. Decrement them all.
	for(int i = 0 ; i < body._exts.size() ; i++){
		if(body._exts[i]){
			bc_value_t::debump(vm._value_stack._value_stack[vm._current_stack_frame + i]);
		}
	}
	vm._value_stack._value_stack.resize(prev_frame_end_pos);


	vm._current_stack_frame = prev_frame_pos;
	return prev_frame_pos;
}

//	#0 is top of stack, last elementis bottom.
//	first: frame_pos, second: framesize-1. Does not include the first slot, which is the prev_frame_pos.
vector<std::pair<int, int>> get_stack_frames(const interpreter_t& vm){
	QUARK_ASSERT(vm.check_invariant());

	int frame_pos = vm._current_stack_frame;

	//	We use the entire current stack to calc top frame's size. This can be wrong, if someone pushed more stuff there. Same goes with the previous stack frames too..
	vector<std::pair<int, int>> result{ { frame_pos, static_cast<int>(vm._value_stack.size()) - frame_pos }};

	while(frame_pos > 1){
		const auto prev_frame_pos = vm._value_stack.load_intq(frame_pos - 1);
		const auto prev_size = (frame_pos - 1) - prev_frame_pos;
		result.push_back(std::pair<int, int>{ frame_pos, prev_size });

		frame_pos = prev_frame_pos;
	}
	return result;
}

/*
typeid_t find_type_by_name(const interpreter_t& vm, const typeid_t& type){
	if(type.get_base_type() == base_type::k_internal_unresolved_type_identifier){
		const auto v = find_symbol_by_name(vm, type.get_unresolved_type_identifier());
		if(v){
			if(v->_symbol._value_type.is_typeid()){
				return v->_value.get_typeid_value();
			}
			else{
				return typeid_t::make_undefined();
			}
		}
		else{
			return typeid_t::make_undefined();
		}
	}
	else{
		return type;
	}
}
*/


/*
//	Warning: returns reference to the found value-entry -- this could be in any environment in the call stack.
std::shared_ptr<value_entry_t> find_symbol_by_name_deep(const interpreter_t& vm, int depth, const std::string& s){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(depth >= 0 && depth < vm._call_stack.size());
	QUARK_ASSERT(s.size() > 0);

	const auto& env = &vm._call_stack[depth];
    const auto& it = std::find_if(
    	env->_body_ptr->_symbols.begin(),
    	env->_body_ptr->_symbols.end(),
    	[&s](const std::pair<std::string, symbol_t>& e) { return e.first == s; }
	);
	if(it != env->_body_ptr->_symbols.end()){
		const auto index = it - env->_body_ptr->_symbols.begin();
		const auto pos = env->_values_offset + index;
		QUARK_ASSERT(pos >= 0 && pos < vm._value_stack.size());

		//	Assumes we are scanning from the top of the stack.
		int parent_steps = static_cast<int>(vm._call_stack.size() - 1 - depth);

		const auto value_entry = value_entry_t{
			vm._value_stack[pos],
			it->first,
			it->second,
			variable_address_t::make_variable_address(parent_steps, static_cast<int>(index))
		};
		return make_shared<value_entry_t>(value_entry);
	}
	else if(depth > 0){
		return find_symbol_by_name_deep(vm, depth - 1, s);
	}
	else{
		return nullptr;
	}
}

//	Warning: returns reference to the found value-entry -- this could be in any environment in the call stack.
std::shared_ptr<value_entry_t> find_symbol_by_name(const interpreter_t& vm, const std::string& s){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(s.size() > 0);

	return find_symbol_by_name_deep(vm, (int)(vm._call_stack.size() - 1), s);
}
*/

std::shared_ptr<value_entry_t> find_global_symbol2(const interpreter_t& vm, const std::string& s){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(s.size() > 0);

	const auto& symbols = vm._imm->_program._globals._symbols;
    const auto& it = std::find_if(
    	symbols.begin(),
    	symbols.end(),
    	[&s](const std::pair<std::string, symbol_t>& e) { return e.first == s; }
	);
	if(it != symbols.end()){
		const auto index = static_cast<int>(it - symbols.begin());
		const auto pos = 1 + index;
		QUARK_ASSERT(pos >= 0 && pos < vm._value_stack.size());

		const auto value_entry = value_entry_t{
			vm._value_stack.load_value_slow(pos, it->second._value_type),
			it->first,
			it->second,
			variable_address_t::make_variable_address(-1, static_cast<int>(index))
		};
		return make_shared<value_entry_t>(value_entry);
	}
	else{
		return nullptr;
	}
}
floyd::value_t find_global_symbol(const interpreter_t& vm, const string& s){
	QUARK_ASSERT(vm.check_invariant());

	return get_global(vm, s);
}
value_t get_global(const interpreter_t& vm, const std::string& name){
	QUARK_ASSERT(vm.check_invariant());

	const auto& result = find_global_symbol2(vm, name);
	if(result == nullptr){
		throw std::runtime_error("Cannot find global.");
	}
	else{
		return bc_to_value(result->_value, result->_symbol._value_type);
	}
}

BC_INLINE const bc_function_definition_t& get_function_def(const interpreter_t& vm, const floyd::bc_value_t& v){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(v.check_invariant());

	const auto function_id = v.get_function_value();
	QUARK_ASSERT(function_id >= 0 && function_id < vm._imm->_program._function_defs.size())

	const auto& function_def = vm._imm->_program._function_defs[function_id];
	return function_def;
}

BC_INLINE int find_frame_from_address(const interpreter_t& vm, int parent_step){
	QUARK_ASSERT(vm.check_invariant());

	if(parent_step == 0){
		return vm._current_stack_frame;
	}
	else if(parent_step == -1){
		//	Address 0 holds dummy prevstack for globals.
		return 1;
	}
	else{
		int frame_pos = vm._current_stack_frame;
		for(auto i = 0 ; i < parent_step ; i++){
			frame_pos = vm._value_stack.load_intq(frame_pos - 1);
		}
		return frame_pos;
	}
}


//??? split into one-argument and multi-argument opcodes.
bc_value_t construct_value_from_typeid(interpreter_t& vm, const typeid_t& type, const typeid_t& arg0_type, const vector<bc_value_t>& arg_values){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(type.check_invariant());

	if(type.is_json_value()){
		QUARK_ASSERT(arg_values.size() == 1);

		const auto& arg0 = arg_values[0];
		const auto arg = bc_to_value(arg0, arg0_type);
		const auto value = value_to_ast_json(arg, json_tags::k_plain);
		return bc_value_t::make_json_value(value._value);
	}
	else if(type.is_bool() || type.is_int() || type.is_float() || type.is_string() || type.is_typeid()){
		QUARK_ASSERT(arg_values.size() == 1);

		const auto& arg = arg_values[0];
		if(type.is_string()){
			if(arg0_type.is_json_value() && arg.get_json_value().is_string()){
				return bc_value_t::make_string(arg.get_json_value().get_string());
			}
			else if(arg0_type.is_string()){
			}
		}
		else{
			if(arg0_type != type){
			}
		}
		return arg;
	}
	else if(type.is_struct()){
/*
	#if DEBUG
		const auto def = type.get_struct_ref();
		QUARK_ASSERT(arg_values.size() == def->_members.size());

		for(int i = 0 ; i < def->_members.size() ; i++){
			const auto v = arg_values[i];
			const auto a = def->_members[i];
			QUARK_ASSERT(v.check_invariant());
			QUARK_ASSERT(v.get_type().get_base_type() != base_type::k_internal_unresolved_type_identifier);
			QUARK_ASSERT(v.get_type() == a._type);
		}
	#endif
*/
		const auto instance = bc_value_t::make_struct_value(type, arg_values);
		QUARK_TRACE(to_compact_string2(instance));

		return instance;
	}
	else if(type.is_vector()){
		const auto& element_type = type.get_vector_element_type();
		QUARK_ASSERT(element_type.is_undefined() == false);

		return bc_value_t::make_vector_value(element_type, arg_values);
	}
	else if(type.is_dict()){
		const auto& element_type = type.get_dict_value_type();
		QUARK_ASSERT(element_type.is_undefined() == false);

		std::map<string, bc_value_t> m;
		for(auto i = 0 ; i < arg_values.size() / 2 ; i++){
			const auto& key = arg_values[i * 2 + 0].get_string_value();
			const auto& value = arg_values[i * 2 + 1];
			m.insert({ key, value });
		}
		return bc_value_t::make_dict_value(element_type, m);
	}
	else if(type.is_function()){
	}
	else if(type.is_unresolved_type_identifier()){
	}
	else{
	}

	QUARK_ASSERT(false);
	throw std::exception();
}

value_t call_function(interpreter_t& vm, const floyd::value_t& f, const vector<value_t>& args){
#if DEBUG
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(f.check_invariant());
	for(const auto i: args){ QUARK_ASSERT(i.check_invariant()); };
	QUARK_ASSERT(f.is_function());
#endif

	const auto& function_def = get_function_def(vm, value_to_bc(f));
	if(function_def._host_function_id != 0){
		const auto& r = call_host_function(vm, function_def._host_function_id, args);
		return r;
	}
	else{
#if DEBUG
		const auto& arg_types = f.get_type().get_function_args();

		//	arity
		QUARK_ASSERT(args.size() == arg_types.size());

		for(int i = 0 ; i < args.size() ; i++){
			if(args[i].get_type() != arg_types[i]){
				QUARK_ASSERT(false);
			}
		}
#endif

		std::vector<bc_pod_value_t> arg_internals;
		for(int i = 0 ; i < args.size() ; i++){
			const auto bc = value_to_bc(args[i]);
			bool is_ext = function_def._body._exts[i];
			if(is_ext){
				bc._pod._ext->_rc++;
			}
			arg_internals.push_back(bc._pod);
		}
		const auto& r = execute_body(vm, function_def._body, &arg_internals[0], static_cast<int>(arg_internals.size()));
		return bc_to_value(r._output, f.get_type().get_function_return());
	}
}



//////////////////////////////////////////		STATEMENTS

QUARK_UNIT_TEST("", "", "", ""){
	const auto s = sizeof(bc_instruction_t);
	QUARK_UT_VERIFY(s == 64);
}



QUARK_UNIT_TEST("", "", "", ""){
	const auto value_size = sizeof(bc_value_t);

/*
	QUARK_UT_VERIFY(value_size == 16);
	QUARK_UT_VERIFY(expression_size == 40);
	QUARK_UT_VERIFY(e_count_offset == 4);
	QUARK_UT_VERIFY(e_offset == 8);
	QUARK_UT_VERIFY(value_offset == 16);
*/



//	QUARK_UT_VERIFY(sizeof(temp) == 56);
}

QUARK_UNIT_TEST("", "", "", ""){
	const auto s = sizeof(variable_address_t);
	QUARK_UT_VERIFY(s == 8);
}
QUARK_UNIT_TEST("", "", "", ""){
	const auto s = sizeof(bc_value_t);
//	QUARK_UT_VERIFY(s == 16);
}


int resolve_register(const interpreter_t& vm, const variable_address_t& reg){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto frame_pos = find_frame_from_address(vm, reg._parent_steps);
	const auto pos = frame_pos + reg._index;
	return pos;
}

bc_value_t read_register_slow(const interpreter_t& vm, const variable_address_t& reg, const typeid_t& type){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value = vm._value_stack.load_value_slow(pos, type);
	return value;
}
bc_value_t read_register_slow(const interpreter_t& vm, const variable_address_t& reg, bc_typeid_t itype){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto type = get_type(vm, itype);
	const auto pos = resolve_register(vm, reg);
	const auto value = vm._value_stack.load_value_slow(pos, type);
	return value;
}
bc_value_t read_register_obj(const interpreter_t& vm, const variable_address_t& reg){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	return vm._value_stack.load_obj(pos);
}
std::string read_register_string(const interpreter_t& vm, const variable_address_t& reg){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value = vm._value_stack.load_obj(pos);
	return value.get_string_value();
}
bool read_register_bool(const interpreter_t& vm, const variable_address_t& reg){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	return vm._value_stack.load_inline_value(pos).get_bool_value();
}
int read_register_int(const interpreter_t& vm, const variable_address_t& reg){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	return vm._value_stack.load_intq(pos);
}
bc_value_t read_register_function(const interpreter_t& vm, const variable_address_t& reg){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value = vm._value_stack.load_inline_value(pos);
	return value;
}
void write_register_slow(interpreter_t& vm, const variable_address_t& reg, const bc_value_t& value, const typeid_t& type){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());
	QUARK_ASSERT(value.check_invariant());

	const auto pos = resolve_register(vm, reg);
	vm._value_stack.replace_value_same_type_SLOW(pos, value, type);
}
void write_register_slow(interpreter_t& vm, const variable_address_t& reg, const bc_value_t& value, bc_typeid_t itype){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());
	QUARK_ASSERT(value.check_invariant());

	const auto type = get_type(vm, itype);
	write_register_slow(vm, reg, value, type);
}

void write_register_bool(interpreter_t& vm, const variable_address_t& reg, bool value){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value2 = bc_value_t::make_bool(value);
	vm._value_stack.replace_inline(pos, value2);
}
void write_register_int(interpreter_t& vm, const variable_address_t& reg, int value){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value2 = bc_value_t::make_int(value);
	vm._value_stack.replace_inline(pos, value2);
}
void write_register_float(interpreter_t& vm, const variable_address_t& reg, float value){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value2 = bc_value_t::make_float(value);
	vm._value_stack.replace_inline(pos, value2);
}
void write_register_string(interpreter_t& vm, const variable_address_t& reg, const std::string& value){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(reg.check_invariant());

	const auto pos = resolve_register(vm, reg);
	const auto value2 = bc_value_t::make_string(value);
	vm._value_stack.replace_obj(pos, value2);
}



void execute_resolve_member_expression(interpreter_t& vm, const bc_instruction_t& expr){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(expr.check_invariant());

	const auto& parent_value = read_register_obj(vm, expr._reg2);
	const auto& member_index = expr._reg3._index;

	const auto& struct_instance = parent_value.get_struct_value();
	QUARK_ASSERT(member_index != -1);

	const bc_value_t value = struct_instance[member_index];
	write_register_slow(vm, expr._reg1, value, expr._instr_type);
}

void execute_lookup_element_expression(interpreter_t& vm, const bc_instruction_t& expr){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(expr.check_invariant());

	const auto& parent_value = read_register_obj(vm, expr._reg2);
	const auto parent_type = get_type(vm, expr._parent_type).get_base_type();
	if(parent_type == base_type::k_string){
		const auto& instance = parent_value.get_string_value();
		const auto lookup_index = read_register_int(vm, expr._reg3);
		if(lookup_index < 0 || lookup_index >= instance.size()){
			throw std::runtime_error("Lookup in string: out of bounds.");
		}
		else{
			const char ch = instance[lookup_index];
			const auto value2 = bc_value_t::make_string(string(1, ch));
			write_register_slow(vm, expr._reg1, value2, expr._instr_type);
		}
	}
	else if(parent_type == base_type::k_json_value){
		//	Notice: the exact type of value in the json_value is only known at runtime = must be checked in interpreter.
		const auto& parent_json_value = parent_value.get_json_value();
		if(parent_json_value.is_object()){
			const auto lookup_key = read_register_string(vm, expr._reg3);

			//	get_object_element() throws if key can't be found.
			const auto& value = parent_json_value.get_object_element(lookup_key);
			const auto value2 = bc_value_t::make_json_value(value);
			write_register_slow(vm, expr._reg1, value2, expr._instr_type);
		}
		else if(parent_json_value.is_array()){
			const auto lookup_index = read_register_int(vm, expr._reg3);
			if(lookup_index < 0 || lookup_index >= parent_json_value.get_array_size()){
				throw std::runtime_error("Lookup in json_value array: out of bounds.");
			}
			else{
				const auto& value = parent_json_value.get_array_n(lookup_index);
				const auto value2 = bc_value_t::make_json_value(value);
				write_register_slow(vm, expr._reg1, value2, expr._instr_type);
			}
		}
		else{
			throw std::runtime_error("Lookup using [] on json_value only works on objects and arrays.");
		}
	}
	else if(parent_type == base_type::k_vector){
		const auto& vec = parent_value.get_vector_value();
		const auto lookup_index = read_register_int(vm, expr._reg3);
		if(lookup_index < 0 || lookup_index >= vec.size()){
			throw std::runtime_error("Lookup in vector: out of bounds.");
		}
		else{
			const bc_value_t value = vec[lookup_index];
			write_register_slow(vm, expr._reg1, value, expr._instr_type);
		}
	}
	else if(parent_type == base_type::k_dict){
		const auto lookup_key = read_register_string(vm, expr._reg3);
		const auto& entries = parent_value.get_dict_value();
		const auto& found_it = entries.find(lookup_key);
		if(found_it == entries.end()){
			throw std::runtime_error("Lookup in dict: key not found.");
		}
		else{
			const bc_value_t value = found_it->second;
			write_register_slow(vm, expr._reg1, value, expr._instr_type);
		}
	}
	else {
		QUARK_ASSERT(false);
		throw std::exception();
	}
}



//	Store function ptr instead of of ID???
//	Notice: host calls and floyd calls have the same type -- we cannot detect host calls until we have a callee value.
void execute_call_expression(interpreter_t& vm, const bc_instruction_t& expr){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(expr.check_invariant());

	const auto& function_value = read_register_function(vm, expr._reg2);
	const int callee_arg_count = expr._reg3._index;

	const auto& function_def = get_function_def(vm, function_value);
	const auto& function_def_arg_count = function_def._args.size();
	const int function_def_dynamic_arg_count = count_function_dynamic_args(function_def._function_type);

	//	_e[...] contains first callee, then each argument.
	//	We need to examine the callee, since we support magic argument lists of varying size.

	if(function_def_arg_count != callee_arg_count){
		QUARK_ASSERT(false);
	}

	if(function_def._host_function_id != 0){
		const auto& host_function = vm._imm->_host_functions.at(function_def._host_function_id);

		const int arg0_stack_pos = vm._value_stack.size() - (function_def_dynamic_arg_count + callee_arg_count);
		int stack_pos = arg0_stack_pos;

		//	Notice that dynamic functions will have each DYN argument with a leading itype as an extra argument.
		std::vector<value_t> arg_values;
		for(int i = 0 ; i < function_def_arg_count ; i++){
			const auto& func_arg_type = function_def._args[i]._type;
			if(func_arg_type.is_internal_dynamic()){
				const auto arg_itype = vm._value_stack.load_intq(stack_pos);
				const auto& arg_type = get_type(vm, static_cast<int16_t>(arg_itype));
				const auto arg_bc = vm._value_stack.load_value_slow(stack_pos + 1, arg_type);

				const auto arg_value = bc_to_value(arg_bc, arg_type);
				arg_values.push_back(arg_value);
				stack_pos += 2;
			}
			else{
				const auto arg_bc = vm._value_stack.load_value_slow(stack_pos + 0, func_arg_type);
				const auto arg_value = bc_to_value(arg_bc, func_arg_type);
				arg_values.push_back(arg_value);
				stack_pos++;
			}
		}

		const auto& result = (host_function)(vm, arg_values);
		const auto bc_result = value_to_bc(result);
		write_register_slow(vm, expr._reg1, bc_result, expr._instr_type);
	}
	else{
		//	Notice that arguments are first in the symbol list.

		//	Future: support dynamic Floyd functions too.
		QUARK_ASSERT(function_def_dynamic_arg_count == 0);

		//	NOTICE: the arg expressions are designed to be run in caller's stack frame -- their addresses are relative it that.
		//	execute_expression() may temporarily use the stack, overwriting stack after frame pointer.
		//	This makes it hard to execute the args and store the directly into the right spot of stack.
		//	Need temp to solve this. Find better solution?
		//??? maybe execute arg expressions while stack frame is set *beyond* our new frame?
		//??? Or change calling conventions to store args *before* frame.

		if(callee_arg_count > 8){
			throw std::runtime_error("Max 8 arguments.");
		}

	    bc_pod_value_t temp[8];
		for(int i = 0 ; i < callee_arg_count ; i++){
			const auto& arg_type = function_def._args[i]._type;
			const auto t = read_register_slow(vm, variable_address_t::make_variable_address(expr._reg2._parent_steps, expr._reg2._index + 1 + i), arg_type);

			if(function_def._body._exts[i]){
				t._pod._ext->_rc++;
			}
			temp[i] = t._pod;
		}

		open_stack_frame2_nobump(vm, function_def._body, &temp[0], callee_arg_count);
		const auto& result = execute_statements(vm, function_def._body._body._statements);
		close_stack_frame(vm, function_def._body);

		QUARK_ASSERT(result._type == statement_result_t::k_returning);
		write_register_slow(vm, expr._reg1, result._output, expr._instr_type);
	}
}

//	This function evaluates all input expressions, then call construct_value_from_typeid() to do the work.
//??? Make several opcodes for construct-value: construct-struct, vector, dict, basic. ALSO casting 1:1 between types.
//??? Optimize -- inline construct_value_from_typeid() to simplify a lot.
void execute_construct_value_expression(interpreter_t& vm, const bc_instruction_t& expr){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(expr.check_invariant());

	const auto args_reg = expr._reg2;
	const auto arg_count = expr._reg3._index;
	const auto& root_value_type = get_type(vm, expr._instr_type);
	const auto basetype = get_basetype(vm, expr._instr_type);
	if(basetype == base_type::k_vector){
		const auto& element_type = root_value_type.get_vector_element_type();
		QUARK_ASSERT(element_type.is_undefined() == false);
		QUARK_ASSERT(root_value_type.is_undefined() == false);

		std::vector<bc_value_t> elements2;
		for(int i = 0 ; i < arg_count ; i++){
			const auto arg = read_register_slow(vm, variable_address_t::make_variable_address(args_reg._parent_steps, args_reg._index + i), element_type);
			elements2.push_back(arg);
		}

	#if DEBUG && FLOYD_BD_DEBUG
		for(const auto& m: elements2){
			QUARK_ASSERT(m.get_debug_type() == element_type);
		}
	#endif
		//??? should use itype.
		const auto& result = construct_value_from_typeid(vm, typeid_t::make_vector(element_type), element_type, elements2);
		write_register_slow(vm, expr._reg1, result, root_value_type);
	}
	else if(basetype == base_type::k_dict){
		const auto& element_type = root_value_type.get_dict_value_type();
		QUARK_ASSERT(root_value_type.is_undefined() == false);
		QUARK_ASSERT(element_type.is_undefined() == false);

		std::vector<bc_value_t> elements2;
		for(auto i = 0 ; i < arg_count / 2 ; i++){
			const auto key = read_register_string(vm, variable_address_t::make_variable_address(args_reg._parent_steps, args_reg._index + i * 2 + 0));
			const auto value = read_register_slow(vm, variable_address_t::make_variable_address(args_reg._parent_steps, args_reg._index + i * 2 + 1), element_type);
			elements2.push_back(bc_value_t::make_string(key));
			elements2.push_back(value);
		}
		const auto& result = construct_value_from_typeid(vm, root_value_type, typeid_t::make_undefined(), elements2);
		write_register_slow(vm, expr._reg1, result, root_value_type);
	}
	else if(basetype == base_type::k_struct){
		const auto& struct_def = root_value_type.get_struct();
		std::vector<bc_value_t> elements2;
		for(int i = 0 ; i < arg_count ; i++){
			const auto member_type = struct_def._members[i]._type;
			const auto arg = read_register_slow(vm, variable_address_t::make_variable_address(args_reg._parent_steps, args_reg._index + i), member_type);
			elements2.push_back(arg);
		}
		const auto& result = construct_value_from_typeid(vm, root_value_type, typeid_t::make_undefined(), elements2);
		write_register_slow(vm, expr._reg1, result, root_value_type);
	}
	else{
		QUARK_ASSERT(arg_count == 1);
		const auto input_arg_type = get_type(vm, expr._parent_type);
		const auto element = read_register_slow(vm, expr._reg2, input_arg_type);
		const auto& result = construct_value_from_typeid(vm, root_value_type, input_arg_type, { element });
		write_register_slow(vm, expr._reg1, result, root_value_type);
	}
}



QUARK_UNIT_TEST("", "", "", ""){
	float a = 10.0f;
	float b = 23.3f;

	bool r = a && b;
	QUARK_UT_VERIFY(r == true);
}


//??? Here we could use a clever box that encode stuff into stackframe/struct etc and lets us quickly access them. No need to encode type in instruction.
//??? get_type() should have all basetypes as first IDs.




statement_result_t execute_statements(interpreter_t& vm, const std::vector<bc_instruction_t>& statements){
	QUARK_ASSERT(vm.check_invariant());

	int pc = 0;
	while(true){
		if(pc == statements.size()){
			return statement_result_t::make__complete_without_value();
		}
		QUARK_ASSERT(pc >= 0);
		QUARK_ASSERT(pc < statements.size());
		const auto& statement = statements[pc];

		QUARK_ASSERT(vm.check_invariant());
		QUARK_ASSERT(statement.check_invariant());

		const auto opcode = statement._opcode;
		if(false){
		}
/*		if(opcode == bc_opcode::k_statement_store_resolve_inline){
			const auto& rhs_value = execute_expression(vm, statement._e[0]);
			const auto frame_pos = find_frame_from_address(vm, statement._v._parent_steps);
			const auto pos = frame_pos + statement._v._index;
			vm._value_stack.replace_inline(pos, rhs_value);
		}
		else if(opcode == bc_opcode::k_statement_store_resolve_obj){
			const auto& rhs_value = execute_expression(vm, statement._e[0]);
			const auto frame_pos = find_frame_from_address(vm, statement._v._parent_steps);
			const auto pos = frame_pos + statement._v._index;
			vm._value_stack.replace_obj(pos, rhs_value);
		}
		else if(opcode == bc_opcode::k_statement_store_resolve_int){
			const auto& rhs_value = execute_expression(vm, statement._e[0]);
			const auto frame_pos = find_frame_from_address(vm, statement._v._parent_steps);
			const auto pos = frame_pos + statement._v._index;
			vm._value_stack.replace_int(pos, rhs_value.get_int_value());
		}
*/
		else if(opcode == bc_opcode::k_store_resolve){
			const auto type = get_type(vm, statement._instr_type);
			const auto value = read_register_slow(vm, statement._reg2, type);
			write_register_slow(vm, statement._reg1, value, type);
			pc++;
		}

		else if(opcode == bc_opcode::k_return){
			const auto type = get_type(vm, statement._instr_type);
			const auto reg1 = resolve_register(vm, statement._reg1);
			const auto value = vm._value_stack.load_value_slow(reg1, type);

	//??? flatten all functions into ONE big list of instructions or not?
			return statement_result_t::make_return_unwind(value);
		}

		else if(opcode == bc_opcode::k_push){
			const auto type = get_type(vm, statement._instr_type);
			const auto value = read_register_slow(vm, statement._reg1, type);
			vm._value_stack.push_value(value, bc_value_t::is_bc_ext(type.get_base_type()));
			pc++;
		}
		else if(opcode == bc_opcode::k_popn){
			const uint32_t n = statement._reg1._index;
			const uint32_t extbits = statement._reg2._index;
			vm._value_stack.pop_batch(n, extbits);
			pc++;
		}



		else if(opcode == bc_opcode::k_branch_zero){
			//??? how to check any type for ZERO?
			const auto value = read_register_bool(vm, statement._reg1);
			if(value){
				pc++;
			}
			else{
				const auto offset = statement._reg2._index;
				pc = pc + offset;
			}
		}
		else if(opcode == bc_opcode::k_jump){
			const auto offset = statement._reg1._index;
			pc = pc + offset;
		}

		else if(opcode == bc_opcode::k_resolve_member){
			execute_resolve_member_expression(vm, statement);
			pc++;
		}
		else if(opcode == bc_opcode::k_lookup_element){
			execute_lookup_element_expression(vm, statement);
			pc++;
		}

		else if(opcode == bc_opcode::k_call){
			execute_call_expression(vm, statement);
			pc++;
		}

		else if(opcode == bc_opcode::k_construct_value){
			execute_construct_value_expression(vm, statement);
			pc++;
		}


		//////////////////////////////		comparison


		else if(opcode == bc_opcode::k_comparison_smaller_or_equal){
			const auto type = get_type(vm, statement._instr_type);
			const auto left_constant = read_register_slow(vm, statement._reg2, type);
			const auto right_constant = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left_constant.get_debug_type() == right_constant.get_debug_type());
		#endif
			long diff = bc_value_t::compare_value_true_deep(left_constant, right_constant, type);
			write_register_bool(vm, statement._reg1, diff <= 0);
			pc++;
		}
		else if(opcode == bc_opcode::k_comparison_smaller){
			const auto type = get_type(vm, statement._instr_type);
			const auto left_constant = read_register_slow(vm, statement._reg2, type);
			const auto right_constant = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left_constant.get_debug_type() == right_constant.get_debug_type());
		#endif
			long diff = bc_value_t::compare_value_true_deep(left_constant, right_constant, type);
			write_register_bool(vm, statement._reg1, diff < 0);
			pc++;
		}
		else if(opcode == bc_opcode::k_comparison_larger_or_equal){
			const auto type = get_type(vm, statement._instr_type);
			const auto left_constant = read_register_slow(vm, statement._reg2, type);
			const auto right_constant = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left_constant.get_debug_type() == right_constant.get_debug_type());
		#endif
			long diff = bc_value_t::compare_value_true_deep(left_constant, right_constant, type);
			write_register_bool(vm, statement._reg1, diff >= 0);
			pc++;
		}
		else if(opcode == bc_opcode::k_comparison_larger){
			const auto type = get_type(vm, statement._instr_type);
			const auto left_constant = read_register_slow(vm, statement._reg2, type);
			const auto right_constant = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left_constant.get_debug_type() == right_constant.get_debug_type());
		#endif
			long diff = bc_value_t::compare_value_true_deep(left_constant, right_constant, type);
			write_register_bool(vm, statement._reg1, diff > 0);
			pc++;
		}

		else if(opcode == bc_opcode::k_logical_equal){
			const auto type = get_type(vm, statement._instr_type);
			const auto left_constant = read_register_slow(vm, statement._reg2, type);
			const auto right_constant = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left_constant.get_debug_type() == right_constant.get_debug_type());
		#endif
			long diff = bc_value_t::compare_value_true_deep(left_constant, right_constant, type);
			write_register_bool(vm, statement._reg1, diff == 0);
			pc++;
		}
		else if(opcode == bc_opcode::k_logical_nonequal){
			const auto type = get_type(vm, statement._instr_type);
			const auto left_constant = read_register_slow(vm, statement._reg2, type);
			const auto right_constant = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left_constant.get_debug_type() == right_constant.get_debug_type());
		#endif
			long diff = bc_value_t::compare_value_true_deep(left_constant, right_constant, type);
			write_register_bool(vm, statement._reg1, diff != 0);
			pc++;
		}


		//////////////////////////////		arithmetic


		else if(opcode == bc_opcode::k_arithmetic_add){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	bool
			if(basetype == base_type::k_bool){
				const bool left2 = left.get_bool_value();
				const bool right2 = right.get_bool_value();
				write_register_bool(vm, statement._reg1, left2 + right2);
				pc++;
			}

			//	int
			else if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();
				write_register_int(vm, statement._reg1, left2 + right2);
				pc++;
			}

			//	float
			else if(basetype == base_type::k_float){
				const float left2 = left.get_float_value();
				const float right2 = right.get_float_value();
				write_register_float(vm, statement._reg1, left2 + right2);
				pc++;
			}

			//	string
			else if(basetype == base_type::k_string){
				const auto& left2 = left.get_string_value();
				const auto& right2 = right.get_string_value();
				write_register_string(vm, statement._reg1, left2 + right2);
				pc++;
			}

			//	vector
			else if(basetype == base_type::k_vector){
				const auto& element_type = type.get_vector_element_type();

				//	Copy vector into elements.
				auto elements2 = left.get_vector_value();
				const auto& right_elements = right.get_vector_value();
				elements2.insert(elements2.end(), right_elements.begin(), right_elements.end());
				const auto& value2 = bc_value_t::make_vector_value(element_type, elements2);
				write_register_slow(vm, statement._reg1, value2, type);
				pc++;
			}
			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}

		else if(opcode == bc_opcode::k_arithmetic_subtract){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	int
			if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();
				write_register_int(vm, statement._reg1, left2 - right2);
				pc++;
			}

			//	float
			else if(basetype == base_type::k_float){
				const float left2 = left.get_float_value();
				const float right2 = right.get_float_value();
				write_register_float(vm, statement._reg1, left2 - right2);
				pc++;
			}

			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}
		else if(opcode == bc_opcode::k_arithmetic_multiply){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	int
			if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();
				write_register_int(vm, statement._reg1, left2 * right2);
				pc++;
			}

			//	float
			else if(basetype == base_type::k_float){
				const float left2 = left.get_float_value();
				const float right2 = right.get_float_value();
				write_register_float(vm, statement._reg1, left2 * right2);
				pc++;
			}

			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}
		else if(opcode == bc_opcode::k_arithmetic_divide){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	int
			if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();
				if(right2 == 0){
					throw std::runtime_error("EEE_DIVIDE_BY_ZERO");
				}
				write_register_int(vm, statement._reg1, left2 / right2);
				pc++;
			}

			//	float
			else if(basetype == base_type::k_float){
				const float left2 = left.get_float_value();
				const float right2 = right.get_float_value();
				if(right2 == 0.0f){
					throw std::runtime_error("EEE_DIVIDE_BY_ZERO");
				}
				write_register_float(vm, statement._reg1, left2 / right2);
				pc++;
			}

			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}
		else if(opcode == bc_opcode::k_arithmetic_remainder){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	int
			if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();
				if(right2 == 0){
					throw std::runtime_error("EEE_DIVIDE_BY_ZERO");
				}
				write_register_int(vm, statement._reg1, left2 % right2);
				pc++;
			}

			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}

		else if(opcode == bc_opcode::k_logical_and){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	bool
			if(basetype == base_type::k_bool){
				const bool left2 = left.get_bool_value();
				const bool right2 = right.get_bool_value();
				write_register_bool(vm, statement._reg1, left2 && right2);
				pc++;
			}

			//	int
			else if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();

				//### Could be replaced by feature to convert any value to bool -- they use a generic comparison for && and ||
				write_register_bool(vm, statement._reg1, (left2 != 0) && (right2 != 0));
				pc++;
			}

			//	float
			//??? Maybe skip support for this.
			else if(basetype == base_type::k_float){
				const float left2 = left.get_float_value();
				const float right2 = right.get_float_value();
				write_register_bool(vm, statement._reg1, (left2 != 0.0f) && (right2 != 0.0f));
				pc++;
			}

			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}
		else if(opcode == bc_opcode::k_logical_or){
			const auto type = get_type(vm, statement._instr_type);
			const auto left = read_register_slow(vm, statement._reg2, type);
			const auto right = read_register_slow(vm, statement._reg3, type);
		#if FLOYD_BD_DEBUG
			QUARK_ASSERT(left.get_debug_type() == right.get_debug_type());
		#endif
			const auto basetype = type.get_base_type();

			//	bool
			if(basetype == base_type::k_bool){
				const bool left2 = left.get_bool_value();
				const bool right2 = right.get_bool_value();
				write_register_bool(vm, statement._reg1, left2 || right2);
				pc++;
			}

			//	int
			else if(basetype == base_type::k_int){
				const int left2 = left.get_int_value();
				const int right2 = right.get_int_value();
				write_register_bool(vm, statement._reg1, (left2 != 0) || (right2 != 0));
				pc++;
			}

			//	float
			else if(basetype == base_type::k_float){
				const float left2 = left.get_float_value();
				const float right2 = right.get_float_value();
				write_register_bool(vm, statement._reg1, (left2 != 0.0f) || (right2 != 0.0f));
				pc++;
			}

			else{
				QUARK_ASSERT(false);
				throw std::exception();
			}
		}

		else{
			QUARK_ASSERT(false);
			throw std::exception();
		}
	}
	return statement_result_t::make__complete_without_value();
}


statement_result_t execute_body(interpreter_t& vm, const bc_body_optimized_t& body, const bc_pod_value_t* init_values, int init_value_count){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(body.check_invariant());

	open_stack_frame2_nobump(vm, body, init_values, init_value_count);
	const auto& r = execute_statements(vm, body._body._statements);
	close_stack_frame(vm, body);
	return r;
}




////////////////////////////////////////		OTHER STUFF





/*
//	Computed goto-dispatch of expressions: -- not faster than switch when running max optimizations. C++ Optimizer makes compute goto?
//	May be better bet when doing a SEQUENCE of opcode dispatches in a loop.
//??? use C++ local variables as our VM locals1?
//https://eli.thegreenplace.net/2012/07/12/computed-goto-for-efficient-dispatch-tables
bc_value_t execute_expression__computed_goto(interpreter_t& vm, const bc_expression_t& e){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(e.check_invariant());

	const auto& op = static_cast<int>(e._opcode);


	//	Not thread safe -- avoid static locals!
	static void* dispatch_table[] = {
		&&bc_expression_opcode___k_expression_literal,
		&&bc_expression_opcode___k_expression_resolve_member,
		&&bc_expression_opcode___k_expression_lookup_element,
		&&bc_expression_opcode___k_expression_load,
		&&bc_expression_opcode___k_expression_call,
		&&bc_expression_opcode___k_expression_construct_value,

		&&bc_expression_opcode___k_expression_arithmetic_unary_minus,

		&&bc_expression_opcode___k_expression_conditional_operator3,

		&&bc_expression_opcode___k_expression_comparison_smaller_or_equal,
		&&bc_expression_opcode___k_expression_comparison_smaller,
		&&bc_expression_opcode___k_expression_comparison_larger_or_equal,
		&&bc_expression_opcode___k_expression_comparison_larger,

		&&bc_expression_opcode___k_expression_logical_equal,
		&&bc_expression_opcode___k_expression_logical_nonequal,

		&&bc_expression_opcode___k_expression_arithmetic_add,
		&&bc_expression_opcode___k_expression_arithmetic_subtract,
		&&bc_expression_opcode___k_expression_arithmetic_multiply,
		&&bc_expression_opcode___k_expression_arithmetic_divide,
		&&bc_expression_opcode___k_expression_arithmetic_remainder,

		&&bc_expression_opcode___k_expression_logical_and,
		&&bc_expression_opcode___k_expression_logical_or
	};
//	#define DISPATCH() goto *dispatch_table[op]

//	DISPATCH();
	goto *dispatch_table[op];
	while (1) {
        bc_expression_opcode___k_expression_literal:
			return e._value;

		bc_expression_opcode___k_expression_resolve_member:
			return execute_resolve_member_expression(vm, e);

		bc_expression_opcode___k_expression_lookup_element:
			return execute_lookup_element_expression(vm, e);

		//??? Optimize by inlining find_env_from_address() and making sep paths.
		bc_expression_opcode___k_expression_load:
			{
				int frame_pos = find_frame_from_address(vm, e._address_parent_step);
				const auto pos = frame_pos + e._address_index;
				QUARK_ASSERT(pos >= 0 && pos < vm._value_stack.size());
				const auto& value = vm._value_stack.load_value_slow(pos, get_type(vm, e._type));
		//		QUARK_ASSERT(value.get_type().get_base_type() == e._basetype);
				return value;
			}

		bc_expression_opcode___k_expression_call:
			return execute_call_expression(vm, e);

		bc_expression_opcode___k_expression_construct_value:
			return execute_construct_value_expression(vm, e);



		bc_expression_opcode___k_expression_arithmetic_unary_minus:
			return execute_arithmetic_unary_minus_expression(vm, e);



		bc_expression_opcode___k_expression_conditional_operator3:
			return execute_conditional_operator_expression(vm, e);



		bc_expression_opcode___k_expression_comparison_smaller_or_equal:
		bc_expression_opcode___k_expression_comparison_smaller:
		bc_expression_opcode___k_expression_comparison_larger_or_equal:
		bc_expression_opcode___k_expression_comparison_larger:
		bc_expression_opcode___k_expression_logical_equal:
		bc_expression_opcode___k_expression_logical_nonequal:
			return execute_comparison_expression(vm, e);


		bc_expression_opcode___k_expression_arithmetic_add:
		bc_expression_opcode___k_expression_arithmetic_subtract:
		bc_expression_opcode___k_expression_arithmetic_multiply:
		bc_expression_opcode___k_expression_arithmetic_divide:
		bc_expression_opcode___k_expression_arithmetic_remainder:

		bc_expression_opcode___k_expression_logical_and:
		bc_expression_opcode___k_expression_logical_or:
			return execute_arithmetic_expression(vm, e);
	}
}
*/



//////////////////////////////////////////		interpreter_t





interpreter_t::interpreter_t(const bc_program_t& program){
	QUARK_ASSERT(program.check_invariant());

	_internal_placeholder_object = bc_value_t::make_string("Internal placeholder object");

	//	Make lookup table from host-function ID to an implementation of that host function in the interpreter.
	const auto& host_functions = get_host_functions();
	std::map<int, HOST_FUNCTION_PTR> host_functions2;
	for(auto& hf_kv: host_functions){
		const auto& function_id = hf_kv.second._signature._function_id;
		const auto& function_ptr = hf_kv.second._f;
		host_functions2.insert({ function_id, function_ptr });
	}

	const auto start_time = std::chrono::high_resolution_clock::now();
	_imm = std::make_shared<interpreter_imm_t>(interpreter_imm_t{start_time, program, host_functions2});

	_current_stack_frame = 0;
	open_stack_frame2_nobump(*this, _imm->_program._globals, nullptr, 0);

	//	Run static intialization (basically run global statements before calling main()).
	/*const auto& r =*/ execute_statements(*this, _imm->_program._globals._statements);
	QUARK_ASSERT(check_invariant());
}

/*
interpreter_t::interpreter_t(const interpreter_t& other) :
	_imm(other._imm),
	_internal_placeholder_object(other._internal_placeholder_object),
	_value_stack(other._value_stack),
	_current_stack_frame(other._current_stack_frame),
	_print_output(other._print_output)
{
	QUARK_ASSERT(other.check_invariant());
	QUARK_ASSERT(check_invariant());
}
*/

void interpreter_t::swap(interpreter_t& other) throw(){
	other._imm.swap(this->_imm);
	_internal_placeholder_object.swap(other._internal_placeholder_object);
	other._value_stack.swap(this->_value_stack);
	std::swap(_current_stack_frame, this->_current_stack_frame);
	other._print_output.swap(this->_print_output);
}

/*
const interpreter_t& interpreter_t::operator=(const interpreter_t& other){
	auto temp = other;
	temp.swap(*this);
	return *this;
}
*/

#if DEBUG
bool interpreter_t::check_invariant() const {
	QUARK_ASSERT(_imm->_program.check_invariant());
	return true;
}
#endif



//////////////////////////////////////////		FUNCTIONS



json_t interpreter_to_json(const interpreter_t& vm){
	vector<json_t> callstack;
	QUARK_ASSERT(vm.check_invariant());

	const auto stack_frames = get_stack_frames(vm);
/*
	for(int env_index = 0 ; env_index < stack_frames.size() ; env_index++){
		const auto frame_pos = stack_frames[env_index];

		const auto local_end = (env_index == (vm._call_stack.size() - 1)) ? vm._value_stack.size() : vm._call_stack[vm._call_stack.size() - 1 - env_index + 1]._values_offset;
		const auto local_count = local_end - e->_values_offset;
		std::vector<json_t> values;
		for(int local_index = 0 ; local_index < local_count ; local_index++){
			const auto& v = vm._value_stack[e->_values_offset + local_index];
		}

		const auto& env = json_t::make_object({
			{ "values", values }
		});
		callstack.push_back(env);
	}
*/

	return json_t::make_object({
		{ "ast", bcprogram_to_json(vm._imm->_program) },
		{ "callstack", json_t::make_array(callstack) }
	});
}


value_t call_host_function(interpreter_t& vm, int function_id, const std::vector<floyd::value_t>& args){
	QUARK_ASSERT(vm.check_invariant());
	QUARK_ASSERT(function_id >= 0);

	const auto& host_function = vm._imm->_host_functions.at(function_id);

	//	arity
//	QUARK_ASSERT(args.size() == host_function._function_type.get_function_args().size());

	const auto& result = (host_function)(vm, args);
	return result;
}

bc_program_t program_to_ast2(const interpreter_context_t& context, const string& program){
	parser_context_t context2{ quark::trace_context_t(context._tracer._verbose, context._tracer._tracer) };
//	parser_context_t context{ quark::make_default_tracer() };
//	QUARK_CONTEXT_TRACE(context._tracer, "Hello");

	const auto& pass1 = floyd::parse_program2(context2, program);
	const auto& pass2 = run_pass2(context2._tracer, pass1);
	const auto& pass3 = floyd::run_pass3(context2._tracer, pass2);


	const auto bc = run_bggen(context2._tracer, pass3);

	return bc;
}

void print_vm_printlog(const interpreter_t& vm){
	QUARK_ASSERT(vm.check_invariant());

	if(vm._print_output.empty() == false){
		std::cout << "print output:\n";
		for(const auto& line: vm._print_output){
			std::cout << line << "\n";
		}
	}
}

std::shared_ptr<interpreter_t> run_global(const interpreter_context_t& context, const string& source){
	auto program = program_to_ast2(context, source);
	auto vm = make_shared<interpreter_t>(program);
//	QUARK_TRACE(json_to_pretty_string(interpreter_to_json(vm)));
	print_vm_printlog(*vm);
	return vm;
}

std::pair<std::shared_ptr<interpreter_t>, value_t> run_main(const interpreter_context_t& context, const string& source, const vector<floyd::value_t>& args){
	auto program = program_to_ast2(context, source);

	//	Runs global code.
	auto vm = make_shared<interpreter_t>(program);

	const auto& main_function = find_global_symbol2(*vm, "main");
	if(main_function != nullptr){
		const auto& result = call_function(*vm, bc_to_value(main_function->_value, main_function->_symbol._value_type), args);
		return { vm, result };
	}
	else{
		return {vm, value_t::make_undefined()};
	}
}

std::pair<std::shared_ptr<interpreter_t>, value_t> run_program(const interpreter_context_t& context, const bc_program_t& program, const vector<floyd::value_t>& args){
	auto vm = make_shared<interpreter_t>(program);

	const auto& main_func = find_global_symbol2(*vm, "main");
	if(main_func != nullptr){
		const auto& r = call_function(*vm, bc_to_value(main_func->_value, main_func->_symbol._value_type), args);
		return { vm, r };
	}
	else{
		return { vm, value_t::make_undefined() };
	}
}


}	//	floyd
