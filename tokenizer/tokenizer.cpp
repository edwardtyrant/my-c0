#include "tokenizer/tokenizer.h"

#include <sstream>
#include <iostream>
#include <string>
#include <cstdlib>

namespace miniplc0 {

	std::pair<std::optional<Token>, std::optional<CompilationError>> Tokenizer::NextToken() {
		if (!_initialized)
			readAll();
		if (_rdr.bad())
			return std::make_pair(std::optional<Token>(), std::make_optional<CompilationError>(0, 0, ErrorCode::ErrStreamError));
		if (isEOF())
			return std::make_pair(std::optional<Token>(), std::make_optional<CompilationError>(0, 0, ErrorCode::ErrEOF));
		auto p = nextToken();
		if (p.second.has_value())
			return std::make_pair(p.first, p.second);
		auto err = checkToken(p.first.value());
		if (err.has_value())
			return std::make_pair(p.first, err.value());
		return std::make_pair(p.first, std::optional<CompilationError>());
	}

	std::pair<std::vector<Token>, std::optional<CompilationError>> Tokenizer::AllTokens() {
		std::vector<Token> result;
		while (true) {
			auto p = NextToken();
			if (p.second.has_value()) {
				if (p.second.value().GetCode() == ErrorCode::ErrEOF)
					return std::make_pair(result, std::optional<CompilationError>());
				else
					return std::make_pair(std::vector<Token>(), p.second);
			}
			result.emplace_back(p.first.value());
		}
	}

