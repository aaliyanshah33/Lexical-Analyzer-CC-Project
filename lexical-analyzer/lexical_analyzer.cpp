// ===========================================================================
//  LEXICAL ANALYZER  —  Compiler Construction Project
//  Single-file C++ implementation of all 6 phases of lexical analysis.
//
//  Phase 1: Input Buffering      -> class InputBuffer (two-buffer + sentinels)
//  Phase 2: Scanning            -> Lexer::skipWhitespaceAndComments()
//  Phase 3: Pattern Matching    -> Lexer::scan* (DFA-style, longest match)
//  Phase 4: Token Creation      -> struct Token  <type, value>
//  Phase 5: Symbol Table Update -> class SymbolTable
//  Phase 6: Token Stream Output -> Lexer::getNextToken() loop in main()
//
//  Build : g++ -std=c++17 lexical_analyzer.cpp -o lexer
//  Run   : ./lexer sample.txt        (or any source file)
// ===========================================================================

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>
#include <iomanip>
#include <cctype>

using namespace std;

// ===========================================================================
//  PHASE 4 : TOKEN  ->  the pair <token_type, attribute_value>
// ===========================================================================
enum class TokenType {
    KEYWORD, IDENTIFIER, NUMBER, STRING_LITERAL,
    OPERATOR, DELIMITER, END_OF_FILE, ERROR
};

string tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::KEYWORD:        return "KEYWORD";
        case TokenType::IDENTIFIER:     return "IDENTIFIER";
        case TokenType::NUMBER:         return "NUMBER";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::OPERATOR:       return "OPERATOR";
        case TokenType::DELIMITER:      return "DELIMITER";
        case TokenType::END_OF_FILE:    return "EOF";
        case TokenType::ERROR:          return "ERROR";
    }
    return "UNKNOWN";
}

struct Token {
    TokenType type;
    string    lexeme;   // exact characters matched
    string    value;    // attribute value
    int       line;
    int       column;
    Token(TokenType t = TokenType::ERROR, string lex = "",
          string val = "", int ln = 0, int col = 0)
        : type(t), lexeme(std::move(lex)), value(std::move(val)),
          line(ln), column(col) {}
};

// ===========================================================================
//  PHASE 1 : INPUT BUFFERING  ->  two-buffer scheme with sentinels
//  Two halves share one array. When the forward pointer hits a half's
//  sentinel ('\0'), the OTHER half is refilled from disk. This gives cheap
//  1-character lookahead (retract) without ever re-reading the file.
// ===========================================================================
class InputBuffer {
public:
    static const size_t HALF = 4096;
    static const char   SENTINEL = '\0';

    explicit InputBuffer(const string& filename)
        : line_(1), column_(0), prevColumn_(0) {
        in_.open(filename, ios::binary);
        if (!in_.is_open())
            throw runtime_error("Cannot open source file: " + filename);
        buffer_[HALF - 1]     = SENTINEL;
        buffer_[2 * HALF - 1] = SENTINEL;
        loadHalf(0);
        forward_ = 0;
    }

    char next() {
        char c = buffer_[forward_];
        if (c == SENTINEL) {
            if (forward_ == HALF - 1)      { loadHalf(HALF); forward_ = HALF;  c = buffer_[forward_]; }
            else if (forward_ == 2*HALF-1) { loadHalf(0);    forward_ = 0;     c = buffer_[forward_]; }
            if (c == SENTINEL) return SENTINEL;   // true end of file
        }
        forward_ = (forward_ + 1) % (2 * HALF);
        track(c);
        return c;
    }

    char peek() {
        char saved = buffer_[forward_];
        if (saved == SENTINEL && (forward_ == HALF - 1 || forward_ == 2*HALF - 1)) {
            char c = next();
            retract(c);
            return c;
        }
        return saved;
    }

    void retract(char c) {
        forward_ = (forward_ + 2 * HALF - 1) % (2 * HALF);
        if (c == '\n') { --line_; column_ = prevColumn_; } else { --column_; }
    }

    int line()   const { return line_; }
    int column() const { return column_; }

private:
    void loadHalf(size_t start) {
        in_.read(&buffer_[start], HALF - 1);
        buffer_[start + static_cast<size_t>(in_.gcount())] = SENTINEL;
    }
    void track(char c) {
        prevColumn_ = column_;
        if (c == '\n') { ++line_; column_ = 0; } else { ++column_; }
    }

    ifstream in_;
    char     buffer_[2 * HALF];
    size_t   forward_;
    int      line_, column_, prevColumn_;
};

// ===========================================================================
//  PHASE 5 : SYMBOL TABLE  ->  stores identifiers / interned string literals
// ===========================================================================
class SymbolTable {
public:
    struct Entry {
        string name;
        string kind;      // "identifier" or "string_literal"
        string type;      // filled by later phases
        int    firstLine;
    };

    int insert(const string& name, const string& kind, int line) {
        auto it = index_.find(name);
        if (it != index_.end()) return it->second;         // already present
        int id = static_cast<int>(entries_.size());
        entries_.push_back(Entry{name, kind, "", line});
        index_[name] = id;
        return id;
    }

