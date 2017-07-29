//
//  text_parser.cpp
//  FloydSpeak
//
//  Created by Marcus Zetterquist on 26/07/16.
//  Copyright © 2016 Marcus Zetterquist. All rights reserved.
//

#include "text_parser.h"

#include "quark.h"
#include <string>
#include <memory>
#include <map>
#include <iostream>
#include <cmath>

using std::vector;
using std::string;
using std::pair;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;





///////////////////////////////		seq_t




seq_t::seq_t(const std::string& s) :
	_str(make_shared<string>(s)),
	_pos(0)
{
	FIRST_debug = _str->c_str() + _pos + 0;
	REST_debug = _str->c_str() + _pos + 1;

	QUARK_ASSERT(check_invariant());
}

seq_t::seq_t(const std::shared_ptr<const std::string>& str, std::size_t pos) :
	_str(str),
	_pos(pos)
{
	QUARK_ASSERT(str);
	QUARK_ASSERT(pos <= str->size());

	FIRST_debug = _str->c_str() + _pos + 0;
	REST_debug = _str->c_str() + _pos + 1;

	QUARK_ASSERT(check_invariant());
}

bool seq_t::operator==(const seq_t& other) const {
	QUARK_ASSERT(check_invariant());
	QUARK_ASSERT(other.check_invariant());

	return first(1) == other.first(1) && get_s() == other.get_s();
}

bool seq_t::check_invariant() const {
	QUARK_ASSERT(_str);
	QUARK_ASSERT(_pos <= _str->size());
	return true;
}


char seq_t::first1_char() const{
	QUARK_ASSERT(check_invariant());

	if(_pos >= _str->size()){
		throw std::runtime_error("");
	}

	return (*_str)[_pos];
}

std::string seq_t::first1() const{
	return first(1);
}

std::string seq_t::first(size_t chars) const{
	QUARK_ASSERT(check_invariant());

	return _pos < _str->size() ? _str->substr(_pos, chars) : string();
}

seq_t seq_t::rest1() const{
	QUARK_ASSERT(check_invariant());

	return rest(1);
}

seq_t seq_t::rest(size_t skip) const{
	QUARK_ASSERT(check_invariant());

	const auto p = std::min(_str->size(), _pos + skip);
	return seq_t(_str, p);
}

std::string seq_t::get_s() const{
	QUARK_ASSERT(check_invariant());

	return _str->substr(_pos);
}

std::size_t seq_t::size() const{
	QUARK_ASSERT(check_invariant());

	return empty() ? 0 : _str->size() - _pos;
}


bool seq_t::empty() const{
	QUARK_ASSERT(check_invariant());

	return _str->size() == _pos;
}

const char* seq_t::c_str() const{
	QUARK_ASSERT(check_invariant());

	return empty() ? nullptr : _str->c_str() + _pos;
}



QUARK_UNIT_TESTQ("seq_t()", ""){
	seq_t("");
}
QUARK_UNIT_TESTQ("seq_t()", ""){
	seq_t("hello, world!");
}


QUARK_UNIT_TESTQ("first_char()", ""){
	QUARK_TEST_VERIFY(seq_t("a").first1_char() == 'a');
}
QUARK_UNIT_TESTQ("first_char()", ""){
	QUARK_TEST_VERIFY(seq_t("abcd").first1_char() == 'a');
}


QUARK_UNIT_TESTQ("first()", ""){
	QUARK_TEST_VERIFY(seq_t("").first1() == "");
}
QUARK_UNIT_TESTQ("first()", ""){
	QUARK_TEST_VERIFY(seq_t("a").first1() == "a");
}
QUARK_UNIT_TESTQ("first()", ""){
	QUARK_TEST_VERIFY(seq_t("abc").first1() == "a");
}


QUARK_UNIT_TESTQ("first(n)", ""){
	QUARK_TEST_VERIFY(seq_t("abc").first(0) == "");
}
QUARK_UNIT_TESTQ("first(n)", ""){
	QUARK_TEST_VERIFY(seq_t("").first(0) == "");
}
QUARK_UNIT_TESTQ("first(n)", ""){
	QUARK_TEST_VERIFY(seq_t("").first(3) == "");
}
QUARK_UNIT_TESTQ("first(n)", ""){
	QUARK_TEST_VERIFY(seq_t("abc").first(1) == "a");
}
QUARK_UNIT_TESTQ("first(n)", ""){
	QUARK_TEST_VERIFY(seq_t("abc").first(3) == "abc");
}


QUARK_UNIT_TESTQ("rest()", ""){
	QUARK_TEST_VERIFY(seq_t("abc").rest1().first1() == "b");
}
QUARK_UNIT_TESTQ("rest()", ""){
	QUARK_TEST_VERIFY(seq_t("").rest1().first1() == "");
}