	// 注意：这里的返回值中 Token 和 CompilationError 只能返回一个，不能同时返回。
	std::pair<std::optional<Token>, std::optional<CompilationError>> Tokenizer::nextToken() {
		// 用于存储已经读到的组成当前token字符
		std::stringstream ss;
		// 分析token的结果，作为此函数的返回值
		std::pair<std::optional<Token>, std::optional<CompilationError>> result;
		// <行号，列号>，表示当前token的第一个字符在源代码中的位置
		std::pair<int64_t, int64_t> pos;
		// 记录当前自动机的状态，进入此函数时是初始状态
		DFAState current_state = DFAState::INITIAL_STATE;
		// 这是一个死循环，除非主动跳出
		// 每一次执行while内的代码，都可能导致状态的变更
		while (true) {
			// 读一个字符，请注意auto推导得出的类型是std::optional<char>
			// 这里其实有两种写法
			// 1. 每次循环前立即读入一个 char
			// 2. 只有在可能会转移的状态读入一个 char
			// 因为我们实现了 unread，为了省事我们选择第一种
			auto current_char = nextChar();
			// 针对当前的状态进行不同的操作
			switch (current_state) {

				// 初始状态
				// 这个 case 我们给出了核心逻辑，但是后面的 case 不用照搬。
			case INITIAL_STATE: {
				// 已经读到了文件尾
				if (!current_char.has_value())
					// 返回一个空的token，和编译错误ErrEOF：遇到了文件尾
					return std::make_pair(std::optional<Token>(), std::make_optional<CompilationError>(0, 0, ErrEOF));

				// 获取读到的字符的值，注意auto推导出的类型是char
				auto ch = current_char.value();
				// 标记是否读到了不合法的字符，初始化为否
				auto invalid = false;

				// 使用了自己封装的判断字符类型的函数，定义于 tokenizer/utils.hpp
				// see https://en.cppreference.com/w/cpp/string/byte/isblank
				if (isBlank(ch)) // 读到的字符是空白字符（空格、换行、制表符等）
					current_state = DFAState::INITIAL_STATE; // 保留当前状态为初始状态，此处直接break也是可以的
				else if (!miniplc0::isprint(ch)) // control codes and backspace
					invalid = true;
				else if (miniplc0::isdigit(ch)) { // 读到的字符是数字

                    if (ch == '0') {
                        current_char = nextChar();
                        ch = current_char.value();
                        // 首先判断是不是十六进制表示
                        if (ch == 'x' || ch == 'X') {
                            current_char = nextChar();
                            ch = current_char.value();
                            if(isxdigit(ch)) {
                                current_state = DFAState::HEX_STATE;
                            }else
                                invalid=true;
                        // 如果不是十六进制表示
                        } else if (isBlank(ch))
                            return std::make_pair(
                                    std::optional<Token>(
                                            Token(TokenType::UNSIGNED_INTEGER, std::to_string(ch), pos, previousPos())),
                                    std::optional<CompilationError>());
                        else{
                            unreadLast();
                            return std::make_pair(
                                    std::optional<Token>(
                                            Token(TokenType::UNSIGNED_INTEGER, std::to_string('0'-48), pos, previousPos())),
                                    std::optional<CompilationError>());
                        }
                    }else
                        current_state = DFAState::UNSIGNED_INTEGER_STATE; // 切换到无符号整数的状态
                }else if (miniplc0::isalpha(ch)) // 读到的字符是英文字母
					current_state = DFAState::IDENTIFIER_STATE; // 切换到标识符的状态
				else {
					switch (ch) {
					case '=': // 如果读到的字符是`=`，则切换到等于号的状态
                        current_char = nextChar();
                        ch = current_char.value();
					    if(ch=='='){
                            current_state = DFAState::EQUAL_STATE;
                            break;
					    }
					    unreadLast();
						current_state = DFAState::EQUAL_SIGN_STATE;
						break;
                    case '<':
                        current_char = nextChar();
                        ch = current_char.value();
                        if(ch=='='){
                            current_state = DFAState::SMALL_EQUAL_STATE;
                            break;
                        }
                        unreadLast();
                        current_state = DFAState::SMALL_STATE;
                        break;
                    case '>':
                        current_char = nextChar();
                        ch = current_char.value();
                        if(ch=='='){
                            current_state = DFAState::BIG_EQUAL_STATE;
                            break;
                        }
                        unreadLast();
                        current_state = DFAState::BIG_STATE;
                        break;
                    case '!':
                        current_char = nextChar();
                        ch = current_char.value();
                        if(ch=='='){
                            current_state = DFAState::NOT_EQUAL_STATE;
                            break;
                        }
                        unreadLast();
                        invalid = true;
                        break;
					case '-':
						// 请填空：切换到减号的状态
                        current_state = DFAState::MINUS_SIGN_STATE;
                        break;
					case '+':
						// 请填空：切换到加号的状态
                        current_state = DFAState::PLUS_SIGN_STATE;
                        break;
					case '*':
						// 请填空：切换状态
                        current_state = DFAState::MULTIPLICATION_SIGN_STATE;
                        break;
					case '/':
						// 请填空：切换状态
                        current_state = DFAState::DIVISION_SIGN_STATE;
                        break;

					///// 请填空：
					///// 对于其他的可接受字符
					///// 切换到对应的状态
                    case '(':
                        // 请填空：切换状态
                        current_state = DFAState::LEFTBRACKET_STATE;
                        break;
                    case ')':
                        // 请填空：切换状态
                        current_state = DFAState::RIGHTBRACKET_STATE;
                        break;
                    case '{':
                        // 请填空：切换状态
                        current_state = DFAState::LEFTBRACE_STATE;
                        break;
                    case '}':
                        // 请填空：切换状态
                        current_state = DFAState::RIGHTBRACE_STATE;
                        break;
                    case ';':
                        // 请填空：切换状态
                        current_state = DFAState::SEMICOLON_STATE;
                        break;
                    case ',':
                        current_state = DFAState::COMMA_STATE;
                        break;

					// 不接受的字符导致的不合法的状态
					default:
						invalid = true;
						break;
					}
				}
				// 如果读到的字符导致了状态的转移，说明它是一个token的第一个字符
				if (current_state != DFAState::INITIAL_STATE)
					pos = previousPos(); // 记录该字符的的位置为token的开始位置
				// 读到了不合法的字符
				if (invalid) {
					// 回退这个字符
					unreadLast();
					// 返回编译错误：非法的输入
					return std::make_pair(std::optional<Token>(), std::make_optional<CompilationError>(pos, ErrorCode::ErrInvalidInput));
				}
				// 如果读到的字符导致了状态的转移，说明它是一个token的第一个字符
				if (current_state != DFAState::INITIAL_STATE) // ignore white spaces
					ss << ch; // 存储读到的字符
				break;
			}

								// 当前状态是无符号整数
			case HEX_STATE: {


/**********************************************
                Block 1
**********************************************/
                if (!current_char.has_value()) {
                    // 如果当前已经读到了文件尾，则解析已经读到的字符串为整数
                    //     解析成功则返回无符号整数类型的token，否则返回编译错误
                    std::string temp = ss.str();
                    char *offset;
                    long res=strtol(temp.c_str(), &offset, 16);
                    long ma = 2;
                    ma = (ma << 31) -1;
                    if (res >= 0 && res <= ma) {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::UNSIGNED_HEX_INTEGER, std::to_string(res), pos, previousPos())),
                                std::optional<CompilationError>());
                    } else {
                        // 超过范围返回溢出错误
                        return std::make_pair(std::optional<Token>(),
                                              std::make_optional<CompilationError>(pos, ErrValueOverflow));
                    }
                }

                // 获取读到的字符的值，注意auto推导出的类型是char
                auto ch = current_char.value();
                // 标记是否读到了不合法的字符，初始化为否
                auto invalid = false;

/**********************************************
                Block 2
**********************************************/
                if (miniplc0::isxdigit(ch)) // 读到的字符是xdigit
                    ss << ch;

/**********************************************
                Block 3
**********************************************/
                else if (miniplc0::isalpha(ch)) {
                    // 读到的字符是英文字母
                    ss << ch;
                    current_state = DFAState::IDENTIFIER_STATE; // 切换到标识符的状态

/**********************************************
                Block 4
**********************************************/
                }else {
                    unreadLast();

                    std::string temp = ss.str();
                    char *offset;
                    long res=strtol(temp.c_str(), &offset, 16);
                    long ma = 2;
                    ma = (ma << 31) -1;
                    if (res >= 0 && res <= ma) {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::UNSIGNED_HEX_INTEGER, std::to_string(res), pos, previousPos())),
                                std::optional<CompilationError>());
                    } else {
                        // 超过范围返回溢出错误
                        return std::make_pair(std::optional<Token>(),
                                              std::make_optional<CompilationError>(pos, ErrValueOverflow));
                    }
                }
				break;
			}

			case UNSIGNED_INTEGER_STATE: {
/**********************************************
                Block 1
**********************************************/
                if (!current_char.has_value()) {
                    // 如果当前已经读到了文件尾，则解析已经读到的字符串为整数
                    //     解析成功则返回无符号整数类型的token，否则返回编译错误
                    std::string temp = ss.str();
                    int i=0;
                    for(i = 0 ; i <temp.length();i++){
                        if (temp[i]!='0') {
                            temp = &temp[i];
                            break;
                        }else if(i==temp.length()-1) {
                            temp = "0";
                        }
                    }

                    long res = std::stol(temp);
                    long ma = 2;
                    ma = (ma << 31) -1;
                    if (std::to_string(res).length() == temp.length() && res >= 0 && res <= ma) {
                        // 长度相等返回这个无符号整数的token
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::UNSIGNED_INTEGER, std::to_string(res), pos, previousPos())),
                                std::optional<CompilationError>());
                    } else {
                        // 长度不相等说明里面含有其他字符
                        // 不是整数返回编译错误
                        return std::make_pair(std::optional<Token>(),
                                              std::make_optional<CompilationError>(pos, ErrEOF));
                    }
                }

                // 获取读到的字符的值，注意auto推导出的类型是char
                auto ch = current_char.value();
                // 标记是否读到了不合法的字符，初始化为否
                auto invalid = false;

