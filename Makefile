# Makefile - 构建 libsgzzlb.so（C++17 动态库）
CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -fPIC -Wall -Wextra
INC := -Icore

SRCS := core/data.cpp core/effects.cpp core/battle.cpp core/scoring.cpp \
        core/tactic_assign.cpp core/recommend.cpp core/api.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := libsgzzlb.so

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -shared -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
