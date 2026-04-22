CXX := g++
CXXFLAGS := -std=c++20 -O2 -pipe -static-libstdc++ -static-libgcc
LDFLAGS := 

all: code

code: code.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

.PHONY: clean
clean:
	rm -f code