/**********************************************
                Block 2
**********************************************/
                if (miniplc0::isdigit(ch)) // 读到的字符是数字
                    ss << ch;

/**********************************************
                Block 3
**********************************************/
                else if (miniplc0::isalpha(ch)) {
                    // 读到的字符是英文字母
                    ss << ch;
                    current_state = DFAState::IDENTIFIER_STATE; // 切换到标识符的状态

/**********************************************
                Block 4
**********************************************/
                }else {
                    unreadLast();

                    std::string temp = ss.str();
                    int i=0;
                    for(i = 0 ; i <temp.length();i++){
                        if (temp[i]!='0') {
                            temp = &temp[i];
                            break;
                        }else if(i==temp.length()-1) {
                            temp = "0";
                        }
                    }

                    long res = std::stol(temp);
                    long ma = 2;
                    ma = (ma << 31) -1;
                    if (std::to_string(res).length() == temp.length() && res >= 0 && res <= ma) {
                        // 长度相等返回这个无符号整数的token
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::UNSIGNED_INTEGER, std::to_string(res), pos, currentPos())),
                                std::optional<CompilationError>());
                    } else {
                        // 长度不相等说明里面含有其他字符
                        // 不是整数返回编译错误
                        return std::make_pair(std::optional<Token>(),
                                              std::make_optional<CompilationError>(pos, ErrEOF));
                    }
                }
                break;
			}
			case IDENTIFIER_STATE: {
				// 请填空：

                // Block 1
				// 如果当前已经读到了文件尾，则解析已经读到的字符串
				//     如果解析结果是关键字，那么返回对应关键字的token，否则返回标识符的token

                // Block 2
				// 如果读到的是字符或字母，则存储读到的字符

                // Block 3
				// 如果读到的字符不是上述情况之一，则回退读到的字符，并解析已经读到的字符串
				//     如果解析结果是关键字，那么返回对应关键字的token，否则返回标识符的token

/**********************************************
                Block 1
**********************************************/
                if (!current_char.has_value()) {
                    // 如果当前已经读到了文件尾，则解析已经读到的字符串
                    //     如果解析结果是关键字，那么返回对应关键字的token，否则返回标识符的token

                    // 参考：C++string与int的相互转换（使用C++11）
                    // 网址：https://blog.csdn.net/m0_37316917/article/details/82712017
                    std::string res = ss.str();
                    auto check=checkToken(Token(IDENTIFIER,res,pos,currentPos()));
                    if(check.has_value()){
                        return std::make_pair(std::optional<Token>(), check);
                    }
                    if (res=="const") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CONST, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="void") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::VOID, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="int") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::INT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="char") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CHAR, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="double") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::DOUBLE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="struct") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::STRUCT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="if") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::IF, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="else") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::ELSE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="switch") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::SWITCH, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    }  else if (res=="case") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CASE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="default") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::DEFAULT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="while") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::WHILE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="for") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::FOR, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="do") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::DO, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="return") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::RETURN, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="break") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::BREAK, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="continue") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CONTINUE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="print") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::PRINT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="scan") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::SCAN, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    }else {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::IDENTIFIER, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    }
                }

                auto ch = current_char.value();
