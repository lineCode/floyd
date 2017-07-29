//
//  main.cpp
//  FloydSpeak
//
//  Created by Marcus Zetterquist on 27/03/16.
//  Copyright © 2016 Marcus Zetterquist. All rights reserved.
//

#include "floyd_parser.h"

#include "parser_primitives.h"
#include "text_parser.h"
#include "parse_statement.h"
#include "parse_expression.h"
#include "parse_function_def.h"
#include "parse_struct_def.h"
#include "utils.h"
#include "json_support.h"
#include "json_parser.h"

#include <string>
#include <memory>
#include <map>
#include <iostream>
#include <cmath>

namespace floyd_parser {


using namespace std;

/*
	AST ABSTRACT SYNTAX TREE

https://en.wikipedia.org/wiki/Abstract_syntax_tree

https://en.wikipedia.org/wiki/Parsing_expression_grammar
https://en.wikipedia.org/wiki/Parsing
*/



//////////////////////////////////////////////////		read_statement()


/*
	Read one statement, including any expressions it uses.
	Supports all statments:
		- return statement
		- struct-definition
		- function-definition
		- define constant, with initializating.

	Never simplifes expressions- the parser is non-lossy.

	Returns:
		_statement
			[return_statement]
				[expression]
			...
		_ast
			May have been updated to hold new function-definition or struct-definition.
		_rest
			will never start with whitespace.
			trailing ";" will be consumed.


	Example return statement:
		#1	"return 3;..."
		#2	"return f(3, 4) + 2;..."

	Example function definitions:
		#1	"int test_func1(){ return 100; };..."
		#2	"string test_func2(int a, float b){ return "sdf" };..."

	Example struct definitions:
		"struct my_image { int width; int height; };"
		"struct my_sprite { string name; my_image image; };..."

	Example variable definitions
		"int a = 10;..."
		"int b = f(a);..."


	OUTPUT

	["return", EXPRESSION ]
	["bind", "<string>", "local_name", EXPRESSION ]
	["def_struct", STRUCT_DEF ]
	["define_function", FUNCTION_DEF ]


	FUTURE
	- Add local scopes / blocks.
	- Include comments
	- Add mutable variables
*/


#if false
struct statement_result_t {
	json_value_t _statement;
	std::string _rest;
};

static statement_result_t read_statement(const string& pos){
	const auto token_pos = read_until(pos, whitespace_chars);

	//	return statement?
	if(token_pos.first == "return"){
		const auto return_statement_pos = parse_return_statement(pos);
		return { return_statement_pos.first, skip_whitespace(return_statement_pos.second) };
	}

	//	struct definition?
	else if(token_pos.first == "struct"){
		const auto a = parse_struct_definition(pos);
		return { a.first, skip_whitespace(a.second) };
	}

	else {
		const auto type_pos = read_required_type_identifier(pos);
		const auto identifier_pos = read_required_single_symbol(type_pos.second);

		/*
			Function definition?
			"int xyz(string a, string b){ ... }
		*/
		if(peek_string(skip_whitespace(identifier_pos.second), "(")){
			const auto function = parse_function_definition1(pos);
			{
				QUARK_SCOPED_TRACE("FUNCTION DEF");
				QUARK_TRACE(json_to_pretty_string(function.first));
			}
            return {
				json_value_t::make_array2({ json_value_t("define_function"), function.first }),
				skip_whitespace(function.second)
			};
		}

		//	Define variable?
		/*
			"int a = 10;"
			"string hello = f(a) + \"_suffix\";";
		*/

		else if(peek_string(skip_whitespace(identifier_pos.second), "=")){
			const auto assignment_statement = parse_assignment_statement(pos);
			return { assignment_statement.first, skip_whitespace(assignment_statement.second) };
		}

		else{
			throw std::runtime_error("syntax error");
		}
	}
}

QUARK_UNIT_TESTQ("read_statement()", ""){
	try{
		const auto result = read_statement("int f()");
		QUARK_TEST_VERIFY(false);
	}
	catch(...){
	}
}

QUARK_UNIT_TESTQ("read_statement()", ""){
	const auto result = read_statement("float test = testx(1234);\n\treturn 3;\n");
	QUARK_TEST_VERIFY(json_to_compact_string(result._statement)
		== R"(["bind", "<float>", "test", ["call", ["@", "testx"], [["k", 1234, "<int>"]]]])");
	QUARK_TEST_VERIFY(result._rest == "return 3;\n");
}

QUARK_UNIT_TESTQ("read_statement()", ""){
	const auto result = read_statement(test_function1);
	QUARK_TEST_VERIFY(json_to_compact_string(result._statement)
		== R"(["define_function", { "args": [], "locals": [], "members": [], "name": "test_function1", "return_type": "<int>", "statements": [["return", ["k", 100, "<int>"]]], "type": "function", "types": {} }])");
	QUARK_TEST_VERIFY(result._rest == "");
}

QUARK_UNIT_TESTQ("read_statement()", ""){
	const auto result = read_statement("struct test_struct0 {int x; string y; float z;};");

	quark::ut_compare(
		json_to_compact_string(result._statement),
		R"(["def-struct", { "members": [{ "name": "x", "type": "<int>" }, { "name": "y", "type": "<string>" }, { "name": "z", "type": "<float>" }], "name": "test_struct0" }])"
	);
	QUARK_TEST_VERIFY(result._rest == ";");
}



static json_value_t add_subscope(const json_value_t& scope, const json_value_t& subscope){
	QUARK_ASSERT(scope.check_invariant());
	QUARK_ASSERT(subscope.check_invariant());

	const auto name = subscope.get_object_element("name").get_string();
	const auto type = subscope.get_object_element("type").get_string();
	const auto type_entry = json_value_t::make_object({
		{ "base_type", type },
		{ "scope_def", subscope }
	});
	if(exists_in(scope, make_vec({ "types", name }))){
		const auto index = get_in(scope, make_vec({ "types", name })).get_array_size();
		return assoc_in(scope, make_vec({"types", name, index }), type_entry);
	}
	else{
		return assoc_in(
			scope,
			make_vec({"types", name }),
			json_value_t::make_array2({ type_entry })
		);
	}
}

std::pair<json_value_t, std::string> read_statements_into_scope_def1(const json_value_t& scope, const string& s){
	QUARK_ASSERT(scope.check_invariant());
	QUARK_ASSERT(s.size() > 0);

	json_value_t scope2 = scope;

	auto pos = skip_whitespace(s);
	while(!pos.empty()){
		const auto statement_pos = read_statement(pos);

		//	Ex: ["return", EXPRESSION]
		const auto statement = statement_pos._statement;

		const auto statement_type = statement.get_array_n(0).get_string();

		//	Definition statements are immediately removed from AST and the types are defined instead.
		if(statement_type == "def-struct"){
			const auto def = statement.get_array_n(1);
			json_value_t scope_def = make_scope_def();
			scope_def = store_object_member(scope_def, "type", "struct");
			scope_def = store_object_member(scope_def, "name", def.get_object_element("name"));
			scope_def = store_object_member(scope_def, "members", def.get_object_element("members"));
			scope2 = add_subscope(scope2, scope_def);
		}
		else if(statement_type == "define_function"){
			scope2 = add_subscope(scope2, statement.get_array_n(1));
		}
//??? Move this logic to pass 2!! Same thing with

		else if(statement_type == "bind"){
			const auto bind_type = statement.get_array_n(1);
			const auto local_name = statement.get_array_n(2);
			const auto expr = statement.get_array_n(3);
			const auto loc = make_member_def(bind_type.get_string(), local_name.get_string(), json_value_t());

			//	Reserve an entry in _members-vector for our variable.
			scope2 = store_object_member(scope2, "locals", push_back(scope2.get_object_element("locals"), loc));
			scope2 = store_object_member(scope2, "statements", push_back(scope2.get_object_element("statements"), statement));
		}
		else{
			scope2 = store_object_member(scope2, "statements", push_back(scope2.get_object_element("statements"), statement));
		}
		pos = skip_whitespace(statement_pos._rest);
	}

	return { scope2, pos };
}

json_value_t parse_program1(const string& program){
	json_value_t a = make_scope_def();
	a = store_object_member(a, "name", "global");
	a = store_object_member(a, "type", "global");

	const auto statements_pos = read_statements_into_scope_def1(a, program);
	QUARK_TRACE(json_to_pretty_string(statements_pos.first));
	return statements_pos.first;
}
#endif

static std::pair<json_value_t, std::string> read_statement2(const string& pos){
	const auto token_pos = read_until(seq_t(pos), whitespace_chars);

	//	return statement?
	if(token_pos.first == "return"){
		const auto return_statement_pos = parse_return_statement(pos);
		return { return_statement_pos.first, skip_whitespace(return_statement_pos.second) };
	}

	//	struct definition?
	else if(token_pos.first == "struct"){
		const auto a = parse_struct_definition(seq_t(pos));
		return { a.first, skip_whitespace(a.second).get_s() };
	}

	else {
		const auto type_pos = read_required_type_identifier(seq_t(pos));
		const auto identifier_pos = read_required_single_symbol(type_pos.second);

		/*
			Function definition?
			"int xyz(string a, string b){ ... }
		*/
		if(peek(skip_whitespace(identifier_pos.second), "(").first){
			const auto function = parse_function_definition2(pos);
            return { function.first, skip_whitespace(function.second)};
		}

		/*
			Define variable?

			"int a = 10;"
			"string hello = f(a) + \"_suffix\";";
		*/
		else if(peek(skip_whitespace(identifier_pos.second), "=").first){
			const auto assignment_statement = parse_assignment_statement(pos);
			return { assignment_statement.first, skip_whitespace(assignment_statement.second) };
		}

		else{
			throw std::runtime_error("syntax error");
		}
	}
}

std::pair<json_value_t, std::string> read_statements2(const string& s){
	QUARK_ASSERT(s.size() > 0);

	vector<json_value_t> statements;
	auto pos = skip_whitespace(s);
	while(!pos.empty()){
		const auto statement_pos = read_statement2(pos);
		const auto statement = statement_pos.first;
		statements.push_back(statement);
		pos = skip_whitespace(statement_pos.second);
	}

	return { json_value_t::make_array2(statements), pos };
}

json_value_t parse_program2(const string& program){
	const auto statements_pos = read_statements2(program);
	QUARK_TRACE(json_to_pretty_string(statements_pos.first));
	return statements_pos.first;
}



//////////////////////////////////////////////////		Test programs



const string kProgram1 =
	"int main(string args){\n"
	"	return 3;\n"
	"}\n";

const string kProgram1JSON = R"(
	{
		"args": [],
		"locals": [],
		"members": [],
		"name": "global",
		"return_type": "",
		"statements": [],
		"type": "global",
		"types": {
			"main": [
				{
					"base_type": "function",
					"scope_def": {
						"args": [
							{
								"name": "args",
								"type": "<string>"
							}
						],
						"locals": [],
						"members": [],
						"name": "main",
						"return_type": "<int>",
						"statements": [
							[
								"return",
								[
									"k",
									3,
									"<int>"
								]
							]
						],
						"type": "function",
						"types": {}
					}
				}
			]
		}
	}
)";


