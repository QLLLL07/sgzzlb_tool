# Makefile - 构建 libsgzzlb.so（C++17 动态库）
CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -fPIC -Wall -Wextra -MMD -MP
INC := -Icore

SRCS := core/data.cpp core/effects.cpp core/battle.cpp core/scoring.cpp \
        core/tactic_assign.cpp core/recommend.cpp core/api.cpp
SRCS += core/account.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)
TARGET := libsgzzlb.so

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -shared -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

.PHONY: all clean

-include $(DEPS)
