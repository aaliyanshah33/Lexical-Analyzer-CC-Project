# Lexical Analyzer (Compiler Construction Project)

A **single-file** C++ implementation of the lexical analysis phase of a compiler.
It reads a source file as a stream of characters and produces a stream of tokens
`<token_type, attribute_value>`, following all six phases from the project
documentation.

Everything lives in one file: **`lexical_analyzer.cpp`**.

## Phases (all inside `lexical_analyzer.cpp`)

| Phase | Documentation topic   | Where in the file |
|-------|-----------------------|-------------------|
| 1 | Input Buffering       | `class InputBuffer` — two-buffer scheme with sentinels + 1-char lookahead |
| 2 | Scanning              | `Lexer::skipWhitespaceAndComments()` — skips whitespace, strips `//` and `/* */` |
| 3 | Pattern Matching      | `Lexer::scan*` — DFA-style recognizers, longest match |
| 4 | Token Creation        | `struct Token` + keyword table |
| 5 | Symbol Table Update   | `class SymbolTable` |
| 6 | Token Stream Output   | `Lexer::getNextToken()` loop in `main()` |

Error handling uses **panic-mode recovery**: each lexical error is logged with
line/column, then the scanner skips ahead and keeps going, so all errors are
reported.

## Token types
`KEYWORD, IDENTIFIER, NUMBER, STRING_LITERAL, OPERATOR, DELIMITER, EOF, ERROR`.

## Build & run

```bash
make          # or: g++ -std=c++17 lexical_analyzer.cpp -o lexer
make run      # runs on sample.txt
./lexer myfile.c
```

Requires a C++17 compiler.