const string kProgram2 =
	"int f(int x, int y, string z){\n"
	"	return 3;\n"
	"}\n";

const string kProgram3 =
	"int main(string args){\n"
	"	int a = 4;\n"
	"	return 3;\n"
	"}\n";

const string kProgram4 =
	"string hello(int x, int y, string z){\n"
	"	return \"test abc\";\n"
	"}\n"
	"int main(string args){\n"
	"	return 3;\n"
	"}\n";

const string kProgram5 =
	"float testx(float v){\n"
	"	return 13.4;\n"
	"}\n"
	"int main(string args){\n"
	"	float test = testx(1234);\n"
	"	return 3;\n"
	"}\n";

const auto kProgram6 =
	"struct pixel { string s; }"
	"string main(){\n"
	"	return \"\";"
	"}\n";

const auto kProgram7 =
	"string main(){\n"
	"	return p.s + a;"
	"}\n";



const string kProgram100 = R"(
	struct pixel { float red; float green; float blue; }
	float get_grey(pixel p){ return (p.red + p.green + p.blue) / 3; }

	float main(){
		pixel p = pixel(1, 0, 0);
		return get_grey(p);
	}
)";

const string kProgram100JSON = R"(
	{
		"args": [],
		"locals": [],
		"members": [],
		"name": "global",
		"return_type": "",
		"statements": [],
		"type": "global",
		"types": {
			"get_grey": [
				{
					"base_type": "function",
					"scope_def": {
						"args": [{ "name": "p", "type": "<pixel>" }],
						"locals": [],
						"members": [],
						"name": "get_grey",
						"return_type": "<float>",
						"statements": [
							[
								"return",
								[
									"/",
									[
										"+",
										["+", ["->", ["@", "p"], "red"], ["->", ["@", "p"], "green"]],
										["->", ["@", "p"], "blue"]
									],
									["k", 3, "<int>"]
								]
							]
						],
						"type": "function",
						"types": {}
					}
				}
			],
			"main": [
				{
					"base_type": "function",
					"scope_def": {
						"args": [],
						"locals": [{ "name": "p", "type": "<pixel>" }],
						"members": [],
						"name": "main",
						"return_type": "<float>",
						"statements": [
							[
								"bind",
								"<pixel>",
								"p",
								[
									"call",
									["@", "pixel"],
									[["k", 1, "<int>"], ["k", 0, "<int>"], ["k", 0, "<int>"]]
								]
							],
							["return", ["call", ["@", "get_grey"], [["@", "p"]]]]
						],
						"type": "function",
						"types": {}
					}
				}
			],
			"pixel": [
				{
					"base_type": "struct",
					"scope_def": {
						"args": [],
						"locals": [],
						"members": [
							{ "name": "red", "type": "<float>" },
							{ "name": "green", "type": "<float>" },
							{ "name": "blue", "type": "<float>" }
						],
						"name": "pixel",
						"return_type": "",
						"statements": [],
						"type": "struct",
						"types": {}
					}
				}
			]
		}
	}
)";