    const vector<Entry>& entries() const { return entries_; }

private:
    vector<Entry> entries_;
    unordered_map<string, int> index_;
};

// ===========================================================================
//  LEXICAL ERROR RECORD  (Section 4: error handling)
// ===========================================================================
struct LexError { string message; int line; int column; };

// ===========================================================================
//  THE LEXER  ->  Phases 2, 3, 6  +  error handling with panic-mode recovery
// ===========================================================================
class Lexer {
public:
    Lexer(const string& filename, SymbolTable& symbols)
        : input_(filename), symbols_(symbols), emittedEof_(false) {}

    // Phase 6: called on demand until END_OF_FILE is produced.
    Token getNextToken() {
        skipWhitespaceAndComments();
        int line = input_.line();
        int col  = input_.column() + 1;
        char c = input_.peek();

        if (c == InputBuffer::SENTINEL) {
            if (emittedEof_) return Token(TokenType::END_OF_FILE, "", "", line, col);
            emittedEof_ = true;
            return Token(TokenType::END_OF_FILE, "<eof>", "", line, col);
        }
        if (isIdentStart(c))                return scanIdentifierOrKeyword(line, col);
        if (isdigit((unsigned char)c))      return scanNumber(line, col);
        if (c == '"')                       return scanString(line, col);

        char first = input_.next();
        if (isDelimiter(first))
            return Token(TokenType::DELIMITER, string(1, first),
                         string(1, first), line, col);
        return scanOperatorOrDelimiter(line, col, first);
    }

    const vector<LexError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

private:
    // ---- character class helpers ----
    static bool isIdentStart(char c) { return isalpha((unsigned char)c) || c == '_'; }
    static bool isIdentPart(char c)  { return isalnum((unsigned char)c) || c == '_'; }
    static bool isDelimiter(char c) {
        return c==';'||c==','||c=='('||c==')'||c=='{'||c=='}'||c=='['||c==']';
    }
    static bool isKeyword(const string& s) {
        static const unordered_set<string> kw = {
            "auto","break","case","char","const","continue","default","do",
            "double","else","enum","extern","float","for","goto","if","int",
            "long","register","return","short","signed","sizeof","static",
            "struct","switch","typedef","union","unsigned","void","volatile",
            "while","bool","true","false","class","namespace","new","delete"
        };
        return kw.count(s) > 0;
    }

    // ---- Phase 2 : scanning (skip whitespace + comments) ----
    void skipWhitespaceAndComments() {
        for (;;) {
            char c = input_.peek();
            if (c==' '||c=='\t'||c=='\r'||c=='\n') { input_.next(); continue; }
            if (c == '/') {
                char first = input_.next();
                char second = input_.peek();
                if (second == '/') {                       // line comment
                    input_.next();
                    while (input_.peek() != '\n' && input_.peek() != InputBuffer::SENTINEL)
                        input_.next();
                    continue;
                }
                if (second == '*') {                       // block comment
                    input_.next();
                    int cl = input_.line(), cc = input_.column();
                    bool closed = false;
                    while (true) {
                        char x = input_.next();
                        if (x == InputBuffer::SENTINEL) break;
                        if (x == '*' && input_.peek() == '/') { input_.next(); closed = true; break; }
                    }
                    if (!closed) errors_.push_back({"Unterminated block comment", cl, cc});
                    continue;
                }
                input_.retract(first);                     // real '/' operator
                return;
            }
            return;
        }
    }

    // ---- Phase 3 : DFA  [A-Za-z_][A-Za-z0-9_]*  then keyword lookup ----
    Token scanIdentifierOrKeyword(int line, int col) {
        string lexeme;
        while (isIdentPart(input_.peek())) lexeme.push_back(input_.next());
        if (isKeyword(lexeme))
            return Token(TokenType::KEYWORD, lexeme, lexeme, line, col);
        symbols_.insert(lexeme, "identifier", line);       // Phase 5
        return Token(TokenType::IDENTIFIER, lexeme, lexeme, line, col);
    }

    // ---- Phase 3 : DFA for numbers  digits('.'digits)?([eE][+-]?digits)? ----
    Token scanNumber(int line, int col) {
        string lexeme;
        while (isdigit((unsigned char)input_.peek())) lexeme.push_back(input_.next());
        if (input_.peek() == '.') {
            lexeme.push_back(input_.next());
            if (!isdigit((unsigned char)input_.peek()))
                errors_.push_back({"Malformed number (digits expected after '.')", line, col});
            while (isdigit((unsigned char)input_.peek())) lexeme.push_back(input_.next());
        }
        char e = input_.peek();
        if (e=='e' || e=='E') {
            lexeme.push_back(input_.next());
            char s = input_.peek();
            if (s=='+'||s=='-') lexeme.push_back(input_.next());
            if (!isdigit((unsigned char)input_.peek()))
                errors_.push_back({"Malformed exponent in number", line, col});
            while (isdigit((unsigned char)input_.peek())) lexeme.push_back(input_.next());
        }
        if (isIdentStart(input_.peek())) {                 // e.g. 12abc
            while (isIdentPart(input_.peek())) lexeme.push_back(input_.next());
            return makeError("Invalid number literal '" + lexeme + "'", lexeme, line, col);
        }
        return Token(TokenType::NUMBER, lexeme, lexeme, line, col);
    }

