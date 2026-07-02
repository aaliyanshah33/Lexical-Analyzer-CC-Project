CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := lexer
SRC      := lexical_analyzer.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) sample.txt

clean:
	rm -f $(TARGET)

.PHONY: run clean