const string kProgram100JSONv2 = R"(
	[
		[
			"def-struct",
			{
				"members": [
					{ "name": "red", "type": "<float>" },
					{ "name": "green", "type": "<float>" },
					{ "name": "blue", "type": "<float>" }
				],
				"name": "pixel"
			}
		],
		[
			"def-func",
			{
				"args": [{ "name": "p", "type": "<pixel>" }],
				"name": "get_grey",
				"return_type": "<float>",
				"statements": [
					[
						"return",
						[
							"/",
							[
								"+",
								["+", ["->", ["@", "p"], "red"], ["->", ["@", "p"], "green"]],
								["->", ["@", "p"], "blue"]
							],
							["k", 3, "<int>"]
						]
					]
				]
			}
		],
		[
			"def-func",
			{
				"args": [],
				"name": "main",
				"return_type": "<float>",
				"statements": [
					[
						"bind",
						"<pixel>",
						"p",
						["call", ["@", "pixel"], [["k", 1, "<int>"], ["k", 0, "<int>"], ["k", 0, "<int>"]]]
					],
					["return", ["call", ["@", "get_grey"], [["@", "p"]]]]
				]
			}
		]
	]
)";

#if false
QUARK_UNIT_TEST("", "parse_program1()", "Program 100", ""){
	ut_compare_jsons(
		parse_program1(kProgram100),
		parse_json(seq_t(kProgram100JSON)).first
	);
}
#endif