QUARK_UNIT_TESTQ("rest(n)", ""){
	QUARK_TEST_VERIFY(seq_t("abc").rest(2).first1() == "c");
}
QUARK_UNIT_TESTQ("rest(n)", ""){
	QUARK_TEST_VERIFY(seq_t("").rest1().first1() == "");
}
QUARK_UNIT_TESTQ("rest(n)", ""){
	QUARK_TEST_VERIFY(seq_t("abc").rest(100).first(100) == "");
}







pair<string, seq_t> read_while(const seq_t& p1, const string& match){
	string a;
	seq_t p2 = p1;

	while(!p2.empty() && match.find(p2.first1_char()) != string::npos){
		a = a + p2.first1_char();
		p2 = p2.rest1();
	}

	return { a, p2 };
}

QUARK_UNIT_TEST("", "read_while()", "", ""){
	QUARK_TEST_VERIFY((read_while(seq_t(""), test_whitespace_chars) == pair<string, seq_t>{ "", seq_t("") }));
}

QUARK_UNIT_TEST("", "read_while()", "", ""){
	QUARK_TEST_VERIFY((read_while(seq_t("\t"), test_whitespace_chars) == pair<string, seq_t>{ "\t", seq_t("") }));
}

QUARK_UNIT_TEST("", "read_while()", "", ""){
	QUARK_TEST_VERIFY((read_while(seq_t("end\t"), test_whitespace_chars) == pair<string, seq_t>{ "", seq_t("end\t") }));
}

QUARK_UNIT_TEST("", "read_while()", "", ""){
	QUARK_TEST_VERIFY((read_while(seq_t("\nend"), test_whitespace_chars) == pair<string, seq_t>{ "\n", seq_t("end") }));
}

QUARK_UNIT_TEST("", "read_while()", "", ""){
	QUARK_TEST_VERIFY((read_while(seq_t("\n\t\rend"), test_whitespace_chars) == pair<string, seq_t>{ "\n\t\r", seq_t("end") }));
}





pair<string, seq_t> read_until(const seq_t& p1, const string& match){
	string a;
	seq_t p2 = p1;

	while(!p2.empty() && match.find(p2.first1_char()) == string::npos){
		a = a + p2.first1_char();
		p2 = p2.rest1();
	}

	return { a, p2 };
}



std::pair<bool, seq_t> peek(const seq_t& p, const std::string& wanted_string){
	const auto size = wanted_string.size();
	if(p.first(size) == wanted_string){
		return { true, p.rest(size) };
	}
	else{
		return { false, p };
	}
}

QUARK_UNIT_TESTQ("peek()", ""){
	const auto result = peek(seq_t("hello, world!"), "hell");
	const auto expected = std::pair<bool, seq_t>(true, seq_t("o, world!"));

	QUARK_TEST_VERIFY(result == expected);
}




	std::string remove_trailing_comma(const std::string& a){
		auto s = a;
		if(s.size() > 1 && s.back() == ','){
			s.pop_back();
		}
		return s;
	}


/*
seq read_while(const string& s, const string& match){
	size_t pos = 0;
	while(pos < s.size() && match.find(s[pos]) != string::npos){
		pos++;
	}

	return seq(
		s.substr(0, pos),
		s.substr(pos)
	);
}

QUARK_UNIT_TEST("", "read_while()", "", ""){
	QUARK_TEST_VERIFY(read_while("", test_whitespace_chars) == seq("", ""));
	QUARK_TEST_VERIFY(read_while(" ", test_whitespace_chars) == seq(" ", ""));
	QUARK_TEST_VERIFY(read_while("    ", test_whitespace_chars) == seq("    ", ""));

	QUARK_TEST_VERIFY(read_while("while", test_whitespace_chars) == seq("", "while"));
	QUARK_TEST_VERIFY(read_while(" while", test_whitespace_chars) == seq(" ", "while"));
	QUARK_TEST_VERIFY(read_while("    while", test_whitespace_chars) == seq("    ", "while"));
}

seq read_until(const string& s, const string& match){
	size_t pos = 0;
	while(pos < s.size() && match.find(s[pos]) == string::npos){
		pos++;
	}

	return { s.substr(0, pos), s.substr(pos) };
}

QUARK_UNIT_TEST("", "read_until()", "", ""){
	QUARK_TEST_VERIFY(read_until("", ",.") == seq("", ""));
	QUARK_TEST_VERIFY(read_until("ab", ",.") == seq("ab", ""));
	QUARK_TEST_VERIFY(read_until("ab,cd", ",.") == seq("ab", ",cd"));
	QUARK_TEST_VERIFY(read_until("ab.cd", ",.") == seq("ab", ".cd"));
}
*/




