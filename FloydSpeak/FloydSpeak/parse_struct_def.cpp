//
//  parser_struct.cpp
//  FloydSpeak
//
//  Created by Marcus Zetterquist on 30/07/16.
//  Copyright © 2016 Marcus Zetterquist. All rights reserved.
//

#include "parse_struct_def.h"

#include "parse_expression.h"
#include "parse_function_def.h"
#include "parse_struct_def.h"
#include "parser_primitives.h"
//#include "parser_ast.h"
#include "json_support.h"
#include "json_writer.h"

namespace floyd_parser {
	using std::string;
	using std::vector;
	using std::pair;
	using std::make_shared;
	using std::shared_ptr;

	/*
		{}
		{int a;}
		{int a = 13;}


		["def_struct", "my_name",
			[
				[ "<int>", "_x", 0 ],
				[ "<int>", "_y", 10 ]
			]
		]
	*/
	std::pair<json_value_t, std::string> parse_struct_body(const std::string& struct_name, const string& s){
		QUARK_ASSERT(peek_string(skip_whitespace(s), "{"));

		const auto s2 = skip_whitespace(s);
		read_required_char(s2, '{');
		const auto body_pos = get_balanced(s2);

		vector<json_value_t> members;
		auto pos = trim_ends(body_pos.first);
		while(!pos.empty()){
			const auto arg_type = read_type(pos);
			const auto arg_name = read_required_single_symbol(arg_type.second);

			string default_value;
			const auto optional_default_value = read_optional_char(skip_whitespace(arg_name.second), '=');
			if(optional_default_value.first){
				pos = skip_whitespace(optional_default_value.second);

				const auto constant_expr_pos_s = read_until(pos, ";");

				const auto constant_expr_pos = parse_expression_seq(seq_t(constant_expr_pos_s.first));
				const auto constant_expr = constant_expr_pos.first;
/*
				if(!constant_expr._constant){
					throw std::runtime_error("Struct member defaults must be constants only, not expressions.");
				}
				if(constant_expr._constant->get_type() != member_type){
					throw std::runtime_error("Struct member defaults type mismatch.");
				}
*/

				const auto a = json_value_t::make_array_skip_nulls({ json_value_t("<" + arg_type.first + ">"), json_value_t(arg_name.first), constant_expr_pos.first });
				members.push_back(a);
				pos = skip_whitespace(constant_expr_pos_s.second);
			}
			else{
				const auto a = json_value_t::make_array_skip_nulls({ json_value_t("<" + arg_type.first + ">"), json_value_t(arg_name.first) });
				members.push_back(a);
				pos = skip_whitespace(optional_default_value.second);
			}
			pos = skip_whitespace(read_required_char(pos, ';'));
		}

		const auto obj = make_object({
			{ "_name", json_value_t(struct_name) },
			{ "_members", members }
		});
		return { obj, body_pos.second };
	}

	QUARK_UNIT_TESTQ("parse_struct_body", ""){
//???		QUARK_TEST_VERIFY((parse_struct_body(type_identifier_t::"{}") == pair<vector<member_t>, string>({}, "")));
	}

	QUARK_UNIT_TESTQ("parse_struct_body", ""){
//???		QUARK_TEST_VERIFY((parse_struct_body(" {} x") == pair<vector<member_t>, string>({}, " x")));
	}


	QUARK_UNIT_TESTQ("parse_struct_body", ""){
		const auto r = parse_struct_body("test_struct0", k_test_struct0_body);
/*???
		QUARK_TEST_VERIFY((
			r == pair<vector<member_t>, string>(make_test_struct0(global)->_members, "" )
		));
*/
	}


	std::pair<json_value_t, std::string>  parse_struct_definition(const string& pos){
		QUARK_ASSERT(pos.size() > 0);

		const auto token_pos = read_until(pos, whitespace_chars);
		QUARK_ASSERT(token_pos.first == "struct");

		const auto struct_name = read_required_single_symbol(token_pos.second);
		const auto name = struct_name.first;

		pair<json_value_t, string> body_pos = parse_struct_body(name, skip_whitespace(struct_name.second));
		auto pos2 = skip_whitespace(body_pos.second);

//		pos2 = read_required_char(pos2, ';');

		return { body_pos.first, pos2 };
	}

	scope_ref_t make_test_struct0(scope_ref_t scope_def){
		return scope_def_t::make_struct(
			type_identifier_t::make("test_struct0"),
			vector<member_t>
			{
				{ type_identifier_t::make_int(), "x" },
				{ type_identifier_t::make_string(), "y" },
				{ type_identifier_t::make_float(), "z" }
			}
		);
	}

	QUARK_UNIT_TESTQ("parse_struct_definition()", ""){
		const auto r = parse_struct_definition("struct pixel { int red; int green; int blue;};");

		quark::ut_compare(
			json_to_compact_string(r.first),
			"sdjfklsdjflsdf"
		);

/*		const auto b = scope_def_t::make_struct(
			type_identifier_t::make("pixel"),
			vector<member_t>{
				member_t(type_identifier_t::make_int(), "red"),
				member_t(type_identifier_t::make_int(), "green"),
				member_t(type_identifier_t::make_int(), "blue")
			}
		);

		QUARK_TEST_VERIFY(*r.first == *b);
*/	}

#if false
	QUARK_UNIT_TESTQ("parse_struct_definition()", ""){
		const auto r = parse_struct_definition("struct pixel { int red = 255; int green = 255; int blue = 255; }");
		QUARK_TEST_VERIFY(*r.first == *scope_def_t::make_struct
			(
				type_identifier_t::make("pixel"),
				vector<member_t>{
					member_t(type_identifier_t::make_int(), "red", value_t(255)),
					member_t(type_identifier_t::make_int(), "green", value_t(255)),
					member_t(type_identifier_t::make_int(), "blue", value_t(255))
				}
			)
		);
	}

	QUARK_UNIT_TESTQ("parse_struct_definition()", ""){
		const auto global = scope_def_t::make_global_scope();
		const auto r = parse_struct_definition(global, "struct pixel { string name = \"lisa\"; float height = 12.3f; }xxx");
		QUARK_TEST_VERIFY(*r.first == *scope_def_t::make_struct
			(
				type_identifier_t::make("pixel"),
				vector<member_t>{
					member_t(type_identifier_t::make_string(), "name", value_t("lisa")),
					member_t(type_identifier_t::make_float(), "height", value_t(12.3f))
				}
			)
		);
	}
#endif

}	//	floyd_parser