QUARK_UNIT_TEST("", "parse_program2()", "Program 100", ""){
	ut_compare_jsons(
		parse_program2(kProgram100),
		parse_json(seq_t(kProgram100JSONv2)).first
	);
}

#if false

QUARK_UNIT_TEST("", "parse_program1()", "Program 1", ""){
	ut_compare_jsons(
		parse_program1(kProgram1),
		parse_json(seq_t(kProgram1JSON)).first
	);
}


QUARK_UNIT_TEST("", "parse_program1()", "kProgram1", ""){
	auto result = parse_program1(kProgram1);
	QUARK_UT_VERIFY(get_in(result, { "types", "main", 0.0, "base_type" }) == "function");
	QUARK_UT_VERIFY(get_in(result, { "types", "main", 0.0, "scope_def", "args", 0.0 }) == json_value_t::make_object({ { "name", "args"}, {"type", "<string>"}}));
	QUARK_UT_VERIFY(get_in(result, { "types", "main", 0.0, "scope_def", "return_type" }) == "<int>");
}

QUARK_UNIT_TEST("", "parse_program1()", "three arguments", ""){

	const auto result = parse_program1(kProgram2);
	QUARK_UT_VERIFY(result);
#if false
	const auto f = make_function_def(
		type_identifier_t::make("f"),
		type_identifier_t::make_int(),
		{
			member_t{ type_identifier_t::make_int(), "x" },
			member_t{ type_identifier_t::make_int(), "y" },
			member_t{ type_identifier_t::make_string(), "z" }
		},
		executable_t({
			make_shared<statement_t>(make__return_statement(expression_t::make_constant(3)))
		}),
		{},
		{}
	);

//	QUARK_TEST_VERIFY((*resolve_function_type(result._global_scope->_types_collector, "f") == *f));
	QUARK_TEST_VERIFY(resolve_function_type(result._global_scope->_types_collector, "f"));

	const auto f2 = resolve_function_type(result._global_scope->_types_collector, "f");
	QUARK_UT_VERIFY(f2->_type == scope_def_t::k_function_scope);

	const auto body = resolve_function_type(f2->_types_collector, "___body");
	QUARK_UT_VERIFY(body->_type == scope_def_t::k_subscope);
	QUARK_UT_VERIFY(body->_executable._statements.size() == 1);
#endif
}