/**********************************************
                Block 2
**********************************************/
                // 如果读到的是字符或字母，则存储读到的字符
                // 或许应该说是读到的是数字或者字幕，则存储读到的字符
                if (miniplc0::isdigit(ch)||miniplc0::isalpha(ch)) // 读到的字符是数字
                    ss << ch;

/**********************************************
                Block 4
**********************************************/
                // 如果读到的字符不是上述情况之一，则回退读到的字符，并解析已经读到的字符串
                //     如果解析结果是关键字，那么返回对应关键字的token，否则返回标识符的token
                else {
                    unreadLast();
                    std::string res = ss.str();
                    auto check=checkToken(Token(IDENTIFIER,res,pos,currentPos()));
                    if(check.has_value()){
                        return std::make_pair(std::optional<Token>(), check);
                    }
                    if (res=="const") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CONST, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="void") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::VOID, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="int") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::INT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="char") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CHAR, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="double") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::DOUBLE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="struct") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::STRUCT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="if") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::IF, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="else") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::ELSE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="switch") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::SWITCH, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    }  else if (res=="case") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CASE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="default") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::DEFAULT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="while") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::WHILE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="for") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::FOR, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="do") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::DO, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="return") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::RETURN, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="break") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::BREAK, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="continue") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::CONTINUE, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="print") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::PRINT, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    } else if (res=="scan") {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::SCAN, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    }else {
                        return std::make_pair(
                                std::optional<Token>(Token(TokenType::IDENTIFIER, res, pos, currentPos())),
                                std::optional<CompilationError>());
                    }
                }


				break;
			}

			// 如果当前状态是加号
			case PLUS_SIGN_STATE: {
				// 请思考这里为什么要回退，在其他地方会不会需要
				unreadLast(); // Yes, we unread last char even if it's an EOF.
				return std::make_pair(std::make_optional<Token>(TokenType::PLUS_SIGN, '+', pos, currentPos()), std::optional<CompilationError>());
			}
			// 当前状态为减号的状态
			case MINUS_SIGN_STATE: {
				// 请填空：回退，并返回减号token
                unreadLast(); // Yes, we unread last char even if it's an EOF.
                return std::make_pair(std::make_optional<Token>(TokenType::MINUS_SIGN, '-', pos, currentPos()), std::optional<CompilationError>());
            }

           // 请填空：
           // 对于其他的合法状态，进行合适的操作
           // 比如进行解析、返回token、返回编译错误
            case DIVISION_SIGN_STATE: {
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::DIVISION_SIGN, '/', pos, currentPos()), std::optional<CompilationError>());
            }
            case MULTIPLICATION_SIGN_STATE: {
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::MULTIPLICATION_SIGN, '*', pos, currentPos()), std::optional<CompilationError>());
            }
            case EQUAL_SIGN_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::EQUAL_SIGN,'=',pos,currentPos()),std::optional<CompilationError>());
            }
            case SEMICOLON_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::SEMICOLON,';',pos,currentPos()),std::optional<CompilationError>());
            }
            case COMMA_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::COMMA,',',pos,currentPos()),std::optional<CompilationError>());
            }
            case LEFTBRACKET_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::LEFT_BRACKET,'(',pos,currentPos()),std::optional<CompilationError>());
            }
            case RIGHTBRACKET_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::RIGHT_BRACKET,')',pos,currentPos()),std::optional<CompilationError>());
            }
            case LEFTBRACE_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::LEFT_BRACE,'{',pos,currentPos()),std::optional<CompilationError>());
            }
            case RIGHTBRACE_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::RIGHT_BRACE,'}',pos,currentPos()),std::optional<CompilationError>());
            }
            case SMALL_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::SMALL,'<',pos,currentPos()),std::optional<CompilationError>());
            }
            case BIG_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::BIG,'>',pos,currentPos()),std::optional<CompilationError>());
            }
            case SMALL_EQUAL_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::SMALL_EQUAL,"<=",pos,currentPos()),std::optional<CompilationError>());
            }
            case BIG_EQUAL_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::BIG_EQUAL,">=",pos,currentPos()),std::optional<CompilationError>());
            }
            case NOT_EQUAL_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::NOT_EQUAL,"!=",pos,currentPos()),std::optional<CompilationError>());
            }
            case EQUAL_STATE:{
                unreadLast();
                return std::make_pair(std::make_optional<Token>(TokenType::EQUAL,"==",pos,currentPos()),std::optional<CompilationError>());
            }

            // 预料之外的状态，如果执行到了这里，说明程序异常
			default:
				DieAndPrint("unhandled state.");
				break;
			}
		}
		// 预料之外的状态，如果执行到了这里，说明程序异常
		return std::make_pair(std::optional<Token>(), std::optional<CompilationError>());
	}

	std::optional<CompilationError> Tokenizer::checkToken(const Token& t) {
		switch (t.GetType()) {
			case IDENTIFIER: {
				auto val = t.GetValueString();
				if (miniplc0::isdigit(val[0]))
					return std::make_optional<CompilationError>(t.GetStartPos().first, t.GetStartPos().second, ErrorCode::ErrInvalidIdentifier);
				break;
			}
		default:
			break;
		}
		return {};
	}

	void Tokenizer::readAll() {
		if (_initialized)
			return;
		for (std::string tp; std::getline(_rdr, tp);)
			_lines_buffer.emplace_back(std::move(tp + "\n"));
		_initialized = true;
		_ptr = std::make_pair<int64_t, int64_t>(0, 0);
		return;
	}

	// Note: We allow this function to return a postion which is out of bound according to the design like std::vector::end().
	std::pair<uint64_t, uint64_t> Tokenizer::nextPos() {
		if (_ptr.first >= _lines_buffer.size())
			DieAndPrint("advance after EOF");
		if (_ptr.second == _lines_buffer[_ptr.first].size() - 1)
			return std::make_pair(_ptr.first + 1, 0);
		else
			return std::make_pair(_ptr.first, _ptr.second + 1);
	}

	std::pair<uint64_t, uint64_t> Tokenizer::currentPos() {
		return _ptr;
	}

	std::pair<uint64_t, uint64_t> Tokenizer::previousPos() {
		if (_ptr.first == 0 && _ptr.second == 0)
			DieAndPrint("previous position from beginning");
		if (_ptr.second == 0)
			return std::make_pair(_ptr.first - 1, _lines_buffer[_ptr.first - 1].size() - 1);
		else
			return std::make_pair(_ptr.first, _ptr.second - 1);
	}

	std::optional<char> Tokenizer::nextChar() {
		if (isEOF())
			return {}; // EOF
		auto result = _lines_buffer[_ptr.first][_ptr.second];
		_ptr = nextPos();
		return result;
	}

	bool Tokenizer::isEOF() {
		return _ptr.first >= _lines_buffer.size();
	}

	// Note: Is it evil to unread a buffer?
	void Tokenizer::unreadLast() {
		_ptr = previousPos();
	}

    // Note: Is it evil to unread a buffer?
    bool Tokenizer::isBlank(char ch) {
        if(ch==0x20||ch==0x09||ch==0x0A||ch==0x0D)
            return true;
        return false;
    }
}