    // ---- Phase 3 : DFA for "..." with escape handling ----
    Token scanString(int line, int col) {
        string lexeme, value;
        lexeme.push_back(input_.next());                   // opening quote
        for (;;) {
            char c = input_.peek();
            if (c == InputBuffer::SENTINEL || c == '\n')
                return makeError("Unterminated string literal", lexeme, line, col);
            c = input_.next();
            lexeme.push_back(c);
            if (c == '"') break;
            if (c == '\\') {
                char esc = input_.next();
                lexeme.push_back(esc);
                switch (esc) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '\\':value.push_back('\\'); break;
                    case '"': value.push_back('"');  break;
                    default:  value.push_back(esc);
                }
                continue;
            }
            value.push_back(c);
        }
        symbols_.insert(lexeme, "string_literal", line);   // Phase 5 (interning)
        return Token(TokenType::STRING_LITERAL, lexeme, value, line, col);
    }

    // ---- Phase 3 : maximal-munch operators & delimiters ----
    Token scanOperatorOrDelimiter(int line, int col, char first) {
        string op(1, first);
        char n1 = input_.peek();
        auto two = [&](char a, char b){ return first==a && n1==b; };

        if ((first=='<' && n1=='<') || (first=='>' && n1=='>')) {   // << >> <<= >>=
            op.push_back(input_.next());
            if (input_.peek() == '=') op.push_back(input_.next());
            return Token(TokenType::OPERATOR, op, op, line, col);
        }
        const char twoCharOps[][2] = {
            {'=','='},{'!','='},{'<','='},{'>','='},{'+','+'},{'-','-'},
            {'&','&'},{'|','|'},{'+','='},{'-','='},{'*','='},{'/','='},
            {'%','='},{'&','='},{'|','='},{'^','='},{'-','>'}
        };
        for (auto& p : twoCharOps)
            if (two(p[0], p[1])) { op.push_back(input_.next());
                                   return Token(TokenType::OPERATOR, op, op, line, col); }

        static const string singles = "+-*/%=<>!&|^~.?:";
        if (singles.find(first) != string::npos)
            return Token(TokenType::OPERATOR, op, op, line, col);

        return makeError(string("Unrecognized character '") + first + "'", op, line, col);
    }

    // ---- Section 4 : error handler + panic-mode recovery ----
    Token makeError(const string& msg, const string& lexeme, int line, int col) {
        errors_.push_back({msg, line, col});
        for (;;) {                                         // skip to next safe point
            char c = input_.peek();
            if (c == InputBuffer::SENTINEL) break;
            if (c==' '||c=='\t'||c=='\n'||c=='\r') break;
            if (isDelimiter(c)) break;
            input_.next();
        }
        return Token(TokenType::ERROR, lexeme, msg, line, col);
    }

    InputBuffer      input_;
    SymbolTable&     symbols_;
    vector<LexError> errors_;
    bool             emittedEof_;
};

// ===========================================================================
//  PHASE 6 : DRIVER  ->  parser-style getNextToken() loop, then reports
// ===========================================================================
int main(int argc, char** argv) {
    string filename = (argc > 1) ? argv[1] : "sample.txt";
    SymbolTable symbols;

    try {
        Lexer lexer(filename, symbols);

        cout << "=== TOKEN STREAM (" << filename << ") ===\n";
        cout << left << setw(16) << "TYPE"
             << setw(24) << "LEXEME" << "POSITION\n"
             << string(56, '-') << '\n';

        for (;;) {
            Token tok = lexer.getNextToken();
            if (tok.type == TokenType::END_OF_FILE) {
                cout << left << setw(16) << "EOF"
                     << setw(24) << "<eof>"
                     << "line " << tok.line << '\n';
                break;
            }
            string pos = "line " + to_string(tok.line) +
                         ", col " + to_string(tok.column);
            cout << left << setw(16) << tokenTypeName(tok.type)
                 << setw(24) << tok.lexeme << pos << '\n';
        }

        cout << "\n=== SYMBOL TABLE ===\n";
        cout << left << setw(6) << "ID" << setw(24) << "NAME"
             << setw(16) << "KIND" << "FIRST LINE\n"
             << string(56, '-') << '\n';
        int id = 0;
        for (const auto& e : symbols.entries())
            cout << left << setw(6) << id++ << setw(24) << e.name
                 << setw(16) << e.kind << e.firstLine << '\n';

        cout << "\n=== LEXICAL ERRORS ===\n";
        if (!lexer.hasErrors()) cout << "None.\n";
        else for (const auto& err : lexer.errors())
            cout << "[line " << err.line << ", col " << err.column << "] "
                 << err.message << '\n';

        return lexer.hasErrors() ? 1 : 0;
    } catch (const exception& ex) {
        cerr << "Fatal: " << ex.what() << '\n';
        return 2;
    }
}