QUARK_UNIT_TEST("", "parse_program1()", "Local variables", ""){
	auto result = parse_program1(kProgram3);
	QUARK_UT_VERIFY(result);
#if false
	const auto f2 = resolve_function_type(result._global_scope->_types_collector, "main");
	QUARK_UT_VERIFY(f2);
	QUARK_UT_VERIFY(f2->_type == scope_def_t::k_function_scope);

	const auto body = resolve_function_type(f2->_types_collector, "___body");
	QUARK_UT_VERIFY(body);
	QUARK_UT_VERIFY(body->_type == scope_def_t::k_subscope);
	QUARK_UT_VERIFY(body->_members.size() == 1);
	QUARK_UT_VERIFY(body->_members[0]._name == "a");
	QUARK_UT_VERIFY(body->_executable._statements.size() == 2);
#endif
}

QUARK_UNIT_TEST("", "parse_program1()", "two functions", ""){
	const auto result = parse_program1(kProgram4);
	QUARK_UT_VERIFY(result);

#if false
	QUARK_TEST_VERIFY(result._global_scope->_executable._statements.size() == 0);

	const auto f = make_function_def(
		type_identifier_t::make("hello"),
		type_identifier_t::make_string(),
		{
			member_t{ type_identifier_t::make_int(), "x" },
			member_t{ type_identifier_t::make_int(), "y" },
			member_t{ type_identifier_t::make_string(), "z" }
		},
		executable_t({
			make_shared<statement_t>(make__return_statement(expression_t::make_constant("test abc")))
		}),
		{}
	);
//	QUARK_TEST_VERIFY((*resolve_function_type(result._global_scope->_types_collector, "hello") == *f));
	QUARK_TEST_VERIFY(resolve_function_type(result._global_scope->_types_collector, "hello"));

	const auto f2 = make_function_def(
		type_identifier_t::make("main"),
		type_identifier_t::make_int(),
		{
			member_t{ type_identifier_t::make_string(), "args" }
		},
		executable_t({
			make_shared<statement_t>(make__return_statement(expression_t::make_constant(3)))
		}),
		{}
	);
//	QUARK_TEST_VERIFY((*resolve_function_type(result._global_scope->_types_collector, "main") == *f2));
	QUARK_TEST_VERIFY(resolve_function_type(result._global_scope->_types_collector, "main"));
#endif
}


QUARK_UNIT_TESTQ("parse_program1()", "Call function a from function b"){
	auto result = parse_program1(kProgram5);
	QUARK_UT_VERIFY(result);
#if false
	QUARK_TEST_VERIFY(result._global_scope->_executable._statements.size() == 0);

	const auto f = make_function_def(
		type_identifier_t::make("testx"),
		type_identifier_t::make_float(),
		{
			member_t{ type_identifier_t::make_float(), "v" }
		},
		executable_t({
			make_shared<statement_t>(make__return_statement(expression_t::make_constant(13.4f)))
		}),
		{}
	);
//	QUARK_TEST_VERIFY((*resolve_function_type(result._global_scope->_types_collector, "testx") == *f));
	QUARK_TEST_VERIFY(resolve_function_type(result._global_scope->_types_collector, "testx"));
#endif
}
#endif


}	//	floyd_parser

