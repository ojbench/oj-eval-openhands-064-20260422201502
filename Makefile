CXX := g++
CXXFLAGS := -std=c++20 -O2 -pipe
LDFLAGS := 

all: code

code: code.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

.PHONY: clean
clean:
	rm -f code
