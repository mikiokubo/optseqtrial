//////////////////////////////////////////////////////////////////////
// lexer
//////////////////////////////////////////////////////////////////////
#include <iostream>
#include <string>
#include <sstream>
#include <limits>

template<typename T>
class Lexer {
public:
	enum class token { nil, INTEGER, STRING, CHAR };
	Lexer(istream& f, T _inf) : from(f), inf(_inf){
		from >> skipws;
	};
	Lexer(const Lexer&) = delete;
	Lexer& operator=(const Lexer&) = delete;
	~Lexer() {}

	void setInfinity(T infinity) { inf = infinity; }
	void error(const string& msg = "") {
		throw runtime_error("parse error befor/at `" + cur_text + "'" + msg);
	}
	token next(token require = token::nil, const string& msg = "") {
		getnext();
		if (require != token::nil && require != cur_tok) error(msg);
		return cur_tok;
	}
	token next(const string& require, const string& msg = "") {
		getnext();
		if (cur_tok != token::STRING || cur_text != require) error(msg);
		return cur_tok;
	}
	token next(char c, const string& msg = "") {
		getnext();
		if (cur_tok != token::CHAR || cur_text.c_str()[0] != c) error(msg);
		return cur_tok;
	}
	void push() {
		if (buf) {
			throw runtime_error("buffer is full.");
		}
		else {
			buf = true;
			buf_tok = cur_tok; cur_tok = bak_tok;
			buf_text = cur_text; cur_text = bak_text;
			buf_line = cur_line; cur_line = bak_line;
		}
	}

	token tok() { return cur_tok; }
	string& text() { return cur_text; }
	char c() { return cur_text.c_str()[0]; }
	T i() { return (cur_text == "inf") ? inf : min<T>(atoi(cur_text.c_str()), inf); }
	T lineno() const { return cur_line; }

private:
	istream& from;
	T lineNo = 1;
	T inf;

	bool buf = false;
	token cur_tok = token::nil, bak_tok = token::nil, buf_tok = token::nil;
	string cur_text, bak_text, buf_text;
	T cur_line = 0, bak_line = 0, buf_line = 0;

	bool isString(char c) {
		if (c > 0 && isalnum(c) || c == '[' || c == ']' || c == '_' || c == '@') return true;
		if (c >= '\x81' && c <= '\x9F' || c >= '\xE0' && c <= '\xFC') return true;
		if (c >= '\x40' && c <= '\x7E' || c >= '\x80' && c <= '\xFC') return true;
		return false;
	}

	bool isTwoByte(char c1, char c2) {
		return false;
		if ((c1 >= '\x81' && c1 <= '\x9F' || c1 >= '\xE0' && c1 <= '\xFC') &&
			(c2 >= '\x40' && c2 <= '\x7E' || c2 >= '\x80' && c2 <= '\xFC')) return true;
		return false;
	}

	bool addString(ostringstream& ss) {
		char c;
		//char cc;
		bool flag = false;
		while ((c = from.get()) != EOF) {
			if (isString(c)) {
				ss << c;
				flag = true;
			}
			//			 else if (!from.eof() && isTwoByte(c, cc = from.get())){
			//				 ss << c << cc;
			//				 flag = true;
			//			 }
			else {
				from.putback(c);
				break;
			}
		}
		return flag;
	}

	void getnext() {
		if (buf) {
			buf = false;
			cur_tok = buf_tok; cur_text = buf_text; cur_line = buf_line;
			return;
		}
		else {
			bak_tok = cur_tok; bak_text = cur_text; bak_line = cur_line;
			int c, cc;
			T i;
			ostringstream ss;
			while ((c = from.get()) != EOF) {
				if (c == '-' || c == '+') {
					cc = from.get();
					if (isdigit(static_cast<int>(cc))) {
						from.putback(cc);
						ss.clear(); from >> i; ss << (c == '-' ? "-" : "+") << i;
						cur_tok = token::INTEGER;
						cur_text = ss.str();
					}
					else {
						from.putback(cc);
						cur_tok = token::CHAR;
						cur_text = c;
					}
				}
				else if (isdigit(c)) {
					from.putback(c);
					ss.clear(); from >> i; ss << i;
					if (addString(ss)) cur_tok = token::STRING;
					else cur_tok = token::INTEGER;
					cur_text = ss.str();
				}
				else if (isString(c)) {
					from.putback(c);
					ss.clear();
					addString(ss);
					if (ss.str() == "inf") cur_tok = token::INTEGER;
					else cur_tok = token::STRING;
					cur_text = ss.str();
				}
				else if (isspace(c)) {
					if (c == '\n') { ++lineNo; }
					//					cout << lineNo << endl;
					continue;
				}
				else if (c == '#') {
					from.ignore(numeric_limits<streamsize>::max(), '\n');
					++lineNo;
					continue;
				}
				else {
					cur_tok = token::CHAR;
					cur_text = c;
				}
				cur_line = lineNo;
				return;
			}
			cur_tok = token::nil;
			cur_text = "EOF";
			cur_line = lineNo;
		}
	}
};