pair<char, seq_t> read_char(const seq_t& s){
	if(!s.empty()){
		return { s.first1_char(), s.rest1() };
	}
	else{
		throw std::runtime_error("expected character.");
	}
}

seq_t read_required_char(const seq_t& s, char ch){
	if(s.size() > 0 && s.first1_char() == ch){
		return s.rest(1);
	}
	else{
		throw std::runtime_error("expected character '" + string(1, ch)  + "'.");
	}
}

pair<bool, seq_t> read_optional_char(const seq_t& s, char ch){
	if(s.size() > 0 && s.first1_char() == ch){
		return { true, s.rest1() };
	}
	else{
		return { false, s };
	}
}


std::string read_required_string(const std::string& s, const std::string& wanted){
	if(s.size() >= wanted.size() && s.substr(0, wanted.size()) == wanted){
		return s.substr(wanted.size());
	}
	else{
		throw std::runtime_error("Expected string");
	}
}

QUARK_UNIT_TESTQ("read_required_string", ""){
	QUARK_TEST_VERIFY(read_required_string("abcdef", "ab") == "cdef");
	QUARK_TEST_VERIFY(read_required_string("abcdef", "abcdef") == "");
}


string trim_ends(const string& s){
	QUARK_ASSERT(s.size() >= 2);

	return s.substr(1, s.size() - 2);
}


float parse_float(const std::string& pos){
	size_t end = -1;
	auto res = std::stof(pos, &end);
	if(isnan(res) || end == 0){
		throw std::runtime_error("EEE_WRONG_CHAR");
	}
	return res;
}


/*
seq get_balanced_pair(const string& s, char start_char, char end_char){
	QUARK_ASSERT(s[0] == start_char);
	QUARK_ASSERT(s.size() >= 2);

	int depth = 0;
	size_t pos = 0;
	while(pos < s.size() && !(depth == 1 && s[pos] == end_char)){
		const char c = s[pos];
		if(c == start_char) {
			depth++;
		}
		else if(c == end_char){
			if(depth == 0){
				throw std::runtime_error("unbalanced ([{< >}])");
			}
			depth--;
		}
		pos++;
	}

	return { s.substr(1, pos - 1), s.substr(pos + 1) };
}
*/


//	{ "{ hello }xxx", '{', '}' } => { " hello ", "xxx" }


/*
QUARK_UNIT_TEST("", "get_balanced_pair()", "", ""){
	QUARK_TEST_VERIFY(get_balanced_pair("()", '(', ')') == seq("", ""));
	QUARK_TEST_VERIFY(get_balanced_pair("(abc)", '(', ')') == seq("abc", ""));
	QUARK_TEST_VERIFY(get_balanced_pair("(abc)def", '(', ')') == seq("abc", "def"));
	QUARK_TEST_VERIFY(get_balanced_pair("((abc))def", '(', ')') == seq("(abc)", "def"));
	QUARK_TEST_VERIFY(get_balanced_pair("((abc)[])def", '(', ')') == seq("(abc)[]", "def"));
}
*/





std::string quote(const std::string& s){
	return std::string("\"") + s + "\"";
}

QUARK_UNIT_TESTQ("quote()", ""){
	QUARK_UT_VERIFY(quote("") == "\"\"");
}

QUARK_UNIT_TESTQ("quote()", ""){
	QUARK_UT_VERIFY(quote("abc") == "\"abc\"");
}



std::string float_to_string(float value){
	std::stringstream s;
	s << value;
	const auto result = s.str();
	return result;
}

QUARK_UNIT_TESTQ("float_to_string()", ""){
	quark::ut_compare(float_to_string(0.0f), "0");
}
QUARK_UNIT_TESTQ("float_to_string()", ""){
	quark::ut_compare(float_to_string(13.0f), "13");
}
QUARK_UNIT_TESTQ("float_to_string()", ""){
	quark::ut_compare(float_to_string(13.5f), "13.5");
}



std::string double_to_string(double value){
	std::stringstream s;
	s << value;
	const auto result = s.str();
	return result;
}

QUARK_UNIT_TESTQ("double_to_string()", ""){
	quark::ut_compare(float_to_string(0.0), "0");
}
QUARK_UNIT_TESTQ("double_to_string()", ""){
	quark::ut_compare(float_to_string(13.0), "13");
}
QUARK_UNIT_TESTQ("double_to_string()", ""){
	quark::ut_compare(float_to_string(13.5), "13.5");
}



