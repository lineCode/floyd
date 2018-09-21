//
//  host_functions.hpp
//  FloydSpeak
//
//  Created by Marcus Zetterquist on 2018-02-23.
//  Copyright © 2018 Marcus Zetterquist. All rights reserved.
//

#ifndef host_functions_hpp
#define host_functions_hpp

#include "quark.h"

#include <string>
#include <map>
#include "ast_typeid.h"
#include "floyd_interpreter.h"

namespace floyd {
	struct expression_t;
	struct value_t;
	struct statement_t;
	struct interpreter_t;


	struct host_function_signature_t {
		int _function_id;
		floyd::typeid_t _function_type;
	};

	std::map<std::string, host_function_signature_t> get_host_function_signatures();


	struct host_function_t {
		host_function_signature_t _signature;
		std::string _name;
		HOST_FUNCTION_PTR _f;
	};

	std::map<int, host_function_t> get_host_functions();

	floyd::value_t unflatten_json_to_specific_type(const json_t& v);
}

#endif /* host_functions_hpp */